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
	}
}

DMibTestRegister(CFileStats_Tests, Malterlib::Perforce);
