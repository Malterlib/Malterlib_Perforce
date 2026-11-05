// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "Malterlib_Perforce_TestServer.h"
#include "Malterlib_Perforce_Wrapper.h"

#include <Mib/Cryptography/UUID>
#include <Mib/File/File>
#include <Mib/Process/Platform>

namespace NMib::NPerforce
{
	using namespace NMib::NConcurrency;
	using namespace NMib::NEncoding;

	namespace
	{
		// The build-friendly client options of every fixture workspace and of the template.
		TCVector<CStr> fg_FixtureClientOptions()
		{
			return {"allwrite", "clobber", "nocompress", "unlocked", "nomodtime", "rmdir"};
		}

		// Connects a fixture session in a blocking context, logging in when the fixture has set a password. Without one the
		// login still runs: it applies the configured user and client to the session, so the ambient P4 environment never
		// leaks into fixture commands, and its failure on the open server (an empty protections table fails `p4 protects`)
		// is expected. `info` runs either way so the wrapper's feature detection (stream ParentView) applies.
		void fg_FixtureConnect(CPerforceClientThrow &o_Client, CStr const &_Password)
		{
			if (!_Password.f_IsEmpty())
				o_Client.f_Login(_Password);
			else
				o_Client.f_NoThrow().f_Login(CStr());

			o_Client.f_NoThrow().f_Info();
		}

		CPerforceClient::CConnectionInfo fg_FixtureConnection(CStr const &_Port, CStr const &_User, CStr const &_Client = {})
		{
			CPerforceClient::CConnectionInfo Info;
			Info.m_Server = _Port;
			Info.m_User = _User;
			Info.m_Client = _Client;
			return Info;
		}
	}

	CStr CPerforceTestServer::fs_FindServerExecutable()
	{
		return NProcess::NPlatform::fg_FindExecutable("p4d");
	}

	TCFuture<void> CPerforceTestServer::f_Start(CStr _Root, CStr _User)
	{
		auto CheckDestroy = co_await f_CheckDestroyedOnResume();

		mp_ServerExecutable = fs_FindServerExecutable();
		if (mp_ServerExecutable.f_IsEmpty())
			co_return DMibErrorInstance("p4d was not found in PATH");

		mp_FixtureRoot = _Root;
		mp_ServerRoot = _Root / "p4d";
		mp_User = _User;
		NFile::CFile::fs_CreateDirectory(mp_ServerRoot);

		// A random base port keeps parallel test processes off each other's servers. A busy port shows up as the server
		// exiting or never answering, and the next candidate is tried.
		CStr Unique = NCryptography::CUniversallyUniqueIdentifier(NCryptography::EUniversallyUniqueIdentifierGenerate_Random)
			.f_GetAsString(NCryptography::EUniversallyUniqueIdentifierFormat_AlphaNum)
		;
		uint32 Hash = fg_StrHashDJB2(Unique.f_GetStr());

		bool bStarted = false;
		for (umint iAttempt = 0; iAttempt < 20 && !bStarted; ++iAttempt)
		{
			mp_Port = "127.0.0.1:{}"_f << (20000 + ((Hash + uint32(iAttempt) * 733) % 40000));
			bStarted = co_await fp_LaunchServer(mp_Port);
		}

		if (!bStarted)
			co_return DMibErrorInstance("Failed to start a p4d test server");

		// The first start let the new p4d initialize its database, which stamps it secure by default (security level 4,
		// no user auto-creation). Stop it, relax that on the initialized database and start it again, so the classic open
		// server behavior the fixtures rely on applies.
		co_await fp_StopServer();

		// The -c flag of p4d runs one configuration command against the database ("set <configurable>=<value>").
		for (CStr const &Configurable : {CStr("security=0"), CStr("dm.user.noautocreate=0"), CStr("run.users.authorize=0")})
		{
			co_await NProcess::CProcessLaunchActor::fs_LaunchSimple
				(
					NProcess::CProcessLaunchActor::CSimpleLaunch
					(
						mp_ServerExecutable
						, {"-r", mp_ServerRoot, "-c", "set {}"_f << Configurable}
						, mp_ServerRoot
						, NProcess::CProcessLaunchActor::ESimpleLaunchFlag_GenerateExceptionOnNonZeroExitCode
					)
				)
			;
		}

		if (!co_await fp_LaunchServer(mp_Port))
			co_return DMibErrorInstance("Failed to restart the p4d test server after configuration");

		co_return {};
	}

