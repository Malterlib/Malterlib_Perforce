// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "Malterlib_Perforce_Ignore.h"
#include "Malterlib_Perforce_FileNames.h"

#include <Mib/File/File>
#include <Mib/Storage/Optional>
#include <Mib/Stream/Streams/Vector>
#include <Mib/String/Appender>

#undef SetPort

#ifdef DCompiler_MSVC
#	pragma warning(push,2)
#endif
#include "clientapi.h"
#include "i18napi.h"
#include "ignore.h"
#include "strarray.h"
#ifdef DCompiler_MSVC
#	pragma warning(pop)
#endif
#ifdef DPlatformFamily_Windows
#	include <Mib/Core/PlatformSpecific/WindowsRegistry>
#	include <Mib/Core/PlatformSpecific/WindowsString>
#else
#	include <errno.h>
#	include <fcntl.h>
#	include <limits.h>
#	include <stdlib.h>
#	include <sys/stat.h>
#	include <unistd.h>
#endif

namespace NMib::NPerforce
{
	using namespace NStr;
	using namespace NFile;
	using namespace NContainer;

	struct CPerforceIgnore::CInternal
	{
		CMutual m_Lock;
		TCMap<CStr, NStorage::TCUniquePointer<Ignore>> m_Matchers; // Per directory, under m_Lock
		StrArray m_IgnoreFiles;                                    // In m_Charsets.m_FileSystem
		CStr m_IgnoreName;                                         // The P4IGNORE value the matchers take for m_IgnoreFiles
		CPerforceCharsets m_Charsets;
	};

	struct CPerforceRootFiles::CInternal
	{
		CInternal(CStr const &_Root, TCSet<CStr> const &_OtherRepositories, CPerforceIgnore::CSettings const &_Settings);

		bool f_IsDirectoryOfRoot(CStr const &_Directory);
		void f_Walk(TCVector<CStr> *o_pFiles, TCVector<CStr> *o_pExcluded, TCSet<CStr> *o_pRuleFiles);

		CStr const m_Root;
		CPerforcePathSet m_OtherRepositories; // By the directories' identities, however a location is spelled
		CPerforceIgnore::CSettings const m_Settings;
		CPerforceIgnore m_Ignore;
		CMutual m_Lock;
		TCMap<CStr, bool> m_Directories;      // Whether each directory asked about is the root's, under m_Lock
	};

	namespace
	{
		// Bytes carried as text through Malterlib's path functions, which work on the separators, dots and drive letters as p4's
		// do on the bytes: each byte is the character of its value (Latin-1), and back, a character above that a '?'.
		CStr fg_BytesAsText(CAnsiStr const &_Bytes)
		{
			CStr Text;
			{
				CStr::CAppender Appender(Text);
				auto *pBytes = (uint8 const *)_Bytes.f_GetStr();
				for (auto *pByte = pBytes, *pEnd = pBytes + _Bytes.f_GetLen(); pByte != pEnd; ++pByte)
					Appender.f_AddUnicodeChar(ch32(*pByte));
			}
			return Text;
		}

		CAnsiStr fg_TextAsBytes(CStr const &_Text)
		{
			CAnsiStr Bytes;
			{
				CAnsiStr::CAppender Appender(Bytes);
				auto *pText = (uint8 const *)_Text.f_GetStr();
				auto *pEnd = pText + _Text.f_GetLen();
				while (pText != pEnd)
				{
					uint8 Byte = *pText++;
					if (Byte < 0x80)
						Appender.f_AddChar(Byte);
					else if ((Byte == 0xC2 || Byte == 0xC3) && pText != pEnd)
						Appender.f_AddChar(uint8(((Byte & 0x03) << 6) | (*pText++ & 0x3F)));
					else
					{
						while (pText != pEnd && (*pText & 0xC0) == 0x80)
							++pText;
						Appender.f_AddChar('?');
					}
				}
			}
			return Bytes;
		}

		// A setting's value as p4 has it in its file names' encoding: a settings file's bytes as they are, and a text encoded as
		// p4 reads its environment in that encoding.
		CAnsiStr fg_ValueBytes(CPerforceIgnore::CSettingValue const &_Value, EPerforceEncoding _Encoding)
		{
			if (_Value.m_bBytes)
				return _Value.m_Bytes;

			return fg_EncodePerforceString(_Value.m_Text, _Encoding);
		}

		// The paths Malterlib opens the file p4 opens by a name's bytes as, in the order p4 tries them. On Windows with UTF-8 file
		// names that is the bytes as UTF-8 when they are, and then as the code page, as FileIO's nt_open2 retries a failed open
		// with nt_wname's CP_ACP conversion; with the code page it is that. Outside Windows the bytes are the name.
		TCVector<CStr> fg_FileNames(CAnsiStr const &_Bytes, EPerforceEncoding _Encoding)
		{
			if (_Bytes.f_IsEmpty())
				return {};

#ifdef DPlatformFamily_Windows
			TCVector<CStr> Names;
			if (_Encoding == EPerforceEncoding_UTF8)
			{
				// p4's conversion of a name to Windows' (CharSetCvtUTF816) takes only valid UTF-8: no stray, truncated or overlong
				// sequences, surrogates or code points above U+10FFFF.
				CStr UTF8Name(_Bytes.f_GetStr(), _Bytes.f_GetLen());
				if (fg_IsValidUTF8(UTF8Name))
					Names.f_Insert(UTF8Name);
			}
			CStr CodePageName = fg_DecodePerforceString(_Bytes, EPerforceEncoding_CodePage);
			if (Names.f_IsEmpty() || Names.f_GetLast() != CodePageName)
				Names.f_Insert(CodePageName);
			return Names;
#else
			return {CStr(_Bytes.f_GetStr(), _Bytes.f_GetLen())};
#endif
		}

		// The type of the file the API opens by the bytes of _pPath, in _Encoding, and in o_Name its name, whether it exists or not: on
		// Windows the first of the names it tries that exists, else the first.
		EPathType fg_OpenedPathType(char const *_pPath, EPerforceEncoding _Encoding, CStr &o_Name)
		{
#ifdef DPlatformFamily_Windows
			TCVector<CStr> Names = fg_FileNames(CAnsiStr(_pPath), _Encoding);
			for (auto &Name : Names)
			{
				if (EPathType Type = fg_GetPathType(Name); Type != EPathType_Missing)
				{
					o_Name = Name;
					return Type;
				}
			}
			o_Name = Names.f_IsEmpty() ? CStr() : Names[0];
			return EPathType_Missing;
#else
			o_Name = _pPath;
			return fg_GetPathType(o_Name);
#endif
		}

		// Every rule file the matcher reads passes here before it is opened: only regular files are read, as reading a FIFO
		// without a writer blocks. The path is the bytes the matcher opens the file by.
		int fg_FilterRuleFile(char const *_pPath)
		{
#ifdef DPlatformFamily_Windows
			auto Encoding = fg_APIFileNameEncoding();
#else
			auto Encoding = EPerforceEncoding_UTF8; // Outside Windows the bytes are the name
#endif
			CStr Name;
			return fg_OpenedPathType(_pPath, Encoding, Name) == EPathType_File;
		}

		// The file p4's SetLocal makes of _Local in the directory _Directory, as p4 joins a P4CONFIG name to the directories it looks in
		// and the matcher a bare ignore file name: a rooted name is the file itself, and the leading . and .. of a relative one are
		// taken from the directory by name, by fg_ParentDirectory as p4 climbs. Outside Windows the rest is joined unchanged and the
		// file system resolves it. On Windows, as PathNT::SetLocal, the drive is the name's, none for a UNC name with backslashes,
		// or else the directory's: a rooted name is on that drive, so with a UNC directory a name like //Server/Share/ignore is
		// that share's file and one like \Config\ignore is on the current drive, where p4 runs, and the system resolves the rest by
		// the names alone. A drive-relative name makes a path with a drive inside it, which names no file: empty.
		CStr fg_LocalFile(CStr const &_Directory, CStr const &_Local)
		{
#ifdef DPlatformFamily_Windows
			auto fIsSeparator = [](ch8 _Character)
				{
					return _Character == '/' || _Character == '\\';
				}
			;

			CStr Path;
			CStr Directory = _Directory;
			CStr Name = _Local;
			if (Name.f_GetLen() >= 2 && Name[1] == ':')
			{
				Path = Name.f_Left(2);
				Name = Name.f_Extract(2);
			}
			else if (Name.f_StartsWith("\\\\"))
				;
			else if (Directory.f_GetLen() >= 2 && Directory[1] == ':')
			{
				Path = Directory.f_Left(2);
				Directory = Directory.f_Extract(2);
			}
#else
			auto fIsSeparator = [](ch8 _Character)
				{
					return _Character == '/';
				}
			;

			CStr Path;
			CStr Directory = _Directory;
			CStr Name = _Local;
#endif

			if (!Name.f_IsEmpty() && fIsSeparator(Name[0]))
				Path += Name;
			else
			{
				Path += Directory;

				// Takes the component from the start of the name with the separator after it, as p4's IsUnder does.
				auto fTakeLeading = [&](CStr const &_Component)
					{
						aint Length = _Component.f_GetLen();
						if (!Name.f_StartsWith(_Component) || (Name.f_GetLen() > Length && !fIsSeparator(Name[Length])))
							return false;

						Name = Name.f_Extract(Name.f_GetLen() > Length ? Length + 1 : Length);
						return true;
					}
				;

				while (true)
				{
					if (fTakeLeading(".."))
						Path = fg_ParentDirectory(Path);
					else if (!fTakeLeading("."))
						break;
				}

				bool bSeparator = !Path.f_IsEmpty() && !Name.f_IsEmpty() && !fIsSeparator(Path.f_GetStr()[Path.f_GetLen() - 1]);
				CStr::CAppender Appender(Path);
				if (bSeparator)
					Appender.f_AddChar('/');
				Appender += Name;
			}

#ifdef DPlatformFamily_Windows
			if (Path.f_GetLen() > 2 && Path.f_Extract(2).f_FindChar(':') >= 0)
				return {};

			return fg_WindowsFullPath(Path);
#else
			return Path;
#endif
		}

