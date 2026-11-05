// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <Mib/Core/Core>
#include <Mib/File/File>
#include <Mib/String/Appender>
#include <Mib/Test/Exception>
#include <Mib/Test/Test>

#include <Mib/Perforce/Ignore>

#ifdef DPlatformFamily_Windows
#	include <Windows.h>
#else
#	include <fcntl.h>
#	include <stdlib.h>
#	include <sys/stat.h>
#	include <unistd.h>
#endif

#undef SetPort

#ifdef DCompiler_MSVC
#	pragma warning(push,2)
#endif
#include "clientapi.h"
#include "charset.h"
#include "enviro.h"
#include "i18napi.h"
#include "ignore.h"
#include "strarray.h"
#ifdef DCompiler_MSVC
#	pragma warning(pop)
#endif

namespace
{
	using namespace NMib;
	using namespace NMib::NStr;
	using namespace NMib::NContainer;
	using namespace NMib::NFile;
	using namespace NMib::NPerforce;

	TCVector<CAnsiStr> *g_pOpenedRuleFiles = nullptr; // The rule files p4's matcher opens while the reference runs

	int fg_RecordRuleFile(char const *_pPath)
	{
		if (g_pOpenedRuleFiles)
			g_pOpenedRuleFiles->f_Insert(CAnsiStr(_pPath));
		return 1;
	}

	enum ESource
	{
		ESource_ConfigFile
		, ESource_EnviroFile
		, ESource_Environment
	};

	// One case of the matrix. On Windows a value set in the environment is text, here in UTF-8, and a settings file's value its
	// bytes; outside Windows both are bytes.
	struct CCase
	{
		CStr f_GetName() const;

		CAnsiStr m_IgnoreName;
		CStr m_Charset;
		CStr m_CommandCharset;
		ESource m_Source = ESource_ConfigFile;
		bool m_bLearned = false;
	};

	struct CResult
	{
		TCVector<CStr> m_ReadTags;   // Of the rule files whose rules apply: each ignores the probe Probe_<tag>.cpp, sorted
		TCVector<CStr> m_OtherTags;  // p4: of the rule files it opened; Malterlib: of the rule files it tracks, sorted
		bool m_bIgnored = false;     // Whether the non-ASCII probe, Probé.cpp, is ignored
	};

	constexpr ch8 const *gc_Tags[] = {"CP1252", "UTF-8", "CP850"}; // The rule files' tags, by the encoding of their names and rules

	CStr CCase::f_GetName() const
	{
		CStr IgnoreName;
		{
			CStr::CAppender Appender(IgnoreName);
			auto *pName = (uint8 const *)m_IgnoreName.f_GetStr();
			for (auto *pCharacter = pName, *pEnd = pName + m_IgnoreName.f_GetLen(); pCharacter != pEnd; ++pCharacter)
			{
				if (*pCharacter < 0x80)
					Appender.f_AddChar(*pCharacter);
				else
					Appender += CStr("\\x{nfh,sj2}"_f << *pCharacter);
			}
		}

		ch8 const *Sources[] = {"P4CONFIG", "enviro file", "environment"};
		return "P4IGNORE={} in the {}, P4CHARSET={}, P4COMMANDCHARSET={}{}"_f
			<< IgnoreName
			<< Sources[m_Source]
			<< (m_Charset ? m_Charset : CStr("unset"))
			<< (m_CommandCharset ? m_CommandCharset : CStr("unset"))
			<< (m_bLearned ? ", learned utf8" : "")
		;
	}

	// A path's bytes as the untranslated Perforce API takes them: on Windows in the code page, elsewhere as they are.
	CAnsiStr fg_PathBytes(CStr const &_Path)
	{
#ifdef DPlatformFamily_Windows
		CAnsiStr Bytes;
		NStr::NPlatform::fg_SystemEncodeAnsiStr(_Path, Bytes, '?');
		return Bytes;
#else
		return CAnsiStr(_Path.f_GetStr(), _Path.f_GetLen());
#endif
	}

	// A name's bytes as text: on Windows decoded from the code page, elsewhere the bytes, which needn't be UTF-8.
	CStr fg_NameText(CAnsiStr const &_Name)
	{
#ifdef DPlatformFamily_Windows
		CStr Name;
		NStr::NPlatform::fg_SystemDecodeAnsiStr(_Name, Name);
		return Name;
#else
		return CStr(_Name.f_GetStr(), _Name.f_GetLen());
#endif
	}

