// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <Mib/Core/Core>
#include <Mib/Concurrency/AsyncDestroy>
#include <Mib/File/File>
#include <Mib/Test/Exception>
#include <Mib/Test/Test>

#include <Mib/Perforce/Ignore>
#include <Mib/Perforce/TestServer>
#include <Mib/Perforce/Wrapper>

#ifdef DPlatformFamily_Windows
#	include <Windows.h>
#	include <AclAPI.h>
#else
#	include <fcntl.h>
#	include <limits.h>
#	include <stdio.h>
#	include <stdlib.h>
#	include <sys/stat.h>
#	include <unistd.h>
#endif

namespace
{
	using namespace NMib;
	using namespace NMib::NStr;
	using namespace NMib::NContainer;
	using namespace NMib::NConcurrency;
	using namespace NMib::NFile;
	using namespace NMib::NPerforce;

	void fg_WriteFile(CStr const &_Path, CStr const &_Contents)
	{
		CFile::fs_CreateDirectoryForFile(_Path);
		CFile::fs_WriteStringToFile(_Path, _Contents, false);
	}

	// The values as their descriptions, joined by ';'.
	CStr fg_JoinDescriptions(TCVector<CPerforceIgnore::CSettingValue> const &_Values)
	{
		TCVector<CStr> Descriptions;
		for (auto &Value : _Values)
			Descriptions.f_Insert(Value.f_GetDescription());
		return CStr::fs_Join(Descriptions, ";");
	}

	CStr fg_MakeTestRoot(CStr const &_Name)
	{
		CStr Root = CFile::fs_GetProgramDirectory() / _Name;
		if (CFile::fs_FileExists(Root, EFileAttrib_Directory))
			CFile::fs_DeleteDirectoryRecursive(Root, true);
		CFile::fs_CreateDirectory(Root);
		NTest::fg_TestAddCleanupPath(Root);

		return Root;
	}

#ifdef DPlatformFamily_Windows
	// A non-ASCII character that the active code page represents, one of several scripts' as code pages represent different ones,
	// or empty when it represents none of them.
	CStr fg_CodePageCharacter()
	{
		for (CStr Character : {"\u00e9", "\u0436", "\u03bb", "\u05d0", "\u0639", "\u0e01", "\u3042", "\u4e2d", "\uac00"})
		{
			CAnsiStr Encoded;
			NStr::NPlatform::fg_SystemEncodeAnsiStr(Character, Encoded, '?');
			CStr Decoded;
			NStr::NPlatform::fg_SystemDecodeAnsiStr(Encoded, Decoded);
			if (Decoded == Character)
				return Character;
		}

		return {};
	}
#endif

	struct CRootFiles_Tests : public NMib::NTest::CTest
	{
		void f_DoTests();
	};