		// The file p4 opens for _Path from the current directory _Directory. Outside Windows the path is joined unchanged and the
		// file system resolves it: .. after a symbolic link leads from the link's target, and a backslash is a character of a name.
		// On Windows a UNC path is that share's, a drive-relative one is from that drive's current directory (the directory itself
		// on its drive; on another drive one p4 shares with this process, so the path is kept), and the rest are fg_LocalFile's.
		CStr fg_OpenedPath(CStr const &_Path, CStr const &_Directory)
		{
#ifdef DPlatformFamily_Windows
			CStr Path = _Path.f_ReplaceChar('\\', '/');
			if (Path.f_StartsWith("//"))
				return fg_WindowsFullPath(Path);

			if (Path.f_GetLen() >= 2 && Path[1] == ':' && (Path.f_GetLen() == 2 || Path[2] != '/'))
			{
				if (_Directory.f_GetLen() >= 2 && _Directory[1] == ':' && _Directory.f_Left(2).f_CmpNoCase(Path.f_Left(2)) == 0)
					return fg_LocalFile(_Directory, Path.f_Extract(2));

				return Path;
			}

			return fg_LocalFile(_Directory, Path);
#else
			if (_Path.f_StartsWith("/"))
				return _Path;

			return fg_DirectoryPrefix(_Directory) + _Path;
#endif
		}

		// fg_LocalFile of names' bytes, as p4's SetLocal joins them.
		CAnsiStr fg_LocalFileBytes(CAnsiStr const &_Directory, CAnsiStr const &_Local)
		{
			return fg_TextAsBytes(fg_LocalFile(fg_BytesAsText(_Directory), fg_BytesAsText(_Local)));
		}

		// The paths the generation tracks for a rule file, which BuildSystem expands by the names alone and follows symbolic links
		// in, as the open does: the file the matcher and the filter open. Outside Windows the path up to its last . or .. component
		// is resolved by CFile::fs_ResolveDirectory, with the directories whose entries decide what it resolves to, and the rest is kept
		// as it is, which the open and BuildSystem resolve alike; when the resolution fails, the path of the component it failed
		// at is tracked instead, whose creation or replacement changes what is opened. On Windows the system resolves . and .. by
		// the names (fg_WindowsFullPath, which keeps a UNC share's root).
		void fg_AddRuleFileDependencies(CStr const &_Path, TCSet<CStr> &o_Dependencies)
		{
#ifdef DPlatformFamily_Windows
			o_Dependencies[fg_WindowsFullPath(_Path)];
#else
			if (!_Path.f_StartsWith("/"))
			{
				o_Dependencies[_Path];
				return;
			}

			ch8 const *pPath = _Path.f_GetStr();
			ch8 const *pPathEnd = pPath + _Path.f_GetLen();
			aint iDotsEnd = -1;
			for (ch8 const *pStart = pPath;;)
			{
				ch8 const *pEnd = pStart;
				while (pEnd != pPathEnd && *pEnd != '/')
					++pEnd;

				aint ComponentLength = pEnd - pStart;
				if ((ComponentLength == 1 || ComponentLength == 2) && *pStart == '.' && pEnd[-1] == '.')
					iDotsEnd = pEnd - pPath;
				if (pEnd == pPathEnd)
					break;
				pStart = pEnd + 1;
			}

			if (iDotsEnd < 0)
			{
				o_Dependencies[_Path];
				return;
			}

			CStr Resolved;
			CStr Failed;
			if (!CFile::fs_ResolveDirectory(_Path.f_Left(iDotsEnd), Resolved, Failed, o_Dependencies))
			{
				o_Dependencies[Failed];
				return;
			}

			CStr Rest = _Path.f_Extract(iDotsEnd);
			if (Rest.f_StartsWith("/"))
				Rest = Rest.f_Extract(1);

			if (Rest.f_IsEmpty())
				o_Dependencies[Resolved];
			else
				o_Dependencies[fg_DirectoryPrefix(Resolved) + Rest];
#endif
		}

