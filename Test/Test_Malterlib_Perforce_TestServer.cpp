// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <Mib/Core/Core>
#include <Mib/Concurrency/AsyncDestroy>
#include <Mib/Container/Set>
#include <Mib/File/File>
#include <Mib/Test/Exception>
#include <Mib/Network/Socket>
#include <Mib/Network/Sockets/TCP>
#include <Mib/Test/Test>
#include <Mib/Time/Stopwatch>

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
			DMibTestSuite("FindServerExecutable")
			{
				DMibExpect(CPerforceTestServer::fs_FindServerExecutable("NoSuchPerforceServerExecutable"), ==, "");
			};

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
				// Fixture submits don't follow ignore files above the workspaces.
				NFile::CFile::fs_WriteStringToFile(Root / ".p4ignore", "*.txt\n*.cpp\n", false);

				// Ambient journal and log settings don't reach p4d.
				CStr PreviousJournal = fg_GetSys()->f_GetEnvironmentVariable("P4JOURNAL");
				CStr PreviousLog = fg_GetSys()->f_GetEnvironmentVariable("P4LOG");
				fg_GetSys()->f_SetEnvironmentVariable("P4JOURNAL", Root / "Elsewhere/journal");
				fg_GetSys()->f_SetEnvironmentVariable("P4LOG", Root / "Elsewhere/log");

				TCActor<CPerforceTestServer> Server = fg_Construct<CPerforceTestServer>();
				auto StartResult = co_await Server(&CPerforceTestServer::f_Start, Root, CStr("tester")).f_Wrap();
				fg_GetSys()->f_SetEnvironmentVariable("P4JOURNAL", PreviousJournal);
				fg_GetSys()->f_SetEnvironmentVariable("P4LOG", PreviousLog);
				if (!StartResult)
					co_return StartResult.f_GetException();
				auto ServerCleanup = co_await fg_AsyncDestroy(Server);

				DMibExpectFalse(NFile::CFile::fs_FileExists(Root / "Elsewhere/journal"));
				DMibExpectFalse(NFile::CFile::fs_FileExists(Root / "Elsewhere/log"));
				DMibExpectTrue(NFile::CFile::fs_FileExists(Root / "p4d/journal"));

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
				}

				{
					DMibTestPath("ClientRootWithSpaces");
					CStr ClientRoot = Root / "Work  Space";
					co_await Server(&CPerforceTestServer::f_CreateClient, CStr("TestSpaces"), CStr("//Streams/Main"), ClientRoot);
					CJsonSorted Spec = co_await Server(&CPerforceTestServer::f_GetClientSpec, CStr("TestSpaces"));
					DMibExpect(Spec["Root"].f_String(), ==, ClientRoot);

					auto Pound = co_await Server(&CPerforceTestServer::f_CreateClient, CStr("TestPound"), CStr("//Streams/Main"), CStr(Root / "Work#Pound")).f_Wrap();
					DMibExpectFalse(Pound);
				}

				{
					DMibTestPath("Sync");

					CStr WorkRoot = Root / "Work";
					co_await Server(&CPerforceTestServer::f_CreateClient, CStr("TestWork"), CStr("//Streams/Main"), WorkRoot);
					auto Connection = co_await Server(&CPerforceTestServer::f_GetConnectionInfo, CStr("TestWork"));
					DMibExpect(Connection.m_Server, ==, Port);

					auto BlockingActorCheckout = fg_BlockingActor();
					CStr ServerID = co_await
						(
							g_Dispatch(BlockingActorCheckout) / [Connection, WorkRoot]() -> CStr
							{
								CPerforceClientThrow Client(Connection);
								Client.f_NoThrow().f_Login(CStr());
								Client.f_Sync(WorkRoot / "...");
								Client.f_NoThrow().f_Info();

								return Client.f_NoThrow().f_GetLastInfo("ServerID");
							}
							% "Failed to sync the test workspace"
						)
					;

					DMibExpectTrue(ServerID.f_StartsWith("Fixture"));

					DMibExpect(NFile::CFile::fs_ReadStringFromFile(WorkRoot / "Readme.txt", true), ==, "Second\n");
					DMibExpect(NFile::CFile::fs_ReadStringFromFile(WorkRoot / "Source/Main.cpp", true), ==, "int main() {}\n");
					DMibExpect(NFile::CFile::fs_ReadStringFromFile(WorkRoot / "Source/Other.cpp", true), ==, "void fg_Other() {}\n");
				}

				{
					// Each overlapping submit reports its own changelist, not the stream's head.
					DMibTestPath("ConcurrentSubmits");
					CStr Stream = "//Streams/Main";
					TCVector<CPerforceTestServer::CFile> FirstFiles = {{"First.txt", "First\n"}};
					TCVector<CPerforceTestServer::CFile> SecondFiles = {{"Second.txt", "Second\n"}};
					CStr FirstDescription = "The first of two overlapping submits";
					CStr SecondDescription = "The second of two overlapping submits";
					auto First = Server(&CPerforceTestServer::f_SubmitFiles, Stream, FirstFiles, FirstDescription);
					auto Second = Server(&CPerforceTestServer::f_SubmitFiles, Stream, SecondFiles, SecondDescription);
					int64 FirstOverlapping = co_await fg_Move(First);
					int64 SecondOverlapping = co_await fg_Move(Second);
					DMibExpect(FirstOverlapping, >, SecondChangelist);
					DMibExpect(SecondOverlapping, >, SecondChangelist);
					DMibExpect(FirstOverlapping, !=, SecondOverlapping);
				}

				{
					// The fixture's server isn't a Unicode one, and refuses a UTF-8 session beyond info.
					DMibTestPath("Charset");
					auto Connection = co_await Server(&CPerforceTestServer::f_GetConnectionInfo, CStr());
					DMibExpect(Connection.m_Charset, ==, "none");
					auto UnicodeConnection = Connection;
					UnicodeConnection.m_Charset = "utf8";

					auto BlockingActorCheckout = fg_BlockingActor();
					auto [bStreams, bUnicodeStreams, bLoggedInStreams] = co_await
						(
							g_Dispatch(BlockingActorCheckout) / [Connection, UnicodeConnection]() -> TCTuple<bool, bool, bool>
							{
								CPerforceClient Client(Connection);
								CPerforceClient UnicodeClient(UnicodeConnection);
								TCVector<CStr> Streams;
								TCVector<CStr> UnicodeStreams;
								bool bUnicode = UnicodeClient.f_GetStreams(UnicodeStreams);

								// A login turns a Unicode session back when the server is not a Unicode one.
								CPerforceClient LoggedInClient(UnicodeConnection);
								TCVector<CStr> LoggedInStreams;
								bool bLoggedIn = LoggedInClient.f_Login(CStr()) && LoggedInClient.f_GetStreams(LoggedInStreams);

								return {Client.f_GetStreams(Streams), bUnicode, bLoggedIn};
							}
						)
					;
					DMibExpectTrue(bStreams);
					DMibExpectFalse(bUnicodeStreams);
					DMibExpectTrue(bLoggedInStreams);
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

					DMibExpectTrue(NFile::CFile::fs_FileExists(Root / "p4tickets", NFile::EFileAttrib_File));
					DMibExpect(NFile::CFile::fs_ReadStringFromFile(Root / "p4tickets", true).f_Find("=tester:"), >=, 0);
				}

				{
					DMibTestPath("ChangedPassword");
					co_await Server(&CPerforceTestServer::f_SetUserPassword, CStr("TestPass456"));
					int64 Changelist = co_await Server
						(
							&CPerforceTestServer::f_SubmitFiles
							, CStr("//Streams/Main")
							, TCVector<CPerforceTestServer::CFile>{{"Readme.txt", "Changed password\n"}}
							, CStr("Edit after changing the password")
						)
					;
					DMibExpect(Changelist, >, 0);
				}

				{
					DMibTestPath("AmbientClientPath");
					NFile::CFile::fs_WriteStringToFile(Root / "p4enviro", "P4CLIENTPATH={}\n"_f << (Root / "Nowhere"), false);
					int64 FourthChangelist = co_await Server
						(
							&CPerforceTestServer::f_SubmitFiles
							, CStr("//Streams/Main")
							, TCVector<CPerforceTestServer::CFile>{{"Readme.txt", "Fourth\n"}}
							, CStr("Edit outside P4CLIENTPATH")
						)
					;
					DMibExpect(FourthChangelist, >, 0);
				}

				co_return {};
			};
		}
	};

	struct CConnectionTimeout_Tests : public NMib::NTest::CTest
	{
		void f_DoTests()
		{
			// A port that accepts connections but never answers.
			DMibTestSuite("ConnectionTimeout") -> TCFuture<void>
			{
				auto pListen = NNetwork::CSocket_TCP::fs_GetFactory()("");
				NNetwork::CNetAddressTCPv4 ListenAddress;
				ListenAddress.f_SetLocalhost();
				ListenAddress.m_Port = 0;
				pListen->f_Listen
					(
						ListenAddress
						, [](NNetwork::ENetTCPState _StateAdded)
						{
						}
						, NNetwork::ENetFlag_None
					)
				;

				CPerforceClient::CConnectionInfo Connection;
				Connection.m_Server = "127.0.0.1:{}"_f << pListen->f_GetListenPort();
				Connection.m_User = "tester";
				Connection.m_Charset = "none";
				Connection.m_Timeout = 1.0;
				CStr Root = NFile::CFile::fs_GetProgramDirectory() / "TestPerforceConnectionTimeout";
				if (NFile::CFile::fs_FileExists(Root, NFile::EFileAttrib_Directory))
					NFile::CFile::fs_DeleteDirectoryRecursive(Root, true);
				NFile::CFile::fs_CreateDirectory(Root);
				NTest::fg_TestAddCleanupPath(Root);
				Connection.m_WorkingDirectory = Root;
				Connection.m_TicketFile = Root / "p4tickets";
				Connection.m_EnviroFile = Root / "p4enviro";

				auto BlockingActorCheckout = fg_BlockingActor();
				auto [bInfo, Error, Seconds] = co_await
					(
						g_Dispatch(BlockingActorCheckout) / [Connection]() -> TCTuple<bool, CStr, fp64>
						{
							NTime::CStopwatch Stopwatch{true};
							CPerforceClient Client(Connection);
							bool bInfo = Client.f_Info();
							return {bInfo, Client.f_GetLastError(), Stopwatch.f_GetTime()};
						}
					)
				;
				pListen->f_Close();

				DMibExpectFalse(bInfo);
				DMibExpect(Error.f_Find("The Perforce server did not answer within 1"), ==, 0);
				DMibExpect(Seconds, <, 30.0);

				co_return {};
			};
		}
	};

	DMibTestRegister(CTestServer_Tests, Malterlib::Perforce);
	DMibTestRegister(CConnectionTimeout_Tests, Malterlib::Perforce);
}
