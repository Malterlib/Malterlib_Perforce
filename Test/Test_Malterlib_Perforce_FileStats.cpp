// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <Mib/Core/Core>
#include <Mib/Concurrency/AsyncDestroy>
#include <Mib/File/File>
#include <Mib/Test/Test>

using namespace NMib;
using namespace NMib::NStr;
using namespace NMib::NContainer;
using namespace NMib::NStorage;
using namespace NMib::NFunction;
using namespace NMib::NConcurrency;
using namespace NMib::NEncoding;

#include <Mib/Perforce/TestServer>
#include <Mib/Perforce/Wrapper>

#include "Malterlib_Perforce_TestHelpers.h"

using namespace NMib::NPerforce;

namespace
{
	// A name holding a wildcard, which a depot path escapes; Windows has no such file names.
#ifdef DPlatformFamily_Windows
	constexpr ch8 const *gc_pStarName = "Star%.txt";
#else
	constexpr ch8 const *gc_pStarName = "Star*.txt";
#endif

	struct CFileStats_Tests : public NMib::NTest::CTest
	{
		void f_DoTests();
	};

	void CFileStats_Tests::f_DoTests()
	{
		// A file moved away is gone from its path at the changelist that moved it, as a deleted one is, and there again once a
		// file is added at the path. The file it was moved to was not there before.
		DMibTestSuite("MovedAway") -> TCFuture<void>
		{
			if (CPerforceTestServer::fs_FindServerExecutable().f_IsEmpty())
			{
				DMibConOut("{}\n", "Skipping the Perforce test of a file moved away: p4d was not found in PATH");
				co_return {};
			}

			CStr Root = NFile::CFile::fs_GetProgramDirectory() / "TestPerforceMovedAway";
			if (NFile::CFile::fs_FileExists(Root, NFile::EFileAttrib_Directory))
				NFile::CFile::fs_DeleteDirectoryRecursive(Root, true);
			NFile::CFile::fs_CreateDirectory(Root);
			NTest::fg_TestAddCleanupPath(Root);

			TCActor<CPerforceTestServer> Server = fg_Construct<CPerforceTestServer>();
			co_await Server(&CPerforceTestServer::f_Start, Root, CStr("tester"));
			auto ServerCleanup = co_await fg_AsyncDestroy(Server);
			co_await Server(&CPerforceTestServer::f_CreateStreamDepot, CStr("Streams"));
			co_await Server(&CPerforceTestServer::f_CreateMainlineStream, CStr("//Streams/Main"));
			int64 Added = co_await Server(&CPerforceTestServer::f_SubmitFiles, CStr("//Streams/Main"), TCVector<CPerforceTestServer::CFile>{{"Moved.txt", "a\n"}}, CStr("Add the file"));

			CStr WorkRoot = Root / "Work";
			co_await Server(&CPerforceTestServer::f_CreateClient, CStr("TestWork"), CStr("//Streams/Main"), WorkRoot);
			auto Connection = co_await Server(&CPerforceTestServer::f_GetConnectionInfo, CStr("TestWork"));

			int64 Moved = co_await
				(
					g_Dispatch(fg_BlockingActor()) / [Connection, WorkRoot]() -> int64
					{
						CPerforceClientThrow Client(Connection);
						Client.f_Login(CStr());
						Client.f_Sync(WorkRoot / "...");
						Client.f_OpenForEdit(WorkRoot / "Moved.txt");
						Client.f_Move(WorkRoot / "Moved.txt", WorkRoot / "Away.txt");
						Client.f_Submit(WorkRoot / "...", "Move the file");

						return Client.f_GetHeadChangelist(WorkRoot / "...");
					}
					% "Failed to move the file"
				)
			;

			int64 AddedAgain = co_await Server
				(
					&CPerforceTestServer::f_SubmitFiles
					, CStr("//Streams/Main")
					, TCVector<CPerforceTestServer::CFile>{{"Moved.txt", "b\n"}}
					, CStr("Add the file again")
				)
			;

			TCVector<bool> Exists = co_await
				(
					g_Dispatch(fg_BlockingActor()) / [Connection, Added, Moved, AddedAgain]() -> TCVector<bool>
					{
						CPerforceClientThrow Client(Connection);
						Client.f_Login(CStr());

						TCVector<bool> Exists;
						for (auto Changelist : {Added, Moved, AddedAgain})
							Exists.f_Insert(Client.f_FileExistsInDepotNotDeleted(CStr("//Streams/Main/Moved.txt@{}"_f << Changelist)));

						for (auto Changelist : {Added, Moved})
							Exists.f_Insert(Client.f_FileExistsInDepotNotDeleted(CStr("//Streams/Main/Away.txt@{}"_f << Changelist)));

						return Exists;
					}
					% "Failed to look up the file"
				)
			;

			DMibExpect(Exists, ==, (TCVector<bool>{true, false, true, false, true}));

			co_return {};
		};

		// The stats of an opened file say what it is opened for and as, where a sync scheduled a resolve still to do, the revision the
		// opened file's changes are based on, and for a move, the file at its other end.
		DMibTestSuite("Opened") -> TCFuture<void>
		{
			if (CPerforceTestServer::fs_FindServerExecutable().f_IsEmpty())
			{
				DMibConOut("{}\n", "Skipping the Perforce test of an opened file's stats: p4d was not found in PATH");
				co_return {};
			}

			CStr Root = NFile::CFile::fs_GetProgramDirectory() / "TestPerforceOpenedStats";
			if (NFile::CFile::fs_FileExists(Root, NFile::EFileAttrib_Directory))
				NFile::CFile::fs_DeleteDirectoryRecursive(Root, true);
			NFile::CFile::fs_CreateDirectory(Root);
			NTest::fg_TestAddCleanupPath(Root);

			TCActor<CPerforceTestServer> Server = fg_Construct<CPerforceTestServer>();
			co_await Server(&CPerforceTestServer::f_Start, Root, CStr("tester"));
			auto ServerCleanup = co_await fg_AsyncDestroy(Server);
			co_await Server(&CPerforceTestServer::f_CreateStreamDepot, CStr("Streams"));
			co_await Server(&CPerforceTestServer::f_CreateMainlineStream, CStr("//Streams/Main"));
			co_await Server
				(
					&CPerforceTestServer::f_SubmitFiles
					, CStr("//Streams/Main")
					, TCVector<CPerforceTestServer::CFile>
					{
						{"Edited.txt", "a\n"}
						, {"Deleted.txt", "b\n"}
						, {"Kept.txt", "c\n"}
						, {"Moved.txt", "d\n"}
						, {"Integrated.txt", "e\n"}
						, {"Another.txt", "f\n"}
						, {"Resolved.txt", "g\n"}
					}
					, CStr("Add the files")
				)
			;

			CStr WorkRoot = Root / "Work";
			co_await Server(&CPerforceTestServer::f_CreateClient, CStr("TestWork"), CStr("//Streams/Main"), WorkRoot);
			auto Connection = co_await Server(&CPerforceTestServer::f_GetConnectionInfo, CStr("TestWork"));

			co_await
				(
					g_Dispatch(fg_BlockingActor())
					/ [Connection, WorkRoot]
					{
						CPerforceClientThrow Client(Connection);
						Client.f_Login(CStr());
						Client.f_Sync(WorkRoot / "...");
						Client.f_OpenForEdit(WorkRoot / "Edited.txt");
						Client.f_OpenForEdit(WorkRoot / "Resolved.txt");
						Client.f_Delete(WorkRoot / "Deleted.txt");
						Client.f_OpenForEdit(WorkRoot / "Moved.txt");
						Client.f_Move(WorkRoot / "Moved.txt", WorkRoot / "MovedTo.txt");

						// A resolve from another file, which fstat lists before the one a sync schedules from the file itself, as its name comes first.
						Client.f_OpenForEdit(WorkRoot / "Integrated.txt");
						TCVector<CStr> MustSync;
						TCVector<CPerforceClient::CMergeError> Errors;
						Client.f_IntegrateFiles("//Streams/Main/Another.txt", "//Streams/Main/Integrated.txt", false, true, MustSync, Errors);
					}
					% "Failed to open the files in the workspace"
				)
			;

			co_await Server
				(
					&CPerforceTestServer::f_SubmitFiles
					, CStr("//Streams/Main")
					, TCVector<CPerforceTestServer::CFile>{{"Edited.txt", "a main\n"}, {"Integrated.txt", "e main\n"}, {"Resolved.txt", "g main\n"}}
					, CStr("Change the files")
				)
			;

			auto Stats = co_await
				(
					g_Dispatch(fg_BlockingActor()) / [Connection, WorkRoot]() -> TCVector<CPerforceClient::CFileStats>
					{
						CPerforceClientThrow Client(Connection);
						Client.f_Login(CStr());
						Client.f_Sync(WorkRoot / "...");

						// A resolve done is still listed until the file is submitted.
						Client.f_ResolveAutomatic(WorkRoot / "Resolved.txt");

						TCVector<CPerforceClient::CFileStats> Stats;
						for (auto pName : {"Edited.txt", "Deleted.txt", "Kept.txt", "Moved.txt", "MovedTo.txt", "Integrated.txt", "Resolved.txt"})
							Stats.f_Insert(Client.f_FileStats(WorkRoot / pName));

						// An object a lookup of another file filled has none of its fields left.
						CPerforceClient::CFileStats Reused;
						Client.f_NoThrow().f_FileStats(WorkRoot / "Edited.txt", Reused);
						Client.f_NoThrow().f_FileStats(WorkRoot / "Kept.txt", Reused);
						Stats.f_Insert(fg_Move(Reused));

						return Stats;
					}
					% "Failed to read the stats of the files"
				)
			;

			DMibAssert(Stats.f_GetLen(), ==, 8u);
			{
				DMibTestPath("Edited");
				DMibExpect(Stats[0].m_Action, ==, "edit");
				DMibExpect(Stats[0].m_Type, ==, "text");
				DMibExpect(Stats[0].m_HaveRev, ==, 2u);
				DMibExpect(Stats[0].m_ResolveBaseFile, ==, "//Streams/Main/Edited.txt");
				DMibExpect(Stats[0].m_ResolveBaseRev, ==, 1u);
			}
			{
				DMibTestPath("Deleted");
				DMibExpect(Stats[1].m_Action, ==, "delete");
				DMibExpect(Stats[1].m_ResolveBaseRev, ==, 0u);
			}
			{
				DMibTestPath("Kept");
				DMibExpect(Stats[2].m_Action, ==, "");
				DMibExpect(Stats[2].m_Type, ==, "");
				DMibExpect(Stats[2].m_HaveRev, ==, 1u);
			}
			{
				DMibTestPath("Moved");
				DMibExpect(Stats[3].m_Action, ==, "move/delete");
				DMibExpect(Stats[3].m_MovedFile, ==, "//Streams/Main/MovedTo.txt");
				DMibExpect(Stats[4].m_Action, ==, "move/add");
				DMibExpect(Stats[4].m_MovedFile, ==, "//Streams/Main/Moved.txt");
			}
			{
				DMibTestPath("Reused");
				DMibExpect(Stats[7].m_Action, ==, "");
				DMibExpect(Stats[7].m_ResolveBaseFile, ==, "");
				DMibExpect(Stats[7].m_ResolveBaseRev, ==, 0u);
				DMibExpect(Stats[7].m_HaveRev, ==, 1u);
			}
			{
				DMibTestPath("SyncResolveAfterIntegration");
				DMibExpect(Stats[5].m_Action, ==, "edit");
				DMibExpect(Stats[5].m_ResolveBaseFile, ==, "//Streams/Main/Integrated.txt");
				DMibExpect(Stats[5].m_ResolveBaseRev, ==, 1u);
			}
			{
				DMibTestPath("Resolved");
				DMibExpect(Stats[6].m_Action, ==, "edit");
				DMibExpect(Stats[6].m_HaveRev, ==, 2u);
				DMibExpect(Stats[6].m_ResolveBaseFile, ==, "");
				DMibExpect(Stats[6].m_ResolveBaseRev, ==, 0u);
			}

			co_return {};
		};

		// A resolve from another file gives the opened file no resolve base, also one based on a revision of the file, as the
		// integration of a branch back into the file merged into it has: p4 diff compares the file with the revision the workspace
		// has.
		DMibTestSuite("OpenedIntegrationBack") -> TCFuture<void>
		{
			if (CPerforceTestServer::fs_FindServerExecutable().f_IsEmpty())
			{
				DMibConOut("{}\n", "Skipping the Perforce test of an integration back into its source: p4d was not found in PATH");
				co_return {};
			}

			CStr Root = NFile::CFile::fs_GetProgramDirectory() / "TestPerforceOpenedIntegrationBack";
			if (NFile::CFile::fs_FileExists(Root, NFile::EFileAttrib_Directory))
				NFile::CFile::fs_DeleteDirectoryRecursive(Root, true);
			NFile::CFile::fs_CreateDirectory(Root);
			NTest::fg_TestAddCleanupPath(Root);

			TCActor<CPerforceTestServer> Server = fg_Construct<CPerforceTestServer>();
			co_await Server(&CPerforceTestServer::f_Start, Root, CStr("tester"));
			auto ServerCleanup = co_await fg_AsyncDestroy(Server);

			CStr WorkRoot = Root / "Work";
			NFile::CFile::fs_CreateDirectory(WorkRoot);
			auto AdminConnection = co_await Server(&CPerforceTestServer::f_GetConnectionInfo, CStr());
			co_await
				(
					g_Dispatch(fg_BlockingActor())
					/ [AdminConnection, WorkRoot]
					{
						CPerforceClientThrow Client(AdminConnection);
						Client.f_Login(CStr());

						CPerforceClient::CClient Spec;
						Spec.m_Root = WorkRoot;
						Spec.m_Description = "A workspace of the depot that isn't a stream's.";
						Spec.m_View = {{.m_From = "//depot/...", .m_To = "//TestWork/..."}};
						Client.f_SetClient("TestWork", Spec);
					}
					% "Failed to set up the workspace"
				)
			;
			auto Connection = co_await Server(&CPerforceTestServer::f_GetConnectionInfo, CStr("TestWork"));

			auto Stats = co_await
				(
					g_Dispatch(fg_BlockingActor()) / [Connection, WorkRoot]() -> CPerforceClient::CFileStats
					{
						CPerforceClientThrow Client(Connection);
						Client.f_Login(CStr());
						auto fWrite = [&](CStr const &_Name, CStr const &_Text)
							{
								NFile::CFile::fs_WriteStringToFile(WorkRoot / _Name, CPerforceTestServer::fs_WorkspaceText(_Text), false);
							}
						;
						TCVector<CStr> MustSync;
						TCVector<CPerforceClient::CMergeError> Errors;

						fWrite("Source.txt", "a\nb\nc\nd\n");
						Client.f_Add(WorkRoot / "Source.txt");
						Client.f_Submit(WorkRoot / "Source.txt", "Add the file");
						Client.f_IntegrateFiles("//depot/Source.txt", "//depot/Branch.txt", false, false, MustSync, Errors);
						Client.f_Submit(WorkRoot / "Branch.txt", "Branch the file");

						Client.f_OpenForEdit(WorkRoot / "Source.txt");
						Client.f_OpenForEdit(WorkRoot / "Branch.txt");
						fWrite("Source.txt", "a\nadded to source\nb\nc\nd\n");
						fWrite("Branch.txt", "a\nb\nc\nadded to branch\nd\n");
						Client.f_Submit(WorkRoot / "...", "Change both");

						// The file is merged into the branch, so the next merge back is based on the file's revision merged.
						Client.f_IntegrateFiles("//depot/Source.txt", "//depot/Branch.txt", false, false, MustSync, Errors);
						Client.f_ResolveAutomatic(WorkRoot / "Branch.txt", 0, true);
						Client.f_Submit(WorkRoot / "Branch.txt", "Merge the file into the branch");

						Client.f_OpenForEdit(WorkRoot / "Source.txt");
						Client.f_OpenForEdit(WorkRoot / "Branch.txt");
						fWrite("Source.txt", "a\nadded to source\nb\nc\nd\nsource again\n");
						fWrite("Branch.txt", "a\nadded to source\nb\nbranch again\nc\nadded to branch\nd\n");
						Client.f_Submit(WorkRoot / "...", "Change both again");

						Client.f_IntegrateFiles("//depot/Branch.txt", "//depot/Source.txt", false, false, MustSync, Errors);
						return Client.f_OpenedFileStats(WorkRoot / "Source.txt");
					}
					% "Failed to integrate the branch back"
				)
			;

			DMibExpect(Stats.m_HaveRev, ==, 3u);
			DMibExpect(Stats.m_ResolveBaseFile, ==, "");
			DMibExpect(Stats.m_ResolveBaseRev, ==, 0u);

			co_return {};
		};

		// A filetype resolve still to do, after the content resolve from the same revision is done, gives the opened file no resolve
		// base: only a content resolve does, as p4 diff compares the file with the revision the workspace has once it is done.
		DMibTestSuite("OpenedFiletypeResolvePending") -> TCFuture<void>
		{
			if (CPerforceTestServer::fs_FindServerExecutable().f_IsEmpty())
			{
				DMibConOut("{}\n", "Skipping the Perforce test of a filetype resolve still to do: p4d was not found in PATH");
				co_return {};
			}

			CStr Root = NFile::CFile::fs_GetProgramDirectory() / "TestPerforceOpenedFiletypeResolvePending";
			if (NFile::CFile::fs_FileExists(Root, NFile::EFileAttrib_Directory))
				NFile::CFile::fs_DeleteDirectoryRecursive(Root, true);
			NFile::CFile::fs_CreateDirectory(Root);
			NTest::fg_TestAddCleanupPath(Root);

			TCActor<CPerforceTestServer> Server = fg_Construct<CPerforceTestServer>();
			co_await Server(&CPerforceTestServer::f_Start, Root, CStr("tester"));
			auto ServerCleanup = co_await fg_AsyncDestroy(Server);
			co_await Server(&CPerforceTestServer::f_CreateStreamDepot, CStr("Streams"));
			co_await Server(&CPerforceTestServer::f_CreateMainlineStream, CStr("//Streams/Main"));
			co_await Server(&CPerforceTestServer::f_SubmitFiles, CStr("//Streams/Main"), TCVector<CPerforceTestServer::CFile>{{"Typed.txt", "a\n"}}, CStr("Add the file"));

			CStr WorkRoot = Root / "Work";
			CStr OtherRoot = Root / "Other";
			co_await Server(&CPerforceTestServer::f_CreateClient, CStr("TestWork"), CStr("//Streams/Main"), WorkRoot);
			co_await Server(&CPerforceTestServer::f_CreateClient, CStr("OtherWork"), CStr("//Streams/Main"), OtherRoot);
			auto Connection = co_await Server(&CPerforceTestServer::f_GetConnectionInfo, CStr("TestWork"));
			auto OtherConnection = co_await Server(&CPerforceTestServer::f_GetConnectionInfo, CStr("OtherWork"));

			auto Stats = co_await
				(
					g_Dispatch(fg_BlockingActor()) / [Connection, OtherConnection, WorkRoot, OtherRoot]() -> CPerforceClient::CFileStats
					{
						CPerforceClientThrow Client(Connection);
						Client.f_Login(CStr());
						Client.f_Sync(WorkRoot / "...");
						Client.f_OpenForEdit(WorkRoot / "Typed.txt", "text+x");

						CPerforceClientThrow Other(OtherConnection);
						Other.f_Login(CStr());
						Other.f_Sync(OtherRoot / "...");
						Other.f_OpenForEdit(OtherRoot / "Typed.txt", "text+k");
						NFile::CFile::fs_WriteStringToFile(OtherRoot / "Typed.txt", CPerforceTestServer::fs_WorkspaceText("b\n"), false);
						Other.f_Submit(OtherRoot / "Typed.txt", "Change the file and its type");

						// The safe resolve takes the content, which only theirs changed, and leaves the types, which both changed.
						Client.f_Sync(WorkRoot / "...");
						Client.f_NoThrow().f_ResolveSafe(WorkRoot / "Typed.txt");
						return Client.f_OpenedFileStats(WorkRoot / "Typed.txt");
					}
					% "Failed to set up the resolves"
				)
			;

			DMibExpect(Stats.m_HaveRev, ==, 2u);
			DMibExpect(Stats.m_ResolveBaseFile, ==, "");
			DMibExpect(Stats.m_ResolveBaseRev, ==, 0u);

			co_return {};
		};

#ifndef DPlatformFamily_Windows
		// The workspace path of a depot file keeps a backslash of its name, which only Windows takes for a separator.
		DMibTestSuite("ClientPathBackslash") -> TCFuture<void>
		{
			if (CPerforceTestServer::fs_FindServerExecutable().f_IsEmpty())
			{
				DMibConOut("{}\n", "Skipping the Perforce test of a client path with a backslash: p4d was not found in PATH");
				co_return {};
			}

			CStr Root = NFile::CFile::fs_GetProgramDirectory() / "TestPerforceClientPathBackslash";
			if (NFile::CFile::fs_FileExists(Root, NFile::EFileAttrib_Directory))
				NFile::CFile::fs_DeleteDirectoryRecursive(Root, true);
			NFile::CFile::fs_CreateDirectory(Root);
			NTest::fg_TestAddCleanupPath(Root);

			TCActor<CPerforceTestServer> Server = fg_Construct<CPerforceTestServer>();
			co_await Server(&CPerforceTestServer::f_Start, Root, CStr("tester"));
			auto ServerCleanup = co_await fg_AsyncDestroy(Server);
			co_await Server(&CPerforceTestServer::f_CreateStreamDepot, CStr("Streams"));
			co_await Server(&CPerforceTestServer::f_CreateMainlineStream, CStr("//Streams/Main"));
			CStr WorkRoot = Root / "Work";
			co_await Server(&CPerforceTestServer::f_CreateClient, CStr("TestWork"), CStr("//Streams/Main"), WorkRoot);
			auto Connection = co_await Server(&CPerforceTestServer::f_GetConnectionInfo, CStr("TestWork"));

			auto ClientPath = co_await
				(
					g_Dispatch(fg_BlockingActor()) / [Connection]() -> CStr
					{
						CPerforceClientThrow Client(Connection);
						Client.f_Login(CStr());
						return Client.f_GetClientPath("//Streams/Main/Back\\slash.txt");
					}
					% "Failed to get the client path"
				)
			;

			DMibExpect(ClientPath, ==, WorkRoot + "/Back\\slash.txt");

			co_return {};
		};
#endif

		// Each file opened, with the changelist its open record is in: a numbered one, or the default, which only a session with
		// tagged output tells, and not an opened stream spec.
		DMibTestSuite("OpenedChangelists") -> TCFuture<void>
		{
			if (CPerforceTestServer::fs_FindServerExecutable().f_IsEmpty())
			{
				DMibConOut("{}\n", "Skipping the Perforce test of the changelists of opened files: p4d was not found in PATH");
				co_return {};
			}

			CStr Root = NFile::CFile::fs_GetProgramDirectory() / "TestPerforceOpenedChangelists";
			if (NFile::CFile::fs_FileExists(Root, NFile::EFileAttrib_Directory))
				NFile::CFile::fs_DeleteDirectoryRecursive(Root, true);
			NFile::CFile::fs_CreateDirectory(Root);
			NTest::fg_TestAddCleanupPath(Root);

			TCActor<CPerforceTestServer> Server = fg_Construct<CPerforceTestServer>();
			co_await Server(&CPerforceTestServer::f_Start, Root, CStr("tester"));
			auto ServerCleanup = co_await fg_AsyncDestroy(Server);
			co_await Server(&CPerforceTestServer::f_CreateStreamDepot, CStr("Streams"));
			co_await Server(&CPerforceTestServer::f_CreateMainlineStream, CStr("//Streams/Main"));
			co_await Server
				(
					&CPerforceTestServer::f_SubmitFiles
					, CStr("//Streams/Main")
					, TCVector<CPerforceTestServer::CFile>{{"Numbered.txt", "a\n"}, {"Default.txt", "b\n"}}
					, CStr("Add the files")
				)
			;

			CStr WorkRoot = Root / "Work";
			co_await Server(&CPerforceTestServer::f_CreateClient, CStr("TestWork"), CStr("//Streams/Main"), WorkRoot);
			auto Connection = co_await Server(&CPerforceTestServer::f_GetConnectionInfo, CStr("TestWork"));

			auto Opened = co_await
				(
					g_Dispatch(fg_BlockingActor()) / [Connection, WorkRoot]() -> TCVector<CStr>
					{
						CPerforceClientThrow Client(Connection);
						Client.f_Login(CStr());
						Client.f_Sync(WorkRoot / "...");
						Client.f_OpenForEdit(WorkRoot / "Numbered.txt");
						Client.f_OpenForEdit(WorkRoot / "Default.txt");
						uint32 Changelist = Client.f_CreateChangelist("Numbered", {}, {"//Streams/Main/Numbered.txt"});
						// p4 opened lists the stream's spec, which isn't a file, also for a path of a file.
						Client.f_OpenStreamSpec();

						TCVector<CPerforceClient::COpenedFile> Files;
						if (!Client.f_NoThrow().f_GetOpenedFiles(WorkRoot / "...", CStr(), Files))
							return {};

						TCVector<CStr> Opened;
						for (auto &File : Files)
							Opened.f_Insert(File.m_DepotFile + " " + (File.m_Change == CStr::fs_ToStr(Changelist) ? CStr("numbered") : File.m_Change));
						Opened.f_Sort();

						TCVector<CPerforceClient::COpenedFile> None;
						if (!Client.f_NoThrow().f_GetOpenedFiles(WorkRoot / "Missing.txt", CStr(), None) || !None.f_IsEmpty())
							Opened.f_Insert("Missing.txt opened");

						TCVector<CPerforceClient::COpenedFile> OneFile;
						if (!Client.f_NoThrow().f_GetOpenedFiles(WorkRoot / "Default.txt", CStr(), OneFile) || OneFile.f_GetLen() != 1)
							Opened.f_Insert("Default.txt not opened alone");

						// f_GetOpened gives the same files, as their depot files.
						TCVector<CStr> DepotFiles = Client.f_GetOpened(WorkRoot / "...", CStr());
						DepotFiles.f_Sort();
						if (DepotFiles != TCVector<CStr>{"//Streams/Main/Default.txt", "//Streams/Main/Numbered.txt"})
							Opened.f_Insert("f_GetOpened differs");

						// Only tagged output has the records read.
						CPerforceClient::CConnectionInfo Untagged = Connection;
						Untagged.m_bDisableTagging = true;
						CPerforceClient UntaggedClient(Untagged);
						TCVector<CPerforceClient::COpenedFile> UntaggedFiles;
						if (!UntaggedClient.f_Login(CStr()))
							Opened.f_Insert("Untagged login failed: " + UntaggedClient.f_GetLastError());
						else if (UntaggedClient.f_GetOpenedFiles(WorkRoot / "...", CStr(), UntaggedFiles))
							Opened.f_Insert("Untagged listed");
						else if (UntaggedClient.f_GetLastError() != "Listing the opened files needs a session with tagged output")
							Opened.f_Insert("Untagged failed with: " + UntaggedClient.f_GetLastError());

						return Opened;
					}
					% "Failed to list the opened files"
				)
			;

			DMibExpect(Opened, ==, (TCVector<CStr>{"//Streams/Main/Default.txt default", "//Streams/Main/Numbered.txt numbered"}));

			co_return {};
		};

		// f_OpenForWrite opens a file for edit, keeps one opened for add, and fails for one opened for delete, which p4 edit leaves as
		// it is and only tells of, also where the file is back in the workspace.
		DMibTestSuite("OpenForWrite") -> TCFuture<void>
		{
			if (CPerforceTestServer::fs_FindServerExecutable().f_IsEmpty())
			{
				DMibConOut("{}\n", "Skipping the Perforce test of opening files for write: p4d was not found in PATH");
				co_return {};
			}

			CStr Root = NFile::CFile::fs_GetProgramDirectory() / "TestPerforceOpenForWrite";
			if (NFile::CFile::fs_FileExists(Root, NFile::EFileAttrib_Directory))
				NFile::CFile::fs_DeleteDirectoryRecursive(Root, true);
			NFile::CFile::fs_CreateDirectory(Root);
			NTest::fg_TestAddCleanupPath(Root);

			TCActor<CPerforceTestServer> Server = fg_Construct<CPerforceTestServer>();
			co_await Server(&CPerforceTestServer::f_Start, Root, CStr("tester"));
			auto ServerCleanup = co_await fg_AsyncDestroy(Server);
			co_await Server(&CPerforceTestServer::f_CreateStreamDepot, CStr("Streams"));
			co_await Server(&CPerforceTestServer::f_CreateMainlineStream, CStr("//Streams/Main"));
			co_await Server
				(
					&CPerforceTestServer::f_SubmitFiles
					, CStr("//Streams/Main")
					, TCVector<CPerforceTestServer::CFile>{{"Edited.txt", "a\n"}, {"Deleted.txt", "b\n"}}
					, CStr("Add the files")
				)
			;

			CStr WorkRoot = Root / "Work";
			co_await Server(&CPerforceTestServer::f_CreateClient, CStr("TestWork"), CStr("//Streams/Main"), WorkRoot);
			auto Connection = co_await Server(&CPerforceTestServer::f_GetConnectionInfo, CStr("TestWork"));

			auto Results = co_await
				(
					g_Dispatch(fg_BlockingActor()) / [Connection, WorkRoot]() -> TCVector<CStr>
					{
						CPerforceClientThrow Client(Connection);
						Client.f_Login(CStr());
						Client.f_Sync(WorkRoot / "...");

						NFile::CFile::fs_WriteStringToFile(WorkRoot / "Added.txt", "c\n", false);
						Client.f_Add(WorkRoot / "Added.txt");
						Client.f_Delete(WorkRoot / "Deleted.txt");
						NFile::CFile::fs_WriteStringToFile(WorkRoot / "Deleted.txt", "b\n", false);

						TCVector<CStr> Results;
						for (CStr const &Name : {"Edited.txt", "Added.txt", "Deleted.txt"})
						{
							if (Client.f_NoThrow().f_OpenForWrite(WorkRoot / Name))
								Results.f_Insert(Name + ": " + Client.f_FileStats(WorkRoot / Name).m_Action);
							else
								Results.f_Insert(Name + ": " + Client.f_NoThrow().f_GetLastError());
						}

						return Results;
					}
					% "Failed to open the files for write"
				)
			;

			DMibExpect
				(
					Results
					, ==
					,
					(
						TCVector<CStr>
						{
							"Edited.txt: edit"
							, "Added.txt: add"
							, CStr("Deleted.txt: '{}' is opened for delete, which p4 edit leaves as it is"_f << (WorkRoot / "Deleted.txt"))
						}
					)
				)
			;

			co_return {};
		};

		// Where an overlay mapping, '+//Depot/...', maps two depot files to the path, the action is that of the one opened: here the
		// file opened for add in the primary depot, while the overlay's, which sorts first, has a submitted revision.
		DMibTestSuite("OpenForWriteOverlay") -> TCFuture<void>
		{
			if (CPerforceTestServer::fs_FindServerExecutable().f_IsEmpty())
			{
				DMibConOut("{}\n", "Skipping the Perforce test of opening files for write with an overlay mapping: p4d was not found in PATH");
				co_return {};
			}

			CStr Root = NFile::CFile::fs_GetProgramDirectory() / "TestPerforceOpenForWriteOverlay";
			if (NFile::CFile::fs_FileExists(Root, NFile::EFileAttrib_Directory))
				NFile::CFile::fs_DeleteDirectoryRecursive(Root, true);
			NFile::CFile::fs_CreateDirectory(Root);
			NTest::fg_TestAddCleanupPath(Root);

			TCActor<CPerforceTestServer> Server = fg_Construct<CPerforceTestServer>();
			co_await Server(&CPerforceTestServer::f_Start, Root, CStr("tester"));
			auto ServerCleanup = co_await fg_AsyncDestroy(Server);
			auto AdminConnection = co_await Server(&CPerforceTestServer::f_GetConnectionInfo, CStr());
			auto SetupConnection = co_await Server(&CPerforceTestServer::f_GetConnectionInfo, CStr("TestSetup"));
			auto Connection = co_await Server(&CPerforceTestServer::f_GetConnectionInfo, CStr("TestWork"));

			auto Result = co_await
				(
					g_Dispatch(fg_BlockingActor()) / [AdminConnection, SetupConnection, Connection, Root]() -> CStr
					{
						CStr SetupRoot = Root / "Setup";
						CStr WorkRoot = Root / "Work";
						NFile::CFile::fs_CreateDirectory(SetupRoot);
						NFile::CFile::fs_CreateDirectory(WorkRoot);

						CPerforceClientThrow Admin(AdminConnection);
						Admin.f_Login(CStr());
						Admin.f_CreateDepot("Primary", "local");
						Admin.f_CreateDepot("Overlay", "local");

						CPerforceClient::CClient SetupSpec;
						SetupSpec.m_Root = SetupRoot;
						SetupSpec.m_Description = "Submits the file.";
						SetupSpec.m_View = {{"//Overlay/...", "//TestSetup/..."}};
						Admin.f_SetClient("TestSetup", SetupSpec);

						CPerforceClient::CClient WorkSpec;
						WorkSpec.m_Root = WorkRoot;
						WorkSpec.m_Description = "Overlays one depot on another.";
						WorkSpec.m_View = {{"//Primary/...", "//TestWork/..."}};
						Admin.f_SetClient("TestWork", WorkSpec);

						CPerforceClientThrow Client(Connection);
						Client.f_Login(CStr());
						NFile::CFile::fs_WriteStringToFile(WorkRoot / "File.txt", "a\n", false);
						Client.f_Add(WorkRoot / "File.txt");

						WorkSpec.m_View.f_Insert({"+//Overlay/...", "//TestWork/..."});
						Admin.f_SetClient("TestWork", WorkSpec);

						CPerforceClientThrow Setup(SetupConnection);
						Setup.f_Login(CStr());
						NFile::CFile::fs_WriteStringToFile(SetupRoot / "File.txt", "b\n", false);
						Setup.f_Add(SetupRoot / "File.txt");
						Setup.f_Submit(SetupRoot / "File.txt", "Add the file");

						if (!Client.f_NoThrow().f_OpenForWrite(WorkRoot / "File.txt"))
							return Client.f_NoThrow().f_GetLastError();

						return Client.f_OpenedFileStats(WorkRoot / "File.txt").m_DepotFile;
					}
					% "Failed to open the file for write"
				)
			;

			DMibExpect(Result, ==, "//Primary/File.txt");

			co_return {};
		};

		// f_DepotFileStats and f_GetTextFileContentsIfExists tell what the depot doesn't have from a lookup that fails: a file it has
		// no revision of, one outside the workspace's root, a revision at a changelist from before the file was added, a revision that
		// deletes the file and #have of a file the workspace hasn't synced are absent, while an empty file is there, and empty.
		DMibTestSuite("AbsentFiles") -> TCFuture<void>
		{
			if (CPerforceTestServer::fs_FindServerExecutable().f_IsEmpty())
			{
				DMibConOut("{}\n", "Skipping the Perforce test of absent files: p4d was not found in PATH");
				co_return {};
			}

			CStr Root = NFile::CFile::fs_GetProgramDirectory() / "TestPerforceAbsentFiles";
			if (NFile::CFile::fs_FileExists(Root, NFile::EFileAttrib_Directory))
				NFile::CFile::fs_DeleteDirectoryRecursive(Root, true);
			NFile::CFile::fs_CreateDirectory(Root);
			NTest::fg_TestAddCleanupPath(Root);

			TCActor<CPerforceTestServer> Server = fg_Construct<CPerforceTestServer>();
			co_await Server(&CPerforceTestServer::f_Start, Root, CStr("tester"));
			auto ServerCleanup = co_await fg_AsyncDestroy(Server);
			co_await Server(&CPerforceTestServer::f_CreateStreamDepot, CStr("Streams"));
			co_await Server(&CPerforceTestServer::f_CreateMainlineStream, CStr("//Streams/Main"));
			int64 BeforeFile = co_await Server
				(
					&CPerforceTestServer::f_SubmitFiles
					, CStr("//Streams/Main")
					, TCVector<CPerforceTestServer::CFile>{{"Other.txt", "o\n"}}
					, CStr("Add another file")
				)
			;
			co_await Server
				(
					&CPerforceTestServer::f_SubmitFiles
					, CStr("//Streams/Main")
					, TCVector<CPerforceTestServer::CFile>{{"File.txt", "a\n"}, {"Empty.txt", ""}, {"Deleted.txt", "d\n"}}
					, CStr("Add the files")
				)
			;

			CStr WorkRoot = Root / "Work";
			co_await Server(&CPerforceTestServer::f_CreateClient, CStr("TestWork"), CStr("//Streams/Main"), WorkRoot);
			auto Connection = co_await Server(&CPerforceTestServer::f_GetConnectionInfo, CStr("TestWork"));

			auto Results = co_await
				(
					g_Dispatch(fg_BlockingActor()) / [Connection, Root, WorkRoot, BeforeFile]() -> TCVector<CStr>
					{
						CPerforceClientThrow Client(Connection);
						Client.f_Login(CStr());
						Client.f_Sync(WorkRoot / "Deleted.txt");
						Client.f_Delete(WorkRoot / "Deleted.txt");
						Client.f_Submit(WorkRoot / "Deleted.txt", "Delete the file");

						TCVector<CStr> Results;
						for (CStr const &Path : {WorkRoot / "File.txt", WorkRoot / "Missing.txt", Root / "Outside.txt"})
						{
							auto Stats = Client.f_DepotFileStats(Path);
							Results.f_Insert("{} {} {}"_f << NFile::CFile::fs_GetFile(Path) << Stats.m_DepotFile << Stats.m_HeadRev);
						}

						for
						(
							CStr Revision :
							{
								CStr("//Streams/Main/File.txt#1")
								, CStr("//Streams/Main/Missing.txt")
								, CStr("//Streams/Main/File.txt@{}"_f << BeforeFile)
								, CStr("//Streams/Main/Deleted.txt#head")
								, CStr("//Streams/Main/File.txt#have")
								, CStr("//Streams/Main/Empty.txt#1")
							}
						)
						{
							auto Contents = Client.f_GetTextFileContentsIfExists(Revision);
							Results.f_Insert(Contents ? *Contents : CStr("none"));
						}

						// Only tagged output tells a revision that deletes the file from an empty one.
						CPerforceClient::CConnectionInfo Untagged = Connection;
						Untagged.m_bDisableTagging = true;
						CPerforceClient UntaggedClient(Untagged);
						NStorage::TCOptional<CStr> UntaggedContents;
						if (!UntaggedClient.f_Login(CStr()))
							Results.f_Insert("Untagged login failed: " + UntaggedClient.f_GetLastError());
						else if (UntaggedClient.f_GetTextFileContentsIfExists("//Streams/Main/Deleted.txt#head", UntaggedContents))
							Results.f_Insert("Untagged read");
						else
							Results.f_Insert(UntaggedClient.f_GetLastError());

						return Results;
					}
					% "Failed to look up the files"
				)
			;

			DMibExpect
				(
					Results
					, ==
					,
					(
						TCVector<CStr>
						{
							"File.txt //Streams/Main/File.txt 1"
							, "Missing.txt  0"
							, "Outside.txt  0"
							, "a\n"
							, "none"
							, "none"
							, "none"
							, "none"
							, ""
							, "Reading a revision that may be absent needs a session with tagged output"
						}
					)
				)
			;

			co_return {};
		};

		// A session of a server without Unicode sends paths in the command charset, the code page on Windows, also to p4 where,
		// files and changes, also a workspace's name, and reads them back the same way, and the workspace's root, which the login gives.
		DMibTestSuite("NonAsciiPaths") -> TCFuture<void>
		{
			if (CPerforceTestServer::fs_FindServerExecutable().f_IsEmpty())
			{
				DMibConOut("{}\n", "Skipping the Perforce test of paths outside ASCII: p4d was not found in PATH");
				co_return {};
			}

			// A character outside ASCII that the command charset holds, which on Windows is the code page.
			CStr Character = fg_CodePageCharacter();
			if (!Character)
			{
				DMibConOut("{}\n", "Skipping the Perforce test of paths outside ASCII: the code page holds none of the characters tried");
				co_return {};
			}

			CStr FileName = "Caf{}.txt"_f << Character;
			CStr DepotFile = "//Streams/Main/" + FileName;

			CStr Root = NFile::CFile::fs_GetProgramDirectory() / "TestPerforceNonAsciiPaths";
			if (NFile::CFile::fs_FileExists(Root, NFile::EFileAttrib_Directory))
				NFile::CFile::fs_DeleteDirectoryRecursive(Root, true);
			NFile::CFile::fs_CreateDirectory(Root);
			NTest::fg_TestAddCleanupPath(Root);

			TCActor<CPerforceTestServer> Server = fg_Construct<CPerforceTestServer>();
			co_await Server(&CPerforceTestServer::f_Start, Root, CStr("tester"));
			auto ServerCleanup = co_await fg_AsyncDestroy(Server);
			co_await Server(&CPerforceTestServer::f_CreateStreamDepot, CStr("Streams"));
			co_await Server(&CPerforceTestServer::f_CreateMainlineStream, CStr("//Streams/Main"));
			int64 Changelist = co_await Server(&CPerforceTestServer::f_SubmitFiles, CStr("//Streams/Main"), TCVector<CPerforceTestServer::CFile>{{FileName, "a\n"}}, CStr("Add the file"));

			CStr WorkRoot = Root / ("W{}rk"_f << Character);
			CStr Workspace = "TestW{}rk"_f << Character;
			co_await Server(&CPerforceTestServer::f_CreateClient, Workspace, CStr("//Streams/Main"), WorkRoot);
			auto Connection = co_await Server(&CPerforceTestServer::f_GetConnectionInfo, Workspace);

			struct CResults
			{
				CStr m_DepotPath;
				CStr m_ClientPath;
				CStr m_WorkspacePath;
				TCVector<CStr> m_HeadFiles;
				int64 m_HeadChangelist = 0;
				TCVector<uint32> m_PendingChangelists;
				uint32 m_Changelist = 0;
				CStr m_ClientRoot;
				CStr m_MovedClientRoot;
			};

			auto Results = co_await
				(
					g_Dispatch(fg_BlockingActor()) / [Connection, WorkRoot, Root, FileName, DepotFile, Character, Workspace]() -> CResults
					{
						CPerforceClientThrow Client(Connection);
						Client.f_Login(CStr());

						CResults Results;
						Results.m_DepotPath = Client.f_GetDepotPath(WorkRoot / FileName);
						Results.m_ClientPath = Client.f_GetClientPath(DepotFile);
						Results.m_WorkspacePath = Client.f_GetWorkspacePath(DepotFile);
						for (auto &File : Client.f_FindHeadFiles("//Streams/Main/Caf{}*"_f << Character))
							Results.m_HeadFiles.f_Insert(File.m_File);
						Results.m_HeadChangelist = Client.f_GetHeadChangelist(DepotFile);

						// The workspace's pending changelists, which p4 changes is asked for by the workspace's name.
						Client.f_Sync(WorkRoot / "...");
						Client.f_OpenForEdit(WorkRoot / FileName);
						Results.m_Changelist = Client.f_CreateChangelist("Pending", {}, {DepotFile});
						for (auto &Changelist : Client.f_GetChangelists(DepotFile, false, Workspace, "pending"))
							Results.m_PendingChangelists.f_Insert(Changelist.m_ChangeID);

						// The root the login gives is the one asked for until a client's spec is written, which can move it.
						Results.m_ClientRoot = Client.f_GetClientRoot();
						CPerforceClient::CClient Spec;
						Spec.m_Root = Root / ("M{}ved"_f << Character);
						Spec.m_Description = "The workspace moved.";
						Spec.m_Stream = "//Streams/Main";
						Client.f_SetClient(Workspace, Spec);
						Results.m_MovedClientRoot = Client.f_GetClientRoot();

						return Results;
					}
					% "Failed to look up the paths"
				)
			;

			DMibExpect(Results.m_DepotPath, ==, DepotFile);
			DMibExpect(Results.m_ClientPath, ==, WorkRoot / FileName);
			DMibExpect(Results.m_WorkspacePath, ==, CStr("//{}/{}"_f << Workspace << FileName));
			DMibExpect(Results.m_PendingChangelists, ==, TCVector<uint32>{Results.m_Changelist});
			DMibExpect(Results.m_HeadFiles, ==, TCVector<CStr>{DepotFile});
			DMibExpect(Results.m_HeadChangelist, ==, Changelist);
			DMibExpect(Results.m_ClientRoot, ==, WorkRoot);
			DMibExpect(Results.m_MovedClientRoot, ==, Root / ("M{}ved"_f << Character));

			co_return {};
		};

		// A revision printed as its bytes, which no charset decodes, with its keywords expanded or as they are written.
		DMibTestSuite("Contents") -> TCFuture<void>
		{
			if (CPerforceTestServer::fs_FindServerExecutable().f_IsEmpty())
			{
				DMibConOut("{}\n", "Skipping the Perforce test of a revision's contents: p4d was not found in PATH");
				co_return {};
			}

			CStr Root = NFile::CFile::fs_GetProgramDirectory() / "TestPerforceContents";
			if (NFile::CFile::fs_FileExists(Root, NFile::EFileAttrib_Directory))
				NFile::CFile::fs_DeleteDirectoryRecursive(Root, true);
			NFile::CFile::fs_CreateDirectory(Root);
			NTest::fg_TestAddCleanupPath(Root);

			TCActor<CPerforceTestServer> Server = fg_Construct<CPerforceTestServer>();
			co_await Server(&CPerforceTestServer::f_Start, Root, CStr("tester"));
			auto ServerCleanup = co_await fg_AsyncDestroy(Server);
			co_await Server(&CPerforceTestServer::f_CreateStreamDepot, CStr("Streams"));
			co_await Server(&CPerforceTestServer::f_CreateMainlineStream, CStr("//Streams/Main"));
			auto Connection = co_await Server(&CPerforceTestServer::f_GetConnectionInfo, CStr());

			co_await
				(
					g_Dispatch(fg_BlockingActor())
					/ [Connection]
					{
						CPerforceClientThrow Client(Connection);
						Client.f_Login(CStr());
						Client.f_SetTypeMap({"text+k //Streams/Main/Keyword.txt"});
					}
					% "Failed to set the type map"
				)
			;

			co_await Server
				(
					&CPerforceTestServer::f_SubmitFiles
					, CStr("//Streams/Main")
					, TCVector<CPerforceTestServer::CFile>{{"Accented.txt", "caf\u00e9\n"}, {"Keyword.txt", "$Revision$\n"}}
					, CStr("Add the files")
				)
			;

			auto Contents = co_await
				(
					g_Dispatch(fg_BlockingActor()) / [Connection]() -> TCVector<CStr>
					{
						CPerforceClientThrow Client(Connection);
						Client.f_Login(CStr());

						auto fText = [](CByteVector const &_Bytes)
							{
								return CStr((ch8 const *)_Bytes.f_GetArray(), _Bytes.f_GetLen());
							}
						;

						return
							{
								fText(Client.f_GetFileContents("//Streams/Main/Accented.txt"))
								, fText(Client.f_GetFileContents("//Streams/Main/Keyword.txt"))
								, fText(Client.f_GetFileContents("//Streams/Main/Keyword.txt", false))
							}
						;
					}
					% "Failed to print the files"
				)
			;

			DMibExpect(Contents, ==, (TCVector<CStr>{"caf\u00e9\n", "$Revision: #1 $\n", "$Revision$\n"}));

			// Bytes that start with NUL, as UTF-32 big endian's signature does, are no string.
			CByteVector Binary;
			for (uint8 Byte : {uint8(0), uint8(0), uint8(0xFE), uint8(0xFF), uint8(0), uint8(1), uint8(0)})
				Binary.f_Insert(Byte);

			CStr WorkRoot = Root / "Work";
			co_await Server(&CPerforceTestServer::f_CreateClient, CStr("TestWork"), CStr("//Streams/Main"), WorkRoot);
			auto WorkConnection = co_await Server(&CPerforceTestServer::f_GetConnectionInfo, CStr("TestWork"));
			auto Printed = co_await
				(
					g_Dispatch(fg_BlockingActor()) / [WorkConnection, WorkRoot, Binary]() -> CByteVector
					{
						CPerforceClientThrow Client(WorkConnection);
						Client.f_Login(CStr());
						NFile::CFile::fs_CreateDirectory(WorkRoot);
						NFile::CFile::fs_WriteFile(WorkRoot / "Binary.bin", Binary);
						Client.f_Add(WorkRoot / "Binary.bin");
						Client.f_Submit(WorkRoot / "...", "Add a binary file");
						return Client.f_GetFileContents("//Streams/Main/Binary.bin");
					}
					% "Failed to print a binary file"
				)
			;

			DMibExpect(Printed, ==, Binary);

			co_return {};
		};

		// A changelist beyond 32 bits, which a server allows with sys.types.allow64.
		DMibTestSuite("Changelist64") -> TCFuture<void>
		{
			if (CPerforceTestServer::fs_FindServerExecutable().f_IsEmpty())
			{
				DMibConOut("{}\n", "Skipping the Perforce test of 64-bit changelists: p4d was not found in PATH");
				co_return {};
			}

			CStr Root = NFile::CFile::fs_GetProgramDirectory() / "TestPerforceChangelist64";
			if (NFile::CFile::fs_FileExists(Root, NFile::EFileAttrib_Directory))
				NFile::CFile::fs_DeleteDirectoryRecursive(Root, true);
			NFile::CFile::fs_CreateDirectory(Root);
			NTest::fg_TestAddCleanupPath(Root);

			TCActor<CPerforceTestServer> Server = fg_Construct<CPerforceTestServer>();
			co_await Server(&CPerforceTestServer::f_Start, Root, CStr("tester"));
			auto ServerCleanup = co_await fg_AsyncDestroy(Server);
			co_await Server(&CPerforceTestServer::f_SetConfigurable, CStr("sys.types.allow64"), CStr("1"));
			co_await Server(&CPerforceTestServer::f_SetCounter, CStr("change"), CStr("5000000000"));
			co_await Server(&CPerforceTestServer::f_CreateStreamDepot, CStr("Streams"));
			co_await Server(&CPerforceTestServer::f_CreateMainlineStream, CStr("//Streams/Main"));
			int64 Changelist = co_await Server(&CPerforceTestServer::f_SubmitFiles, CStr("//Streams/Main"), TCVector<CPerforceTestServer::CFile>{{"File.txt", "a\n"}}, CStr("Add a file"));

			auto Connection = co_await Server(&CPerforceTestServer::f_GetConnectionInfo, CStr());
			auto Stats = co_await
				(
					g_Dispatch(fg_BlockingActor())
					/ [Connection]()
					{
						CPerforceClientThrow Client(Connection);
						Client.f_Login(CStr());
						return Client.f_FileStats("//Streams/Main/File.txt");
					}
					% "Failed to read the file's stats"
				)
			;

			DMibExpect(Changelist, >, int64(5000000000));
			DMibExpect(Stats.m_HeadChange, ==, uint64(Changelist));

			co_return {};
		};

		// p4 diff names the depot revision it compares a file with, in a record of each file, which a result has only for its own
		// file: of a moved file, the revision the workspace has of the file it is moved from, also once a sync advanced it past the
		// one the move started from.
		DMibTestSuite("DiffComparedRevision") -> TCFuture<void>
		{
			if (CPerforceTestServer::fs_FindServerExecutable().f_IsEmpty())
			{
				DMibConOut("{}\n", "Skipping the Perforce test of the revision p4 diff compares with: p4d was not found in PATH");
				co_return {};
			}

			CStr Root = NFile::CFile::fs_GetProgramDirectory() / "TestPerforceDiffComparedRevision";
			if (NFile::CFile::fs_FileExists(Root, NFile::EFileAttrib_Directory))
				NFile::CFile::fs_DeleteDirectoryRecursive(Root, true);
			NFile::CFile::fs_CreateDirectory(Root);
			NTest::fg_TestAddCleanupPath(Root);

			TCActor<CPerforceTestServer> Server = fg_Construct<CPerforceTestServer>();
			co_await Server(&CPerforceTestServer::f_Start, Root, CStr("tester"));
			auto ServerCleanup = co_await fg_AsyncDestroy(Server);
			co_await Server(&CPerforceTestServer::f_CreateStreamDepot, CStr("Streams"));
			co_await Server(&CPerforceTestServer::f_CreateMainlineStream, CStr("//Streams/Main"));
			co_await Server
				(
					&CPerforceTestServer::f_SubmitFiles
					, CStr("//Streams/Main")
					, TCVector<CPerforceTestServer::CFile>{{"Source.txt", "a\nb\n"}, {"Changed.txt", "c\n"}, {"Unchanged.txt", "u\n"}, {" Spaced.txt", "s\n"}}
					, CStr("Add the file")
				)
			;

			CStr WorkRoot = Root / "Work";
			CStr OtherRoot = Root / "Other";
			co_await Server(&CPerforceTestServer::f_CreateClient, CStr("TestWork"), CStr("//Streams/Main"), WorkRoot);
			co_await Server(&CPerforceTestServer::f_CreateClient, CStr("OtherWork"), CStr("//Streams/Main"), OtherRoot);
			auto Connection = co_await Server(&CPerforceTestServer::f_GetConnectionInfo, CStr("TestWork"));
			auto OtherConnection = co_await Server(&CPerforceTestServer::f_GetConnectionInfo, CStr("OtherWork"));

			auto Diffs = co_await
				(
					g_Dispatch(fg_BlockingActor()) / [Connection, OtherConnection, WorkRoot, OtherRoot]() -> TCVector<CPerforceClient::CFileDiff>
					{
						CPerforceClientThrow Client(Connection);
						Client.f_Login(CStr());
						Client.f_Sync(WorkRoot / "...");
						Client.f_OpenForEdit(WorkRoot / "Source.txt");
						Client.f_Move(WorkRoot / "Source.txt", WorkRoot / "Moved.txt");
						NFile::CFile::fs_WriteStringToFile(WorkRoot / "Moved.txt", CPerforceTestServer::fs_WorkspaceText("a\nc\n"), false);
						Client.f_OpenForEdit(WorkRoot / "Unchanged.txt");
						Client.f_OpenForEdit(WorkRoot / "Changed.txt");
						NFile::CFile::fs_WriteStringToFile(WorkRoot / "Changed.txt", CPerforceTestServer::fs_WorkspaceText("c2\n"), false);
						Client.f_OpenForEdit(WorkRoot / " Spaced.txt");
						NFile::CFile::fs_WriteStringToFile(WorkRoot / " Spaced.txt", CPerforceTestServer::fs_WorkspaceText("s2\n"), false);

						CPerforceClientThrow Other(OtherConnection);
						Other.f_Login(CStr());
						Other.f_Sync(OtherRoot / "...");
						Other.f_OpenForEdit(OtherRoot / "Source.txt", "binary");
						NFile::CFile::fs_WriteStringToFile(OtherRoot / "Source.txt", "a\nb\nx\ny\nz\n", false);
						Other.f_Submit(OtherRoot / "Source.txt", "Change the file and make it binary");

						Client.f_NoThrow().f_Sync(WorkRoot / "...");
						// Only tagged output names the revisions compared.
						CPerforceClient::CConnectionInfo Untagged = Connection;
						Untagged.m_bDisableTagging = true;
						CPerforceClient UntaggedClient(Untagged);
						if (!UntaggedClient.f_Login(CStr()))
							DMibError("Untagged login failed: " + UntaggedClient.f_GetLastError());
						TCVector<CPerforceClient::CFileDiff> UntaggedDiffs;
						if (UntaggedClient.f_DiffOpenedFiles({WorkRoot / "Changed.txt"}, UntaggedDiffs))
							return {};
						if (UntaggedClient.f_GetLastError() != "Diffing the opened files needs a session with tagged output")
							DMibError("Untagged diff failed with: " + UntaggedClient.f_GetLastError());

						return Client.f_DiffOpenedFiles({WorkRoot / "Unchanged.txt", WorkRoot / "Moved.txt", WorkRoot / "Changed.txt", WorkRoot / " Spaced.txt"});
					}
					% "Failed to diff the moved file"
				)
			;

			// Each result has the record of its own file.
			TCMap<CStr, CPerforceClient::CFileDiff const *> ByName;
			for (auto const &Diff : Diffs)
				ByName[NFile::CFile::fs_GetFile(Diff.m_File)] = &Diff;
			DMibAssertTrue(ByName.f_FindEqual("Moved.txt"));
			DMibAssertTrue(ByName.f_FindEqual("Changed.txt"));
			DMibAssertTrue(ByName.f_FindEqual(" Spaced.txt"));
			DMibExpect((*ByName.f_FindEqual(" Spaced.txt"))->m_DepotFile, ==, "//Streams/Main/ Spaced.txt");
			auto const &Moved = **ByName.f_FindEqual("Moved.txt");
			DMibExpect(Moved.m_DepotFile, ==, "//Streams/Main/Source.txt");
			DMibExpect(Moved.m_Revision, ==, 2u);
			DMibAssert(Moved.m_Ranges.f_GetLen(), ==, 1u);
			DMibExpect(Moved.m_Ranges[0].m_nOld, ==, 4u);
			auto const &Changed = **ByName.f_FindEqual("Changed.txt");
			DMibExpect(Changed.m_DepotFile, ==, "//Streams/Main/Changed.txt");
			DMibExpect(Changed.m_Revision, ==, 1u);
			if (auto *pUnchanged = ByName.f_FindEqual("Unchanged.txt"))
			{
				DMibExpect((*pUnchanged)->m_DepotFile, ==, "//Streams/Main/Unchanged.txt");
				DMibExpect((*pUnchanged)->m_Revision, ==, 1u);
			}

			co_return {};
		};

		// The types of several revisions from one fstat are in the order asked, also two revisions of one file, and a revision the
		// workspace has. A revision that doesn't exist, or that the workspace doesn't have, has none, and the others keep theirs, also a
		// later revision asked of a file the workspace has no revision of, and one after #none, and #have of a file outside the
		// workspace's view. The server takes a name with an escape as it stores it, so one spelled with other letters for its digits
		// names the file only on a server that ignores case.
		DMibTestSuite("RevisionTypes") -> TCFuture<void>
		{
			if (CPerforceTestServer::fs_FindServerExecutable().f_IsEmpty())
			{
				DMibConOut("{}\n", "Skipping the Perforce test of the types of revisions: p4d was not found in PATH");
				co_return {};
			}

			CStr Root = NFile::CFile::fs_GetProgramDirectory() / "TestPerforceRevisionTypes";
			if (NFile::CFile::fs_FileExists(Root, NFile::EFileAttrib_Directory))
				NFile::CFile::fs_DeleteDirectoryRecursive(Root, true);
			NFile::CFile::fs_CreateDirectory(Root);
			NTest::fg_TestAddCleanupPath(Root);

			TCActor<CPerforceTestServer> Server = fg_Construct<CPerforceTestServer>();
			co_await Server(&CPerforceTestServer::f_Start, Root, CStr("tester"));
			auto ServerCleanup = co_await fg_AsyncDestroy(Server);
			co_await Server(&CPerforceTestServer::f_CreateStreamDepot, CStr("Streams"));
			co_await Server(&CPerforceTestServer::f_CreateMainlineStream, CStr("//Streams/Main"));
			// A stream the workspace doesn't map, whose files are outside its view.
			co_await Server(&CPerforceTestServer::f_CreateMainlineStream, CStr("//Streams/Outside"));
			co_await Server(&CPerforceTestServer::f_SubmitFiles, CStr("//Streams/Outside"), TCVector<CPerforceTestServer::CFile>{{"Away.txt", "a\n"}}, CStr("Add a file outside"));
			co_await Server
				(
					&CPerforceTestServer::f_SubmitFiles
					, CStr("//Streams/Main")
					, TCVector<CPerforceTestServer::CFile>{{"Other.txt", "a\n"}, {"Removed.txt", "a\n"}, {gc_pStarName, "a\n"}}
					, CStr("Add the files")
				)
			;

			CStr WorkRoot = Root / "Work";
			co_await Server(&CPerforceTestServer::f_CreateClient, CStr("TestWork"), CStr("//Streams/Main"), WorkRoot);
			auto Connection = co_await Server(&CPerforceTestServer::f_GetConnectionInfo, CStr("TestWork"));

			CStr StarFile = CPerforceClient::fs_EscapeFileName(gc_pStarName);
			CStr StarFileLower = StarFile.f_Replace("%2A", "%2a");

			struct CTypes
			{
				TCVector<CStr> m_Types;
				CStr m_CaseHandling;
			};

			auto Types = co_await
				(
					g_Dispatch(fg_BlockingActor()) / [Connection, WorkRoot, StarFile, StarFileLower]() -> CTypes
					{
						CPerforceClientThrow Client(Connection);
						Client.f_Login(CStr());
						Client.f_Info();
						CTypes Result;
						Result.m_CaseHandling = Client.f_NoThrow().f_GetLastInfo("caseHandling");
						Client.f_Sync(WorkRoot / "...");

						NFile::CFile::fs_WriteStringToFile(WorkRoot / "Retyped.txt", "a\n", false);
						Client.f_Add(WorkRoot / "Retyped.txt");
						Client.f_Submit(WorkRoot / "Retyped.txt", "Add the file");
						Client.f_OpenForEdit(WorkRoot / "Retyped.txt", "binary");
						Client.f_Submit(WorkRoot / "Retyped.txt", "Make it binary");
						Client.f_Sync(WorkRoot / "Removed.txt#0");

						Result.m_Types = Client.f_GetRevisionTypes
							(
								{
									"//Streams/Main/Retyped.txt#2"
									, "//Streams/Main/Retyped.txt#5"
									, "//Streams/Main/Other.txt#have"
									, "//Streams/Main/Unsynced.txt#have"
									, "//Streams/Main/Retyped.txt#1"
									, "//Streams/Main/Removed.txt#have"
									, "//Streams/Main/Removed.txt#1"
									, "//Streams/Main/Retyped.txt#none"
									, "//Streams/Main/Retyped.txt#2"
									, CStr("//Streams/Main/{}#1"_f << StarFile)
									, CStr("//Streams/Main/{}#1"_f << StarFileLower)
									, "//Streams/Outside/Away.txt#have"
									, "//Streams/Outside/Away.txt#1"
								}
							)
						;

						return Result;
					}
					% "Failed to look up the types"
				)
			;

			DMibAssertTrue(Types.m_CaseHandling == "sensitive" || Types.m_CaseHandling == "insensitive");
			CStr LowerType = Types.m_CaseHandling == "insensitive" || StarFileLower == StarFile ? "text" : "";
			DMibExpect(Types.m_Types, ==, (TCVector<CStr>{"binary", "", "text", "", "text", "", "text", "", "binary", "text", LowerType, "", "text"}));

			// The stats of the revision the workspace has of a file outside its view are those of no revision.
			auto OutsideHave = co_await
				(
					g_Dispatch(fg_BlockingActor()) / [Connection]() -> uint32
					{
						CPerforceClientThrow Client(Connection);
						Client.f_Login(CStr());
						return Client.f_HaveFileStats("//Streams/Outside/Away.txt").m_HaveRev;
					}
					% "Failed to read the revision the workspace has"
				)
			;
			DMibExpect(OutsideHave, ==, 0u);

			// A request fstat could give more than one entry or none for, a selector other than a revision number, #have, #head or #none,
			// a range, a wildcard or no selector, fails the call before p4 runs, as does a number larger than a revision's, which p4d takes
			// for another.
			auto Rejected = co_await
				(
					g_Dispatch(fg_BlockingActor()) / [Connection]() -> TCVector<bool>
					{
						CPerforceClientThrow Client(Connection);
						Client.f_Login(CStr());

						TCVector<bool> Rejected;
						for
						(
							CStr Request :
							{
								"//Streams/Main/Other.txt@1"
								, "//Streams/Main/Other.txt#1,#2"
								, "//Streams/Main/....txt#1"
								, "//Streams/Main/*.txt#1"
								, "//Streams/Main/Other.txt#4294967297"
								, "//Streams/Main/Other.txt"
								, "//Streams/Main/Other.txt#"
							}
						)
						{
							TCVector<CStr> Types;
							Rejected.f_Insert(!Client.f_NoThrow().f_GetRevisionTypes({"//Streams/Main/Other.txt#head", Request}, Types));
						}

						return Rejected;
					}
					% "Failed to ask for the types"
				)
			;
			DMibExpect(Rejected, ==, (TCVector<bool>{true, true, true, true, true, true, true}));

			// The listing under a root gives each file the fields of its own record, also where a -T filter leaves out the depot file.
			auto Listed = co_await
				(
					g_Dispatch(fg_BlockingActor()) / [Connection, WorkRoot]() -> TCVector<CStr>
					{
						CPerforceClientThrow Client(Connection);
						Client.f_Login(CStr());

						// The stats are the query's, whatever the vector held.
						TCVector<CPerforceClient::CFileStats> Stats;
						Stats.f_Insert();
						auto Files = Client.f_GetFileStatFilesUnder(WorkRoot, {"-T", "clientFile,headType", "//Streams/Main/..."}, Stats);

						TCVector<CStr> Listed;
						if (Stats.f_GetLen() != Files.f_GetLen())
							return TCVector<CStr>{CStr("{} stats of {} files"_f << Stats.f_GetLen() << Files.f_GetLen())};

						for (umint iFile = 0; iFile < Files.f_GetLen(); ++iFile)
							Listed.f_Insert("{} {}"_f << NFile::CFile::fs_GetFile(Files[iFile]) << Stats[iFile].m_HeadType);

						return Listed;
					}
					% "Failed to list the files"
				)
			;

			DMibExpect(Listed, ==, (TCVector<CStr>{"Other.txt text", "Removed.txt text", "Retyped.txt binary", CStr(gc_pStarName) + " text"}));

			co_return {};
		};

		// A server that ignores case gives a revision's depot file as the depot spells it, which is the revision asked in another
		// case.
		DMibTestSuite("RevisionTypesCaseInsensitive") -> TCFuture<void>
		{
			if (CPerforceTestServer::fs_FindServerExecutable().f_IsEmpty())
			{
				DMibConOut("{}\n", "Skipping the Perforce test of the types of revisions on a server that ignores case: p4d was not found in PATH");
				co_return {};
			}

			CStr Root = NFile::CFile::fs_GetProgramDirectory() / "TestPerforceRevisionTypesCaseInsensitive";
			if (NFile::CFile::fs_FileExists(Root, NFile::EFileAttrib_Directory))
				NFile::CFile::fs_DeleteDirectoryRecursive(Root, true);
			NFile::CFile::fs_CreateDirectory(Root);
			NTest::fg_TestAddCleanupPath(Root);

			TCActor<CPerforceTestServer> Server = fg_Construct<CPerforceTestServer>();
			co_await Server(&CPerforceTestServer::f_StartCaseInsensitive, Root, CStr("tester"));
			auto ServerCleanup = co_await fg_AsyncDestroy(Server);
			co_await Server(&CPerforceTestServer::f_CreateStreamDepot, CStr("Streams"));
			co_await Server(&CPerforceTestServer::f_CreateMainlineStream, CStr("//Streams/Main"));
			co_await Server(&CPerforceTestServer::f_SubmitFiles, CStr("//Streams/Main"), TCVector<CPerforceTestServer::CFile>{{"Upper.txt", "a\n"}}, CStr("Add a file"));

			CStr WorkRoot = Root / "Work";
			co_await Server(&CPerforceTestServer::f_CreateClient, CStr("TestWork"), CStr("//Streams/Main"), WorkRoot);
			auto Connection = co_await Server(&CPerforceTestServer::f_GetConnectionInfo, CStr("TestWork"));

			auto Types = co_await
				(
					g_Dispatch(fg_BlockingActor()) / [Connection, WorkRoot]() -> TCVector<CStr>
					{
						CPerforceClientThrow Client(Connection);
						Client.f_Login(CStr());
						Client.f_Sync(WorkRoot / "...");

						return Client.f_GetRevisionTypes({"//Streams/Main/UPPER.TXT#1", "//Streams/Main/upper.txt#have"});
					}
					% "Failed to look up the types"
				)
			;

			DMibExpect(Types, ==, (TCVector<CStr>{"text", "text"}));

			co_return {};
		};
	}
}

DMibTestRegister(CFileStats_Tests, Malterlib::Perforce);
