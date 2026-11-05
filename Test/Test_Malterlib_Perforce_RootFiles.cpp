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

#ifndef DPlatformFamily_Windows
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

	CStr fg_MakeTestRoot(CStr const &_Name)
	{
		CStr Root = CFile::fs_GetProgramDirectory() / _Name;
		if (CFile::fs_FileExists(Root, EFileAttrib_Directory))
			CFile::fs_DeleteDirectoryRecursive(Root, true);
		CFile::fs_CreateDirectory(Root);
		NTest::fg_TestAddCleanupPath(Root);

		return Root;
	}

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
			CPerforceIgnore::CSettings Settings{.m_IgnoreNames = ".p4ignore", .m_ConfigName = ".p4config"};
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
			}

			{
				DMibTestPath("List");
				TCSet<CStr> OtherRepositories;
				OtherRepositories[Workspace / "Other"];
				CPerforceRootFiles RootFiles(Workspace, OtherRepositories, Settings);
				DMibExpect(RootFiles.f_List(), ==, (TCVector<CStr>{".p4ignore", "A.cpp", "Keep.log", "Source/.p4ignore", "Source/B.cpp"}));
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
				CPerforceRootFiles(Workspace, OtherRepositories, CPerforceIgnore::CSettings{.m_IgnoreNames = ".p4ignore", .m_ConfigName = "Config/p4config"})
					.f_ListExcluded(&PathConfigRuleFiles)
				;
				TCVector<CStr> AbsoluteConfigRuleFiles;
				CPerforceRootFiles(Workspace, OtherRepositories, CPerforceIgnore::CSettings{.m_IgnoreNames = ".p4ignore", .m_ConfigName = Root / "Shared.p4config"})
					.f_ListExcluded(&AbsoluteConfigRuleFiles)
				;
				DMibExpectTrue(PathConfigRuleFiles.f_Contains(Workspace / "Config/p4config") >= 0);
				DMibExpectTrue(PathConfigRuleFiles.f_Contains(Root / "Config/p4config") >= 0);
				DMibExpectTrue(AbsoluteConfigRuleFiles.f_Contains(Root / "Shared.p4config") >= 0);
				DMibExpectFalse(AbsoluteConfigRuleFiles.f_Contains(Workspace / (Root / "Shared.p4config")) >= 0);

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
				DMibExpect(CPerforceIgnore::fs_ResolveIgnoreNames("Config/ignore;.p4ignore", "C:/Root"), ==, "C:\\Root\\Config\\ignore;.p4ignore");
				DMibExpect(CPerforceIgnore::fs_ResolveIgnoreNames("Config\\ignore", "C:/Root"), ==, "C:\\Root\\Config\\ignore");
				DMibExpect(CPerforceIgnore::fs_ResolveIgnoreNames("Config/ignore", "C:/Root"), ==, "Config/ignore");
				// Drive-rooted and drive-relative paths resolve against the directory's drive.
				DMibExpect(CPerforceIgnore::fs_ResolveIgnoreNames("\\Config\\ignore", "D:/Root"), ==, "D:\\Config\\ignore");
				DMibExpect(CPerforceIgnore::fs_ResolveIgnoreNames("D:Config\\ignore", "D:/Root"), ==, "D:\\Root\\Config\\ignore");
				DMibExpect(CPerforceIgnore::fs_ResolveIgnoreNames("C:Config\\ignore", "D:/Root"), ==, "C:Config\\ignore");
#else
				DMibExpect(CPerforceIgnore::fs_ResolveIgnoreNames("Config/ignore;.p4ignore", "/Root"), ==, "/Root/Config/ignore;.p4ignore");
				DMibExpect(CPerforceIgnore::fs_ResolveIgnoreNames("Config/ignore", "/Root"), ==, "/Root/Config/ignore");
				DMibExpect(CPerforceIgnore::fs_ResolveIgnoreNames("./Config/ignore:/Shared/ignore", "/Root"), ==, "/Root/Config/ignore;/Shared/ignore");
				// p4 makes the separators of a list its own, and keeps a single name as it is.
				DMibExpect(CPerforceIgnore::fs_ResolveIgnoreNames("Config\\ignore;.p4ignore", "/Root"), ==, "/Root/Config/ignore;.p4ignore");
				DMibExpect(CPerforceIgnore::fs_ResolveIgnoreNames("Config\\ignore", "/Root"), ==, "Config\\ignore");
#endif

				fg_WriteFile(Workspace / "Config/ignore", "Keep.log\n");
				CPerforceIgnore Ignore(CPerforceIgnore::CSettings{.m_IgnoreNames = CPerforceIgnore::fs_ResolveIgnoreNames("Config/ignore", Workspace), .m_ConfigName = ".p4config"});
				DMibExpectTrue(Ignore.f_IsIgnored(Workspace / "Keep.log", false));
			}

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
				DMibTestPath("CustomIgnoreFileName");
				fg_WriteFile(Workspace / "p4ignore.txt", "A.cpp\n");
				CPerforceIgnore Ignore(CPerforceIgnore::CSettings{.m_IgnoreNames = "p4ignore.txt", .m_ConfigName = ".p4config"});
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

					return {Settings.m_IgnoreNames, Settings.m_ConfigName, Settings.m_EnviroFile};
				}
			;

			CStr Default = ".p4ignore;p4ignore.txt;{};{}"_f << fNative(Home / ".p4ignore") << fNative(Home / "p4ignore.txt");
#ifdef DPlatformFamily_Windows
			// p4 has no default enviro file on Windows.
			CStr DefaultEnviro;
			CStr EnviroIgnore = Default;
			CStr EnviroConfig;
#else
			CStr DefaultEnviro = Home / ".p4enviro";
			CStr EnviroIgnore = "Enviro.ignore";
			CStr EnviroConfig = ".p4config";
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
				}
			;
			DMibExpect(Settings, ==, Expected);

			// $home is the home directory without the separator ending it.
			auto TrailingHome = CPerforceIgnore::fs_GetSettings(Plain, {{HomeVariable, Home + "/"}, {"P4IGNORE", "$home.ignore"}});
			DMibExpect(TrailingHome.m_IgnoreNames, ==, Home + ".ignore");
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
			// with a server that isn't a Unicode one.
			TCVector<CStr> SpecialDirectories = {"Special @#%"};
#ifndef DPlatformFamily_Windows
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
	}

	DMibTestRegister(CRootFiles_Tests, Malterlib::Perforce);
}
