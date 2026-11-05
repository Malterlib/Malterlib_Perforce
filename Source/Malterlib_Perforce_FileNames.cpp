// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "Malterlib_Perforce_FileNames.h"

#include <Mib/File/File>

#ifdef DPlatformFamily_Windows
#	include <Windows.h>
#else
#	include <sys/stat.h>
#endif

#ifdef DCompiler_MSVC
#	pragma warning(push,2)
#endif
#include "stdhdrs.h"
#include "strbuf.h"
#include "i18napi.h"
#include "charcvt.h"
#include "charset.h"
#include "enviro.h"
#ifdef DCompiler_MSVC
#	pragma warning(pop)
#endif

namespace NMib::NPerforce
{
	// What the path is, following symbolic links. Outside Windows the path goes to stat as it is, so a backslash is a character of a
	// name, as for p4.
	EPathType fg_GetPathType(NStr::CStr const &_Path)
	{
#ifdef DPlatformFamily_Windows
		if (NFile::CFile::fs_FileExists(_Path, NFile::EFileAttrib_Directory))
			return EPathType_Directory;
		if (NFile::CFile::fs_FileExists(_Path, NFile::EFileAttrib_File))
			return EPathType_File;
		return EPathType_Missing;
#else
		struct stat Stat;
		if (stat(_Path.f_GetStr(), &Stat) != 0)
			return EPathType_Missing;
		if (S_ISREG(Stat.st_mode))
			return EPathType_File;
		if (S_ISDIR(Stat.st_mode))
			return EPathType_Directory;
		return EPathType_Special;
#endif
	}

	// The directory with one separator ending it, which the paths under it start with, also for a root like / or D:/.
	NStr::CStr fg_DirectoryPrefix(NStr::CStr const &_Directory)
	{
		if (_Directory.f_EndsWith("/"))
			return _Directory;

		return _Directory + "/";
	}

	// The path with the separator of the platform, which the Perforce API uses.
	NStr::CStr fg_PerforcePath(NStr::CStr const &_Path)
	{
#ifdef DPlatformFamily_Windows
		return _Path.f_ReplaceChar('/', '\\');
#else
		return _Path;
#endif
	}

	// A path of the Perforce API with Malterlib's separators. Only on Windows is a backslash a separator: elsewhere it is a character
	// of a name, which NFile::CFile::fs_GetMalterlibPath would turn into a separator.
	NStr::CStr fg_MalterlibPath(NStr::CStr const &_Path)
	{
#ifdef DPlatformFamily_Windows
		return NFile::CFile::fs_GetMalterlibPath(_Path);
#else
		return _Path;
#endif
	}

#ifdef DPlatformFamily_Windows
	// The path Windows opens for _Path, with Malterlib's separators: . and .. resolved by the names as GetFullPathNameW resolves
	// them, which stops at a drive's root or a UNC share's root, and a rooted path without a drive on the current drive.
	NStr::CStr fg_WindowsFullPath(NStr::CStr const &_Path)
	{
		if (_Path.f_IsEmpty())
			return _Path;

		NStr::CWStr Path = _Path;
		DWORD nLength = GetFullPathNameW((wchar_t const *)Path.f_GetStr(), 0, nullptr, nullptr);
		if (nLength == 0)
			return NFile::CFile::fs_GetMalterlibPath(_Path);

		NStr::CWStr FullPath;
		auto *pFullPath = FullPath.f_GetStr(nLength);
		DWORD nWritten = GetFullPathNameW((wchar_t const *)Path.f_GetStr(), nLength, (wchar_t *)pFullPath, nullptr);
		if (nWritten == 0 || nWritten >= nLength)
			return NFile::CFile::fs_GetMalterlibPath(_Path);

		FullPath.f_SetAt(nWritten, 0);
		FullPath.f_TrimSize();

		return NFile::CFile::fs_GetMalterlibPath(NStr::CStr(FullPath));
	}
#endif

