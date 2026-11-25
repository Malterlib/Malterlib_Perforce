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
	struct CContentCharset_Tests : public NMib::NTest::CTest
	{
		void f_DoTests();
	};

	void CContentCharset_Tests::f_DoTests()
	{
		// A session in a charset that isn't UTF-8 logs in as a UTF-8 one to a Unicode server, while the files' contents stay in the
		// charset, as p4 writes them.
		DMibTestSuite("Login") -> TCFuture<void>
		{
			if (CPerforceTestServer::fs_FindServerExecutable().f_IsEmpty())
			{
				DMibConOut("{}\n", "Skipping the Perforce test of the contents' charset: p4d was not found in PATH");
				co_return {};
			}

			CStr Root = NFile::CFile::fs_GetProgramDirectory() / "TestPerforceContentCharset";
			if (NFile::CFile::fs_FileExists(Root, NFile::EFileAttrib_Directory))
				NFile::CFile::fs_DeleteDirectoryRecursive(Root, true);
			NFile::CFile::fs_CreateDirectory(Root);
			NTest::fg_TestAddCleanupPath(Root);

			TCActor<CPerforceTestServer> Server = fg_Construct<CPerforceTestServer>();
			co_await Server(&CPerforceTestServer::f_StartUnicode, Root / "Server", CStr("tester"));
			auto ServerCleanup = co_await fg_AsyncDestroy(Server);
			co_await Server(&CPerforceTestServer::f_CreateStreamDepot, CStr("Streams"));
			co_await Server(&CPerforceTestServer::f_CreateMainlineStream, CStr("//Streams/Main"));
			auto AdminConnection = co_await Server(&CPerforceTestServer::f_GetConnectionInfo, CStr());

			co_await
				(
					g_Dispatch(fg_BlockingActor())
					/ [AdminConnection]
					{
						CPerforceClientThrow Client(AdminConnection);
						Client.f_Login(CStr());
						Client.f_SetTypeMap({"unicode //Streams/Main/Accented.txt"});
					}
					% "Failed to set the type map"
				)
			;

			co_await Server(&CPerforceTestServer::f_SubmitFiles, CStr("//Streams/Main"), TCVector<CPerforceTestServer::CFile>{{"Accented.txt", "café\n"}}, CStr("Add the file"));

			CStr WorkRoot = Root / "Work";
			co_await Server(&CPerforceTestServer::f_CreateClient, CStr("TestWork"), CStr("//Streams/Main"), WorkRoot);
			auto Connection = co_await Server(&CPerforceTestServer::f_GetConnectionInfo, CStr("TestWork"));
			Connection.m_Charset = "iso8859-1";

			auto Synced = co_await
				(
					g_Dispatch(fg_BlockingActor()) / [Connection, WorkRoot]() -> CByteVector
					{
						CPerforceClientThrow Client(Connection);
						Client.f_Login(CStr());
						Client.f_Sync(CPerforceClient::fs_EscapeFileName(WorkRoot) / "...");
						return NFile::CFile::fs_ReadFile(WorkRoot / "Accented.txt");
					}
					% "Failed to sync the file"
				)
			;

			CByteVector Latin;
			for (uint8 Byte : {uint8('c'), uint8('a'), uint8('f'), uint8(0xE9)})
				Latin.f_Insert(Byte);
			CStr NewLine = CPerforceTestServer::fs_WorkspaceText("\n");
			Latin.f_Insert((uint8 const *)NewLine.f_GetStr(), NewLine.f_GetLen());

			DMibExpect(Synced, ==, Latin);

			co_return {};
		};
	}
}

DMibTestRegister(CContentCharset_Tests, Malterlib::Perforce);