	TCFuture<CStr> CPerforceTestServer::f_GetPort()
	{
		co_return mp_Port;
	}

	TCFuture<bool> CPerforceTestServer::fp_LaunchServer(CStr _Port)
	{
		auto CheckDestroy = co_await f_CheckDestroyedOnResume();

		NProcess::CProcessLaunchParams Params;
		Params.m_Target = mp_ServerExecutable;
		Params.m_Parameters = NProcess::CProcessLaunchParams::fs_GetParams({"-r", mp_ServerRoot, "-p", _Port});
		Params.m_WorkingDirectory = mp_ServerRoot;

		TCPromise<void> ExitPromise;
		mp_ExitPromise = ExitPromise;

		Params.m_fOnStateChange = [ExitPromise](NProcess::CProcessLaunchStateChangeVariant const &_State, fp64 _TimeSinceLaunch)
			{
				switch (_State.f_GetTypeID())
				{
				case NProcess::EProcessLaunchState_Launched: return;
				case NProcess::EProcessLaunchState_Exited:
				case NProcess::EProcessLaunchState_LaunchFailed:
					break;
				}

				if (!ExitPromise.f_IsSet())
					ExitPromise.f_SetResult();
			}
		;

		mp_LaunchActor = fg_Construct();

		NProcess::CProcessLaunchActor::CLaunch LaunchConfig(Params);
		mp_LaunchSubscription = co_await mp_LaunchActor(&NProcess::CProcessLaunchActor::f_Launch, fg_Move(LaunchConfig), fg_ThisActor(this));

		// Wait until the server answers `p4 info` (each probe is a new blocking API session), it exits (a busy port), or
		// the timeout runs out.
		CStr Port = _Port;
		CStr User = mp_User;
		for (umint iProbe = 0; iProbe < 100; ++iProbe)
		{
			if (ExitPromise.f_IsSet())
				break;

			auto BlockingActorCheckout = fg_BlockingActor();
			bool bReady = co_await
				(
					g_Dispatch(BlockingActorCheckout) / [Port, User]() -> bool
					{
						CPerforceClient Client(fg_FixtureConnection(Port, User));
						return Client.f_Info();
					}
				)
			;
			if (bReady)
				co_return true;

			co_await fg_Timeout(0.1);
		}

		co_await fp_StopServer();

		co_return false;
	}

	TCFuture<void> CPerforceTestServer::fp_StopServer()
	{
		// No destroy guard: this also runs from fp_Destroy, where a guard would cancel the teardown itself.
		co_await fg_DestroySubscription(mp_LaunchSubscription);

		// Destroying the subscription kills the process tree. Wait briefly for the exit notification so the database is
		// settled before a restart, but never hang on it.
		if (mp_ExitPromise)
		{
			auto ExitResult = co_await mp_ExitPromise->f_Future().f_Timeout(10.0, "p4d did not exit").f_Wrap();
			(void)ExitResult;
			mp_ExitPromise.f_Clear();
		}

		if (mp_LaunchActor)
			co_await fg_Move(mp_LaunchActor).f_Destroy();

		co_return {};
	}

	TCFuture<void> CPerforceTestServer::fp_Destroy()
	{
		co_await fp_StopServer();

		co_return {};
	}