	// The string converted by the Perforce API's converter between UTF-8 and the charset, as the API translates with it, or
	// unchanged when the conversion fails. To the charset a character it can't represent becomes '?'.
	NStr::CStr fg_ConvertCharset(NStr::CStr const &_Str, int _Charset, bool _bToCharset)
	{
		if (_Str.f_IsEmpty())
			return _Str;

		CharSetCvt *pToCharset = CharSetCvt::FindCvt(CharSetCvt::UTF_8, CharSetCvt::CharSet(_Charset));
		if (!pToCharset)
			return _Str;

		CharSetCvt *pConverter = _bToCharset ? pToCharset : pToCharset->ReverseCvt();
		auto Cleanup = g_OnScopeExit / [&]
			{
				if (pConverter != pToCharset)
					delete pConverter;
				delete pToCharset;
			}
		;
		if (!pConverter)
			return _Str;

		int Length = 0;
		char const *pConverted = _bToCharset
			? pConverter->FastCvtQues(_Str.f_GetStr(), int(_Str.f_GetLen()), &Length)
			: pConverter->FastCvt(_Str.f_GetStr(), int(_Str.f_GetLen()), &Length)
		;
		if (!pConverted)
			return _Str;

		return NStr::CStr((ch8 const *)pConverted, Length);
	}

	// p4's charsets, selected from its settings exactly as its command line client selects them, where the Perforce API leaves
	// the selection to its user:
	// - clientPrepareEnv (clientmain.cc): with P4CHARSET (_pCharset, as -C) set, its charset is the content's, and the command
	//   charset is P4COMMANDCHARSET (_pCommandCharset, looked up with _pEnviro, as -Q) or else P4CHARSET, replaced by
	//   CharSetApi::Discover's when it is wide; then SetTrans(command, content, command, command). So P4CHARSET none with
	//   P4COMMANDCHARSET utf8 has UTF-8 file names. A charset it can't look up or discover stops p4.
	// - Without P4CHARSET, Client::Init's SetupUnicode takes Client::GetCharset's fallback, the P4_<port>_CHARSET p4 learned from
	//   the server (_pLearnedCharset), and LateUnicodeSetup gives it to SetTrans as the charset of every purpose, without
	//   P4COMMANDCHARSET. Without that either, p4 learns it when it connects, which an offline caller can't: untranslated.
	// - Client::SetTrans (clienti18n.cc): all of them NOCONV is untranslated; otherwise the file names' charset becomes the
	//   settings' (Enviro::SetCharSet) and GlobalCharSet. On Windows the API's file functions then take a name in UTF-8 when that
	//   charset is a Unicode one (FileIO's DOUNICODE, nt_wname's UTF-8 conversion), and otherwise in the active code page
	//   (nt_wname's CP_ACP), directory listings give names in the same encoding (FileSys::ScanDir), and the settings' values from
	//   the environment come in it too (Enviro::ReadItemPlatform's GetEnvW or GetEnv); settings files' values are their bytes
	//   (Enviro::ReadConfig), which the API opens files by as they are. So that one encoding, m_FileSystem, is the one of the
	//   settings' values, of the names files are opened by and of the paths the ignore matcher matches (the names p4
	//   reconcile lists). Outside Windows names are bytes in every charset.
	CPerforceCharsets fg_PerforceCharsets(ch8 const *_pCharset, ch8 const *_pCommandCharset, ch8 const *_pLearnedCharset, Enviro *_pEnviro)
	{
		CPerforceCharsets Charsets;
		int Command = CharSetApi::NOCONV;
		int Content = CharSetApi::NOCONV;
		if (_pCharset)
		{
			Command = CharSetApi::Lookup(_pCharset);
			if (Command < 0)
			{
				Charsets.m_Error = "P4CHARSET '{}' is not a charset"_f << _pCharset;
				return Charsets;
			}

			Content = Command;
			if (_pCommandCharset)
			{
				Command = CharSetApi::Lookup(_pCommandCharset, _pEnviro);
				if (Command < 0)
				{
					Charsets.m_Error = "P4COMMANDCHARSET '{}' is not a charset"_f << _pCommandCharset;
					return Charsets;
				}
			}

			if (CharSetApi::Granularity(CharSetApi::CharSet(Command)) != 1)
			{
				Command = CharSetApi::Discover(_pEnviro);
				if (Command < 0)
				{
					Charsets.m_Error = "A wide charset needs P4COMMANDCHARSET set to another charset, and none was found from the locale";
					return Charsets;
				}
			}
		}
		else if (_pLearnedCharset)
		{
			Command = CharSetApi::Lookup(_pLearnedCharset);
			if (Command < 0)
			{
				Charsets.m_Error = "The charset '{}' learned from the server is not a charset"_f << _pLearnedCharset;
				return Charsets;
			}

			Content = Command;
		}

		if (Command == CharSetApi::NOCONV && Content == CharSetApi::NOCONV)
			return Charsets;

		Charsets.m_Command = Command;
		Charsets.m_Content = Content;
		Charsets.m_FileNames = Command;
		Charsets.m_bTranslated = true;
		Charsets.m_FileSystem = fg_FileNameEncoding(Command);
		return Charsets;
	}

