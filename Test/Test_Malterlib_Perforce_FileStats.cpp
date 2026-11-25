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

using namespace NMib::NPerforce;

namespace
{
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
	}
}

DMibTestRegister(CFileStats_Tests, Malterlib::Perforce);