	TCFuture<void> CPerforceTestServer::f_CreateStreamDepot(CStr _Depot)
	{
		auto CheckDestroy = co_await f_CheckDestroyedOnResume();

		CStr Port = mp_Port;
		CStr User = mp_User;
		CStr Password = mp_Password;

		auto BlockingActorCheckout = fg_BlockingActor();
		co_await
			(
				g_Dispatch(BlockingActorCheckout)
				/ [Port, User, Password, Depot = _Depot]()
				{
					CPerforceClientThrow Client(fg_FixtureConnection(Port, User));
					fg_FixtureConnect(Client, Password);
					Client.f_CreateDepot(Depot, "stream");
				}
				% "Failed to create the fixture stream depot"
			)
		;

		co_return {};
	}

	TCFuture<void> CPerforceTestServer::f_CreateMainlineStream(CStr _Stream)
	{
		auto CheckDestroy = co_await f_CheckDestroyedOnResume();

		CStr Port = mp_Port;
		CStr User = mp_User;
		CStr Password = mp_Password;

		auto BlockingActorCheckout = fg_BlockingActor();
		co_await
			(
				g_Dispatch(BlockingActorCheckout)
				/ [Port, User, Password, Stream = _Stream]()
				{
					CPerforceClientThrow Client(fg_FixtureConnection(Port, User));
					fg_FixtureConnect(Client, Password);

					CStr Name = Stream;
					aint iSlash = Name.f_FindReverse("/");
					if (iSlash >= 0)
						Name = Name.f_Extract(iSlash + 1);

					CPerforceClient::CStream Spec;
					Spec.m_Name = Name;
					Spec.m_Parent = "none";
					Spec.m_Type = "mainline";
					Spec.m_Description = "Test stream.";
					Spec.m_Options = {"allsubmit", "unlocked", "toparent", "fromparent", "mergedown"};
					Spec.m_Paths = {"share ..."};
					Spec.m_bInheritParentView = true;

					Client.f_SetStream(Stream, Spec);
				}
				% "Failed to create the fixture stream"
			)
		;

		co_return {};
	}

	TCFuture<void> CPerforceTestServer::f_CreateTemplateClient(CStr _Name, CStr _Stream)
	{
		co_return co_await f_CreateClient(_Name, _Stream, mp_FixtureRoot / "TemplateRoot");
	}

	TCFuture<void> CPerforceTestServer::f_CreateClient(CStr _Name, CStr _Stream, CStr _Root)
	{
		auto CheckDestroy = co_await f_CheckDestroyedOnResume();

		CStr Port = mp_Port;
		CStr User = mp_User;
		CStr Password = mp_Password;

		auto BlockingActorCheckout = fg_BlockingActor();
		co_await
			(
				g_Dispatch(BlockingActorCheckout)
				/ [Port, User, Password, Name = _Name, Stream = _Stream, Root = _Root]()
				{
					CPerforceClientThrow Client(fg_FixtureConnection(Port, User));
					fg_FixtureConnect(Client, Password);

					CPerforceClient::CClient Spec;
					Spec.m_Root = Root;
					Spec.m_Description = "Perforce test client.";
					Spec.m_Options = fg_FixtureClientOptions();
					Spec.m_Stream = Stream;
					Client.f_SetClient(Name, Spec);
				}
				% "Failed to create the fixture client"
			)
		;

		co_return {};
	}