	void CRootFiles_Tests::f_DoTests()
	{
		DMibTestSuite("Ignore")
		{
			CStr Root = fg_MakeTestRoot("TestPerforceRootFiles");
			// Explicit, so the user's environment and enviro file do not change what is ignored.
			CPerforceIgnore::CSettings Settings{.m_IgnoreNames = {".p4ignore"}, .m_ConfigName = ".p4config"};
			CStr Workspace = Root / "Workspace";

			fg_WriteFile(Workspace / ".p4config", "P4CLIENT=Test\n");
			fg_WriteFile(Workspace / ".p4ignore", "/BuildSystem/\n*.log\n!Keep.log\n/Generated\n");
			fg_WriteFile(Workspace / "A.cpp", "");
			fg_WriteFile(Workspace / "Keep.log", "");
			fg_WriteFile(Workspace / "Debug.log", "");
			fg_WriteFile(Workspace / "BuildSystem/Out.h", "");
			fg_WriteFile(Workspace / "Generated/Generated.cpp", "");
			fg_WriteFile(Workspace / "Source/B.cpp", "");
			fg_WriteFile(Workspace / "Source/.p4ignore", "Local.cpp\n");
			fg_WriteFile(Workspace / "Source/Local.cpp", "");
			fg_WriteFile(Workspace / "Source/Deep/Local.cpp", "");
			// A git repository checked out in the workspace, and one that the build system knows by its location.
			fg_WriteFile(Workspace / "Repository/.git/HEAD", "ref: refs/heads/main\n");
			fg_WriteFile(Workspace / "Repository/Repository.cpp", "");
			fg_WriteFile(Workspace / "Other/Other.cpp", "");
#ifndef DPlatformFamily_Windows
			// Excluded, but not among the wildcards of the exclusions, where it would match other files.
			fg_WriteFile(Workspace / "Literal?.log", "");
#endif

			{
				DMibTestPath("Matcher");
				CPerforceIgnore Ignore(Settings);
				// A rule for what is below a directory excludes the files, and a rule for the directory the directory.
				DMibExpectFalse(Ignore.f_IsIgnored(Workspace / "BuildSystem", true));
				DMibExpectTrue(Ignore.f_IsIgnored(Workspace / "BuildSystem/Out.h", false));
				DMibExpectTrue(Ignore.f_IsIgnored(Workspace / "Generated", true));
				DMibExpectTrue(Ignore.f_IsIgnored(Workspace / "Debug.log", false));
				DMibExpectTrue(Ignore.f_IsIgnored(Workspace / ".p4config", false));
				DMibExpectTrue(Ignore.f_IsIgnored(Workspace / "Source/Deep/Local.cpp", false));
				DMibExpectFalse(Ignore.f_IsIgnored(Workspace / "Keep.log", false));
				DMibExpectFalse(Ignore.f_IsIgnored(Workspace / "Source", true));
				DMibExpectFalse(Ignore.f_IsIgnored(Workspace / "Source/B.cpp", false));
				// The rules of a directory apply below it only.
				DMibExpectFalse(Ignore.f_IsIgnored(Workspace / "Local.cpp", false));
#if defined(DPlatformFamily_Windows) || defined(DPlatformFamily_macOS)
				// As for p4 on Windows and macOS, rules match regardless of case.
				DMibExpectTrue(Ignore.f_IsIgnored(Workspace / "generated", true));
				DMibExpectTrue(Ignore.f_IsIgnored(Workspace / "DEBUG.LOG", false));
#else
				DMibExpectFalse(Ignore.f_IsIgnored(Workspace / "generated", true));
#endif
			}

			{
				DMibTestPath("List");
				TCSet<CStr> OtherRepositories;
				OtherRepositories[Workspace / "Other"];
				CPerforceRootFiles RootFiles(Workspace, OtherRepositories, Settings);
				DMibExpect(RootFiles.f_List(), ==, (TCVector<CStr>{".p4ignore", "A.cpp", "Keep.log", "Source/.p4ignore", "Source/B.cpp"}));
				// A repository's location is its directory however the case of the location is spelled where the file system ignores
				// case: on Windows, and on macOS a volume that ignores it, as by default.
#if defined(DPlatformFamily_Windows)
				bool bIgnoresCase = true;
#elif defined(DPlatformFamily_macOS)
				bool bIgnoresCase = pathconf(Workspace.f_GetStr(), _PC_CASE_SENSITIVE) == 0;
#else
				bool bIgnoresCase = false;
#endif
				TCSet<CStr> OtherCaseRepositories;
				OtherCaseRepositories[Workspace / "OTHER"];
				auto OtherCaseFiles = CPerforceRootFiles(Workspace, OtherCaseRepositories, Settings).f_List();
				if (bIgnoresCase)
					DMibExpect(OtherCaseFiles, ==, RootFiles.f_List());
				else
					DMibExpect(OtherCaseFiles, !=, RootFiles.f_List());

				// Also when the case differs in a non-ASCII letter, or the letter's normalization does: a location is the directory
				// exactly when the file system finds the directory by it. macOS volumes find it by either normalization, also those
				// that keep case.
				{
					DMibTestPath("NonASCIICase");
					struct CLetter
					{
						CStr m_Lower;
						CStr m_Upper;
						CStr m_UpperDecomposed;
						CStr m_LowerDecomposed;
					};
					CLetter Letter{"\u00e9", "\u00c9", "E\u0301", "e\u0301"};
#ifdef DPlatformFamily_Windows
					// A letter the active code page represents, as the paths are matched in it.
					Letter = {};
					for (CLetter const &Candidate : {CLetter{"\u00e9", "\u00c9", "E\u0301", "e\u0301"}, CLetter{"\u0436", "\u0416"}, CLetter{"\u03bb", "\u039b"}})
					{
						CAnsiStr Encoded;
						NStr::NPlatform::fg_SystemEncodeAnsiStr(Candidate.m_Lower, Encoded, '?');
						CStr Decoded;
						NStr::NPlatform::fg_SystemDecodeAnsiStr(Encoded, Decoded);
						if (Decoded == Candidate.m_Lower)
						{
							Letter = Candidate;
							break;
						}
					}
					if (Letter.m_Lower.f_IsEmpty())
						DMibConOut("Skipping the non-ASCII case of repository locations: the active code page has none of the letters\n");
#endif
					if (!Letter.m_Lower.f_IsEmpty())
					{
						CStr CaseRoot = Root / "NonASCIICase";
						CStr Library = "Lib" + Letter.m_Lower;
						fg_WriteFile(CaseRoot / "A.cpp", "");
						fg_WriteFile(CaseRoot / Library / (Library + ".cpp"), "");
						for (CStr const &Spelling : {"LIB" + Letter.m_Upper, "LIB" + Letter.m_UpperDecomposed, "Lib" + Letter.m_LowerDecomposed})
						{
							if (Spelling == "LIB" || Spelling == "Lib")
								continue;
							DMibTestPath(Spelling);
							TCSet<CStr> CaseRepositories;
							CaseRepositories[CaseRoot / Spelling];
							bool bSameDirectory = CFile::fs_FileExists(CaseRoot / Spelling, EFileAttrib_Directory);
							if (Spelling == "LIB" + Letter.m_Upper)
								DMibExpect(bSameDirectory, ==, bIgnoresCase);
#ifdef DPlatformFamily_macOS
							if (Spelling == "Lib" + Letter.m_LowerDecomposed)
								DMibExpectTrue(bSameDirectory);
#endif
							auto CaseFiles = CPerforceRootFiles(CaseRoot, CaseRepositories, Settings).f_List();
							if (bSameDirectory)
								DMibExpect(CaseFiles, ==, TCVector<CStr>{"A.cpp"});
							else
								DMibExpect(CaseFiles, ==, (TCVector<CStr>{"A.cpp", Library / (Library + ".cpp")}));
						}
					}
#ifndef DPlatformFamily_Windows
					{
						// Spellings a volume that ignores case takes for the same directory although no case folding of the
						// names makes them equal: U+1C80, an old form of U+0432, finds Lib\u0432 on case-insensitive APFS.
						DMibTestPath("FoldingTables");
						CStr FoldingRoot = Root / "FoldingTables";
						fg_WriteFile(FoldingRoot / "A.cpp", "");
						fg_WriteFile(FoldingRoot / "Lib\u0432/Lib.cpp", "");
						for (CStr const &Spelling : {CStr("Lib\u1c80"), CStr("LIB\u0412")})
						{
							DMibTestPath(Spelling);
							TCSet<CStr> FoldingRepositories;
							FoldingRepositories[FoldingRoot / Spelling];
							bool bSameDirectory = CFile::fs_FileExists(FoldingRoot / Spelling, EFileAttrib_Directory);
							auto FoldingFiles = CPerforceRootFiles(FoldingRoot, FoldingRepositories, Settings).f_List();
							if (bSameDirectory)
								DMibExpect(FoldingFiles, ==, TCVector<CStr>{"A.cpp"});
							else
								DMibExpect(FoldingFiles, ==, (TCVector<CStr>{"A.cpp", "Lib\u0432/Lib.cpp"}));
						}
					}
#endif
				}

				TCVector<CStr> RuleFiles;
				DMibExpect
					(
						RootFiles.f_ListExcluded(&RuleFiles)
						, ==
						,
						(
							TCVector<CStr>
							{
								".p4config"
								, "BuildSystem/Out.h"
								, "Debug.log"
								, "Generated/*"
								, "Other/*"
								, "Repository/*"
								, "Source/Deep/Local.cpp"
								, "Source/Local.cpp"
							}
						)
					)
				;
				TCSet<CStr> RuleFileSet;
				for (auto &RuleFile : RuleFiles)
					RuleFileSet[RuleFile];
				DMibExpectTrue(RuleFileSet.f_FindEqual(Workspace / ".p4config"));
				DMibExpectTrue(RuleFileSet.f_FindEqual(Workspace / ".p4ignore"));
				DMibExpectTrue(RuleFileSet.f_FindEqual(Workspace / "Source/.p4ignore"));
				DMibExpectTrue(RuleFileSet.f_FindEqual(Workspace / "Source/Deep/.p4ignore"));
				DMibExpectTrue(RuleFileSet.f_FindEqual(Root / ".p4ignore"));
				DMibExpectTrue(RuleFileSet.f_FindEqual(Root / ".p4config"));
				DMibExpectFalse(RuleFileSet.f_FindEqual(Workspace / "Generated/.p4ignore"));
				DMibExpectFalse(RuleFileSet.f_FindEqual(Workspace / "Source/.p4config"));

				// A P4CONFIG with a path is looked for from the root and above it, and an absolute one is the file itself.
				TCVector<CStr> PathConfigRuleFiles;
				CPerforceRootFiles(Workspace, OtherRepositories, CPerforceIgnore::CSettings{.m_IgnoreNames = {".p4ignore"}, .m_ConfigName = "Config/p4config"})
					.f_ListExcluded(&PathConfigRuleFiles)
				;
				TCVector<CStr> AbsoluteConfigRuleFiles;
				CPerforceRootFiles(Workspace, OtherRepositories, CPerforceIgnore::CSettings{.m_IgnoreNames = {".p4ignore"}, .m_ConfigName = Root / "Shared.p4config"})
					.f_ListExcluded(&AbsoluteConfigRuleFiles)
				;
				DMibExpectTrue(PathConfigRuleFiles.f_Contains(Workspace / "Config/p4config") >= 0);
				DMibExpectTrue(PathConfigRuleFiles.f_Contains(Root / "Config/p4config") >= 0);
				DMibExpectTrue(AbsoluteConfigRuleFiles.f_Contains(Root / "Shared.p4config") >= 0);
				DMibExpectFalse(AbsoluteConfigRuleFiles.f_Contains(Workspace / (Root / "Shared.p4config")) >= 0);

				// A root spelled with a separator ending it lists and contains the same files.
				CPerforceRootFiles SeparatorRootFiles(Workspace + "/", OtherRepositories, Settings);
				DMibExpect(SeparatorRootFiles.f_List(), ==, RootFiles.f_List());
				DMibExpectTrue(SeparatorRootFiles.f_Contains(Workspace / "Source/B.cpp"));
				DMibExpectFalse(SeparatorRootFiles.f_Contains(Workspace / "Debug.log"));
				DMibExpectFalse(SeparatorRootFiles.f_Contains(Workspace + "/"));

				// A location that doesn't exist yet is the directory it names when it is made, and the files under it are not the
				// root's: they are identified by the nearest existing directory and the rest of their path.
				CPerforceRootFiles NotYetFiles(Workspace, TCSet<CStr>{Workspace / "NotYet/Lib"}, Settings);
				DMibExpectFalse(NotYetFiles.f_Contains(Workspace / "NotYet/Lib/New.cpp"));
				DMibExpectTrue(NotYetFiles.f_Contains(Workspace / "NotYet/New.cpp"));

				// The membership of single files, existing or not, agrees with the listing.
				DMibExpectTrue(RootFiles.f_Contains(Workspace / "A.cpp"));
				DMibExpectTrue(RootFiles.f_Contains(Workspace / "Source/B.cpp"));
				DMibExpectTrue(RootFiles.f_Contains(Workspace / "Source/New.cpp"));
				DMibExpectFalse(RootFiles.f_Contains(Workspace / "Debug.log"));
				DMibExpectFalse(RootFiles.f_Contains(Workspace / "BuildSystem/Out.h"));
				DMibExpectFalse(RootFiles.f_Contains(Workspace / "Generated/Generated.cpp"));
				DMibExpectFalse(RootFiles.f_Contains(Workspace / "Source/Deep/Local.cpp"));
				DMibExpectFalse(RootFiles.f_Contains(Workspace / "Repository/Repository.cpp"));
				DMibExpectFalse(RootFiles.f_Contains(Workspace / "Other/Other.cpp"));
				DMibExpectFalse(RootFiles.f_Contains(Workspace / ".p4config"));
				DMibExpectFalse(RootFiles.f_Contains(Root / "Outside.cpp"));

				// Paths asked about out of the order of a walk get the same answers.
				CPerforceIgnore Ignore(Settings);
				DMibExpectFalse(Ignore.f_IsIgnored(Workspace / "A.cpp", false));
				DMibExpectFalse(Ignore.f_IsIgnored(Workspace / "Source", true));
				DMibExpectFalse(Ignore.f_IsIgnored(Workspace / "Source/B.cpp", false));
				DMibExpectTrue(Ignore.f_IsIgnored(Workspace / "Debug.log", false));
				DMibExpectFalse(Ignore.f_IsIgnored(Workspace / "BuildSystem", true));
				DMibExpectTrue(Ignore.f_IsIgnored(Workspace / "BuildSystem/Out.h", false));
				DMibExpectTrue(Ignore.f_IsIgnored(Workspace / "Generated", true));
				DMibExpectFalse(Ignore.f_IsIgnored(Workspace / "Source/Deep", true));
				DMibExpectTrue(Ignore.f_IsIgnored(Workspace / "Source/Deep/Local.cpp", false));
			}

			{
				// Relative paths are from the settings' directory; bare names are looked for above each path.
				DMibTestPath("ResolveNames");
#ifdef DPlatformFamily_Windows
				// A list gets backslashes; a single name is kept, and without a backslash it is a bare name, as for p4.
				DMibExpect(CPerforceIgnore::fs_ResolveIgnoreNames("Config/ignore;.p4ignore", "C:/Root"), ==, (TCVector<CStr>{"C:\\Root\\Config\\ignore", ".p4ignore"}));
				DMibExpect(CPerforceIgnore::fs_ResolveIgnoreNames("Config\\ignore", "C:/Root"), ==, (TCVector<CStr>{"C:\\Root\\Config\\ignore"}));
				DMibExpect(CPerforceIgnore::fs_ResolveIgnoreNames("Config/ignore", "C:/Root"), ==, (TCVector<CStr>{"Config/ignore"}));
				// Drive-rooted and drive-relative paths resolve against the directory's drive.
				DMibExpect(CPerforceIgnore::fs_ResolveIgnoreNames("\\Config\\ignore", "D:/Root"), ==, (TCVector<CStr>{"D:\\Config\\ignore"}));
				DMibExpect(CPerforceIgnore::fs_ResolveIgnoreNames("D:Config\\ignore", "D:/Root"), ==, (TCVector<CStr>{"D:\\Root\\Config\\ignore"}));
				DMibExpect(CPerforceIgnore::fs_ResolveIgnoreNames("C:Config\\ignore", "D:/Root"), ==, (TCVector<CStr>{"C:Config\\ignore"}));
				// Under a UNC directory a drive-rooted path is on the current drive, where p4 runs, which Windows spells in capitals.
				DMibExpect
					(
						CPerforceIgnore::fs_ResolveIgnoreNames("\\Config\\ignore", "//Server/Share/Root")
						, ==
						, (TCVector<CStr>{CFile::fs_GetDrive(CFile::fs_GetCurrentDirectory()).f_UpperCase() + "\\Config\\ignore"})
					)
				;
				// A single name with a drive and forward slashes has no backslash, so it is a bare name to the matcher.
				DMibExpect(CPerforceIgnore::fs_ResolveIgnoreNames("C:/Config/ignore", "D:/Root"), ==, (TCVector<CStr>{"C:/Config/ignore"}));
#else
				DMibExpect(CPerforceIgnore::fs_ResolveIgnoreNames("Config/ignore;.p4ignore", "/Root"), ==, (TCVector<CStr>{"/Root/Config/ignore", ".p4ignore"}));
				DMibExpect(CPerforceIgnore::fs_ResolveIgnoreNames("Config/ignore", "/Root"), ==, (TCVector<CStr>{"/Root/Config/ignore"}));
				// A relative path is joined to the directory unchanged, as the file system resolves it from there.
				DMibExpect(CPerforceIgnore::fs_ResolveIgnoreNames("./Config/ignore:/Shared/ignore", "/Root"), ==, (TCVector<CStr>{"/Root/./Config/ignore", "/Shared/ignore"}));
				// p4 makes the separators of a list its own, and keeps a single name as it is.
				DMibExpect(CPerforceIgnore::fs_ResolveIgnoreNames("Config\\ignore;.p4ignore", "/Root"), ==, (TCVector<CStr>{"/Root/Config/ignore", ".p4ignore"}));
				DMibExpect(CPerforceIgnore::fs_ResolveIgnoreNames("Config\\ignore", "/Root"), ==, (TCVector<CStr>{"Config\\ignore"}));
				// A single path keeps its backslashes, which are characters of a name here.
				DMibExpect(CPerforceIgnore::fs_ResolveIgnoreNames("Config\\Local/../ignore", "/Root"), ==, (TCVector<CStr>{"/Root/Config\\Local/../ignore"}));
#endif
				// Only the whole value "unset" is no files; in a list it is a file's name.
				DMibExpect(CPerforceIgnore::fs_ResolveIgnoreNames("unset", Workspace), ==, TCVector<CStr>{});
				DMibExpect(CPerforceIgnore::fs_ResolveIgnoreNames("unset;", Workspace), ==, TCVector<CStr>{"unset"});
				DMibExpect(CPerforceIgnore::fs_ResolveIgnoreNames(";unset", Workspace), ==, TCVector<CStr>{"unset"});

				fg_WriteFile(Workspace / "Config/ignore", "Keep.log\n");
				CPerforceIgnore Ignore(CPerforceIgnore::CSettings{.m_IgnoreNames = CPerforceIgnore::fs_ResolveIgnoreNames("Config/ignore", Workspace), .m_ConfigName = ".p4config"});
				DMibExpectTrue(Ignore.f_IsIgnored(Workspace / "Keep.log", false));

				// A directory with the list's separators in its name keeps a path resolved under it whole.
#ifdef DPlatformFamily_Windows
				CStr SeparatorRoot = Root / "Separator;Root";
				CStr SeparatorIgnoreFile = (SeparatorRoot / "Config/ignore").f_ReplaceChar('/', '\\');
#else
				CStr SeparatorRoot = Root / "Separator:Root;";
				CStr SeparatorIgnoreFile = SeparatorRoot / "Config/ignore";
#endif
				fg_WriteFile(SeparatorRoot / "Config/ignore", "Keep.log\n");
				auto SeparatorNames = CPerforceIgnore::fs_ResolveIgnoreNames("Config/ignore;.p4ignore", SeparatorRoot);
				DMibExpect(SeparatorNames, ==, (TCVector<CStr>{SeparatorIgnoreFile, ".p4ignore"}));
				CPerforceIgnore SeparatorIgnore(CPerforceIgnore::CSettings{.m_IgnoreNames = SeparatorNames, .m_ConfigName = ".p4config"});
				DMibExpectTrue(SeparatorIgnore.f_IsIgnored(SeparatorRoot / "Keep.log", false));
				DMibExpectFalse(SeparatorIgnore.f_IsIgnored(SeparatorRoot / "A.cpp", false));

				// A file named unset in a list is read; the value unset alone reads no file.
				CStr UnsetRoot = Root / "Unset";
				fg_WriteFile(UnsetRoot / "unset", "A.cpp\n");
				CPerforceIgnore UnsetListIgnore(CPerforceIgnore::CSettings{.m_IgnoreNames = CPerforceIgnore::fs_ResolveIgnoreNames("unset;", UnsetRoot)});
				DMibExpectTrue(UnsetListIgnore.f_IsIgnored(UnsetRoot / "A.cpp", false));
				CPerforceIgnore UnsetIgnore(CPerforceIgnore::CSettings{.m_IgnoreNames = CPerforceIgnore::fs_ResolveIgnoreNames("unset", UnsetRoot)});
				DMibExpectFalse(UnsetIgnore.f_IsIgnored(UnsetRoot / "A.cpp", false));
#ifdef DPlatformFamily_Windows

				// A bare name with a drive and a root is that file for every directory, also among the rule files.
				CStr DriveIgnoreFile = Root / "Drive.ignore";
				fg_WriteFile(DriveIgnoreFile, "Keep.log\n");
				CPerforceIgnore::CSettings DriveSettings{.m_IgnoreNames = CPerforceIgnore::fs_ResolveIgnoreNames(DriveIgnoreFile, Workspace), .m_ConfigName = ".p4config"};
				DMibExpectTrue(CPerforceIgnore(DriveSettings).f_IsIgnored(Workspace / "Keep.log", false));
				TCVector<CStr> DriveRuleFiles;
				CPerforceRootFiles(Workspace, {}, DriveSettings).f_ListExcluded(&DriveRuleFiles);
				DMibExpectTrue(DriveRuleFiles.f_Contains(DriveIgnoreFile) >= 0);
				DMibExpectFalse(DriveRuleFiles.f_Contains(Workspace / DriveIgnoreFile) >= 0);
#endif
			}

#ifndef DPlatformFamily_Windows
			{
				// FIFOs, sockets and devices are not files of the root: reading a FIFO would block.
				DMibTestPath("SpecialFiles");
				CStr Special = Root / "Special";
				fg_WriteFile(Special / "A.cpp", "");
				DMibAssert(mkfifo((Special / "Pipe.cpp").f_GetStr(), 0600), ==, 0);
				CPerforceRootFiles RootFiles(Special, {}, Settings);
				DMibExpect(RootFiles.f_List(), ==, TCVector<CStr>{"A.cpp"});
				DMibExpect(RootFiles.f_ListExcluded(), ==, TCVector<CStr>{"Pipe.cpp"});
				DMibExpectTrue(RootFiles.f_Contains(Special / "A.cpp"));
				DMibExpectFalse(RootFiles.f_Contains(Special / "Pipe.cpp"));

				// Rule files that are FIFOs, also through a symbolic link, are not read, as reading one would block.
				CStr SpecialRules = Root / "SpecialRules";
				fg_WriteFile(SpecialRules / "A.cpp", "");
				DMibAssert(mkfifo((SpecialRules / ".p4ignore").f_GetStr(), 0600), ==, 0);
				DMibAssert(mkfifo((Root / "Pipe.ignore").f_GetStr(), 0600), ==, 0);
				DMibAssert(symlink((Root / "Pipe.ignore").f_GetStr(), (SpecialRules / "Linked.ignore").f_GetStr()), ==, 0);
				DMibExpect(CPerforceRootFiles(SpecialRules, {}, Settings).f_List(), ==, TCVector<CStr>{"A.cpp"});
				CPerforceIgnore::CSettings PathSettings{.m_IgnoreNames = {Root / "Pipe.ignore", SpecialRules / "Linked.ignore", "Linked.ignore"}};
				DMibExpectFalse(CPerforceIgnore(PathSettings).f_IsIgnored(SpecialRules / "A.cpp", false));
			}
#endif

#ifndef DPlatformFamily_Windows
			{
				// Outside Windows a backslash is a character of a name: the rule and settings files read are the ones p4 opens, with no
				// file where a backslash would be a separator.
				DMibTestPath("Backslashes");
				CStr Backslash = Root / "Backslash";
				CFile::fs_CreateDirectory(Backslash);
				// Malterlib's file functions take a backslash for a separator, so these files are made and removed by the system's.
				TCVector<CStr> NativePaths;
				auto RemoveNative = g_OnScopeExit / [&]
					{
						for (aint iPath = aint(NativePaths.f_GetLen()) - 1; iPath >= 0; --iPath)
							remove(NativePaths[iPath].f_GetStr());
					}
				;
				// The helpers have no expectations of their own, which would repeat their test paths.
				auto fWriteNative = [&](CStr const &_Path, CStr const &_Contents)
					{
						int File = open(_Path.f_GetStr(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
						if (File < 0)
							return false;

						NativePaths.f_Insert(_Path);
						bool bWritten = write(File, _Contents.f_GetStr(), _Contents.f_GetLen()) == ssize_t(_Contents.f_GetLen());
						close(File);

						return bWritten;
					}
				;
				auto fMakeNativeDirectory = [&](CStr const &_Path)
					{
						if (mkdir(_Path.f_GetStr(), 0700) != 0)
							return false;

						NativePaths.f_Insert(_Path);

						return true;
					}
				;

				DMibAssertTrue(fWriteNative(Backslash / "Local\\ignore", "A.cpp\n"));
				DMibAssertTrue(fMakeNativeDirectory(Backslash / "Rules\\Local"));
				DMibAssertTrue(fWriteNative(Backslash / "Rules\\Local/ignore", "B.cpp\n"));
				auto BareNames = CPerforceIgnore::fs_ResolveIgnoreNames("Local\\ignore", Backslash);
				auto PathNames = CPerforceIgnore::fs_ResolveIgnoreNames("Rules\\Local/ignore", Backslash);
				DMibExpect(PathNames, ==, TCVector<CStr>{Backslash + "/Rules\\Local/ignore"});
				DMibExpectTrue(CPerforceIgnore(CPerforceIgnore::CSettings{.m_IgnoreNames = BareNames}).f_IsIgnored(Backslash / "A.cpp", false));
				DMibExpectTrue(CPerforceIgnore(CPerforceIgnore::CSettings{.m_IgnoreNames = PathNames}).f_IsIgnored(Backslash / "B.cpp", false));

				// P4ENVIRO, P4CONFIG and $configdir keep them too.
				CStr ConfigDirectory = Backslash / "Config\\Directory";
				DMibAssertTrue(fMakeNativeDirectory(ConfigDirectory));
				DMibAssertTrue(fWriteNative(Backslash / "Local\\enviro", "P4CONFIG=Local\\p4config\n"));
				DMibAssertTrue(fWriteNative(ConfigDirectory / "Local\\p4config", "P4IGNORE=$configdir/Config.ignore\n"));
				auto ConfigSettings = CPerforceIgnore::fs_GetSettings(ConfigDirectory, {{"P4ENVIRO", Backslash / "Local\\enviro"}});
				DMibExpect
					(
						(TCVector<CStr>{ConfigSettings.m_EnviroFile, ConfigSettings.m_ConfigName.f_GetDescription(), ConfigSettings.f_GetConfigFile(ConfigDirectory)})
						, ==
						, (TCVector<CStr>{Backslash + "/Local\\enviro", "Local\\p4config", ConfigDirectory + "/Local\\p4config"})
					)
				;
				// A settings file's value is its bytes.
				DMibExpect(ConfigSettings.m_IgnoreNames, ==, TCVector<CPerforceIgnore::CSettingValue>{CPerforceIgnore::CSettingValue::fs_FromBytes(CStr(ConfigDirectory + "/Config.ignore").f_GetStr())});

				// The rule files are spelled as p4 opens them.
				CStr RuleRoot = Root / "BackslashRules";
				fg_WriteFile(RuleRoot / "A.cpp", "");
				TCVector<CStr> RuleFiles;
				CPerforceRootFiles
					(
						RuleRoot
						, {}
						, CPerforceIgnore::CSettings{.m_IgnoreNames = PathNames, .m_ConfigName = "Local\\p4config", .m_EnviroFile = Backslash + "/Local\\enviro"}
					)
					.f_ListExcluded(&RuleFiles)
				;
				TCVector<CStr> MissingRuleFiles;
				for (auto &RuleFile : {Backslash + "/Rules\\Local/ignore", RuleRoot + "/Local\\p4config", Root + "/Local\\p4config", Backslash + "/Local\\enviro"})
				{
					if (RuleFiles.f_Contains(RuleFile) < 0)
						MissingRuleFiles.f_Insert(RuleFile);
				}
				DMibExpect(MissingRuleFiles, ==, TCVector<CStr>{});

				// The leading . and .. of a P4CONFIG are taken from the directory by name, as p4 joins them, and the rest is kept.
				DMibExpect(CPerforceIgnore::CSettings{.m_ConfigName = "../Shared.p4config"}.f_GetConfigFile(Backslash), ==, Root + "/Shared.p4config");
				DMibExpect(CPerforceIgnore::CSettings{.m_ConfigName = "./Config/../p4config"}.f_GetConfigFile(Backslash), ==, Backslash + "/Config/../p4config");
			}

			{
				// A relative ignore path is opened through the file system: .. after a symbolic link leads from the link's target.
				DMibTestPath("LinkedParent");
				CStr Linked = Root / "Linked";
				fg_WriteFile(Root / "Shared/Sub/Placeholder", "");
				fg_WriteFile(Root / "Shared/ignore", "A.cpp\n");
				fg_WriteFile(Linked / "ignore", "B.cpp\n");
				DMibAssert(symlink((Root / "Shared/Sub").f_GetStr(), (Linked / "Config").f_GetStr()), ==, 0);
				auto LinkedNames = CPerforceIgnore::fs_ResolveIgnoreNames("Config/../ignore", Linked);
				DMibExpect(LinkedNames, ==, TCVector<CStr>{Linked + "/Config/../ignore"});
				CPerforceIgnore LinkedIgnore(CPerforceIgnore::CSettings{.m_IgnoreNames = LinkedNames});
				DMibExpectTrue(LinkedIgnore.f_IsIgnored(Linked / "A.cpp", false));
				DMibExpectFalse(LinkedIgnore.f_IsIgnored(Linked / "B.cpp", false));

				// The generation tracks the files opened, spelled so that expanding them by the names alone keeps them: a .. after
				// the link from the link's target, and a missing directory before a .. itself.
				char ResolvedShared[PATH_MAX];
				DMibAssertTrue(realpath((Root / "Shared").f_GetStr(), ResolvedShared) != nullptr);
				CStr Shared = ResolvedShared;
				CPerforceIgnore::CSettings LinkedSettings
					{
						.m_IgnoreNames = CPerforceIgnore::fs_ResolveIgnoreNames("Config/../ignore;Config/../Missing.ignore;Missing/../ignore", Linked)
						, .m_ConfigName = "Config/../linked.p4config"
					}
				;
				TCVector<CStr> LinkedRuleFiles;
				CPerforceRootFiles(Linked, {}, LinkedSettings).f_ListExcluded(&LinkedRuleFiles);
				TCVector<CStr> Tracked;
				for (CStr const &File : {Shared + "/ignore", Shared + "/Missing.ignore", Linked + "/Missing", Shared + "/linked.p4config", Linked + "/Config/../ignore"})
				{
					if (LinkedRuleFiles.f_Contains(File) >= 0)
						Tracked.f_Insert(File);
				}
				DMibExpect(Tracked, ==, (TCVector<CStr>{Shared + "/ignore", Shared + "/Missing.ignore", Linked + "/Missing", Shared + "/linked.p4config"}));

				{
					// A directory's settings files, by which BuildSystem knows a Perforce root before any walk: the P4CONFIG file p4
					// opens through the link, which is only in the link's target, and the files tracked for it and the enviro file,
					// resolved the same way, with the directory holding the link.
					DMibTestPath("SettingsFiles");
					fg_WriteFile(Root / "Shared/linked.p4config", "P4CLIENT=Linked\n");
					fg_WriteFile(Root / "Shared/linked.p4enviro", "");
					char ResolvedLinkedPath[PATH_MAX];
					DMibAssertTrue(realpath(Linked.f_GetStr(), ResolvedLinkedPath) != nullptr);
					CStr LinkDirectory = ResolvedLinkedPath;
					auto SettingsFiles = CPerforceIgnore::fs_GetSettings
						(
							Linked
							, {{"P4CONFIG", "Config/../linked.p4config"}, {"P4ENVIRO", Linked + "/Config/../linked.p4enviro"}}
						)
						.f_GetSettingsFiles(Linked)
					;
					DMibExpect(SettingsFiles.m_ConfigFile, ==, Linked + "/Config/../linked.p4config");
					TCVector<CStr> Dependencies;
					for (CStr const &File : {Shared + "/linked.p4config", Shared + "/linked.p4enviro", LinkDirectory, Linked + "/linked.p4config"})
					{
						if (SettingsFiles.m_Dependencies.f_FindEqual(File))
							Dependencies.f_Insert(File);
					}
					DMibExpect(Dependencies, ==, (TCVector<CStr>{Shared + "/linked.p4config", Shared + "/linked.p4enviro", LinkDirectory}));
				}

				// Retargeting a link before the .. changes the file opened, so the directories holding the links are tracked too,
				// also a link in a link's target, as the write time of a link's directory changes when the link is replaced.
				fg_WriteFile(Root / "Other/Sub/Placeholder", "");
				fg_WriteFile(Root / "Links/Placeholder", "");
				DMibAssert(symlink((Root / "Shared/Sub").f_GetStr(), (Root / "Links/Current").f_GetStr()), ==, 0);
				DMibAssert(symlink((Root / "Links/Current").f_GetStr(), (Linked / "Chained").f_GetStr()), ==, 0);
				char ResolvedPath[PATH_MAX];
				DMibAssertTrue(realpath(Linked.f_GetStr(), ResolvedPath) != nullptr);
				CStr ResolvedLinked = ResolvedPath;
				DMibAssertTrue(realpath((Root / "Links").f_GetStr(), ResolvedPath) != nullptr);
				CStr ResolvedLinks = ResolvedPath;
				DMibAssertTrue(realpath((Root / "Other").f_GetStr(), ResolvedPath) != nullptr);
				CStr Other = ResolvedPath;

				CPerforceIgnore::CSettings ChainedSettings{.m_IgnoreNames = CPerforceIgnore::fs_ResolveIgnoreNames("Chained/../chained.ignore", Linked)};
				auto fChainedRuleFiles = [&]
					{
						TCVector<CStr> RuleFiles;
						CPerforceRootFiles(Linked, {}, ChainedSettings).f_ListExcluded(&RuleFiles);
						TCVector<CStr> Found;
						for (CStr const &File : {Shared + "/chained.ignore", Other + "/chained.ignore", ResolvedLinked, ResolvedLinks})
						{
							if (RuleFiles.f_Contains(File) >= 0)
								Found.f_Insert(File);
						}
						return Found;
					}
				;
				DMibExpect(fChainedRuleFiles(), ==, (TCVector<CStr>{Shared + "/chained.ignore", ResolvedLinked, ResolvedLinks}));

				NTime::CTime OldTime = NTime::CTime::fs_NowUTC() - NTime::CTimeSpanConvert::fs_CreateMinuteSpan(10);
				CFile::fs_SetWriteTime(ResolvedLinks, OldTime);
				DMibAssert(unlink((Root / "Links/Current").f_GetStr()), ==, 0);
				DMibAssert(symlink((Root / "Other/Sub").f_GetStr(), (Root / "Links/Current").f_GetStr()), ==, 0);
				DMibExpect(CFile::fs_GetWriteTime(ResolvedLinks), !=, OldTime);
				DMibExpect(fChainedRuleFiles(), ==, (TCVector<CStr>{Other + "/chained.ignore", ResolvedLinked, ResolvedLinks}));
			}

			{
				// A .. discards an ordinary directory, which the open still needs, so the directory holding it is tracked: its
				// removal changes that directory's write time.
				DMibTestPath("DiscardedParent");
				CStr Plain = Root / "Plain";
				fg_WriteFile(Plain / "Config/Placeholder", "");
				fg_WriteFile(Plain / "A/B/Placeholder", "");
				fg_WriteFile(Plain / "plain.ignore", "A.cpp\n");
				fg_WriteFile(Root / "PlainWork/Placeholder", "");
				char ResolvedPath[PATH_MAX];
				DMibAssertTrue(realpath(Plain.f_GetStr(), ResolvedPath) != nullptr);
				CStr ResolvedPlain = ResolvedPath;

				CPerforceIgnore::CSettings PlainSettings
					{
						.m_IgnoreNames = CPerforceIgnore::fs_ResolveIgnoreNames(Plain + "/Config/../plain.ignore;" + Plain + "/A/B/../../nested.ignore", Root / "PlainWork")
					}
				;
				auto fPlainRuleFiles = [&]
					{
						TCVector<CStr> RuleFiles;
						CPerforceRootFiles(Root / "PlainWork", {}, PlainSettings).f_ListExcluded(&RuleFiles);
						TCVector<CStr> Found;
						for
							(
								CStr const &File
								: {ResolvedPlain, ResolvedPlain + "/A", ResolvedPlain + "/plain.ignore", ResolvedPlain + "/nested.ignore", ResolvedPlain + "/Config"}
							)
						{
							if (RuleFiles.f_Contains(File) >= 0)
								Found.f_Insert(File);
						}
						return Found;
					}
				;
				DMibExpect
					(
						fPlainRuleFiles()
						, ==
						, (TCVector<CStr>{ResolvedPlain, ResolvedPlain + "/A", ResolvedPlain + "/plain.ignore", ResolvedPlain + "/nested.ignore"})
					)
				;

				NTime::CTime OldTime = NTime::CTime::fs_NowUTC() - NTime::CTimeSpanConvert::fs_CreateMinuteSpan(10);
				CFile::fs_SetWriteTime(ResolvedPlain, OldTime);
				CFile::fs_DeleteDirectoryRecursive(Plain / "Config");
				DMibExpect(CFile::fs_GetWriteTime(ResolvedPlain), !=, OldTime);
				// The open now fails at the missing directory, whose creation is tracked instead.
				DMibExpect
					(
						fPlainRuleFiles()
						, ==
						, (TCVector<CStr>{ResolvedPlain, ResolvedPlain + "/A", ResolvedPlain + "/nested.ignore", ResolvedPlain + "/Config"})
					)
				;
			}

			{
				// Generated: rule-file paths through symbolic links, .. inside link targets, nested and looping links and missing
				// components. Whatever a change to the tree does to what the open of such a path opens, it changes the existence or
				// write time of a tracked file, which is what BuildSystem compares, and the file opened is among them.
				DMibTestPath("Resolution");
				CStr Base = Root / "Resolution";
				TCVector<CStr> Starts = {"W/Config", "W/Chain", "W/Dotted", "W/Loop", "W/Nope", "W/Config/Deeper", "Rules/A", "Rules/A/B", "Links/Current"};
				TCVector<CStr> Endings = {"/../ignore", "/../../ignore", "/./ignore", "/../Sub/../ignore", "/B/../ignore", "/Deeper/../ignore", "/.."};

				auto fBuild = [&](CStr const &_Tree)
					{
						fg_WriteFile(_Tree / "Rules/ignore", "");
						fg_WriteFile(_Tree / "Rules/Sub/ignore", "");
						fg_WriteFile(_Tree / "Rules/Sub/Deeper/ignore", "");
						fg_WriteFile(_Tree / "Rules/A/ignore", "");
						CFile::fs_CreateDirectory(_Tree / "Rules/A/B");
						fg_WriteFile(_Tree / "Other/ignore", "");
						fg_WriteFile(_Tree / "Other/X/ignore", "");
						fg_WriteFile(_Tree / "W/ignore", "");
						fg_WriteFile(_Tree / "Work/Placeholder", "");
						CFile::fs_CreateDirectory(_Tree / "Links");
						DMibAssert(symlink("../Rules/A/B", (_Tree / "Links/Current").f_GetStr()), ==, 0);
						DMibAssert(symlink((_Tree / "Rules/A/B/../../Sub").f_GetStr(), (_Tree / "W/Config").f_GetStr()), ==, 0);
						DMibAssert(symlink("../Links/Current", (_Tree / "W/Chain").f_GetStr()), ==, 0);
						DMibAssert(symlink("../Rules/./A/../Sub/Deeper", (_Tree / "W/Dotted").f_GetStr()), ==, 0);
						DMibAssert(symlink("Loop", (_Tree / "W/Loop").f_GetStr()), ==, 0);

						NTime::CTime OldTime = NTime::CTime::fs_NowUTC() - NTime::CTimeSpanConvert::fs_CreateMinuteSpan(10);
						for
							(
								CStr const &Entry
								: {
									"", "Rules", "Rules/ignore", "Rules/Sub", "Rules/Sub/ignore", "Rules/Sub/Deeper", "Rules/Sub/Deeper/ignore", "Rules/A"
									, "Rules/A/ignore", "Rules/A/B", "Other", "Other/ignore", "Other/X", "Other/X/ignore", "W", "W/ignore", "Links"
								}
							)
						{
							CFile::fs_SetWriteTime(Entry.f_IsEmpty() ? _Tree : _Tree / Entry, OldTime);
						}
					}
				;
				auto fRelink = [](CStr const &_Link, CStr const &_Target)
					{
						DMibAssert(unlink(_Link.f_GetStr()), ==, 0);
						DMibAssert(symlink(_Target.f_GetStr(), _Link.f_GetStr()), ==, 0);
					}
				;
				TCVector<TCTuple<CStr, NFunction::TCFunction<void (CStr const &_Tree)>>> Changes =
					{
						{"RemoveB", [](CStr const &_Tree){DMibAssert(rmdir((_Tree / "Rules/A/B").f_GetStr()), ==, 0);}}
						, {"RemoveA", [](CStr const &_Tree){CFile::fs_DeleteDirectoryRecursive(_Tree / "Rules/A");}}
						, {"RemoveSub", [](CStr const &_Tree){CFile::fs_DeleteDirectoryRecursive(_Tree / "Rules/Sub");}}
						, {"RemoveDeeper", [](CStr const &_Tree){CFile::fs_DeleteDirectoryRecursive(_Tree / "Rules/Sub/Deeper");}}
						, {"RemoveRulesIgnore", [](CStr const &_Tree){CFile::fs_DeleteFile(_Tree / "Rules/ignore");}}
						, {"RemoveSubIgnore", [](CStr const &_Tree){CFile::fs_DeleteFile(_Tree / "Rules/Sub/ignore");}}
						, {"RetargetConfig", [&](CStr const &_Tree){fRelink(_Tree / "W/Config", _Tree / "Other/X/../X");}}
						, {"RetargetCurrent", [&](CStr const &_Tree){fRelink(_Tree / "Links/Current", "../Other/X");}}
						, {"RetargetDotted", [&](CStr const &_Tree){fRelink(_Tree / "W/Dotted", "../Other/./X");}}
						, {"BreakLoop", [&](CStr const &_Tree){fRelink(_Tree / "W/Loop", "../Rules/Sub");}}
						, {"CreateNope", [](CStr const &_Tree){CFile::fs_CreateDirectory(_Tree / "W/Nope");}}
						, {
							"CreateNopeLink"
							, [](CStr const &_Tree){DMibAssert(symlink("../Rules/A", (_Tree / "W/Nope").f_GetStr()), ==, 0);}
						}
						, {
							"ReplaceBWithLink"
							, [](CStr const &_Tree)
							{
								DMibAssert(rmdir((_Tree / "Rules/A/B").f_GetStr()), ==, 0);
								DMibAssert(symlink("../../Other/X", (_Tree / "Rules/A/B").f_GetStr()), ==, 0);
							}
						}
					}
				;

				// The file the open opens, or empty when it fails.
				auto fOpened = [](CStr const &_Path) -> CStr
					{
						int File = open(_Path.f_GetStr(), O_RDONLY | O_NONBLOCK | O_CLOEXEC);
						if (File < 0)
							return {};

						struct stat Stats;
						bool bStat = fstat(File, &Stats) == 0;
						close(File);
						if (!bStat)
							return {};

						return "{}:{}"_f << uint64(Stats.st_dev) << uint64(Stats.st_ino);
					}
				;
				// What BuildSystem compares of a tracked file: its write time, through symbolic links, or that it is missing.
				struct CWriteTime
				{
					auto operator <=> (CWriteTime const &_Right) const noexcept = default;

					NTime::CTime m_Time;
					bool m_bExists = false;
				};
				auto fWriteTime = [](CStr const &_Path) -> CWriteTime
					{
						try
						{
							return {.m_Time = CFile::fs_GetWriteTime(_Path), .m_bExists = true};
						}
						catch (CException const &)
						{
							return {};
						}
					}
				;

				TCVector<CStr> Failures;
				umint nChanged = 0;
				for (auto &[ChangeName, fChange] : Changes)
				{
					DMibTestPath(ChangeName);
					CStr Tree = Base / ChangeName;
					fBuild(Tree);

					struct CCheck
					{
						CStr m_Path;
						CStr m_Opened;
						TCMap<CStr, CWriteTime> m_WriteTimes;
					};
					TCVector<CCheck> Checks;
					for (auto &Start : Starts)
					{
						for (auto &Ending : Endings)
						{
							auto &Check = Checks.f_Insert();
							Check.m_Path = Tree / Start + Ending;
							Check.m_Opened = fOpened(Check.m_Path);
							TCVector<CStr> RuleFiles;
							CPerforceRootFiles(Tree / "Work", {}, CPerforceIgnore::CSettings{.m_IgnoreNames = {Check.m_Path}}).f_ListExcluded(&RuleFiles);
							bool bOpenedTracked = Check.m_Opened.f_IsEmpty();
							for (auto &RuleFile : RuleFiles)
							{
								Check.m_WriteTimes[RuleFile] = fWriteTime(RuleFile);
								bOpenedTracked = bOpenedTracked || fOpened(RuleFile) == Check.m_Opened;
							}
							if (!bOpenedTracked)
								Failures.f_Insert("{}: the file opened is not tracked"_f << Check.m_Path);
						}
					}

					fChange(Tree);

					for (auto &Check : Checks)
					{
						if (fOpened(Check.m_Path) == Check.m_Opened)
							continue;

						++nChanged;
						bool bNoticed = false;
						for (auto &WriteTime : Check.m_WriteTimes)
							bNoticed = bNoticed || fWriteTime(Check.m_WriteTimes.fs_GetKey(WriteTime)) != WriteTime;
						if (!bNoticed)
							Failures.f_Insert("{} after {}: what is opened changed, and no tracked file did"_f << Check.m_Path << ChangeName);
					}
				}

				DMibExpect(Failures, ==, TCVector<CStr>{});
				DMibExpect(nChanged, >, 100u);
			}
#endif

#ifndef DPlatformFamily_Windows
			{
				// An unlistable directory fails the listing rather than drop its files.
				DMibTestPath("Unreadable");
				fg_WriteFile(Workspace / "Locked/Locked.cpp", "");
				TCVector<TCTuple<CStr, EFileAttrib>> Modes =
					{
						{"NoRead", EFileAttrib_UnixAttributesValid | EFileAttrib_UserWrite}
						, {"NoSearch", EFileAttrib_UnixAttributesValid | EFileAttrib_UserRead}
					}
				;
				for (auto &[Name, Mode] : Modes)
				{
					DMibTestPath(Name);
					CFile::fs_SetAttributes(Workspace / "Locked", Mode);
					// As root the permissions are bypassed, and the listing rightly succeeds.
					if (access((Workspace / "Locked").f_GetStr(), R_OK | X_OK) != 0)
					{
						CPerforceRootFiles RootFiles(Workspace, {}, Settings);
						DMibExpectException(RootFiles.f_List(), DMibErrorInstance("'{}' cannot be listed"_f << (Workspace / "Locked")));
						// A root that cannot be listed leaves out every file.
						DMibExpect(CPerforceRootFiles(Workspace / "Locked", {}, Settings).f_ListExcluded(), ==, TCVector<CStr>{"*"});
					}
					else
						DMibConOut("{}\n", "Skipping the unreadable directory test: its permissions are bypassed");
					CFile::fs_SetAttributes(Workspace / "Locked", EFileAttrib_UnixAttributesValid | EFileAttrib_UserRead | EFileAttrib_UserWrite | EFileAttrib_UserExecute);
				}
				CFile::fs_DeleteDirectoryRecursive(Workspace / "Locked");
			}
#endif

			{
				// A root that doesn't exist or isn't a directory cannot be listed.
				DMibTestPath("MissingRoot");
				DMibExpectException
					(
						CPerforceRootFiles(Workspace / "Missing", {}, Settings).f_List()
						, DMibErrorInstance("'{}' cannot be listed"_f << (Workspace / "Missing"))
					)
				;
				DMibExpectException
					(
						CPerforceRootFiles(Workspace / "A.cpp", {}, Settings).f_List()
						, DMibErrorInstance("'{}' cannot be listed"_f << (Workspace / "A.cpp"))
					)
				;
			}

#ifdef DPlatformFamily_Windows
			{
				// The listing decides whether a directory can be listed, which needs fewer rights than reading it as a file.
				DMibTestPath("Rights");
				CStr Locked = Workspace / "Locked";
				fg_WriteFile(Locked / "Locked.cpp", "");
				CWStr LockedPath = Locked.f_ReplaceChar('/', '\\');
				// The error of setting up the denial, after which _fTest has run; the helper has no expectations of its own, which would
				// repeat their test paths.
				auto fWithRightsDenied = [&](DWORD _Rights, NFunction::TCFunction<void ()> const &_fTest) -> DWORD
					{
						PACL pOldAcl = nullptr;
						PSECURITY_DESCRIPTOR pDescriptor = nullptr;
						DWORD Error = GetNamedSecurityInfoW
							(
								(LPWSTR)LockedPath.f_GetStr()
								, SE_FILE_OBJECT
								, DACL_SECURITY_INFORMATION
								, nullptr
								, nullptr
								, &pOldAcl
								, nullptr
								, &pDescriptor
							)
						;
						if (Error != ERROR_SUCCESS)
							return Error;

						auto FreeDescriptor = g_OnScopeExit / [&]
							{
								LocalFree(pDescriptor);
							}
						;

						EXPLICIT_ACCESSW Access{};
						Access.grfAccessPermissions = _Rights;
						Access.grfAccessMode = DENY_ACCESS;
						Access.grfInheritance = NO_INHERITANCE;
						BuildTrusteeWithNameW(&Access.Trustee, (LPWSTR)L"CURRENT_USER");
						PACL pNewAcl = nullptr;
						Error = SetEntriesInAclW(1, &Access, pOldAcl, &pNewAcl);
						if (Error != ERROR_SUCCESS)
							return Error;

						auto FreeAcl = g_OnScopeExit / [&]
							{
								LocalFree(pNewAcl);
							}
						;

						auto fSetAcl = [&](PACL _pAcl)
							{
								return SetNamedSecurityInfoW((LPWSTR)LockedPath.f_GetStr(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr, _pAcl, nullptr);
							}
						;
						Error = fSetAcl(pNewAcl);
						if (Error != ERROR_SUCCESS)
							return Error;

						auto Restore = g_OnScopeExit / [&]
							{
								fSetAcl(pOldAcl);
							}
						;

						// The backup privilege, which Malterlib enables where the process has it, bypasses the rights, so the test runs on a
						// copy of the token without it.
						if (!ImpersonateSelf(SecurityImpersonation))
							return GetLastError();

						auto Revert = g_OnScopeExit / [&]
							{
								RevertToSelf();
							}
						;

						HANDLE pToken = nullptr;
						if (!OpenThreadToken(GetCurrentThread(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, TRUE, &pToken))
							return GetLastError();

						auto CloseToken = g_OnScopeExit / [&]
							{
								CloseHandle(pToken);
							}
						;

						TOKEN_PRIVILEGES Privileges{};
						Privileges.PrivilegeCount = 1;
						Privileges.Privileges[0].Attributes = SE_PRIVILEGE_REMOVED;
						if (!LookupPrivilegeValueW(nullptr, SE_BACKUP_NAME, &Privileges.Privileges[0].Luid))
							return GetLastError();

						// ERROR_NOT_ALL_ASSIGNED: the token doesn't have the privilege.
						AdjustTokenPrivileges(pToken, FALSE, &Privileges, 0, nullptr, nullptr);
						Error = GetLastError();
						if (Error != ERROR_SUCCESS && Error != ERROR_NOT_ALL_ASSIGNED)
							return Error;

						_fTest();

						return DWORD(ERROR_SUCCESS);
					}
				;

				// Without the rights of reading extended attributes the directory can't be read as a file, but can be listed.
				DWORD ReadError = fWithRightsDenied
					(
						FILE_READ_EA
						, [&]
						{
							DMibExpectTrue(CFile::fs_CheckFileRights(Locked, EFileRight_Read) != ECheckFileRights_Access);
							DMibExpect(CPerforceRootFiles(Locked, {}, Settings).f_List(), ==, TCVector<CStr>{"Locked.cpp"});
						}
					)
				;
				DMibExpect(ReadError, ==, DWORD(ERROR_SUCCESS));
				DWORD ListError = fWithRightsDenied
					(
						FILE_LIST_DIRECTORY
						, [&]
						{
							DMibExpectException(CPerforceRootFiles(Workspace, {}, Settings).f_List(), DMibErrorInstance("'{}' cannot be listed"_f << Locked));
							DMibExpect(CPerforceRootFiles(Locked, {}, Settings).f_ListExcluded(), ==, TCVector<CStr>{"*"});
						}
					)
				;
				DMibExpect(ListError, ==, DWORD(ERROR_SUCCESS));
				CFile::fs_DeleteDirectoryRecursive(Locked);
			}
#endif

			{
				DMibTestPath("CustomIgnoreFileName");
				fg_WriteFile(Workspace / "p4ignore.txt", "A.cpp\n");
				CPerforceIgnore Ignore(CPerforceIgnore::CSettings{.m_IgnoreNames = {"p4ignore.txt"}, .m_ConfigName = ".p4config"});
				DMibExpectTrue(Ignore.f_IsIgnored(Workspace / "A.cpp", false));
				DMibExpectFalse(Ignore.f_IsIgnored(Workspace / "Debug.log", false));
			}
		};

		DMibTestSuite("Settings")
		{
			CStr Root = fg_MakeTestRoot("TestPerforceSettings");
			CStr Home = Root / "Home";
			CStr Outer = Root / "Outer";
			CStr Inner = Outer / "Inner";
			CStr Plain = Root / "Plain";
			CFile::fs_CreateDirectory(Plain);
			fg_WriteFile(Home / ".p4enviro", "P4CONFIG=.p4config\nP4IGNORE=Enviro.ignore\n");
			fg_WriteFile(Home / "Other.enviro", "P4IGNORE=Other.ignore\n");
			fg_WriteFile(Outer / ".p4config", "P4CLIENT=Outer\nP4IGNORE=Outer.ignore\n");
			fg_WriteFile(Inner / ".p4config", "P4IGNORE=$configdir/Inner.ignore  \nP4IGNORE=Second.ignore\n");
			// p4 reads the bytes as they are: the byte order mark is part of the first name.
			CStr ByteOrderMark = Root / "ByteOrderMark";
			fg_WriteFile(ByteOrderMark / ".p4config", "\xEF\xBB\xBF" "P4IGNORE=Marked.ignore\n");
			CStr LowerCase = Root / "LowerCase";
			fg_WriteFile(LowerCase / ".p4config", "p4ignore=Lower.ignore\nP4IGNORE=Upper.ignore\n");
#ifndef DPlatformFamily_Windows
			// A settings file that is a FIFO, also through a symbolic link, is not read, as reading it would block.
			CStr LinkedFifo = Root / "LinkedFifo";
			CFile::fs_CreateDirectory(LinkedFifo);
			DMibAssert(mkfifo((Root / "Config.fifo").f_GetStr(), 0600), ==, 0);
			DMibAssert(symlink((Root / "Config.fifo").f_GetStr(), (LinkedFifo / ".p4config").f_GetStr()), ==, 0);
			auto LinkedFifoSettings = CPerforceIgnore::fs_GetSettings(LinkedFifo, {{"P4CONFIG", ".p4config"}, {"P4ENVIRO", (Root / "Config.fifo").f_GetStr()}});
			DMibExpect(LinkedFifoSettings.m_ConfigName, ==, ".p4config");
#endif

			auto fNative = [](CStr const &_Path)
				{
#ifdef DPlatformFamily_Windows
					return _Path.f_ReplaceChar('/', '\\');
#else
					return _Path;
#endif
				}
			;

			CStr HomeVariable = CPerforceIgnore::fs_GetEnvironmentVariables().f_GetLast();
			auto fSettings = [&](CStr const &_Directory, TCMap<CStr, CStr> const &_Environment) -> TCVector<CStr>
				{
					auto Environment = _Environment;
					Environment[HomeVariable] = Home;
					auto Settings = CPerforceIgnore::fs_GetSettings(_Directory, Environment);

					return {fg_JoinDescriptions(Settings.m_IgnoreNames), Settings.m_ConfigName.f_GetDescription(), Settings.m_EnviroFile};
				}
			;

			CStr Default = ".p4ignore;p4ignore.txt;{};{}"_f << fNative(Home / ".p4ignore") << fNative(Home / "p4ignore.txt");
#ifdef DPlatformFamily_Windows
			// p4 has no default enviro file on Windows.
			CStr DefaultEnviro;
			CStr EnviroIgnore = Default;
			CStr EnviroConfig;
			// The environment ignores case on Windows.
			CStr LowerCaseEnvironmentIgnore = "Env.ignore";
#else
			CStr DefaultEnviro = Home / ".p4enviro";
			CStr EnviroIgnore = "Enviro.ignore";
			CStr EnviroConfig = ".p4config";
			CStr LowerCaseEnvironmentIgnore = EnviroIgnore;
#endif
#if defined(DPlatformFamily_Windows) || defined(DPlatformFamily_macOS)
			// p4 folds case on Windows and macOS: the names of a file are taken regardless of case, the first line of a name counting.
			CStr LowerCaseIgnore = "Lower.ignore";
#else
			CStr LowerCaseIgnore = "Upper.ignore";
#endif
			TCMap<CStr, CStr> NoConfig = {{"P4CONFIG", ".p4config"}};
			TCVector<TCVector<CStr>> Expected =
				{
					// The nearest P4CONFIG file outranks those above it, and its first line of a variable counts.
					{fNative(Inner) + "/Inner.ignore", ".p4config", DefaultEnviro}
					, {"Outer.ignore", ".p4config", DefaultEnviro}
					// A P4CONFIG file outranks the environment, which outranks the enviro file.
					, {"Outer.ignore", ".p4config", DefaultEnviro}
					, {"Env.ignore", ".p4config", DefaultEnviro}
					// A variable the environment doesn't set comes from the enviro file, and one set empty is unset.
					, {EnviroIgnore, EnviroConfig, DefaultEnviro}
					, {Default, EnviroConfig, DefaultEnviro}
					, {EnviroIgnore, "", DefaultEnviro}
					// P4ENVIRO and $home are the environment's.
					, {"Other.ignore", "", Home / "Other.enviro"}
					, {EnviroIgnore, EnviroConfig, DefaultEnviro}
					, {Home + fNative("/Home.ignore"), EnviroConfig, DefaultEnviro}
					, {EnviroIgnore, ".p4config", DefaultEnviro}
					, {LowerCaseIgnore, ".p4config", DefaultEnviro}
					, {LowerCaseEnvironmentIgnore, EnviroConfig, DefaultEnviro}
				}
			;
			TCVector<TCVector<CStr>> Settings =
				{
					fSettings(Inner, NoConfig)
					, fSettings(Outer, NoConfig)
					, fSettings(Outer, {{"P4CONFIG", ".p4config"}, {"P4IGNORE", "Env.ignore"}})
					, fSettings(Plain, {{"P4CONFIG", ".p4config"}, {"P4IGNORE", "Env.ignore"}})
					, fSettings(Plain, {})
					, fSettings(Plain, {{"P4IGNORE", ""}})
					, fSettings(Inner, {{"P4CONFIG", ""}})
					, fSettings(Plain, {{"P4ENVIRO", "$home/Other.enviro"}})
					, fSettings(Plain, {{"P4ENVIRO", ""}})
					, fSettings(Plain, {{"P4IGNORE", fNative("$home/Home.ignore")}})
					, fSettings(ByteOrderMark, NoConfig)
					, fSettings(LowerCase, NoConfig)
					, fSettings(Plain, {{"p4ignore", "Env.ignore"}})
				}
			;
			DMibExpect(Settings, ==, Expected);

			// $home is the home directory without the separator ending it.
			auto TrailingHome = CPerforceIgnore::fs_GetSettings(Plain, {{HomeVariable, Home + "/"}, {"P4IGNORE", "$home.ignore"}});
			DMibExpect(TrailingHome.m_IgnoreNames, ==, TCVector<CStr>{Home + ".ignore"});

#ifndef DPlatformFamily_Windows
			// The default enviro file of a relative home is opened from the directory, as a relative P4ENVIRO is, not from the
			// process's current directory.
			auto RelativeHome = CPerforceIgnore::fs_GetSettings(Root, {{HomeVariable, "Home"}});
			DMibExpect
				(
					(TCVector<CStr>{RelativeHome.m_EnviroFile, fg_JoinDescriptions(RelativeHome.m_IgnoreNames)})
					, ==
					, (TCVector<CStr>{Root / "Home/.p4enviro", "Enviro.ignore"})
				)
			;
#endif

#ifdef DPlatformFamily_Windows
			// p4 reads P4ENVIRO through the code page, so a name with a character outside it names no file p4 opens, and its
			// settings don't apply.
			if (GetACP() == 1252)
			{
				fg_WriteFile(Root / "Enviro \u6f22/p4enviro", "P4IGNORE=Outside.ignore\n");
				auto OutsideSettings = CPerforceIgnore::fs_GetSettings(Plain, {{"P4ENVIRO", Root / "Enviro \u6f22/p4enviro"}});
				DMibExpect
					(
						(TCVector<CStr>{OutsideSettings.m_EnviroFile, fg_JoinDescriptions(OutsideSettings.m_IgnoreNames)})
						, ==
						, (TCVector<CStr>{Root / "Enviro ?/p4enviro", ".p4ignore;p4ignore.txt"})
					)
				;
			}
#endif

#ifdef DPlatformFamily_Windows
			if (GetACP() == 1252)
			{
				// With UTF-8 file names p4 opens a P4CONFIG by its UTF-8 name and, when that fails, by the code page's, so both are
				// tracked before either exists, and the one that exists makes the directory a Perforce root.
				DMibTestPath("TwoConfigSpellings");
				CStr TwoSpellings = Root / "TwoSpellings";
				CFile::fs_CreateDirectory(TwoSpellings);
				auto fSettingsFiles = [&]
					{
						return CPerforceIgnore::fs_GetSettings(TwoSpellings, {{"P4CONFIG", "caf\u00e9.cfg"}, {"P4CHARSET", "utf8"}}).f_GetSettingsFiles(TwoSpellings);
					}
				;
				CStr UTF8Spelling = TwoSpellings / "caf\u00e9.cfg";
				CStr CodePageSpelling = TwoSpellings / "caf\u00c3\u00a9.cfg";
				auto Missing = fSettingsFiles();
				DMibExpect(Missing.m_ConfigFile, ==, "");
				DMibExpectTrue(Missing.m_Dependencies.f_FindEqual(UTF8Spelling));
				DMibExpectTrue(Missing.m_Dependencies.f_FindEqual(CodePageSpelling));
				fg_WriteFile(CodePageSpelling, "P4CLIENT=Fallback\n");
				DMibExpect(fSettingsFiles().m_ConfigFile, ==, CodePageSpelling);
			}
#endif

			// P4CHARSET and P4COMMANDCHARSET are read like the other settings: a P4CONFIG file outranks the environment.
			fg_WriteFile(Root / "Charset/.p4config", "P4CHARSET=cp850\n");
			auto CharsetSettings = CPerforceIgnore::fs_GetSettings
				(
					Root / "Charset"
					, {{"P4CONFIG", ".p4config"}, {"P4CHARSET", "winansi"}, {"P4COMMANDCHARSET", "cp1251"}}
				)
			;
			DMibExpect((TCVector<CStr>{CharsetSettings.m_Charset, CharsetSettings.m_CommandCharset}), ==, (TCVector<CStr>{"cp850", "cp1251"}));

			// Without P4CHARSET the charset p4 learned for the port the settings give, here a P4CONFIG file's, is read from the
			// environment too, as p4's Client::GetCharset reads it, and its variable is named for the environment generation uses.
			fg_WriteFile(Root / "Learned/.p4config", "P4PORT=ssl:server=1666\n");
			auto LearnedSettings = CPerforceIgnore::fs_GetSettings
				(
					Root / "Learned"
					, {{"P4CONFIG", ".p4config"}, {"P4PORT", "other:1666"}, {"P4_ssl:server@1666_CHARSET", "utf8"}, {"P4_other:1666_CHARSET", "cp850"}}
				)
			;
			DMibExpect
				(
					(TCVector<CStr>{LearnedSettings.m_LearnedCharset, LearnedSettings.m_LearnedCharsetVariable})
					, ==
					, (TCVector<CStr>{"utf8", "P4_ssl:server@1666_CHARSET"})
				)
			;
			DMibExpect
				(
					(
						TCVector<bool>
						{
							CPerforceIgnore::fs_IsLearnedCharsetVariable("P4_ssl:server@1666_CHARSET")
							, CPerforceIgnore::fs_IsLearnedCharsetVariable("P4CHARSET")
							, CPerforceIgnore::fs_IsLearnedCharsetVariable("P4_CHARSET")
						}
					)
					, ==
					, (TCVector<bool>{true, false, false})
				)
			;

			{
				// A learned charset of a port whose name isn't ASCII, like an rsh: port's path, is the settings file's setting of
				// that name's bytes, which p4 reads untranslated, and its environment variable the name as p4 reads it.
				DMibTestPath("NonASCIIPort");
#ifdef DPlatformFamily_Windows
				CAnsiStr Port = "rsh:C:\\D\xE9pot\\p4d.exe";
				bool bTest = GetACP() == 1252;
#else
				CAnsiStr Port = "rsh:/D\xC3\xA9pot/p4d";
				bool bTest = true;
#endif
				if (bTest)
				{
					auto fWriteBytes = [](CStr const &_Path, CAnsiStr const &_Bytes)
						{
							CFile::fs_CreateDirectoryForFile(_Path);
							CByteVector Data;
							Data.f_Insert((uint8 const *)_Bytes.f_GetStr(), _Bytes.f_GetLen());
							CFile::fs_WriteFile(_Path, Data);
						}
					;
					fWriteBytes(Root / "NonASCIIPort/.p4config", CAnsiStr("P4PORT=") + Port + "\n");
					fWriteBytes(Root / "NonASCIIPort.p4enviro", CAnsiStr("P4_") + Port + "_CHARSET=utf8\n");
					auto PortSettings = CPerforceIgnore::fs_GetSettings
						(
							Root / "NonASCIIPort"
							, {{"P4CONFIG", ".p4config"}, {"P4ENVIRO", Root / "NonASCIIPort.p4enviro"}}
						)
					;
#ifdef DPlatformFamily_Windows
					CStr Variable = "P4_rsh:C:\\D\u00e9pot\\p4d.exe_CHARSET";
#else
					CStr Variable = "P4_rsh:/D\u00e9pot/p4d_CHARSET";
#endif
					DMibExpect
						(
							(TCVector<CStr>{PortSettings.m_LearnedCharset, PortSettings.m_LearnedCharsetVariable})
							, ==
							, (TCVector<CStr>{"utf8", Variable})
						)
					;
				}
			}

			// A P4CONFIG's .. leads to the directory's parent, as p4 climbs, also from a directory spelled with a separator ending it.
			fg_WriteFile(Outer / "Up.p4config", "P4IGNORE=Up.ignore\n");
			fg_WriteFile(Inner / "Up.p4config", "P4IGNORE=Inner.ignore\n");
			TCVector<TCVector<CStr>> UpSettings;
			for (CStr const &Directory : {Inner, Inner + "/"})
			{
				auto Settings = CPerforceIgnore::fs_GetSettings(Directory, {{"P4CONFIG", "../Up.p4config"}});
				UpSettings.f_Insert(TCVector<CStr>{fg_JoinDescriptions(Settings.m_IgnoreNames), Settings.f_GetConfigFile(Directory)});
			}
			DMibExpect(UpSettings, ==, (TCVector<TCVector<CStr>>{{"Up.ignore", Outer / "Up.p4config"}, {"Up.ignore", Outer / "Up.p4config"}}));

#ifdef DPlatformFamily_Windows
			// The P4CONFIG files of a UNC path end at its share's root, above which p4 doesn't climb. The share doesn't exist, so
			// the root can't be listed, while its rule files are given.
			{
				CPerforceIgnore::CSettings UNCSettings;
				UNCSettings.m_ConfigName = ".p4config";
				CStr Share = "//127.0.0.1/MalterlibNoSuchShare";
				TCVector<CStr> RuleFiles;
				auto Excluded = CPerforceRootFiles(Share / "Project", {}, UNCSettings).f_ListExcluded(&RuleFiles);
				DMibExpect(Excluded, ==, TCVector<CStr>{"*"});
				DMibExpect(RuleFiles, ==, (TCVector<CStr>{Share / ".p4config", Share / "Project/.p4config"}));

				// Under a UNC root, a bare ignore name that is a UNC path with forward slashes is that share's file, as the
				// matcher's SetLocal makes it, and a drive-rooted P4CONFIG is on the current drive, where p4 runs.
				CPerforceIgnore::CSettings RootedSettings
					{
						.m_IgnoreNames = CPerforceIgnore::fs_ResolveIgnoreNames(Share / "Rules/ignore", Share / "Project")
						, .m_ConfigName = "\\Rules\\.p4config"
					}
				;
				CStr CurrentDrive = CFile::fs_GetDrive(CFile::fs_GetCurrentDirectory()).f_UpperCase();
				DMibExpect(RootedSettings.f_GetConfigFile(Share / "Project"), ==, CurrentDrive + "/Rules/.p4config");
				TCVector<CStr> RootedRuleFiles;
				CPerforceRootFiles(Share / "Project", {}, RootedSettings).f_ListExcluded(&RootedRuleFiles);
				DMibExpect(RootedRuleFiles, ==, (TCVector<CStr>{Share / "Rules/ignore", CurrentDrive + "/Rules/.p4config"}));

				// A .. at a UNC share's root stays at the root, as Windows opens the path, for P4IGNORE, whose names have p4's
				// separators, P4CONFIG and P4ENVIRO.
				DMibTestPath("ShareRootParent");
				CStr ShareParent = "\\\\127.0.0.1\\MalterlibNoSuchShare\\..";
				DMibExpect
					(
						(
							TCVector<CStr>
							{
								fg_JoinDescriptions(CPerforceIgnore::fs_ResolveIgnoreNames(ShareParent + "\\ignore", Share / "Project"))
								, CPerforceIgnore::CSettings{.m_ConfigName = ShareParent + "\\p4config"}.f_GetConfigFile(Share / "Project")
								, CPerforceIgnore::fs_GetSettings(Share / "Project", {{"P4CONFIG", "noconfig"}, {"P4ENVIRO", ShareParent + "\\enviro"}}).m_EnviroFile
							}
						)
						, ==
						, (TCVector<CStr>{"\\\\127.0.0.1\\MalterlibNoSuchShare\\ignore", Share / "p4config", Share / "enviro"})
					)
				;
			}
#endif
		};

		DMibTestSuite("EscapeFileName")
		{
			DMibExpect(CPerforceClient::fs_EscapeFileName("/Root @#%*/A.cpp"), ==, "/Root %40%23%25%2A/A.cpp");
			DMibExpect(CPerforceClient::fs_EscapeFileName("/Root/A.cpp"), ==, "/Root/A.cpp");
		};

		DMibTestSuite("ChangedFiles") -> TCFuture<void>
		{
			if (CPerforceTestServer::fs_FindServerExecutable().f_IsEmpty())
			{
				DMibConOut("{}\n", "Skipping the Perforce changed files tests: p4d was not found in PATH");
				co_return {};
			}

			CStr Root = fg_MakeTestRoot("TestPerforceChangedFiles");

			TCActor<CPerforceTestServer> Server = fg_Construct<CPerforceTestServer>();
			co_await Server(&CPerforceTestServer::f_Start, Root, CStr("tester"));
			auto ServerCleanup = co_await fg_AsyncDestroy(Server);

			co_await Server(&CPerforceTestServer::f_CreateStreamDepot, CStr("Streams"));
			co_await Server(&CPerforceTestServer::f_CreateMainlineStream, CStr("//Streams/Main"));

			int64 FirstChangelist = co_await Server
				(
					&CPerforceTestServer::f_SubmitFiles
					, CStr("//Streams/Main")
					, TCVector<CPerforceTestServer::CFile>{{"Old.cpp", "First\n"}, {"Edited.cpp", "First\n"}}
					, CStr("Add the first files")
				)
			;

			// Names with p4's revision and wildcard characters. Windows doesn't allow *, and the non-ASCII name tests UTF-8 paths
			// with a server that isn't a Unicode one, which on Windows are passed in the code page, so there the name's character is
			// one the active code page represents.
			TCVector<CStr> SpecialDirectories = {"Special @#%"};
#ifdef DPlatformFamily_Windows
			if (CStr Character = fg_CodePageCharacter())
				SpecialDirectories.f_Insert("Special " + Character);
			else
			{
				DMibConOut
					(
						"Skipping the Perforce test of a non-ASCII name: the active code page {} represents none of the characters tried\n"
						, GetACP()
					)
				;
			}
#else
			SpecialDirectories.f_Insert("Special *\u00e9");
#endif
			TCVector<CPerforceTestServer::CFile> SecondFiles = {{"Source/New.cpp", "Second\n"}};
			for (auto const &Directory : SpecialDirectories)
				SecondFiles.f_Insert(CPerforceTestServer::CFile{Directory / "New.cpp", "Second\n"});

			int64 SecondChangelist = co_await Server(&CPerforceTestServer::f_SubmitFiles, CStr("//Streams/Main"), SecondFiles, CStr("Add other files"));
			DMibExpect(SecondChangelist, >, FirstChangelist);

			// Outside Windows the workspace's root is not ASCII, which its client spec keeps as it is.
#ifdef DPlatformFamily_Windows
			CStr WorkRoot = Root / "Work";
#else
			CStr WorkRoot = Root / "Work\u00e9";
#endif
			co_await Server(&CPerforceTestServer::f_CreateClient, CStr("TestWork"), CStr("//Streams/Main"), WorkRoot);

			// The same directory on a file system that ignores case, which p4 accepts as the workspace's root.
			CStr OtherCaseRoot = Root / "WORK";
#ifndef DPlatformFamily_Windows
			OtherCaseRoot += "\u00e9";
#endif
			CFile::fs_CreateDirectory(WorkRoot);
			bool bOtherCase = CFile::fs_FileExists(OtherCaseRoot, EFileAttrib_Directory);

			struct CQueried
			{
				TCVector<CStr> m_OpenedBefore;
				TCVector<CStr> m_Opened;
				TCVector<CStr> m_ChangedFromSecond;
				TCVector<CStr> m_ChangedAfterSecond;
				TCVector<TCVector<CStr>> m_SpecialChangedFromSecond;
				TCVector<CStr> m_ChangedUnderOtherCase;
				TCVector<CStr> m_ChangedUnderSeparatorRoot;
				TCVector<CStr> m_SparseChanged;
				TCVector<CStr> m_SparseChangedUnderSource;
				TCVector<CStr> m_DittoChanged;
				TCVector<CStr> m_FileViewChanged;
			};

			auto Connection = co_await Server(&CPerforceTestServer::f_GetConnectionInfo, CStr("TestWork"));
			auto BlockingActorCheckout = fg_BlockingActor();
			CQueried Queried = co_await
				(
					g_Dispatch(BlockingActorCheckout) / [Connection, Root, WorkRoot, FirstChangelist, SecondChangelist, SpecialDirectories, OtherCaseRoot, bOtherCase]() -> CQueried
					{
						// An open server, without a protections table.
						CPerforceClientThrow Client(Connection);
						Client.f_Login(CStr());
						Client.f_Sync(WorkRoot / "...");

						auto fQuery = [&](TCVector<CStr> const &_Arguments)
							{
								auto Files = Client.f_GetFileStatClientFiles(_Arguments);
								Files.f_Sort();
								return Files;
							}
						;

						CQueried Result;
						Result.m_OpenedBefore = fQuery({"-Ro", WorkRoot / "..."});

						Client.f_OpenForEdit(WorkRoot / "Edited.cpp");
						fg_WriteFile(WorkRoot / "Added.cpp", "Added\n");
						Client.f_Add(WorkRoot / "Added.cpp");

						Result.m_Opened = fQuery({"-Ro", WorkRoot / "..."});
						Result.m_ChangedFromSecond = fQuery({"-c", CStr::fs_ToStr(SecondChangelist), WorkRoot / "..."});
						Result.m_ChangedAfterSecond = fQuery({"-c", CStr::fs_ToStr(SecondChangelist + 1), WorkRoot / "..."});

						for (auto const &Directory : SpecialDirectories)
						{
							CStr Special = CPerforceClient::fs_EscapeFileName(WorkRoot / Directory);
							Result.m_SpecialChangedFromSecond.f_Insert(fQuery({"-c", CStr::fs_ToStr(SecondChangelist), Special / "..."}));
						}

						// A root spelled with a separator ending it gives the files under it.
						Result.m_ChangedUnderSeparatorRoot = Client.f_GetFileStatFilesUnder
							(
								WorkRoot / "Source/"
								, {"-c", CStr::fs_ToStr(SecondChangelist), WorkRoot / "Source/..."}
							)
						;

						// The files under a root are spelled as it is, while p4 spells them as the workspace's root.
						if (bOtherCase)
						{
							auto Files = Client.f_GetFileStatFilesUnder(OtherCaseRoot / "Source", {"-c", CStr::fs_ToStr(SecondChangelist), OtherCaseRoot / "Source/..."});
							Files.f_Sort();
							Result.m_ChangedUnderOtherCase = fg_Move(Files);
						}

						// A classic workspace whose view maps two parts of the depot to other paths in the workspace.
						CStr SparseRoot = Root / "Sparse";
						auto SparseConnection = Connection;
						SparseConnection.m_Client = "TestSparse";
						CPerforceClientThrow SparseClient(SparseConnection);
						SparseClient.f_Login(CStr());
						CPerforceClient::CClient SparseSpec;
						SparseSpec.m_Root = SparseRoot;
						SparseSpec.m_Description = "Perforce test client with a sparse view.";
						SparseSpec.m_View =
							{
								{.m_From = "//Streams/Main/Source/...", .m_To = "//TestSparse/Src/..."}
								, {.m_From = "//Streams/Main/Edited.cpp", .m_To = "//TestSparse/Edited.cpp"}
							}
						;
						SparseClient.f_SetClient("TestSparse", SparseSpec);

						auto fSparseQuery = [&](CStr const &_Root)
							{
								auto Files = SparseClient.f_GetFileStatFilesUnder(_Root, {"-c", CStr::fs_ToStr(FirstChangelist), _Root / "..."});
								Files.f_Sort();
								return Files;
							}
						;
						Result.m_SparseChanged = fSparseQuery(SparseRoot);
						Result.m_SparseChangedUnderSource = fSparseQuery(SparseRoot / "Src");

						SparseSpec.m_View.f_Insert({.m_From = "&//Streams/Main/Source/...", .m_To = "//TestSparse/Copy/..."});
						SparseClient.f_SetClient("TestSparse", SparseSpec);
						Result.m_DittoChanged = fSparseQuery(SparseRoot);

						// A view of single files, whose mappings have no directory.
						SparseSpec.m_Root = Root / "Files";
						SparseSpec.m_View = {{.m_From = "//Streams/Main/Edited.cpp", .m_To = "//TestSparse/Edited.cpp"}};
						SparseClient.f_SetClient("TestSparse", SparseSpec);
						Result.m_FileViewChanged = fSparseQuery(Root / "Files");

						return Result;
					}
					% "Failed to query the test workspace"
				)
			;

			DMibExpect(Queried.m_OpenedBefore, ==, TCVector<CStr>{});
			DMibExpect(Queried.m_Opened, ==, (TCVector<CStr>{WorkRoot / "Added.cpp", WorkRoot / "Edited.cpp"}));
			TCVector<CStr> ChangedFromSecond = {WorkRoot / "Source/New.cpp"};
			TCVector<TCVector<CStr>> SpecialChangedFromSecond;
			for (auto const &Directory : SpecialDirectories)
			{
				ChangedFromSecond.f_Insert(WorkRoot / Directory / "New.cpp");
				SpecialChangedFromSecond.f_Insert(TCVector<CStr>{WorkRoot / Directory / "New.cpp"});
			}
			ChangedFromSecond.f_Sort();
			DMibExpect(Queried.m_ChangedFromSecond, ==, ChangedFromSecond);
			DMibExpect(Queried.m_ChangedAfterSecond, ==, TCVector<CStr>{});
			DMibExpect(Queried.m_ChangedUnderSeparatorRoot, ==, TCVector<CStr>{WorkRoot / "Source/New.cpp"});
			DMibExpect(Queried.m_SpecialChangedFromSecond, ==, SpecialChangedFromSecond);
			DMibExpect(Queried.m_SparseChanged, ==, (TCVector<CStr>{Root / "Sparse/Edited.cpp", Root / "Sparse/Src/New.cpp"}));
			DMibExpect(Queried.m_SparseChangedUnderSource, ==, (TCVector<CStr>{Root / "Sparse/Src/New.cpp"}));
			DMibExpect(Queried.m_DittoChanged, ==, (TCVector<CStr>{Root / "Sparse/Copy/New.cpp", Root / "Sparse/Edited.cpp", Root / "Sparse/Src/New.cpp"}));
			DMibExpect(Queried.m_FileViewChanged, ==, (TCVector<CStr>{Root / "Files/Edited.cpp"}));
			if (bOtherCase)
				DMibExpect(Queried.m_ChangedUnderOtherCase, ==, (TCVector<CStr>{OtherCaseRoot / "Source/New.cpp"}));
			else
				DMibConOut("{}\n", "Skipping the Perforce test of a root spelled with other case: the file system does not ignore case");

#ifndef DPlatformFamily_Windows
			// The fixture edits an existing non-ASCII file rather than add it again.
			int64 ThirdChangelist = co_await Server
				(
					&CPerforceTestServer::f_SubmitFiles
					, CStr("//Streams/Main")
					, TCVector<CPerforceTestServer::CFile>{{SpecialDirectories.f_GetLast() / "New.cpp", "Third\n"}}
					, CStr("Edit a file")
				)
			;
			DMibExpect(ThirdChangelist, >, SecondChangelist);
#endif

			co_return {};
		};

		DMibTestSuite("TranslatedCharset") -> TCFuture<void>
		{
			if (CPerforceTestServer::fs_FindServerExecutable().f_IsEmpty())
			{
				DMibConOut("{}\n", "Skipping the Perforce translated charset test: p4d was not found in PATH");
				co_return {};
			}

			CStr Root = fg_MakeTestRoot("TestPerforceTranslatedCharset");

			TCActor<CPerforceTestServer> Server = fg_Construct<CPerforceTestServer>();
			co_await Server(&CPerforceTestServer::f_StartUnicode, Root, CStr("tester"));
			auto ServerCleanup = co_await fg_AsyncDestroy(Server);

			co_await Server(&CPerforceTestServer::f_CreateStreamDepot, CStr("Streams"));
			co_await Server(&CPerforceTestServer::f_CreateMainlineStream, CStr("//Streams/Main"));
			co_await Server
				(
					&CPerforceTestServer::f_SubmitFiles
					, CStr("//Streams/Main")
					, TCVector<CPerforceTestServer::CFile>{{"Special \u00e9/New.cpp", "First\n"}}
					, CStr("Add a non-ASCII file")
				)
			;

			CStr WorkRoot = Root / "Work";
			co_await Server(&CPerforceTestServer::f_CreateClient, CStr("TestWork"), CStr("//Streams/Main"), WorkRoot);

			// A session of a charset that isn't UTF-8, used as constructed, takes and gives the strings in its command charset, which
			// the wrapper converts from and to UTF-8 with the API's converters, also on Windows, where it needn't be the code page:
			// cp850 spells \u00e9 0x82, cp1252 (winansi) 0xe9. The files the API opens itself, its enviro, ticket and P4CONFIG files
			// and its working directory, are named in its file names' encoding instead, so a non-ASCII directory holding them is
			// found in every charset: the session's client comes from the enviro file there.
			CStr Settings = Root / "Settings \u00e9";
			fg_WriteFile(Settings / "Command.p4config", "P4CHARSET=winansi\nP4COMMANDCHARSET=cp850\n");
			fg_WriteFile(Settings / "p4enviro", "P4CLIENT=TestWork\n");

			auto Connection = co_await Server(&CPerforceTestServer::f_GetConnectionInfo, CStr("TestWork"));
			Connection.m_WorkingDirectory = Settings;
			Connection.m_EnviroFile = Settings / "p4enviro";
			Connection.m_TicketFile = Settings / "p4tickets";
			Connection.m_Client = "";
			TCVector<CPerforceClient::CConnectionInfo> Connections;
			for (ch8 const *pCharset : {"winansi", "cp850", "utf8"})
				Connections.f_Insert(Connection).m_Charset = pCharset;
			{
				auto &ConfigConnection = Connections.f_Insert(Connection);
				ConfigConnection.m_Charset = "";
				ConfigConnection.m_CommandCharset.f_Clear(); // From the P4CONFIG file
				ConfigConnection.m_ConfigName = CStr("Command.p4config");
			}
			{
				// Unicode contents without a command charset: the API passes the strings on as the bytes they are, UTF-8 for the
				// Unicode server, also on Windows.
				auto &UntranslatedCommandConnection = Connections.f_Insert(Connection);
				UntranslatedCommandConnection.m_Charset = "utf8";
				UntranslatedCommandConnection.m_CommandCharset = CStr("none");
			}

			auto BlockingActorCheckout = fg_BlockingActor();
			TCVector<TCVector<CStr>> Queried = co_await
				(
					g_Dispatch(BlockingActorCheckout) / [Connections, WorkRoot]() -> TCVector<TCVector<CStr>>
					{
						TCVector<TCVector<CStr>> Results;
						for (auto &Connection : Connections)
						{
							CPerforceClientThrow Client(Connection);
							Results.f_Insert(Client.f_GetFileStatClientFiles({WorkRoot / "..."}));
							Results.f_Insert(Client.f_GetFileStatClientFiles({CPerforceClient::fs_EscapeFileName(WorkRoot / "Special \u00e9") / "..."}));
						}
						return Results;
					}
					% "Failed to query the translated workspace"
				)
			;

			CStr Expected = WorkRoot / "Special \u00e9/New.cpp";
			DMibExpect
				(
					Queried
					, ==
					, (TCVector<TCVector<CStr>>{{Expected}, {Expected}, {Expected}, {Expected}, {Expected}, {Expected}, {Expected}, {Expected}, {Expected}, {Expected}})
				)
			;

			co_return {};
		};

		DMibTestSuite("WideCharset") -> TCFuture<void>
		{
#ifdef DPlatformFamily_Windows
			DMibConOut("{}\n", "Skipping the Perforce wide charset test: on Windows the command charset of a wide one is the console's code page");
			co_return {};
#else
			if (CPerforceTestServer::fs_FindServerExecutable().f_IsEmpty())
			{
				DMibConOut("{}\n", "Skipping the Perforce wide charset test: p4d was not found in PATH");
				co_return {};
			}

			CStr Root = fg_MakeTestRoot("TestPerforceWideCharset");

			TCActor<CPerforceTestServer> Server = fg_Construct<CPerforceTestServer>();
			co_await Server(&CPerforceTestServer::f_StartUnicode, Root, CStr("tester"));
			auto ServerCleanup = co_await fg_AsyncDestroy(Server);

			co_await Server(&CPerforceTestServer::f_CreateStreamDepot, CStr("Streams"));
			co_await Server(&CPerforceTestServer::f_CreateMainlineStream, CStr("//Streams/Main"));
			co_await Server
				(
					&CPerforceTestServer::f_SubmitFiles
					, CStr("//Streams/Main")
					, TCVector<CPerforceTestServer::CFile>{{"Special \u00e9/New.cpp", "First\n"}}
					, CStr("Add a non-ASCII file")
				)
			;

			CStr WorkRoot = Root / "Work";
			co_await Server(&CPerforceTestServer::f_CreateClient, CStr("TestWork"), CStr("//Streams/Main"), WorkRoot);

			// A wide charset (UTF-16 or UTF-32) is only the content's: the port, client, user, directories and arguments are in the
			// command charset, P4COMMANDCHARSET or, when that is unset or wide too, the locale's, as in p4.
			CStr Settings = Root / "Settings";
			fg_WriteFile(Settings / "Wide.p4config", "P4CHARSET=utf16le\n");
			fg_WriteFile(Settings / "Command.p4config", "P4CHARSET=utf16le\nP4COMMANDCHARSET=iso8859-1\n");
			fg_WriteFile(Settings / "WideCommand.p4config", "P4CHARSET=utf32\nP4COMMANDCHARSET=utf16be\n");
			fg_WriteFile(Settings / "UnknownCommand.p4config", "P4CHARSET=utf16le\nP4COMMANDCHARSET=nosuchcharset\n");

			auto Connection = co_await Server(&CPerforceTestServer::f_GetConnectionInfo, CStr("TestWork"));
			Connection.m_WorkingDirectory = Settings;
			TCVector<CPerforceClient::CConnectionInfo> Connections;
			Connections.f_Insert(Connection).m_Charset = "utf16le";
			for (CStr ConfigName : {"Wide.p4config", "Command.p4config", "WideCommand.p4config", "UnknownCommand.p4config"})
			{
				auto &ConfigConnection = Connections.f_Insert(Connection);
				ConfigConnection.m_Charset = "";
				ConfigConnection.m_CommandCharset.f_Clear(); // From the P4CONFIG file
				ConfigConnection.m_ConfigName = ConfigName;
			}

			auto BlockingActorCheckout = fg_BlockingActor();
			TCVector<TCVector<CStr>> Queried = co_await
				(
					g_Dispatch(BlockingActorCheckout) / [Connections, WorkRoot]() -> TCVector<TCVector<CStr>>
					{
						TCVector<TCVector<CStr>> Results;
						for (auto &Connection : Connections)
						{
							CPerforceClient Client(Connection);
							TCVector<CStr> Files;
							TCVector<CStr> EscapedFiles;
							if
								(
									!Client.f_GetFileStatClientFiles({WorkRoot / "..."}, Files)
									|| !Client.f_GetFileStatClientFiles({CPerforceClient::fs_EscapeFileName(WorkRoot / "Special \u00e9") / "..."}, EscapedFiles)
								)
							{
								Results.f_Insert({Client.f_GetLastError()});
								continue;
							}
							Results.f_Insert(Files);
							Results.f_Insert(EscapedFiles);
						}
						return Results;
					}
					% "Failed to query the workspace with wide charsets"
				)
			;

			CStr Expected = WorkRoot / "Special \u00e9/New.cpp";
			DMibExpect
				(
					Queried
					, ==
					, (TCVector<TCVector<CStr>>{{Expected}, {Expected}, {Expected}, {Expected}, {Expected}, {Expected}, {Expected}, {Expected}, {"P4COMMANDCHARSET 'nosuchcharset' is not a charset"}})
				)
			;

			co_return {};
#endif
		};
	}

	DMibTestRegister(CRootFiles_Tests, Malterlib::Perforce);
}