	// The encoding the API takes file names in with the file names' charset (SetTrans' fnames, NOCONV untranslated): on Windows
	// UTF-8 for a Unicode charset and else the code page (fg_PerforceCharsets); outside Windows the bytes as they are.
	EPerforceEncoding fg_FileNameEncoding(int _FileNamesCharset)
	{
#ifdef DPlatformFamily_Windows
		if (CharSetApi::isUnicode(CharSetApi::CharSet(_FileNamesCharset)))
			return EPerforceEncoding_UTF8;
		return EPerforceEncoding_CodePage;
#else
		return EPerforceEncoding_UTF8;
#endif
	}

	// A string of the Perforce API in UTF-8. The code page's bytes are decoded as Windows decodes them; UTF-8 is taken as it is.
	NStr::CStr fg_DecodePerforceString(NStr::CAnsiStr const &_Bytes, EPerforceEncoding _Encoding)
	{
#ifdef DPlatformFamily_Windows
		if (_Encoding == EPerforceEncoding_CodePage)
		{
			NStr::CStr Decoded;
			NStr::NPlatform::fg_SystemDecodeAnsiStr(_Bytes, Decoded);
			return Decoded;
		}
#endif
		return NStr::CStr(_Bytes.f_GetStr(), _Bytes.f_GetLen());
	}

	// A UTF-8 string in an encoding of the Perforce API. In the code page a character it can't represent becomes '?', as Windows
	// gives a name or environment value in the code page.
	NStr::CAnsiStr fg_EncodePerforceString(NStr::CStr const &_String, EPerforceEncoding _Encoding)
	{
#ifdef DPlatformFamily_Windows
		if (_Encoding == EPerforceEncoding_CodePage)
		{
			NStr::CAnsiStr Encoded;
			NStr::NPlatform::fg_SystemEncodeAnsiStr(_String, Encoded, '?');
			return Encoded;
		}
#endif
		return NStr::CAnsiStr(_String.f_GetStr(), _String.f_GetLen());
	}
}

#ifdef DPlatformFamily_Windows
namespace NMib::NPerforce
{
	CFileNameCharset::CFileNameCharset(int _Charset)
	{
		GlobalCharSet::UseAlt(true);
		GlobalCharSet::Set(_Charset);
	}

	CFileNameCharset::~CFileNameCharset()
	{
		GlobalCharSet::UseAlt(false);
	}

	// The encoding the Perforce API's file functions take a name in on this thread: UTF-8 when GlobalCharSet is a Unicode charset
	// (FileIO's DOUNICODE), else the code page.
	EPerforceEncoding fg_APIFileNameEncoding()
	{
		if (CharSetApi::isUnicode(CharSetApi::CharSet(GlobalCharSet::Get())))
			return EPerforceEncoding_UTF8;
		return EPerforceEncoding_CodePage;
	}
}
#endif