		// The contents of a regular file, also through a symbolic link, or false when it is missing, isn't a regular file or can't
		// be read: reading a FIFO without a writer blocks. Outside Windows the path is opened as it is, as p4 opens it, where
		// CFile's functions would take a backslash for a separator.
		bool fg_ReadRegularFile(CStr const &_Path, CAnsiStr &o_Contents)
		{
#ifdef DPlatformFamily_Windows
			try
			{
				if (fg_GetPathType(_Path) != EPathType_File)
					return false;

				CByteVector Data = CFile::fs_ReadFile(_Path);
				o_Contents = CAnsiStr((ch8 const *)Data.f_GetArray(), Data.f_GetLen());
			}
			catch (CException const &)
			{
				return false;
			}

			return true;
#else
			// Without O_NONBLOCK, opening a FIFO waits for a writer.
			int File = open(_Path.f_GetStr(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
			if (File < 0)
				return false;

			auto Cleanup = g_OnScopeExit / [&]
				{
					close(File);
				}
			;

			struct stat Stat;
			if (fstat(File, &Stat) != 0 || !S_ISREG(Stat.st_mode))
				return false;

			CByteVector Data;
			constexpr umint c_ChunkSize = 65536;
			while (true)
			{
				umint nUsed = Data.f_GetLen();
				Data.f_SetLen(nUsed + c_ChunkSize);
				ssize_t nRead = read(File, Data.f_GetArray() + nUsed, c_ChunkSize);
				Data.f_SetLen(nUsed + (nRead > 0 ? umint(nRead) : 0));
				if (nRead == 0)
					break;
				if (nRead < 0 && errno != EINTR)
					return false;
			}

			o_Contents = CAnsiStr((ch8 const *)Data.f_GetArray(), Data.f_GetLen());

			return true;
#endif
		}
	}

	namespace
	{
#ifdef DPlatformFamily_Windows
		constexpr ch8 const *gc_pHomeVariable = "USERPROFILE";
		constexpr ch8 const *gc_pRegistryPathVariable = "P4TEST_REGISTRY_PATH_T4_TEST"; // Replaces the path of p4's registry keys
#else
		constexpr ch8 const *gc_pHomeVariable = "HOME";
#endif
		// The name of the P4CONFIG files read, whatever P4CONFIG is, the same bytes in every encoding.
		constexpr ch8 const *gc_pConfigName = ".p4config";

		// A setting's name in a P4CONFIG or enviro file, its bytes as text (fg_BytesAsText), as p4 compares it: on Windows and macOS,
		// where p4 folds case, regardless of the case of ASCII letters, the only ones p4's comparison folds (StrPtr::SCompare).
		CStr fg_SettingName(CStr const &_Name)
		{
#if defined(DPlatformFamily_Windows) || defined(DPlatformFamily_macOS)
			CStr Name;
			{
				CStr::CAppender Appender(Name);
				auto *pName = (uint8 const *)_Name.f_GetStr();
				for (auto *pCharacter = pName, *pEnd = pName + _Name.f_GetLen(); pCharacter != pEnd; ++pCharacter)
					Appender.f_AddChar(*pCharacter >= 'a' && *pCharacter <= 'z' ? uint8(*pCharacter - 'a' + 'A') : *pCharacter);
			}
			return Name;
#else
			return _Name;
#endif
		}

		// A variable's name in the environment as p4 looks it up: regardless of case on Windows, whose environment ignores case, also
		// of letters outside ASCII.
		CStr fg_EnvironmentName(CStr const &_Name)
		{
#ifdef DPlatformFamily_Windows
			return NStr::NPlatform::fg_WindowsUpperCaseName(_Name);
#else
			return _Name;
#endif
		}

		// The settings of a P4CONFIG or P4ENVIRO file as p4 reads them into o_Settings, by fg_SettingName (Enviro::ReadConfig): a
		// line sets the variable named up to its first '=' to the rest, without the spaces ending it, and a variable already set
		// keeps its value. The values are the bytes as they are. In a P4CONFIG file, _ConfigFileBytes being the name p4 opened it
		// by, $configdir is that name's directory. A name is its bytes, also bytes that aren't ASCII, which the P4_<port>_CHARSET of
		// a port with such a path has, and a byte order mark is part of the first name. A file that is missing, isn't a regular
		// file, also through a symbolic link, or can't be read sets nothing.
		void fg_ReadSettingsFile(CStr const &_Path, CAnsiStr const &_ConfigFileBytes, TCMap<CStr, CAnsiStr> &o_Settings)
		{
			CAnsiStr Contents;
			if (!fg_ReadRegularFile(_Path, Contents))
				return;

			CAnsiStr ConfigDirectory = fg_TextAsBytes(fg_PerforcePath(fg_ParentDirectory(fg_BytesAsText(_ConfigFileBytes))));
			for (auto &Line : Contents.f_Split("\n"))
			{
				ch8 const *pLine = Line.f_GetStr();
				ch8 const *pLineEnd = pLine + Line.f_GetLen();
				if (pLineEnd != pLine && pLineEnd[-1] == '\r')
					--pLineEnd;
				while (pLineEnd != pLine && pLineEnd[-1] == ' ')
					--pLineEnd;

				CAnsiStr Setting(pLine, umint(pLineEnd - pLine));
				auto iEquals = Setting.f_FindChar('=');
				if (iEquals < 0)
					continue;

				CStr Name = fg_SettingName(fg_BytesAsText(Setting.f_Left(iEquals)));
				if (o_Settings.f_FindEqual(Name))
					continue;

				CAnsiStr Value = Setting.f_Extract(iEquals + 1);
				if (_ConfigFileBytes)
					Value = Value.f_Replace("$configdir", ConfigDirectory);

				o_Settings[Name] = Value;
			}
		}

		// The .p4config file p4 tries in the directory, by the directory's bytes in the encoding.
		CAnsiStr fg_ConfigFileBytes(CStr const &_Directory, EPerforceEncoding _Encoding)
		{
			return fg_LocalFileBytes(fg_EncodePerforceString(_Directory, _Encoding), gc_pConfigName);
		}

		// Calls _fOnConfigFile with each directory from _Directory up, the nearest first, and the .p4config file p4 tries in it, by its
		// bytes in the encoding, up to where p4 stops climbing, on Windows a UNC share's root too, or the config root, or until
		// _fOnConfigFile returns false.
		template <typename tf_FOnConfigFile>
		void fg_ForEachConfigFile(CStr const &_Directory, CStr const &_ConfigRoot, EPerforceEncoding _Encoding, tf_FOnConfigFile &&_fOnConfigFile)
		{
			for (CStr Directory = _Directory; Directory && fg_IsInConfigRoot(Directory, _ConfigRoot);)
			{
				if (!_fOnConfigFile(Directory, fg_ConfigFileBytes(Directory, _Encoding)))
					break;

				CStr Parent = fg_ParentDirectory(Directory);
				if (Parent == Directory)
					break;
				Directory = Parent;
			}
		}

		// Whether the settings file named, a .p4config or the enviro file, gives settings: a file, of type _Type, in the config root
		// (fg_IsConfigFileInRoot).
		bool fg_ConfigFileGivesSettings(CStr const &_ConfigFile, EPathType _Type, CStr const &_ConfigRoot)
		{
			return _ConfigFile && _Type == EPathType_File && fg_IsConfigFileInRoot(_ConfigFile, _ConfigRoot);
		}

		// The .p4config file p4 opens by _ConfigFile's bytes in _Encoding, where it gives settings (fg_ConfigFileGivesSettings), or empty.
		CStr fg_SettingsConfigFile(CAnsiStr const &_ConfigFile, EPerforceEncoding _Encoding, CStr const &_ConfigRoot)
		{
			CStr ConfigFile;
			EPathType Type = fg_OpenedPathType(_ConfigFile.f_GetStr(), _Encoding, ConfigFile);
			return fg_ConfigFileGivesSettings(ConfigFile, Type, _ConfigRoot) ? ConfigFile : CStr();
		}

		// The charsets p4 selects with the settings (fg_PerforceCharsets), untranslated where p4 would stop.
		CPerforceCharsets fg_SettingsCharsets(CPerforceIgnore::CSettings const &_Settings)
		{
			auto fValue = [](CStr const &_Value) -> ch8 const *
				{
					return _Value ? _Value.f_GetStr() : nullptr;
				}
			;
			CPerforceCharsets Charsets = fg_PerforceCharsets(fValue(_Settings.m_Charset), fValue(_Settings.m_CommandCharset), fValue(_Settings.m_LearnedCharset), nullptr);
			if (Charsets.m_Error)
				return {};
			return Charsets;
		}

		// A name of the registry, which ignores the case of names, also of letters outside ASCII.
		CStr fg_RegistryName(CStr const &_Name)
		{
#ifdef DPlatformFamily_Windows
			return NStr::NPlatform::fg_WindowsUpperCaseName(_Name);
#else
			return _Name.f_UpperCase();
#endif
		}

		// The code points of UTF-16 data in the little endian Windows stores it in, an unpaired surrogate as -1. A last odd byte is
		// no unit.
		TCVector<int32> fg_UTF16CodePoints(CByteVector const &_Data)
		{
			TCVector<int32> CodePoints;
			umint nUnits = _Data.f_GetLen() / 2;
			auto fUnit = [&](umint _iUnit) -> uint32
				{
					uint16 Unit = 0;
					NMemory::fg_MemCopy(&Unit, _Data.f_GetArray() + _iUnit * 2, sizeof(Unit));
					return fg_ByteSwapLE(Unit);
				}
			;
			for (umint iUnit = 0; iUnit < nUnits; ++iUnit)
			{
				uint32 Unit = fUnit(iUnit);
				if (Unit >= 0xD800 && Unit < 0xDC00 && iUnit + 1 < nUnits && fUnit(iUnit + 1) >= 0xDC00 && fUnit(iUnit + 1) < 0xE000)
				{
					ch16 const Pair[] = {ch16(Unit), ch16(fUnit(iUnit + 1))};
					CodePoints.f_Insert(int32(*NStr::CStrIteratorUTF16(Pair, 2)));
					++iUnit;
				}
				else if (Unit >= 0xD800 && Unit < 0xE000)
					CodePoints.f_Insert(-1);
				else
					CodePoints.f_Insert(int32(Unit));
			}
			return CodePoints;
		}

		// A registry value's data as UTF-16 text up to its first NUL, with '?' for an unpaired surrogate, as the code page's
		// conversion gives it. False when there is one, which p4's conversion from UTF-16 rejects.
		bool fg_RegistryText(CByteVector const &_Data, CStr &o_Text)
		{
			bool bValid = true;
			CStr::CAppender Appender(o_Text);
			for (int32 CodePoint : fg_UTF16CodePoints(_Data))
			{
				if (CodePoint == 0)
					break;

				if (CodePoint < 0)
				{
					bValid = false;
					Appender.f_AddChar('?');
				}
				else
					Appender.f_AddUnicodeChar(ch32(CodePoint));
			}
			return bValid;
		}

		// Text as the code page's API gives it, like RegQueryValueExA and GetEnvironmentVariableA: with the best-fit conversion of
		// WideCharToMultiByte, which can turn a character into a separator, so p4 parses the converted bytes, not the text.
		CPerforceIgnore::CSettingValue fg_CodePageValue(CStr const &_Text)
		{
			return CPerforceIgnore::CSettingValue::fs_FromBytes(fg_EncodePerforceString(_Text, EPerforceEncoding_CodePage));
		}

		// A registry value as p4 reads it in its file names' encoding, up to the first NUL: with UTF-8 names, a Unicode charset's,
		// the data as UTF-16 (GetRegValueW), and else, or when p4 can't convert that, as RegQueryValueExA gives it (GetRegValue):
		// a string's characters in the code page (fg_CodePageValue) and any other type's bytes as they are.
		CPerforceIgnore::CSettingValue fg_RegistrySettingValue(CPerforceIgnore::CRegistryValue const &_Value, EPerforceEncoding _Encoding)
		{
			CStr Text;
			bool bValid = fg_RegistryText(_Value.m_Data, Text);
			if (_Encoding == EPerforceEncoding_UTF8 && bValid)
				return Text;

			if (_Value.f_IsString())
				return fg_CodePageValue(Text);

			auto pData = (ch8 const *)_Value.m_Data.f_GetArray();
			return CPerforceIgnore::CSettingValue::fs_FromBytes(CAnsiStr(pData, NStr::fg_StrLen(pData, _Value.m_Data.f_GetLen())));
		}
	}

	auto CPerforceIgnore::CRegistryValue::fs_FromText(CStr const &_Text) -> CRegistryValue
	{
		CWStr Units = _Text;
		NStream::TCBinaryStreamMemory<> Stream;
		NStream::fg_FeedEndianArrayToStream(Stream, Units.f_GetStr(), Units.f_GetLen() + 1, EEndian_Little);

		CRegistryValue Value;
		Value.m_Data = Stream.f_MoveVector();
		return Value;
	}

	// REG_SZ, REG_EXPAND_SZ and REG_MULTI_SZ, which p4 reads like a REG_SZ, without expanding or splitting them.
	bool CPerforceIgnore::CRegistryValue::f_IsString() const
	{
		return m_Type == 1 || m_Type == 2 || m_Type == 7;
	}

	// The type and the data: the characters of a string of valid UTF-16, with a NUL as \0 and a backslash as \\, else the bytes
	// in hexadecimal.
	CStr CPerforceIgnore::CRegistryValue::f_GetDescription() const
	{
		auto CodePoints = fg_UTF16CodePoints(m_Data);
		bool bText = f_IsString() && m_Data.f_GetLen() % 2 == 0 && CodePoints.f_Contains(-1) < 0;

		CStr Description;
		{
			CStr::CAppender Appender(Description);
			Appender += CStr("type {} "_f << m_Type);
			if (bText)
			{
				for (int32 CodePoint : CodePoints)
				{
					if (CodePoint == 0)
						Appender += "\\0";
					else if (CodePoint == '\\')
						Appender += "\\\\";
					else
						Appender.f_AddUnicodeChar(ch32(CodePoint));
				}
			}
			else
				Appender += CStr("{}"_f << CStrFormatBinaryWrapper(m_Data.f_GetArray(), m_Data.f_GetLen()));
		}
		return Description;
	}

	// The values of a setting in each key, for showing: different values can be described alike (f_GetState).
	CStr CPerforceIgnore::CRegistrySettings::f_GetDescription(CStr const &_Name) const
	{
		auto fDescription = [&](TCMap<CStr, CRegistryValue> const &_Values) -> CStr
			{
				auto *pValue = _Values.f_FindEqual(fg_RegistryName(_Name));
				return pValue ? pValue->f_GetDescription() : CStr("unset");
			}
		;
		return "HKEY_CURRENT_USER: {}, HKEY_LOCAL_MACHINE: {}"_f << fDescription(m_User) << fDescription(m_Machine);
	}

	// The values of a setting in each key exactly, each one's type and bytes in hexadecimal, for knowing when one changes.
	CStr CPerforceIgnore::CRegistrySettings::f_GetState(CStr const &_Name) const
	{
		auto fState = [&](TCMap<CStr, CRegistryValue> const &_Values) -> CStr
			{
				auto *pValue = _Values.f_FindEqual(fg_RegistryName(_Name));
				if (!pValue)
					return "unset";

				CStr State;
				{
					CStr::CAppender Appender(State);
					Appender += CStr("type {} data {}"_f << pValue->m_Type << CStrFormatBinaryWrapper(pValue->m_Data.f_GetArray(), pValue->m_Data.f_GetLen()));
				}
				return State;
			}
		;
		return "HKEY_CURRENT_USER: {}, HKEY_LOCAL_MACHINE: {}"_f << fState(m_User) << fState(m_Machine);
	}

	// A location that doesn't exist excludes the path it has, below its nearest existing ancestor.
	void CPerforcePathSet::f_Insert(CStr const &_Path)
	{
		CKey Key;
		if (fsp_GetKey(_Path, Key))
			mp_Keys[Key];
	}

	bool CPerforcePathSet::f_Contains(CStr const &_Path) const
	{
		CKey Key;
		return fsp_GetKey(_Path, Key) && mp_Keys.f_FindEqual(Key) != nullptr;
	}

	// The identity of the path (st_dev and st_ino, or the volume serial number and file ID on Windows), or of its nearest existing
	// ancestor with the rest of the path, taken off at Malterlib's separators.
	bool CPerforcePathSet::fsp_GetKey(CStr const &_Path, CKey &o_Key)
	{
		CStr Path = _Path;
		while (Path.f_GetLen() > 1 && Path.f_EndsWith("/") && !Path.f_EndsWith(":/"))
			Path = Path.f_Left(Path.f_GetLen() - 1);

		for (CStr Existing = Path; !Existing.f_IsEmpty();)
		{
			try
			{
				if (CFile::fs_FileExists(Existing))
				{
					o_Key.m_Existing = CFile::fs_GetUniqueIdentifier(Existing);
					o_Key.m_Rest = Path.f_Extract(Existing.f_GetLen());
					return true;
				}
			}
			catch (CException const &)
			{
			}

			aint iSeparator = Existing.f_FindCharReverse('/');
			if (iSeparator < 0)
				return false;

			CStr Parent = Existing.f_Left(iSeparator);
			if (Parent.f_IsEmpty() || Parent.f_EndsWith(":") || Parent == "/")
				Parent = Existing.f_Left(iSeparator + 1);
			if (Parent == Existing)
				return false;
			Existing = Parent;
		}

		return false;
	}

	CPerforceIgnore::CSettingValue::CSettingValue(CStr const &_Text)
		: m_Text(_Text)
	{
	}

	CPerforceIgnore::CSettingValue::CSettingValue(ch8 const *_pText)
		: m_Text(_pText)
	{
	}

	auto CPerforceIgnore::CSettingValue::fs_FromBytes(CAnsiStr const &_Bytes) -> CSettingValue
	{
		CSettingValue Value;
		Value.m_Bytes = _Bytes;
		Value.m_bBytes = true;
		return Value;
	}

	bool CPerforceIgnore::CSettingValue::f_IsEmpty() const
	{
		return m_bBytes ? m_Bytes.f_IsEmpty() : m_Text.f_IsEmpty();
	}

	// The text, or the bytes with those outside ASCII as \xNN.
	CStr CPerforceIgnore::CSettingValue::f_GetDescription() const
	{
		if (!m_bBytes)
			return m_Text;

		CStr Description;
		{
			CStr::CAppender Appender(Description);
			auto *pBytes = (uint8 const *)m_Bytes.f_GetStr();
			for (auto *pByte = pBytes, *pEnd = pBytes + m_Bytes.f_GetLen(); pByte != pEnd; ++pByte)
			{
				if (*pByte < 0x80)
					Appender.f_AddChar(*pByte);
				else
					Appender += CStr("\\x{nfh,sj2}"_f << *pByte);
			}
		}
		return Description;
	}

	// A value as text, in UTF-8: a settings file's bytes decoded from p4's file names' encoding, as the first name p4 opens a file
	// by them as.
	CStr CPerforceIgnore::CSettings::f_GetText(CSettingValue const &_Value) const
	{
		if (!_Value.m_bBytes)
			return _Value.m_Text;

		TCVector<CStr> Names = fg_FileNames(_Value.m_Bytes, fg_SettingsCharsets(*this).m_FileSystem);
		return Names.f_IsEmpty() ? CStr() : Names[0];
	}

	// The .p4config file in the directory, as it is opened, whether it exists or not: of the names p4 tries, the first that exists,
	// else the first.
	CStr CPerforceIgnore::CSettings::f_GetConfigFile(CStr const &_Directory) const
	{
		EPerforceEncoding Encoding = fg_SettingsCharsets(*this).m_FileSystem;
		CStr Name;
		fg_OpenedPathType(fg_ConfigFileBytes(_Directory, Encoding).f_GetStr(), Encoding, Name);
		return Name;
	}

	// The .p4config file of the directory when it gives settings (fg_SettingsConfigFile), or empty.
	CStr CPerforceIgnore::CSettings::f_GetSettingsConfigFile(CStr const &_Directory) const
	{
		if (!fg_IsInConfigRoot(_Directory, m_ConfigRoot))
			return {};

		EPerforceEncoding Encoding = fg_SettingsCharsets(*this).m_FileSystem;
		return fg_SettingsConfigFile(fg_ConfigFileBytes(_Directory, Encoding), Encoding, m_ConfigRoot);
	}

	// The nearest directory from _Directory up whose .p4config gives settings (f_GetSettingsConfigFile), or empty.
	CStr CPerforceIgnore::CSettings::f_FindSettingsConfigDirectory(CStr const &_Directory) const
	{
		EPerforceEncoding Encoding = fg_SettingsCharsets(*this).m_FileSystem;
		CStr Found;
		fg_ForEachConfigFile
			(
				_Directory
				, m_ConfigRoot
				, Encoding
				, [&](CStr const &_ConfigDirectory, CAnsiStr const &_ConfigFile)
				{
					if (!fg_SettingsConfigFile(_ConfigFile, Encoding, m_ConfigRoot))
						return true;

					Found = _ConfigDirectory;
					return false;
				}
			)
		;

		return Found;
	}

	// The .p4config file of the directory, when it gives settings (f_GetSettingsConfigFile), and the files that decide the settings,
	// whether they exist or not, as p4 opens them: the enviro file and each .p4config tried from the directory up to m_ConfigRoot,
	// also in the file names' encoding of the untranslated reading of the charsets. A .p4config is tracked by its path, as its
	// creation or replacement changes the settings, rather than by its directory, whose entries change with every file made in it.
	auto CPerforceIgnore::CSettings::f_GetSettingsFiles(CStr const &_Directory) const -> CSettingsFiles
	{
		CSettingsFiles Files;
		if (m_EnviroFile)
			fg_AddRuleFileDependencies(m_EnviroFile, Files.m_Dependencies);

		Files.m_ConfigFile = f_GetSettingsConfigFile(_Directory);

		auto fAddConfigFiles = [&](EPerforceEncoding _Encoding)
			{
				fg_ForEachConfigFile
					(
						_Directory
						, m_ConfigRoot
						, _Encoding
						, [&](CStr const &_ConfigDirectory, CAnsiStr const &_ConfigFile)
						{
							for (auto &Name : fg_FileNames(_ConfigFile, _Encoding))
								Files.m_Dependencies[Name];

							return true;
						}
					)
				;
			}
		;
		fAddConfigFiles(fg_SettingsCharsets(*this).m_FileSystem);
		fAddConfigFiles(CPerforceCharsets().m_FileSystem);

		return Files;
	}

	namespace
	{
		// The names of a P4IGNORE as p4 run in _Directory takes them, split by the matcher's own parsing: a list (split on ';',
		// and ':' outside Windows) gets the platform's separators while a single name is kept as it is, and only a name with the
		// platform's separator is a path; any other is looked for in every directory above a path. The whole value "unset" is no
		// files. The matcher opens a path from the process's current directory, so a relative path is the one opened from
		// _Directory, and as the directory can hold the list's separators the matcher takes the names as a list.
		TCVector<CStr> fg_ResolveIgnoreNameTexts(CStr const &_IgnoreNames, CStr const &_Directory)
		{
			if (_IgnoreNames == "unset")
				return {};

			TCVector<CStr> Names;
			{
				Ignore Parser;
				StrArray Files;
				Parser.GetIgnoreFiles(StrRef(_IgnoreNames.f_GetStr()), 1, 1, Files);
				for (int iFile = 0; iFile < Files.Count(); ++iFile)
					Names.f_Insert(CStr(Files.Get(iFile)->Text()));
			}

#ifdef DPlatformFamily_Windows
			constexpr ch8 c_PathSeparator = '\\';
#else
			constexpr ch8 c_PathSeparator = '/';
#endif
			TCVector<CStr> Resolved;
			for (auto &Name : Names)
			{
				if (Name.f_IsEmpty())
					continue;

				if (Name.f_FindChar(c_PathSeparator) < 0)
				{
					Resolved.f_Insert(Name);
					continue;
				}

#ifdef DPlatformFamily_Windows
				// \Config\ignore is on the directory's drive, where p4 runs, and with a UNC directory on the current drive, and
				// D:Config\ignore on that drive is relative to the directory. On another drive the path stays relative to that drive's
				// current directory, which p4 shares.
				CStr Path = fg_OpenedPath(Name, _Directory);
				Resolved.f_Insert(Path == Name.f_ReplaceChar('\\', '/') ? Name : fg_PerforcePath(Path));
#else
				Resolved.f_Insert(fg_OpenedPath(Name, _Directory));
#endif
			}

			return Resolved;
		}

		// The names of a P4IGNORE value as p4 run in _Directory takes them: of text as texts, and of a settings file's bytes as
		// bytes, joined to the directory's bytes in p4's file names' encoding, as p4's matcher joins them.
		TCVector<CPerforceIgnore::CSettingValue> fg_ResolveIgnoreNames(CPerforceIgnore::CSettingValue const &_IgnoreNames, CStr const &_Directory, EPerforceEncoding _Encoding)
		{
			if (!_IgnoreNames.m_bBytes)
				return CPerforceIgnore::fs_ResolveIgnoreNames(_IgnoreNames.m_Text, _Directory);

			TCVector<CPerforceIgnore::CSettingValue> Resolved;
			CStr Directory = fg_BytesAsText(fg_EncodePerforceString(_Directory, _Encoding));
			for (auto &Name : fg_ResolveIgnoreNameTexts(fg_BytesAsText(_IgnoreNames.m_Bytes), Directory))
				Resolved.f_Insert(CPerforceIgnore::CSettingValue::fs_FromBytes(fg_TextAsBytes(Name)));
			return Resolved;
		}
	}

	// The variables of the environment fs_GetSettings and fs_ReadRegistry read by name, the home directory's last. They also read the
	// charset p4 learned for the port the settings give, P4_<port>_CHARSET (CSettings::m_LearnedCharsetVariable), whose name
	// fs_IsLearnedCharsetVariable tells.
	TCVector<CStr> CPerforceIgnore::fs_GetEnvironmentVariables()
	{
		TCVector<CStr> Variables = fs_GetRegistryVariables();
#ifdef DPlatformFamily_Windows
		Variables.f_Insert(gc_pRegistryPathVariable);
#endif
		Variables.f_Insert(gc_pHomeVariable);
		return Variables;
	}

	// The settings fs_GetSettings reads by name from the registry too, which also has the charsets p4 learned.
	TCVector<CStr> CPerforceIgnore::fs_GetRegistryVariables()
	{
		return {"P4IGNORE", "P4ENVIRO", "P4CHARSET", "P4COMMANDCHARSET", "P4PORT"};
	}

	// The absolute _Path, a directory, as the system resolves it outside Windows: its symbolic links, . and .. resolved one component
	// at a time (CFile::fs_ResolveDirectory), so a .. after a link leaves the link's target. On Windows, and where it can't be resolved, the
	// path is given as it is.
	CStr CPerforceIgnore::fs_ResolveDirectory(CStr const &_Path)
	{
#ifdef DPlatformFamily_Windows
		return _Path;
#else
		CStr Resolved;
		CStr Failed;
		TCSet<CStr> Dependencies;
		if (!CFile::fs_ResolveDirectory(_Path, Resolved, Failed, Dependencies))
			return _Path;

		return Resolved;
#endif
	}

	// The process's environment, as Malterlib has it, for reading the settings as p4 run by the process does.
	TCMap<CStr, CStr> CPerforceIgnore::fs_GetProcessEnvironment()
	{
		return TCMap<CStr, CStr>(fg_GetSys()->f_Environment());
	}

	// Whether p4 reads a P4CONFIG or enviro file by these bytes, in the file names' encoding of the settings' charset, for the
	// filter of the API's settings: only a regular file, as fs_GetSettings reads, since reading a FIFO without a writer blocks, and
	// only one in _ConfigRoot, as spelled (fg_IsConfigFileInRoot), which is empty for the enviro file.
	int CPerforceIgnore::fs_FilterSettingsFile(char const *_pPath, int _Charset, CStr const &_ConfigRoot)
	{
		return !fg_SettingsConfigFile(CAnsiStr(_pPath), fg_FileNameEncoding(_Charset), _ConfigRoot).f_IsEmpty();
	}

	// Whether a variable of the environment is the charset p4 learned for some port, P4_<port>_CHARSET, named as p4 looks it up.
	bool CPerforceIgnore::fs_IsLearnedCharsetVariable(CStr const &_Name)
	{
		CStr Name = fg_EnvironmentName(_Name);
		return Name.f_GetLen() > 11 && Name.f_StartsWith("P4_") && Name.f_EndsWith("_CHARSET");
	}

	// The registry's values of the settings fs_GetSettings reads (fs_GetRegistryVariables and the learned charsets), as p4 reads
	// them from software\perforce\environment, or the path P4TEST_REGISTRY_PATH_T4_TEST gives, an empty one the root: in a key
	// opened for writing too, in the 32-bit view where p4 is built for x64, so a process that can't write HKEY_LOCAL_MACHINE's reads
	// none of it.
	auto CPerforceIgnore::fs_ReadRegistry(TCMap<CStr, CStr> const &_Environment) -> CRegistrySettings
	{
		CRegistrySettings Registry;
#ifdef DPlatformFamily_Windows
		using CWin32Registry = NMib::NPlatform::CWin32_Registry;

		CStr KeyPath = "software\\perforce\\environment";
		for (auto iVariable = _Environment.f_GetIterator(); iVariable; ++iVariable)
		{
			if (fg_EnvironmentName(iVariable.f_GetKey()) == gc_pRegistryPathVariable)
			{
				KeyPath = *iVariable;
				break;
			}
		}

		TCSet<CStr> Variables;
		for (auto &Variable : fs_GetRegistryVariables())
			Variables[Variable];

		auto fReadKey = [&](CWin32Registry::ERegRoot _Root, TCMap<CStr, CRegistryValue> &o_Values)
			{
				CWin32Registry Key(_Root, KeyPath, CWin32Registry::EAccess::mc_ReadWrite);
				TCVector<CStr> Names;
				Key.f_EnumValues("", Names);
				for (auto &RegistryName : Names)
				{
					CStr Name = fg_RegistryName(RegistryName);
					if (!Variables.f_FindEqual(Name) && !fs_IsLearnedCharsetVariable(Name))
						continue;

					CWin32Registry::CRawValue Value;
					if (Key.f_ReadRaw("", RegistryName, Value))
						o_Values[Name] = CRegistryValue{.m_Data = fg_Move(Value.m_Data), .m_Type = Value.m_Type};
				}
			}
		;
#	ifdef DArchitecture_x64
		fReadKey(CWin32Registry::ERegRoot_Win32_CurrentUser, Registry.m_User);
		fReadKey(CWin32Registry::ERegRoot_Win32_LocalMachine, Registry.m_Machine);
#	else
		fReadKey(CWin32Registry::ERegRoot_CurrentUser, Registry.m_User);
		fReadKey(CWin32Registry::ERegRoot_LocalMachine, Registry.m_Machine);
#	endif
#endif
		return Registry;
	}

	// The settings p4 run in the directory uses, read from _Environment in place of the process's environment, the P4CONFIG files,
	// the enviro file and _Registry (fs_ReadRegistry). As in p4, a P4CONFIG file outranks the environment, which outranks the enviro
	// file, then HKEY_CURRENT_USER's key, then HKEY_LOCAL_MACHINE's. A value set empty is unset and hides what ranks below it, except
	// a registry value of empty text, which p4 takes as set. P4ENVIRO is the environment's or the registry's, $home the environment's
	// home directory, and without P4IGNORE the ignore files are p4's default, .p4ignore and p4ignore.txt above a path and in the home
	// directory. The names of the files are taken regardless of case on Windows and macOS, and those of the environment and the
	// registry on Windows.
	//
	// The P4CONFIG files are the .p4config files, whatever P4CONFIG is, so no setting outside the project chooses which of its files
	// give settings. A file above _ConfigRoot, the project's root, which another user can write and whose port can name a command,
	// gives none, nor does any where _Directory is outside the root, by the paths as spelled from it (fg_IsConfigFileInRoot). One
	// that is a symbolic link is read as any other. An empty _ConfigRoot reads them in every directory.
	//
	// The values are read as p4 has them, in its file names' encoding (fg_PerforceCharsets), and decoded to UTF-8 only for Malterlib's
	// paths. As p4 selects the charsets before its translation changes that encoding, P4ENVIRO and the charset settings are read
	// untranslated, with the P4CONFIG files found in that encoding, and the others with the translation's.
	auto CPerforceIgnore::fs_GetSettings(CStr const &_Directory, TCMap<CStr, CStr> const &_Environment, CRegistrySettings const &_Registry, CStr const &_ConfigRoot) -> CSettings
	{
		CSettings Settings;
		Settings.m_ConfigRoot = _ConfigRoot;

		TCMap<CStr, CStr> Environment;
		for (auto iVariable = _Environment.f_GetIterator(); iVariable; ++iVariable)
		{
			CStr Name = fg_EnvironmentName(iVariable.f_GetKey());
			if (!Environment.f_FindEqual(Name))
				Environment[Name] = *iVariable;
		}

		// The home directory is without a separator ending it, as p4 has it, and on Windows the Windows directory where USERPROFILE is
		// unset or empty, which p4's Enviro::Get takes as unset (Enviro::GetHome).
		auto *pHome = Environment.f_FindEqual(gc_pHomeVariable);
		CStr Home = pHome ? *pHome : CStr();
#ifdef DPlatformFamily_Windows
		bool bHome = true;
		if (Home.f_IsEmpty())
			Home = fg_WindowsDirectory();
#else
		bool bHome = pHome != nullptr;
#endif
		if (Home.f_EndsWith("/") || Home.f_EndsWith("\\"))
			Home = Home.f_Left(Home.f_GetLen() - 1);

		// A value with $home the home directory's as p4 has it in the encoding, as p4's Enviro::GetItem replaces it.
		auto fReplaceHome = [&](CSettingValue const &_Value, EPerforceEncoding _Encoding) -> CSettingValue
			{
				if (_Value.m_bBytes)
					return CSettingValue::fs_FromBytes(_Value.m_Bytes.f_Replace("$home", fg_EncodePerforceString(Home, _Encoding)));

				return _Value.m_Text.f_Replace("$home", Home);
			}
		;

		// The registry's value, HKEY_CURRENT_USER's before HKEY_LOCAL_MACHINE's. A value without data is unset, as p4's value then
		// has no length, while it hides HKEY_LOCAL_MACHINE's.
		auto fRegistry = [&](EPerforceEncoding _Encoding, CStr const &_Name) -> NStorage::TCOptional<CSettingValue>
			{
				CStr Name = fg_RegistryName(_Name);
				for (auto *pValues : {&_Registry.m_User, &_Registry.m_Machine})
				{
					auto *pValue = pValues->f_FindEqual(Name);
					if (!pValue)
						continue;

					if (pValue->m_Data.f_IsEmpty())
						return {};

					return fReplaceHome(fg_RegistrySettingValue(*pValue, _Encoding), _Encoding);
				}

				return {};
			}
		;

		// The enviro file's default is the home directory's .p4enviro, which p4 has on Windows only when P4ENVIRO names it. p4
		// reads P4ENVIRO before any translation, so the file is the one its untranslated spelling names, on Windows the code
		// page's (Enviro::GetEnv's GetEnvironmentVariableA, GetRegValue), which the later translated reads open as well.
		CPerforceCharsets Untranslated;
		EPerforceEncoding UntranslatedEncoding = Untranslated.m_FileSystem;
		NStorage::TCOptional<CSettingValue> EnviroFile;
		if (auto *pEnviroFile = Environment.f_FindEqual("P4ENVIRO"))
		{
			if (*pEnviroFile)
			{
				// p4 reads it through the code page, with the best-fit conversion that can make its syntax of other characters.
				if (UntranslatedEncoding == EPerforceEncoding_CodePage)
					EnviroFile = fReplaceHome(fg_CodePageValue(*pEnviroFile), UntranslatedEncoding);
				else
					EnviroFile = CSettingValue(pEnviroFile->f_Replace("$home", Home));
			}
		}
		else
			EnviroFile = fRegistry(UntranslatedEncoding, "P4ENVIRO");

		if (EnviroFile)
		{
			if (!EnviroFile->f_IsEmpty())
			{
				CStr Path = EnviroFile->m_bBytes ? fg_DecodePerforceString(EnviroFile->m_Bytes, UntranslatedEncoding) : EnviroFile->m_Text;
				Settings.m_EnviroFile = fg_OpenedPath(Path, _Directory);
			}
		}
#ifndef DPlatformFamily_Windows
		else if (pHome && *pHome)
			Settings.m_EnviroFile = fg_OpenedPath(*pHome + "/.p4enviro", _Directory);
#endif
		if (Settings.m_EnviroFile)
			Settings.m_EnviroFile = fg_DecodePerforceString(fg_EncodePerforceString(Settings.m_EnviroFile, UntranslatedEncoding), UntranslatedEncoding);

		TCMap<CStr, CAnsiStr> EnviroSettings;
		if (Settings.m_EnviroFile)
			fg_ReadSettingsFile(Settings.m_EnviroFile, {}, EnviroSettings);

		// A setting, unset where p4 has no value for it: a settings file's bytes, or the environment's or the registry's text.
		// A name is the bytes as text (fg_BytesAsText) in the settings files and text in the environment and the registry, the same
		// for ASCII names.
		auto fGetNamed = [&]
			(EPerforceEncoding _Encoding, TCMap<CStr, CAnsiStr> const *_pConfigSettings, CStr const &_FileName, CStr const &_EnvironmentName) -> NStorage::TCOptional<CSettingValue>
			{
				CStr SettingName = fg_SettingName(_FileName);
				CAnsiStr const *pBytes = _pConfigSettings ? _pConfigSettings->f_FindEqual(SettingName) : nullptr;
				if (!pBytes)
				{
					if (auto *pText = Environment.f_FindEqual(fg_EnvironmentName(_EnvironmentName)))
					{
						if (pText->f_IsEmpty())
							return {};

						return fReplaceHome(_Encoding == EPerforceEncoding_CodePage ? fg_CodePageValue(*pText) : CSettingValue(*pText), _Encoding);
					}
					pBytes = EnviroSettings.f_FindEqual(SettingName);
				}
				if (!pBytes)
					return fRegistry(_Encoding, _EnvironmentName);

				if (pBytes->f_IsEmpty())
					return {};

				return fReplaceHome(CSettingValue::fs_FromBytes(*pBytes), _Encoding);
			}
		;
		auto fGet = [&](EPerforceEncoding _Encoding, TCMap<CStr, CAnsiStr> const *_pConfigSettings, CStr const &_Name) -> NStorage::TCOptional<CSettingValue>
			{
				return fGetNamed(_Encoding, _pConfigSettings, _Name, _Name);
			}
		;
		auto fGetBytes = [&](EPerforceEncoding _Encoding, TCMap<CStr, CAnsiStr> const *_pConfigSettings, CStr const &_Name) -> CAnsiStr
			{
				auto Value = fGet(_Encoding, _pConfigSettings, _Name);
				return Value ? fg_ValueBytes(*Value, _Encoding) : CAnsiStr();
			}
		;
		// The .p4config files of the directory and those above it, the nearest first, up to where p4 stops climbing, on Windows a UNC
		// share's root too, or the config root, with the directories in the encoding. Each is the first of the names p4 tries that
		// exists.
		auto fConfigSettings = [&](EPerforceEncoding _Encoding) -> TCMap<CStr, CAnsiStr>
			{
				TCMap<CStr, CAnsiStr> ConfigSettings;
				fg_ForEachConfigFile
					(
						_Directory
						, _ConfigRoot
						, _Encoding
						, [&](CStr const &_ConfigDirectory, CAnsiStr const &_ConfigFile)
						{
							if (CStr Name = fg_SettingsConfigFile(_ConfigFile, _Encoding, _ConfigRoot))
								fg_ReadSettingsFile(Name, _ConfigFile, ConfigSettings);

							return true;
						}
					)
				;

				return ConfigSettings;
			}
		;

		// The charset settings are names, which compare as the bytes they are.
		TCMap<CStr, CAnsiStr> UntranslatedConfigSettings = fConfigSettings(UntranslatedEncoding);
		auto Charset = fGet(UntranslatedEncoding, &UntranslatedConfigSettings, "P4CHARSET");
		if (Charset)
			Settings.m_Charset = fg_BytesAsText(fg_ValueBytes(*Charset, UntranslatedEncoding));
		Settings.m_CommandCharset = fg_BytesAsText(fGetBytes(UntranslatedEncoding, &UntranslatedConfigSettings, "P4COMMANDCHARSET"));
		if (!Charset)
		{
			// The variable's name is P4PORT's bytes, which the environment has as p4 reads it untranslated.
			auto PortValue = fGet(UntranslatedEncoding, &UntranslatedConfigSettings, "P4PORT");
			CAnsiStr Port = PortValue ? fg_ValueBytes(*PortValue, UntranslatedEncoding) : CAnsiStr("perforce:1666");
			CAnsiStr NameBytes;
			{
				CAnsiStr::CAppender Appender(NameBytes);
				Appender += "P4_";
				Appender += Port.f_Replace("=", "@");
				Appender += "_CHARSET";
			}
			Settings.m_LearnedCharsetVariable = fg_DecodePerforceString(NameBytes, UntranslatedEncoding);
			if (auto LearnedCharset = fGetNamed(UntranslatedEncoding, &UntranslatedConfigSettings, fg_BytesAsText(NameBytes), Settings.m_LearnedCharsetVariable))
				Settings.m_LearnedCharset = fg_BytesAsText(fg_ValueBytes(*LearnedCharset, UntranslatedEncoding));
		}

		EPerforceEncoding Encoding = fg_SettingsCharsets(Settings).m_FileSystem;
		TCMap<CStr, CAnsiStr> ConfigSettings = Encoding == UntranslatedEncoding ? UntranslatedConfigSettings : fConfigSettings(Encoding);

		if (auto IgnoreNames = fGet(Encoding, &ConfigSettings, "P4IGNORE"))
			Settings.m_IgnoreNames = fg_ResolveIgnoreNames(*IgnoreNames, _Directory, Encoding);
		else if (bHome)
			Settings.m_IgnoreNames = fs_ResolveIgnoreNames(".p4ignore;p4ignore.txt;{}/.p4ignore;{}/p4ignore.txt"_f << Home << Home, _Directory);
		else
			Settings.m_IgnoreNames = {".p4ignore", "p4ignore.txt"};

		return Settings;
	}

	// The names of a P4IGNORE text as p4 run in _Directory takes them (fg_ResolveIgnoreNameTexts).
	TCVector<CPerforceIgnore::CSettingValue> CPerforceIgnore::fs_ResolveIgnoreNames(CStr const &_IgnoreNames, CStr const &_Directory)
	{
		TCVector<CSettingValue> Resolved;
		for (auto &Name : fg_ResolveIgnoreNameTexts(_IgnoreNames, _Directory))
			Resolved.f_Insert(Name);
		return Resolved;
	}

	CPerforceIgnore::CPerforceIgnore(CSettings const &_Settings)
		: mp_pInternal(fg_Construct())
	{
		auto &Internal = *mp_pInternal;

		// The matcher takes the value "unset" for no files, which a value starting with a line break can't be.
		// The matcher opens files by names in p4's file names' encoding and matches paths in it against the rules' bytes, so the
		// names and paths are given to it in that encoding, as p4 has them.
		Internal.m_Charsets = fg_SettingsCharsets(_Settings);
		{
			CStr::CAppender Appender(Internal.m_IgnoreName);
			for (auto &Name : _Settings.m_IgnoreNames)
			{
				Appender.f_AddChar('\n');
				Appender += Name.f_GetDescription();
				Internal.m_IgnoreFiles.Put()->Set(fg_ValueBytes(Name, Internal.m_Charsets.m_FileSystem).f_GetStr());
			}
		}
		if (Internal.m_IgnoreName.f_IsEmpty())
			Internal.m_IgnoreName = "\n";
	}

	CPerforceIgnore::~CPerforceIgnore() = default;

	// _Path is absolute. A directory matches as where p4 walks a workspace and leaves it out with everything below: a rule naming
	// the directory excludes it, while one naming what is below it, like "/BuildSystem/", excludes only the files.
	bool CPerforceIgnore::f_IsIgnored(CStr const &_Path, bool _bDirectory) const
	{
		auto &Internal = *mp_pInternal;
		CAnsiStr Path = fg_EncodePerforceString(fg_PerforcePath(_Path), Internal.m_Charsets.m_FileSystem);
		StrRef PathRef(Path.f_GetStr());
		StrRef IgnoreName(Internal.m_IgnoreName.f_GetStr());
		char const *pConfigName = gc_pConfigName;

		DMibLock(Internal.m_Lock);
#ifdef DPlatformFamily_Windows
		// As p4's translation sets GlobalCharSet to the file names' charset, which decides how the API's file functions take names.
		CFileNameCharset FileNameCharset(Internal.m_Charsets.m_FileNames);
#endif
		// A matcher caches the last directory's rules assuming a walk, and answers wrongly when asked about other directories in
		// between, so each directory has its own.
		auto &pMatcher = Internal.m_Matchers[fg_ParentDirectory(_Path)];
		if (!pMatcher)
		{
			pMatcher = fg_Construct();
			pMatcher->SetIgnoreFiles(IgnoreName, Internal.m_IgnoreFiles);
			pMatcher->SetFileFilter(fg_FilterRuleFile);
		}

		if (_bDirectory)
			return pMatcher->RejectDir(PathRef, IgnoreName, pConfigName) != 0;

		return pMatcher->Reject(PathRef, IgnoreName, pConfigName) != 0;
	}

	CPerforceRootFiles::CInternal::CInternal(CStr const &_Root, TCSet<CStr> const &_OtherRepositories, CPerforceIgnore::CSettings const &_Settings)
		: m_Root(_Root)
		, m_Settings(_Settings)
		, m_Ignore(_Settings)
	{
		for (auto &Repository : _OtherRepositories)
			m_OtherRepositories.f_Insert(Repository);
	}

	// A directory is not the root's when it is another repository's, by location or by its .git, a symbolic link, or ignored.
	bool CPerforceRootFiles::CInternal::f_IsDirectoryOfRoot(CStr const &_Directory)
	{
		{
			DMibLock(m_Lock);
			if (auto *pKnown = m_Directories.f_FindEqual(_Directory))
				return *pKnown;
		}

		bool bOfRoot
			= !m_OtherRepositories.f_Contains(_Directory)
			&& !CFile::fs_FileExists(_Directory, EFileAttrib_Link)
			&& !CFile::fs_FileExists(_Directory / ".git", EFileAttrib_Directory | EFileAttrib_File)
			&& !m_Ignore.f_IsIgnored(_Directory, true)
		;

		DMibLock(m_Lock);
		m_Directories[_Directory] = bOfRoot;

		return bOfRoot;
	}

	CPerforceRootFiles::CPerforceRootFiles(CStr const &_Root, TCSet<CStr> const &_OtherRepositories, CPerforceIgnore::CSettings const &_Settings)
		: mp_pInternal(fg_Construct(_Root, _OtherRepositories, _Settings))
	{
	}

	CPerforceRootFiles::~CPerforceRootFiles() = default;

	auto CPerforceRootFiles::f_GetSettings() const -> CPerforceIgnore::CSettings const &
	{
		return mp_pInternal->m_Settings;
	}

	// Whether f_List would list the file, which need not exist. _Path is absolute and spelled like the root.
	bool CPerforceRootFiles::f_Contains(CStr const &_Path) const
	{
		return f_IsOfRoot(_Path) && !CFile::fs_FileExists(_Path, EFileAttrib_Link) && fg_GetPathType(_Path) != EPathType_Special;
	}

	// Whether a file at the path is the root's, by the directories it is in and the ignore rules, whatever is at the path now: a
	// depot revision's path, whose workspace file can be anything. _Path is absolute and spelled like the root.
	bool CPerforceRootFiles::f_IsOfRoot(CStr const &_Path) const
	{
		auto &Internal = *mp_pInternal;
		CStr Relative;
		if (!fg_IsLexicallyIn(_Path, Internal.m_Root, Relative) || Relative.f_IsEmpty())
			return false;

		aint iRelative = _Path.f_GetLen() - Relative.f_GetLen();
		for (aint iSeparator = _Path.f_Find(iRelative, "/"); iSeparator >= 0; iSeparator = _Path.f_Find(iSeparator + 1, "/"))
		{
			if (!Internal.f_IsDirectoryOfRoot(_Path.f_Left(iSeparator)))
				return false;
		}

		return !Internal.m_Ignore.f_IsIgnored(_Path, false);
	}

	// Without o_pExcluded an unlistable directory fails the walk: it reads as empty and would drop its files unnoticed. Outside
	// Windows one that is readable but not searchable gives names without attributes, which are dropped too. On Windows the
	// listing decides, as a directory can be listed without the rights of reading it as a file.
	void CPerforceRootFiles::CInternal::f_Walk(TCVector<CStr> *o_pFiles, TCVector<CStr> *o_pExcluded, TCSet<CStr> *o_pRuleFiles)
	{
		// The ignore files of bare names, looked for in every directory above a path, the .p4config files of the root and the enviro
		// file, whether they exist or not. A path that names no file is left out.
		// A file p4 opens by bytes is any of the names p4 tries, so each is tracked.
		EPerforceEncoding Encoding = fg_SettingsCharsets(m_Settings).m_FileSystem;
		auto fAddRuleFile = [&](CAnsiStr const &_Bytes)
			{
				for (auto &Name : fg_FileNames(_Bytes, Encoding))
					fg_AddRuleFileDependencies(Name, *o_pRuleFiles);
			}
		;
		// A bare name in the directory, as p4 joins them in its file names' encoding.
		auto fAddLocalRuleFile = [&](CStr const &_Directory, CAnsiStr const &_Name)
			{
				fAddRuleFile(fg_LocalFileBytes(fg_EncodePerforceString(_Directory, Encoding), _Name));
			}
		;

		TCSet<CAnsiStr> IgnoreNames;
		if (o_pRuleFiles)
		{
			// A name with the platform's separator is a path, as the matcher tells them.
#ifdef DPlatformFamily_Windows
			constexpr ch8 c_PathSeparator = '\\';
#else
			constexpr ch8 c_PathSeparator = '/';
#endif
			for (auto &Name : m_Settings.m_IgnoreNames)
			{
				CAnsiStr NameBytes = fg_ValueBytes(Name, Encoding);
				if (NameBytes.f_IsEmpty())
					continue;
				if (NameBytes.f_FindChar(c_PathSeparator) < 0)
					IgnoreNames[NameBytes];
				else
					fAddRuleFile(NameBytes);
			}

			for (auto &File : m_Settings.f_GetSettingsFiles(m_Root).m_Dependencies)
				(*o_pRuleFiles)[File];

			for (CStr Directory = m_Root; Directory;)
			{
				if (Directory != m_Root)
				{
					for (auto &Name : IgnoreNames)
						fAddLocalRuleFile(Directory, Name);
				}

				CStr Parent = fg_ParentDirectory(Directory);
				if (Parent == Directory)
					break;
				Directory = Parent;
			}
		}

		// A name with a wildcard character would match other files, and the wildcards have no escape. The root itself is every
		// file.
		auto fInsertExcluded = [&](CStr const &_Path, bool _bDirectory)
			{
				CStr Relative = CFile::fs_MakePathRelative(_Path, m_Root);
				if (Relative.f_FindChars("*?") >= 0)
					return;

				if (Relative.f_IsEmpty())
					o_pExcluded->f_Insert("*");
				else
					o_pExcluded->f_Insert(Relative + (_bDirectory ? "/*" : ""));
			}
		;

		TCVector<CStr> Directories = {m_Root};

		while (!Directories.f_IsEmpty())
		{
			CStr Directory = Directories.f_GetLast();
			Directories.f_Remove(Directories.f_GetLen() - 1);

			// A listing that fails part of the way fails as a whole.
			TCVector<CFile::CFoundFile> Entries;
#ifdef DPlatformFamily_Windows
			bool bListable = true;
#else
			bool bListable = access(Directory.f_GetStr(), R_OK | X_OK) == 0;
#endif
			if (bListable)
			{
				try
				{
					CFile::fs_FindFilesGeneral
						(
							Directory / "*"
							, [&](CStr const &_Path, EFileAttrib _Attribs, bool _bPreRecurse)
							{
								if (!_bPreRecurse)
								{
									auto &Entry = Entries.f_Insert();
									Entry.m_Path = _Path;
									Entry.m_Attribs = _Attribs;
								}

								return false;
							}
						)
					;
				}
				catch (CExceptionFile const &)
				{
					bListable = false;
				}
#ifdef DPlatformFamily_Windows
				// A missing directory lists as empty, so a directory that vanished is unlistable, as on POSIX.
				if (bListable && Entries.f_IsEmpty() && !CFile::fs_FileExists(Directory, EFileAttrib_Directory))
					bListable = false;
#endif
			}

			if (!bListable)
			{
				if (!o_pExcluded)
					DMibError("'{}' cannot be listed"_f << Directory);

				fInsertExcluded(Directory, true);
				continue;
			}

			// A directory of the root that becomes another repository's, as its .git is created, is no longer the root's. A .git
			// that is there is not followed, as git changes the time of its directory with every command.
			if (o_pRuleFiles)
			{
				fg_AddRuleFileDependencies(Directory / ".git", *o_pRuleFiles);
				for (auto &Name : IgnoreNames)
					fAddLocalRuleFile(Directory, Name);
			}

			for (auto &Entry : Entries)
			{
				bool bDirectory = (Entry.m_Attribs & EFileAttrib_Directory) != 0;

				bool bOfRoot
					= bDirectory
					? f_IsDirectoryOfRoot(Entry.m_Path)
					: !(Entry.m_Attribs & EFileAttrib_Link) && fg_GetPathType(Entry.m_Path) != EPathType_Special && !m_Ignore.f_IsIgnored(Entry.m_Path, false)
				;
				if (!bOfRoot)
				{
					if (o_pExcluded)
						fInsertExcluded(Entry.m_Path, bDirectory);
				}
				else if (bDirectory)
					Directories.f_Insert(Entry.m_Path);
				else if (o_pFiles)
					o_pFiles->f_Insert(CFile::fs_MakePathRelative(Entry.m_Path, m_Root));
			}
		}
	}

	// The root's files, relative to it and sorted. Fails when a directory of the root cannot be listed.
	TCVector<CStr> CPerforceRootFiles::f_List() const
	{
		TCVector<CStr> Files;
		mp_pInternal->f_Walk(&Files, nullptr, nullptr);
		Files.f_Sort();

		return Files;
	}

	// Wildcards relative to the root, sorted, for what f_List leaves out, for rules that know no P4IGNORE: "<Directory>/*" for a
	// directory, also one that cannot be listed, and the path of a file. A file excluded after the walk is not among them, nor a
	// path with a wildcard character. o_pRuleFiles gets the ignore and P4CONFIG files p4 reads for the root, also those that don't
	// exist, so that one created later can be noticed, as fg_AddRuleFileDependencies tracks the files opened: with the
	// directories holding the symbolic links that decide which file a path with . or .. opens, and the components a .. discards.
	TCVector<CStr> CPerforceRootFiles::f_ListExcluded(TCVector<CStr> *o_pRuleFiles) const
	{
		TCVector<CStr> Excluded;
		TCSet<CStr> RuleFiles;
		mp_pInternal->f_Walk(nullptr, &Excluded, o_pRuleFiles ? &RuleFiles : nullptr);
		Excluded.f_Sort();

		if (o_pRuleFiles)
		{
			for (auto &RuleFile : RuleFiles)
				o_pRuleFiles->f_Insert(RuleFile);
		}

		return Excluded;
	}
}
