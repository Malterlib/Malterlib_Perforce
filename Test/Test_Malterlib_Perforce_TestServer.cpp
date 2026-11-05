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

#ifdef DPlatformFamily_Windows
#	include <Windows.h>
#else
#	include <fcntl.h>
#	include <stdlib.h>
#	include <sys/stat.h>
#	include <unistd.h>
#endif

using namespace NMib::NPerforce;

namespace
{
	NAtomic::TCAtomic<uint32> g_AmbientRun = 0; // Numbers the runs of the ambient special files test in this process

	struct CTestServer_Tests : public NMib::NTest::CTest
	{
		void f_DoTests()
		{
			DMibTestSuite("FindServerExecutable")
			{
				DMibExpect(CPerforceTestServer::fs_FindServerExecutable("NoSuchPerforceServerExecutable"), ==, "");
			};

			// The fixture's sessions read none of the ambient settings files they override, also while a Unicode server's login sets
			// up their translation, or a session without a charset learns the server's as it connects, so one a reader blocks on, a
			// FIFO or on Windows a named pipe, doesn't stop them, and an ambient P4COMMANDCHARSET doesn't change them.
			DMibTestSuite("AmbientSpecialFiles") -> TCFuture<void>
			{
				if (CPerforceTestServer::fs_FindServerExecutable().f_IsEmpty())
				{
					DMibConOut("{}\n", "Skipping the Perforce ambient special files test: p4d was not found in PATH");
					co_return {};
				}

				// Unique for each run, also of other deployments' executables at the same time.
#ifdef DPlatformFamily_Windows
				uint64 ProcessID = GetCurrentProcessId();
#else
				uint64 ProcessID = uint64(getpid());
#endif
				CStr RunName = "{}_{}"_f << ProcessID << g_AmbientRun.f_FetchAdd(1);
				CStr Root = NFile::CFile::fs_GetProgramDirectory() / "TestPerforceAmbientSpecialFiles" / RunName;
				NFile::CFile::fs_CreateDirectory(Root);
				NTest::fg_TestAddCleanupPath(Root);

				// An absolute P4CONFIG is that file in every directory p4 looks in.
				TCVector<CStr> Readers;
#ifdef DPlatformFamily_Windows
				CStr AmbientConfig = "\\\\.\\pipe\\MalterlibPerforceAmbient_{}_Config"_f << RunName;
				CStr AmbientEnviro = "\\\\.\\pipe\\MalterlibPerforceAmbient_{}_Enviro"_f << RunName;
				TCVector<HANDLE> Pipes;
				auto ClosePipes = g_OnScopeExit / [&]
					{
						for (HANDLE pPipe : Pipes)
							CloseHandle(pPipe);
					}
				;
				bool bCreated = true;
				for (auto &Name : {AmbientConfig, AmbientEnviro})
				{
					HANDLE pPipe = CreateNamedPipeA(Name.f_GetStr(), PIPE_ACCESS_OUTBOUND, PIPE_TYPE_BYTE | PIPE_NOWAIT, 1, 0, 0, 0, nullptr);
					if (pPipe == INVALID_HANDLE_VALUE)
						bCreated = false;
					else
						Pipes.f_Insert(pPipe);
				}
				DMibAssertTrue(bCreated);
#else
				CStr AmbientConfig = Root / "Ambient.p4config";
				CStr AmbientEnviro = Root / "Ambient.p4enviro";
				DMibAssert(mkfifo(AmbientConfig.f_GetStr(), 0600), ==, 0);
				DMibAssert(mkfifo(AmbientEnviro.f_GetStr(), 0600), ==, 0);
#endif

				// A reader waiting on one is recorded and let go: a FIFO's reader gets the end of the file when a writer opens and
				// closes it, and a pipe's when it is disconnected.
				NAtomic::TCAtomic<bool> bDone = false;
				auto pWatchdog = NThread::CThreadObject::fs_StartThread
					(
						[&](NThread::CThreadObject *_pThread) -> aint
						{
							while (!bDone.f_Load())
							{
#ifdef DPlatformFamily_Windows
								for (umint iPipe = 0; iPipe < Pipes.f_GetLen(); ++iPipe)
								{
									if (!ConnectNamedPipe(Pipes[iPipe], nullptr) && GetLastError() == ERROR_PIPE_CONNECTED)
									{
										Readers.f_Insert(iPipe == 0 ? AmbientConfig : AmbientEnviro);
										DisconnectNamedPipe(Pipes[iPipe]);
									}
								}
#else
								for (auto &Fifo : {AmbientConfig, AmbientEnviro})
								{
									int File = open(Fifo.f_GetStr(), O_WRONLY | O_NONBLOCK | O_CLOEXEC);
									if (File >= 0)
									{
										Readers.f_Insert(Fifo);
										close(File);
									}
								}
#endif
								NSys::fg_Thread_Sleep(0.05f);
							}
							return 0;
						}
						, "Perforce ambient special files watchdog"
					)
				;
				auto fStopWatchdog = [&]
					{
						if (!bDone.f_Exchange(true))
							pWatchdog->f_Stop(true);
					}
				;

				// The API reads the process's environment itself, not Malterlib's copy of it.
				TCVector<TCTuple<CStr, TCOptional<CStr>>> Previous;
				bool bSet = true;
				// An ambient P4COMMANDCHARSET p4 can't use would stop the sessions, and without P4CHARSET a session learns the server's.
				for
					(
						auto &[Name, Value]
						: TCVector<TCTuple<CStr, TCOptional<CStr>>>
						{
							{"P4CONFIG", AmbientConfig}
							, {"P4ENVIRO", AmbientEnviro}
							, {"P4COMMANDCHARSET", CStr("nosuchcharset")}
							, {"P4CHARSET", {}}
						}
					)
				{
#ifdef DPlatformFamily_Windows
					char PreviousValue[32768];
					DWORD Length = GetEnvironmentVariableA(Name.f_GetStr(), PreviousValue, sizeof(PreviousValue));
					Previous.f_Insert({Name, Length > 0 && Length < sizeof(PreviousValue) ? TCOptional<CStr>(CStr(PreviousValue)) : TCOptional<CStr>()});
					bSet = SetEnvironmentVariableA(Name.f_GetStr(), Value ? Value->f_GetStr() : nullptr) && bSet;
#else
					char const *pPrevious = getenv(Name.f_GetStr());
					Previous.f_Insert({Name, pPrevious ? TCOptional<CStr>(CStr(pPrevious)) : TCOptional<CStr>()});
					bSet = (Value ? setenv(Name.f_GetStr(), Value->f_GetStr(), 1) : unsetenv(Name.f_GetStr())) == 0 && bSet;
#endif
				}
				auto Restore = g_OnScopeExit / [&]
					{
						for (auto &[Name, Value] : Previous)
						{
#ifdef DPlatformFamily_Windows
							SetEnvironmentVariableA(Name.f_GetStr(), Value ? Value->f_GetStr() : nullptr);
#else
							if (Value)
								setenv(Name.f_GetStr(), Value->f_GetStr(), 1);
							else
								unsetenv(Name.f_GetStr());
#endif
						}
						fStopWatchdog();
					}
				;
				DMibAssertTrue(bSet);

				TCActor<CPerforceTestServer> Server = fg_Construct<CPerforceTestServer>();
				co_await Server(&CPerforceTestServer::f_StartUnicode, Root, CStr("tester"));
				auto ServerCleanup = co_await fg_AsyncDestroy(Server);

				auto Connection = co_await Server(&CPerforceTestServer::f_GetConnectionInfo, CStr());
				auto BlockingActorCheckout = fg_BlockingActor();
				CStr UserName = co_await
					(
						g_Dispatch(BlockingActorCheckout) / [Connection]() -> CStr
						{
							CPerforceClientThrow Client(Connection);
							Client.f_Login(CStr());
							Client.f_NoThrow().f_Info();
							return Client.f_NoThrow().f_GetLastInfo("userName");
						}
						% "Failed to query the fixture"
					)
				;

				// Without a charset the session learns the Unicode server's as it connects (Init's LearnUnicode stores "auto" in the
				// enviro file), and the translation that sets up reads the settings again, keeping the connection's P4CONFIG.
				auto LearningConnection = Connection;
				LearningConnection.m_Charset = "";
				CStr LearningUserName = co_await
					(
						g_Dispatch(BlockingActorCheckout) / [LearningConnection]() -> CStr
						{
							CPerforceClientThrow Client(LearningConnection);
							Client.f_Login(CStr());
							Client.f_NoThrow().f_Info();
							return Client.f_NoThrow().f_GetLastInfo("userName");
						}
						% "Failed to query the fixture without a charset"
					)
				;
				CStr LearnedEnviro = NFile::CFile::fs_ReadStringFromFile(Connection.m_EnviroFile, true);

				fStopWatchdog();
				DMibExpect(Readers, ==, TCVector<CStr>{});
				DMibExpect(UserName, ==, "tester");
				DMibExpect(LearningUserName, ==, "tester");
				DMibExpectTrue(LearnedEnviro.f_Find("_CHARSET=auto") >= 0);

				co_return {};
			};

			// A UTF-8 session that falls back to a server that isn't Unicode reads the identity the connection leaves to the settings
			// again in the code page, as an untranslated session reads it: here the environment's client, whose UTF-8 spelling would
			// name another client.
			DMibTestSuite("CharsetFallbackIdentity") -> TCFuture<void>
			{
#ifndef DPlatformFamily_Windows
				DMibConOut("{}\n", "Skipping the Perforce charset fallback identity test: outside Windows the settings' bytes are the same in every charset");
				co_return {};
#else
				if (CPerforceTestServer::fs_FindServerExecutable().f_IsEmpty())
				{
					DMibConOut("{}\n", "Skipping the Perforce charset fallback identity test: p4d was not found in PATH");
					co_return {};
				}
				if (GetACP() != 1252)
				{
					DMibConOut("Skipping the Perforce charset fallback identity test: the active code page is {}, not 1252\n", GetACP());
					co_return {};
				}

				CStr RunName = "{}_{}"_f << uint64(GetCurrentProcessId()) << g_AmbientRun.f_FetchAdd(1);
				CStr Root = NFile::CFile::fs_GetProgramDirectory() / "TestPerforceCharsetFallbackIdentity" / RunName;
				NFile::CFile::fs_CreateDirectory(Root);
				NTest::fg_TestAddCleanupPath(Root);

				TCActor<CPerforceTestServer> Server = fg_Construct<CPerforceTestServer>();
				co_await Server(&CPerforceTestServer::f_Start, Root, CStr("tester"));
				auto ServerCleanup = co_await fg_AsyncDestroy(Server);
				co_await Server(&CPerforceTestServer::f_CreateStreamDepot, CStr("Streams"));
				co_await Server(&CPerforceTestServer::f_CreateMainlineStream, CStr("//Streams/Main"));
				CStr ClientName = "T\u00e9st";
				co_await Server(&CPerforceTestServer::f_CreateClient, ClientName, CStr("//Streams/Main"), CStr(Root / "Work"));

				// The fixture's untranslated sessions find the client by its name in the code page, as they created it.
				CJsonSorted Spec = co_await Server(&CPerforceTestServer::f_GetClientSpec, ClientName);
				DMibAssertTrue(Spec["Exists"].f_Boolean());
				DMibExpect(Spec["Stream"].f_String(), ==, "//Streams/Main");

				auto Connection = co_await Server(&CPerforceTestServer::f_GetConnectionInfo, CStr());
				Connection.m_Client = "";
				Connection.m_Charset = "utf8";

				wchar_t Previous[32768];
				DWORD nPrevious = GetEnvironmentVariableW(L"P4CLIENT", Previous, 32768);
				bool bHadPrevious = nPrevious > 0 && nPrevious < 32768;
				SetEnvironmentVariableW(L"P4CLIENT", L"T\u00e9st");
				auto Restore = g_OnScopeExit / [&]
					{
						SetEnvironmentVariableW(L"P4CLIENT", bHadPrevious ? Previous : nullptr);
					}
				;

				auto BlockingActorCheckout = fg_BlockingActor();
				CStr Client = co_await
					(
						g_Dispatch(BlockingActorCheckout) / [Connection]() -> CStr
						{
							CPerforceClientThrow Client(Connection);
							Client.f_Login(CStr());
							Client.f_NoThrow().f_Info();
							return Client.f_NoThrow().f_GetLastInfo("clientName");
						}
						% "Failed to query the fixture"
					)
				;
				DMibExpect(Client, ==, ClientName);

				co_return {};
#endif
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

					// A query p4d refuses fails rather than report a missing client: "..." lists every client but names none.
					auto Refused = co_await Server(&CPerforceTestServer::f_GetClientSpec, CStr("...")).f_Wrap();
					DMibExpectFalse(Refused);
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

				{
					// Ambient settings, here from the enviro file and the P4CONFIG file it names, don't change who the fixture's
					// sessions are. The ambient clients exist, so a session using one would report it; the fixture's own client of
					// a session without one doesn't, which p4 info reports as unknown.
					DMibTestPath("AmbientIdentity");
					co_await Server(&CPerforceTestServer::f_CreateClient, CStr("AmbientClient"), CStr("//Streams/Main"), CStr(Root / "AmbientClient"));
					co_await Server(&CPerforceTestServer::f_CreateClient, CStr("ConfigClient"), CStr("//Streams/Main"), CStr(Root / "ConfigClient"));
					NFile::CFile::fs_WriteStringToFile
						(
							Root / "p4enviro"
							, "P4CLIENT=AmbientClient\nP4HOST=AmbientHost\nP4USER=AmbientUser\nP4PASSWD=AmbientPassword\nP4PORT=127.0.0.1:1\nP4CONFIG=Ambient.p4config\n"
							, false
						)
					;
					NFile::CFile::fs_WriteStringToFile(Root / "Ambient.p4config", "P4CLIENT=ConfigClient\nP4USER=ConfigUser\nP4PORT=127.0.0.1:1\n", false);
					int64 Changelist = co_await Server
						(
							&CPerforceTestServer::f_SubmitFiles
							, CStr("//Streams/Main")
							, TCVector<CPerforceTestServer::CFile>{{"Readme.txt", "Ambient identity\n"}}
							, CStr("Edit with ambient settings")
						)
					;
					DMibExpect(Changelist, >, 0);

					auto Connection = co_await Server(&CPerforceTestServer::f_GetConnectionInfo, CStr());
					auto BlockingActorCheckout = fg_BlockingActor();
					TCVector<CStr> Identity = co_await
						(
							g_Dispatch(BlockingActorCheckout) / [Connection]() -> TCVector<CStr>
							{
								CPerforceClientThrow Client(Connection);
								Client.f_Login(CStr());
								Client.f_NoThrow().f_Info();
								auto &NoThrow = Client.f_NoThrow();

								return {NoThrow.f_GetLastInfo("userName"), NoThrow.f_GetLastInfo("clientName"), NoThrow.f_GetLastInfo("clientHost")};
							}
							% "Failed to query the fixture session"
						)
					;
					DMibExpect(Identity, ==, (TCVector<CStr>{"tester", "*unknown*", "PerforceTestServer"}));
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