	// Writes a file in a directory, named by bytes, which outside Windows needn't be UTF-8, and on macOS can't be.
	void fg_WriteFileBytes(CStr const &_Directory, CAnsiStr const &_Name, CAnsiStr const &_Contents)
	{
		CFile::fs_CreateDirectory(_Directory);
#ifdef DPlatformFamily_Windows
		CByteVector Contents;
		Contents.f_Insert((uint8 const *)_Contents.f_GetStr(), _Contents.f_GetLen());
		CFile::fs_WriteFile(_Directory / fg_NameText(_Name), Contents);
#else
		CAnsiStr Path = fg_PathBytes(_Directory) + "/" + _Name;
		int File = open(Path.f_GetStr(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
		if (File < 0)
			return;
		[[maybe_unused]] ssize_t nWritten = write(File, _Contents.f_GetStr(), _Contents.f_GetLen());
		close(File);
#endif
	}

	// The tag of a rule file, read with Malterlib's file functions, or empty.
	CStr fg_ReadTag(CStr const &_Path)
	{
		CByteVector Data;
		try
		{
#ifdef DPlatformFamily_Windows
			if (!CFile::fs_FileExists(_Path, EFileAttrib_File))
				return {};
			Data = CFile::fs_ReadFile(_Path);
#else
			int File = open(_Path.f_GetStr(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
			if (File < 0)
				return {};
			struct stat Stat;
			if (fstat(File, &Stat) != 0 || !S_ISREG(Stat.st_mode))
			{
				close(File);
				return {};
			}
			Data.f_SetLen(4096);
			ssize_t nRead = read(File, Data.f_GetArray(), Data.f_GetLen());
			close(File);
			Data.f_SetLen(nRead > 0 ? umint(nRead) : 0);
#endif
		}
		catch (CException const &)
		{
			return {};
		}

		CStr Contents((ch8 const *)Data.f_GetArray(), Data.f_GetLen());
		if (!Contents.f_StartsWith("# tag "))
			return {};
		return Contents.f_Extract(6, Contents.f_FindChar('\n') - 6);
	}

	// What p4 does, through the Perforce API's own code: clientPrepareEnv's steps as p4 runs it with -d (clientmain.cc, written
	// out here with the API's calls, as linking the command line client's own needs the rest of it), Client::Init's setup of a
	// learned charset (SetupUnicode's LateUnicodeSetup, here without connecting), Client::GetIgnoreFile and P4CONFIG, the path of
	// the probe as p4 reconcile lists it (FileSys::ScanDir) and the matcher's Reject, recording the rule files it opens and reading
	// them with the API's file functions.
	CResult fg_Reference(CStr const &_WorkspaceText)
	{
		GlobalCharSet::UseAlt(true);
		GlobalCharSet::Set(0);
		auto Cleanup = g_OnScopeExit / []
			{
				g_pOpenedRuleFiles = nullptr;
				GlobalCharSet::UseAlt(false);
			}
		;

		CResult Result;
		Error P4Error;
		ClientApi Client;
		Enviro Enviro;

		// clientPrepareEnv: -d, the separate settings read from the directory, and P4CHARSET with P4COMMANDCHARSET. p4 run in the
		// directory has it in the code page untranslated (HostEnv::GetCwdbyCS), as given here.
		Client.SetCwd(fg_PathBytes(_WorkspaceText).f_GetStr());
		Enviro.Config(Client.GetCwd());
		if (char const *pCharset = Enviro.Get("P4CHARSET"))
		{
			int Charset = CharSetApi::Lookup(pCharset); // CharSetCvt::Lookup, which is CharSetApi's
			if (Charset == CharSetApi::CSLOOKUP_ERROR)
				return Result;

			int Content = Charset;
			if (char const *pCommandCharset = Enviro.Get("P4COMMANDCHARSET"))
			{
				Charset = CharSetApi::Lookup(pCommandCharset, &Enviro);
				if (Charset == -1)
					return Result;
			}
			if (CharSetApi::Granularity(CharSetApi::CharSet(Charset)) != 1)
			{
				Charset = CharSetApi::Discover(&Enviro);
				if (Charset == -1)
					return Result;
			}
			Client.SetTrans(Charset, Content, Charset, Charset);
		}

		// Without P4CHARSET clientPrepareEnv sets up no translation, and Init's SetupUnicode takes the charset learned for the port.
		if (!Enviro.Get("P4CHARSET"))
		{
			CAnsiStr Learned = Client.GetCharset().Text();
			if (Learned)
			{
				int Charset = CharSetApi::Lookup(Learned.f_GetStr());
				if (Charset >= 0)
					Client.SetTrans(Charset);
			}
		}

		// With a translation p4 run in the directory has it again in its file names' charset (SetTrans clears its own cwd): with
		// UTF-8 file names in UTF-8.
		CAnsiStr Workspace = fg_PathBytes(_WorkspaceText);
		if (CharSetApi::isUnicode(CharSetApi::CharSet(GlobalCharSet::Get())))
		{
			Workspace = CAnsiStr(_WorkspaceText.f_GetStr(), _WorkspaceText.f_GetLen());
			Client.SetCwd(Workspace.f_GetStr());
		}

		StrBuf IgnoreFile;
		IgnoreFile.Set(Client.GetIgnoreFile());
		char const *pConfigName = Client.GetEnviro()->Get("P4CONFIG");

		FileSys *pDirectory = FileSys::Create(FST_BINARY);
		pDirectory->Set(StrRef(Workspace.f_GetStr()));
		StrArray *pNames = pDirectory->ScanDir(&P4Error);
		delete pDirectory;
		TCVector<CAnsiStr> Opened;
		g_pOpenedRuleFiles = &Opened;
		Client.GetIgnore()->SetFileFilter(fg_RecordRuleFile);
		for (int iName = 0; pNames && iName < pNames->Count(); ++iName)
		{
			CAnsiStr Name = pNames->Get(iName)->Text();
			if (!Name.f_StartsWith("Prob"))
				continue;
#ifdef DPlatformFamily_Windows
			CAnsiStr ProbePath = Workspace + "\\" + Name;
#else
			CAnsiStr ProbePath = Workspace + "/" + Name;
#endif
			bool bIgnored = Client.GetIgnore()->Reject(StrRef(ProbePath.f_GetStr()), IgnoreFile, pConfigName) != 0;
			if (!Name.f_StartsWith("Probe_"))
				Result.m_bIgnored = bIgnored;
			else if (bIgnored)
				Result.m_ReadTags.f_Insert(CStr(Name.f_GetStr() + 6, Name.f_GetLen() - 10));
		}
		g_pOpenedRuleFiles = nullptr;
		delete pNames;
		Result.m_ReadTags.f_Sort();

		TCSet<CStr> Tags;
		for (auto &Name : Opened)
		{
			FileSys *pFile = FileSys::Create(FST_BINARY);
			pFile->Set(StrRef(Name.f_GetStr()));
			Error OpenError;
			pFile->Open(FOM_READ, &OpenError);
			if (!OpenError.Test())
			{
				char Buffer[4096];
				int nRead = pFile->Read(Buffer, sizeof(Buffer), &OpenError);
				pFile->Close(&OpenError);
				CStr Contents(Buffer, nRead > 0 ? nRead : 0);
				if (Contents.f_StartsWith("# tag "))
					Tags[Contents.f_Extract(6, Contents.f_FindChar('\n') - 6)];
			}
			delete pFile;
		}
		for (auto &Tag : Tags)
			Result.m_OtherTags.f_Insert(Tag);

		return Result;
	}

	// The process's environment, which p4 reads: variables set for a case and restored after it.
	struct CProcessEnvironment
	{
		~CProcessEnvironment();

		void f_Set(CStr const &_Name, CAnsiStr const *_pValue);

		TCMap<CStr, TCOptional<CAnsiStr>> m_Previous;
	};

	CProcessEnvironment::~CProcessEnvironment()
	{
		for (auto iPrevious = m_Previous.f_GetIterator(); iPrevious; ++iPrevious)
		{
			auto &Value = *iPrevious;
			f_Set(iPrevious.f_GetKey(), Value ? &*Value : nullptr);
		}
	}

	// On Windows _pValue is UTF-8 text, set as text; outside Windows bytes.
	void CProcessEnvironment::f_Set(CStr const &_Name, CAnsiStr const *_pValue)
	{
#ifdef DPlatformFamily_Windows
		auto fWide = [](CStr const &_String) -> TCVector<wchar_t>
			{
				TCVector<wchar_t> Wide;
				int nWide = MultiByteToWideChar(CP_UTF8, 0, _String.f_GetStr(), -1, nullptr, 0);
				Wide.f_SetLen(umint(nWide > 0 ? nWide : 1));
				Wide[0] = 0;
				MultiByteToWideChar(CP_UTF8, 0, _String.f_GetStr(), -1, Wide.f_GetArray(), nWide);
				return Wide;
			}
		;
		auto Name = fWide(_Name);
		if (!m_Previous.f_FindEqual(_Name))
		{
			TCOptional<CAnsiStr> Previous;
			wchar_t Buffer[32768];
			DWORD nLength = GetEnvironmentVariableW(Name.f_GetArray(), Buffer, 32768);
			if (nLength > 0 && nLength < 32768)
			{
				int nUTF8 = WideCharToMultiByte(CP_UTF8, 0, Buffer, -1, nullptr, 0, nullptr, nullptr);
				TCVector<char> UTF8;
				UTF8.f_SetLen(umint(nUTF8));
				WideCharToMultiByte(CP_UTF8, 0, Buffer, -1, UTF8.f_GetArray(), nUTF8, nullptr, nullptr);
				Previous = CAnsiStr(UTF8.f_GetArray());
			}
			m_Previous[_Name] = Previous;
		}
		if (_pValue)
			SetEnvironmentVariableW(Name.f_GetArray(), fWide(CStr(_pValue->f_GetStr(), _pValue->f_GetLen())).f_GetArray());
		else
			SetEnvironmentVariableW(Name.f_GetArray(), nullptr);
#else
		if (!m_Previous.f_FindEqual(_Name))
		{
			char const *pPrevious = getenv(_Name.f_GetStr());
			m_Previous[_Name] = pPrevious ? TCOptional<CAnsiStr>(CAnsiStr(pPrevious)) : TCOptional<CAnsiStr>();
		}
		if (_pValue)
			setenv(_Name.f_GetStr(), _pValue->f_GetStr(), 1);
		else
			unsetenv(_Name.f_GetStr());
#endif
	}

	struct CCharsets_Tests : public NMib::NTest::CTest
	{
		void f_DoTests();
	};

	void CCharsets_Tests::f_DoTests()
	{
		// Generated: settings values naming a rule file by non-ASCII bytes, from a P4CONFIG file, the enviro file and the environment,
		// under every combination of P4CHARSET, P4COMMANDCHARSET and the charset p4 learned for the port. In each case the rule
		// files read and whether a non-ASCII probe is ignored must be what the Perforce API's own code gives (fg_Reference).
		DMibTestSuite("Matrix")
		{
			CStr Root = CFile::fs_GetProgramDirectory() / "TestPerforceCharsets";
			if (CFile::fs_FileExists(Root, EFileAttrib_Directory))
				CFile::fs_DeleteDirectoryRecursive(Root, true);
			CFile::fs_CreateDirectory(Root);
			NTest::fg_TestAddCleanupPath(Root);
			CFile::fs_CreateDirectory(Root / "Home");

			// The rule files' names in each encoding's bytes, each file tagged and ignoring the probe Probé.cpp written in the same
			// encoding. On Windows the names on disk are the code page's decoding of those bytes, so the UTF-8 bytes name the file
			// of the CP1252 ones when p4 names files in UTF-8.
			struct CEncoding
			{
				CAnsiStr m_Character;
				ch8 const *m_pTag;
			};
			TCVector<CEncoding> Encodings = {{"\xE9", gc_Tags[0]}, {"\xC3\xA9", gc_Tags[1]}, {"\x82", gc_Tags[2]}};
#ifdef DPlatformFamily_Windows
			if (GetACP() != 1252)
			{
				DMibConOut("Skipping the Perforce charset matrix: the active code page is {}, not 1252\n", GetACP());
				return;
			}
			CAnsiStr Separator = "\\";
#else
			CAnsiStr Separator = "/";
#endif
			TCVector<CStr> Charsets = {"", "none", "utf8", "winansi", "cp850"};
			TCVector<CStr> CommandCharsets = {"", "utf8"};

			TCVector<CStr> Failures;
			umint nCases = 0;
			umint nTagged = 0;
			umint nIgnored = 0;
			for (auto &Encoding : Encodings)
			{
				for (ESource Source : {ESource_ConfigFile, ESource_EnviroFile, ESource_Environment})
				{
					for (auto &Charset : Charsets)
					{
						for (auto &CommandCharset : CommandCharsets)
						{
							for (bool bLearned : {false, true})
							{
								// The paths are text, of the deployment's directory, and only the rule files' names are bytes.
								CStr CaseRoot = Root / CStr("Case{}"_f << nCases);
								++nCases;
								CStr Workspace = CaseRoot / "Workspace";
								CStr Rules = CaseRoot / "Rules";

								// p4 opens a relative P4IGNORE path from its current directory, which isn't the case's. A settings
								// file's value is the bytes of the path as the untranslated API takes it.
								CAnsiStr RuleName = CAnsiStr("caf") + Encoding.m_Character + ".ignore";
								CCase Case;
								Case.m_IgnoreName = fg_PathBytes(Rules) + Separator + RuleName;
								Case.m_Charset = Charset;
								Case.m_CommandCharset = CommandCharset;
								Case.m_Source = Source;
								Case.m_bLearned = bLearned;

								for (auto &RuleEncoding : Encodings)
								{
									fg_WriteFileBytes
										(
											Rules
											, CAnsiStr("caf") + RuleEncoding.m_Character + ".ignore"
											, CAnsiStr("# tag ") + RuleEncoding.m_pTag + "\nProbe_" + RuleEncoding.m_pTag + ".cpp\nProb" + RuleEncoding.m_Character + ".cpp\n"
										)
									;
								}
								for (ch8 const *pTag : gc_Tags)
									fg_WriteFileBytes(Workspace, CAnsiStr("Probe_") + pTag + ".cpp", "");
#ifdef DPlatformFamily_Windows
								fg_WriteFileBytes(Workspace, "Prob\xE9.cpp", "");
#else
								fg_WriteFileBytes(Workspace, "Prob\xC3\xA9.cpp", "");
#endif

								// The environment's value is text on Windows, whose code page bytes are the name's, and bytes elsewhere.
								CStr EnvironmentIgnoreName = Rules / fg_NameText(RuleName);
								CAnsiStr Settings;
								{
									CAnsiStr::CAppender Appender(Settings);
									Appender += "P4IGNORE=";
									Appender += Case.m_IgnoreName;
									Appender.f_AddChar('\n');
									if (Charset)
									{
										Appender += "P4CHARSET=";
										Appender += Charset.f_GetStr();
										Appender.f_AddChar('\n');
									}
									if (CommandCharset)
									{
										Appender += "P4COMMANDCHARSET=";
										Appender += CommandCharset.f_GetStr();
										Appender.f_AddChar('\n');
									}
								}

								CAnsiStr EnviroSettings = bLearned ? CAnsiStr("P4_127.0.0.1:1_CHARSET=utf8\n") : CAnsiStr();
								if (Source == ESource_EnviroFile)
									EnviroSettings += Settings;
								fg_WriteFileBytes(CaseRoot, "p4enviro", EnviroSettings);
								if (Source == ESource_ConfigFile)
									fg_WriteFileBytes(Workspace, ".p4config", Settings);

								// Text, as the environment's values are on Windows; elsewhere the same bytes.
								TCMap<CStr, CAnsiStr> Variables;
								auto fText = [](CStr const &_Text)
									{
										return CAnsiStr(_Text.f_GetStr(), _Text.f_GetLen());
									}
								;
								Variables["P4PORT"] = "127.0.0.1:1";
								Variables["P4ENVIRO"] = fText(CaseRoot / "p4enviro");
								Variables["P4CONFIG"] = ".p4config";
#ifdef DPlatformFamily_Windows
								Variables["USERPROFILE"] = fText(Root / "Home");
#else
								Variables["HOME"] = fText(Root / "Home");
#endif
								if (Source == ESource_Environment)
								{
									Variables["P4IGNORE"] = fText(EnvironmentIgnoreName);
									if (Charset)
										Variables["P4CHARSET"] = Charset.f_GetStr();
									if (CommandCharset)
										Variables["P4COMMANDCHARSET"] = CommandCharset.f_GetStr();
								}

								CResult Reference;
								{
									CProcessEnvironment ProcessEnvironment;
									for (CStr Name : {"P4CONFIG", "P4IGNORE", "P4ENVIRO", "P4CHARSET", "P4COMMANDCHARSET", "P4PORT", "P4CLIENT", "P4USER", "HOME", "USERPROFILE"})
										ProcessEnvironment.f_Set(Name, Variables.f_FindEqual(Name));
									CAnsiStr NoRegistry = "Software\\MalterlibPerforceTestNoRegistry";
									ProcessEnvironment.f_Set("P4TEST_REGISTRY_PATH_T4_TEST", &NoRegistry);
									Reference = fg_Reference(Workspace);
								}

								TCMap<CStr, CStr> Environment;
								for (auto iVariable = Variables.f_GetIterator(); iVariable; ++iVariable)
									Environment[iVariable.f_GetKey()] = CStr(iVariable->f_GetStr(), iVariable->f_GetLen());
								CStr WorkspacePath = Workspace;
								auto IgnoreSettings = CPerforceIgnore::fs_GetSettings(WorkspacePath, Environment);
								TCVector<CStr> RuleFiles;
								CPerforceRootFiles(WorkspacePath, {}, IgnoreSettings).f_ListExcluded(&RuleFiles);
								TCSet<CStr> Tags;
								for (auto &RuleFile : RuleFiles)
								{
									if (CStr Tag = fg_ReadTag(RuleFile))
										Tags[Tag];
								}
								CResult Result;
								for (auto &Tag : Tags)
									Result.m_OtherTags.f_Insert(Tag);
								CPerforceIgnore Ignore(IgnoreSettings);
								Result.m_bIgnored = Ignore.f_IsIgnored(WorkspacePath / "Prob\u00e9.cpp", false);
								for (ch8 const *pTag : gc_Tags)
								{
									if (Ignore.f_IsIgnored(WorkspacePath / CStr("Probe_{}.cpp"_f << pTag), false))
										Result.m_ReadTags.f_Insert(pTag);
								}
								Result.m_ReadTags.f_Sort();

								// The rules of the files p4 reads apply, the non-ASCII probe matches as in p4, and every file p4 opens is
								// tracked.
								bool bTracked = true;
								for (auto &Tag : Reference.m_OtherTags)
									bTracked = bTracked && Tags.f_FindEqual(Tag);
								if (Result.m_ReadTags != Reference.m_ReadTags || Result.m_bIgnored != Reference.m_bIgnored || !bTracked)
								{
									Failures.f_Insert
										(
											"{}: p4 applies [{}], opens [{}] and {} the probe; Malterlib applies [{}], tracks [{}] and {} it"_f
											<< Case.f_GetName()
											<< CStr::fs_Join(Reference.m_ReadTags, ", ")
											<< CStr::fs_Join(Reference.m_OtherTags, ", ")
											<< (Reference.m_bIgnored ? "ignores" : "keeps")
											<< CStr::fs_Join(Result.m_ReadTags, ", ")
											<< CStr::fs_Join(Result.m_OtherTags, ", ")
											<< (Result.m_bIgnored ? "ignores" : "keeps")
										)
									;
								}
								nTagged += Reference.m_ReadTags.f_IsEmpty() ? 0 : 1;
								nIgnored += Reference.m_bIgnored ? 1 : 0;
							}
						}
					}
				}
			}

			DMibExpect(Failures, ==, TCVector<CStr>{});
			// The matrix reaches the files and the rules: p4 applies a tagged file at least in the cases of the UTF-8 name (macOS can't
			// have the other names), and in some the non-ASCII probe matches.
			DMibExpect(nTagged, >=, nCases / 3);
			DMibExpect(nIgnored, >, nCases / 10);
		};
	}

	DMibTestRegister(CCharsets_Tests, Malterlib::Perforce);
}
