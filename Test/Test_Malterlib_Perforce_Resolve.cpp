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
	struct CResolve_Tests : public NMib::NTest::CTest
	{
		void f_DoTests();
	};

	void CResolve_Tests::f_DoTests()
	{
		// The safe resolve takes the side that changed a file changed on one side only, and leaves a file changed on both sides
		// unresolved, also when its changes don't conflict.
		DMibTestSuite("Resolve") -> TCFuture<void>
		{
			if (CPerforceTestServer::fs_FindServerExecutable().f_IsEmpty())
			{
				DMibConOut("{}\n", "Skipping the Perforce resolve tests: p4d was not found in PATH");
				co_return {};
			}

			CStr Root = NFile::CFile::fs_GetProgramDirectory() / "TestPerforceResolve";
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
					, TCVector<CPerforceTestServer::CFile>{{"Both.txt", "1\n2\n3\n4\n5\n"}, {"Theirs.txt", "a\nb\nc\n"}}
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
						Client.f_OpenForEdit(WorkRoot / "Both.txt");
						Client.f_OpenForEdit(WorkRoot / "Theirs.txt");
						NFile::CFile::fs_WriteStringToFile(WorkRoot / "Both.txt", "1 work\n2\n3\n4\n5\n", false);
					}
					% "Failed to edit the files in the workspace"
				)
			;

			co_await Server
				(
					&CPerforceTestServer::f_SubmitFiles
					, CStr("//Streams/Main")
					, TCVector<CPerforceTestServer::CFile>{{"Both.txt", "1\n2\n3\n4\n5 main\n"}, {"Theirs.txt", "a\nb\nc main\n"}}
					, CStr("Change the files")
				)
			;

			struct CResult
			{
				TCVector<CPerforceClient::CUnresolvedFile> m_Scheduled;
				TCVector<CPerforceClient::CUnresolvedFile> m_Left;
				TCVector<CPerforceClient::CUnresolvedFile> m_LeftOfResolved;
				bool m_bResolvedNothing = false;
				CStr m_UntaggedError;
			};

			CResult Result = co_await
				(
					g_Dispatch(fg_BlockingActor()) / [Connection, WorkRoot]() -> CResult
					{
						CPerforceClientThrow Client(Connection);
						Client.f_Login(CStr());
						Client.f_Sync(WorkRoot / "...");

						CResult Result;
						Result.m_Scheduled = Client.f_GetUnresolvedFiles(CStr());
						Client.f_ResolveSafe(CStr());
						Result.m_Left = Client.f_GetUnresolvedFiles(CStr());

						Result.m_bResolvedNothing = Client.f_NoThrow().f_ResolveSafe(WorkRoot / "Theirs.txt");
						Result.m_LeftOfResolved = Client.f_GetUnresolvedFiles(WorkRoot / "Theirs.txt");

						// An untagged session, whose output names no files, refuses rather than finds none.
						auto UntaggedConnection = Connection;
						UntaggedConnection.m_bDisableTagging = true;
						CPerforceClientThrow Untagged(UntaggedConnection);
						Untagged.f_Login(CStr());
						TCVector<CPerforceClient::CUnresolvedFile> UntaggedFiles;
						if (Untagged.f_NoThrow().f_GetUnresolvedFiles(CStr(), 0, UntaggedFiles))
							Result.m_UntaggedError = "Listed";
						else
							Result.m_UntaggedError = Untagged.f_NoThrow().f_GetLastError();

						return Result;
					}
					% "Failed to resolve the files in the workspace"
				)
			;

			auto fClientFiles = [](TCVector<CPerforceClient::CUnresolvedFile> const &_Files)
				{
					TCVector<CStr> ClientFiles;
					for (auto const &File : _Files)
						ClientFiles.f_Insert(File.m_ClientFile);
					return ClientFiles;
				}
			;

			DMibExpect(fClientFiles(Result.m_Scheduled), ==, (TCVector<CStr>{WorkRoot / "Both.txt", WorkRoot / "Theirs.txt"}));
			DMibExpect(fClientFiles(Result.m_Left), ==, TCVector<CStr>{WorkRoot / "Both.txt"});
			DMibAssert(Result.m_Left.f_GetLen(), ==, 1);
			DMibExpect(Result.m_Left[0].m_FromFile, ==, "//Streams/Main/Both.txt");
			DMibExpect(Result.m_Left[0].m_ResolveType, ==, "content");
			DMibExpect(Result.m_Left[0].m_StartFromRev, ==, 1u);
			DMibExpect(Result.m_Left[0].m_EndFromRev, ==, 2u);
			DMibExpect(NFile::CFile::fs_ReadStringFromFile(WorkRoot / "Both.txt", true), ==, "1 work\n2\n3\n4\n5\n");
			DMibExpect(NFile::CFile::fs_ReadStringFromFile(WorkRoot / "Theirs.txt", true), ==, CPerforceTestServer::fs_WorkspaceText("a\nb\nc main\n"));

			DMibExpectTrue(Result.m_bResolvedNothing);
			DMibExpect(fClientFiles(Result.m_LeftOfResolved), ==, TCVector<CStr>{});
			DMibExpect(Result.m_UntaggedError, ==, "Listing the files that need resolving needs a session with tagged output");

			co_return {};
		};
	}

	DMibTestRegister(CResolve_Tests, Malterlib::Perforce);
}