	TCFuture<int64> CPerforceTestServer::f_SubmitFiles(CStr _Stream, TCVector<CFile> _Files, CStr _Description)
	{
		auto CheckDestroy = co_await f_CheckDestroyedOnResume();

		umint iWorkspace = ++mp_nSubmitWorkspaces;
		CStr WorkRoot = mp_FixtureRoot / ("Submit{}"_f << iWorkspace);
		CStr ClientName = "PerforceTestServer_{}_{}"_f << fg_StrHashDJB2(mp_Port.f_GetStr()) << iWorkspace;

		CStr Port = mp_Port;
		CStr User = mp_User;
		CStr Password = mp_Password;

		auto BlockingActorCheckout = fg_BlockingActor();
		co_return co_await
			(
				g_Dispatch(BlockingActorCheckout) / [Port, User, Password, Stream = _Stream, Files = _Files, Description = _Description, WorkRoot, ClientName]() -> int64
				{
					CPerforceClientThrow Client(fg_FixtureConnection(Port, User, ClientName));
					fg_FixtureConnect(Client, Password);

					CPerforceClient::CClient WorkspaceSpec;
					WorkspaceSpec.m_Root = WorkRoot;
					WorkspaceSpec.m_Description = "Perforce test submit workspace.";
					WorkspaceSpec.m_Options = fg_FixtureClientOptions();
					WorkspaceSpec.m_Stream = Stream;
					Client.f_SetClient(ClientName, WorkspaceSpec);

					// Bring the new workspace to head so edits of existing files start from their depot contents. An empty
					// stream syncs nothing, which fails harmlessly.
					Client.f_NoThrow().f_Sync("//...");

					for (auto const &File : Files)
					{
						CStr Local = WorkRoot / File.m_Path;
						NFile::CFile::fs_CreateDirectoryForFile(Local);
						NFile::CFile::fs_WriteStringToFile(Local, File.m_Contents, false);

						CStr DepotPath = "{}/{}"_f << Stream << File.m_Path;
						if (Client.f_NoThrow().f_FileExistsInDepotNotDeleted(DepotPath))
							Client.f_OpenForEdit(Local);
						else
							Client.f_Add(Local);
					}

					Client.f_Submit(WorkRoot / "...", Description);

					return Client.f_GetHeadChangelist("{}/..."_f << Stream);
				}
				% "Failed to submit fixture files"
			)
		;
	}

	TCFuture<void> CPerforceTestServer::f_SetUserPassword(CStr _Password)
	{
		auto CheckDestroy = co_await f_CheckDestroyedOnResume();

		CStr Port = mp_Port;
		CStr User = mp_User;

		auto BlockingActorCheckout = fg_BlockingActor();
		co_await
			(
				g_Dispatch(BlockingActorCheckout)
				/ [Port, User, Password = _Password]()
				{
					CPerforceClientThrow Client(fg_FixtureConnection(Port, User));
					Client.f_NoThrow().f_Login(CStr());
					Client.f_NoThrow().f_Info();

					// A locked server needs a protections table: the wrapper's login runs `p4 protects` after
					// authenticating, and an empty table fails it. Grant the fixture user everything, like a real server's
					// superuser.
					Client.f_SetProtections({CStr("super user {} * //..."_f << User)});
					Client.f_SetUserPassword(User, Password);
				}
				% "Failed to set the fixture user password"
			)
		;

		// Later fixture sessions log in with it.
		mp_Password = _Password;

		co_return {};
	}

	TCFuture<CJsonSorted> CPerforceTestServer::f_GetClientSpec(CStr _ClientName)
	{
		auto CheckDestroy = co_await f_CheckDestroyedOnResume();

		CStr Port = mp_Port;
		CStr User = mp_User;
		CStr Password = mp_Password;

		auto BlockingActorCheckout = fg_BlockingActor();
		co_return co_await
			(
				g_Dispatch(BlockingActorCheckout) / [Port, User, Password, ClientName = _ClientName]() -> CJsonSorted
				{
					CPerforceClientThrow Client(fg_FixtureConnection(Port, User));
					fg_FixtureConnect(Client, Password);

					CJsonSorted Result(EJsonType_Object);

					CPerforceClient::CClient Spec;
					bool bExists = Client.f_NoThrow().f_GetClient(ClientName, Spec);
					Result["Exists"] = bExists;
					if (!bExists)
						return Result;

					Result["Root"] = Spec.m_Root;
					Result["Stream"] = Spec.m_Stream;
					Result["Owner"] = Spec.m_Owner;
					CJsonSorted &Options = Result["Options"];
					for (auto const &Option : Spec.m_Options)
						Options.f_Insert(Option);

					return Result;
				}
				% "Failed to read the fixture client spec"
			)
		;
	}
}
