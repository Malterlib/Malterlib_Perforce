// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <Mib/Core/Core>
#include <Mib/Concurrency/AsyncDestroy>
#include <Mib/Container/Set>
#include <Mib/File/File>
#include <Mib/Test/Exception>
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
	struct CTestServer_Tests : public NMib::NTest::CTest
	{
		void f_DoTests()
		{
			DMibTestSuite("TestServer") -> TCFuture<void>
			{
				if (CPerforceTestServer::fs_FindServerExecutable().f_IsEmpty())
				{
					DMibConOut("{}\n", "Skipping the Perforce test server tests: p4d was not found in PATH");
					co_return {};
				}

				CStr Root = NFile::CFile::fs_GetProgramDirectory() / "TestPerforceTestServer";
				if (NFile::CFile::fs_FileExists(Root, NFile::EFileAttrib_Directory))
					NFile::CFile::fs_DeleteDirectoryRecursive(Root, true);
				NFile::CFile::fs_CreateDirectory(Root);
				NTest::fg_TestAddCleanupPath(Root);

				TCActor<CPerforceTestServer> Server = fg_Construct<CPerforceTestServer>();
				co_await Server(&CPerforceTestServer::f_Start, Root, CStr("tester"));
				auto ServerCleanup = co_await fg_AsyncDestroy(Server);

				CStr Port = co_await Server(&CPerforceTestServer::f_GetPort);
				DMibExpectTrue(Port.f_StartsWith("127.0.0.1:"));

				co_await Server(&CPerforceTestServer::f_CreateStreamDepot, CStr("Streams"));
				co_await Server(&CPerforceTestServer::f_CreateMainlineStream, CStr("//Streams/Main"));

				int64 FirstChangelist = co_await Server
					(
						&CPerforceTestServer::f_SubmitFiles
						, CStr("//Streams/Main")
						, TCVector<CPerforceTestServer::CFile>{{"Readme.txt", "First\n"}, {"Source/Main.cpp", "int main() {}\n"}}
						, CStr("Add the first files")
					)
				;
				int64 SecondChangelist = co_await Server
					(
						&CPerforceTestServer::f_SubmitFiles
						, CStr("//Streams/Main")
						, TCVector<CPerforceTestServer::CFile>{{"Readme.txt", "Second\n"}, {"Source/Other.cpp", "void fg_Other() {}\n"}}
						, CStr("Edit one file and add another")
					)
				;
				DMibExpect(SecondChangelist, >, FirstChangelist);

				{
					DMibTestPath("TemplateClient");

					co_await Server(&CPerforceTestServer::f_CreateTemplateClient, CStr("TestTemplate"), CStr("//Streams/Main"));
					CJsonSorted Spec = co_await Server(&CPerforceTestServer::f_GetClientSpec, CStr("TestTemplate"));
					DMibExpectTrue(Spec["Exists"].f_Boolean());
					DMibExpect(Spec["Stream"].f_String(), ==, "//Streams/Main");
					DMibExpect(Spec["Owner"].f_String(), ==, "tester");

					TCSet<CStr> Options;
					for (auto const &Option : Spec["Options"].f_Array())
						Options[Option.f_String()];
					DMibExpectTrue(Options.f_FindEqual("allwrite"));
					DMibExpectTrue(Options.f_FindEqual("clobber"));

					CJsonSorted Missing = co_await Server(&CPerforceTestServer::f_GetClientSpec, CStr("NoSuchClient"));
					DMibExpectFalse(Missing["Exists"].f_Boolean());

					// A query p4d refuses fails rather than report a missing client: "..." lists every client but names none.
					auto Refused = co_await Server(&CPerforceTestServer::f_GetClientSpec, CStr("...")).f_Wrap();
					DMibExpectFalse(Refused);
				}

				{
					DMibTestPath("Sync");

					CStr WorkRoot = Root / "Work";
					co_await Server(&CPerforceTestServer::f_CreateClient, CStr("TestWork"), CStr("//Streams/Main"), WorkRoot);

					auto BlockingActorCheckout = fg_BlockingActor();
					co_await
						(
							g_Dispatch(BlockingActorCheckout)
							/ [Port, WorkRoot]()
							{
								CPerforceClientThrow Client(Port, "tester", "TestWork");
								Client.f_NoThrow().f_Login(CStr());
								Client.f_Sync(WorkRoot / "...");
							}
							% "Failed to sync the test workspace"
						)
					;

					DMibExpect(NFile::CFile::fs_ReadStringFromFile(WorkRoot / "Readme.txt", true), ==, "Second\n");
					DMibExpect(NFile::CFile::fs_ReadStringFromFile(WorkRoot / "Source/Main.cpp", true), ==, "int main() {}\n");
					DMibExpect(NFile::CFile::fs_ReadStringFromFile(WorkRoot / "Source/Other.cpp", true), ==, "void fg_Other() {}\n");
				}

				{
					DMibTestPath("Password");

					co_await Server(&CPerforceTestServer::f_SetUserPassword, CStr("TestPass123"));
					int64 ThirdChangelist = co_await Server
						(
							&CPerforceTestServer::f_SubmitFiles
							, CStr("//Streams/Main")
							, TCVector<CPerforceTestServer::CFile>{{"Readme.txt", "Third\n"}}
							, CStr("Edit after setting a password")
						)
					;
					DMibExpect(ThirdChangelist, >, SecondChangelist);
				}

				co_return {};
			};
		}
	};

	DMibTestRegister(CTestServer_Tests, Malterlib::Perforce);
}
