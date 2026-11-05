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
		TCVector<CStr> fg_FixtureClientOptions()
		{
			return {"allwrite", "clobber", "nocompress", "unlocked", "nomodtime", "rmdir"};
		}

		// Without the user's P4 variables, like P4JOURNAL, P4LOG or P4NAME, so the server writes nothing outside its root.
		NProcess::CProcessLaunchParams fg_ServerLaunchParams(CStr const &_Executable, CStr const &_ServerRoot, TCVector<CStr> const &_Parameters)
		{
			TCVector<CStr> Parameters = {"-r", _ServerRoot, "-J", _ServerRoot / "journal", "-L", _ServerRoot / "log"};
			Parameters.f_Insert(_Parameters);

			NProcess::CProcessLaunchParams Params;
			Params.m_Target = _Executable;
			Params.m_Parameters = NProcess::CProcessLaunchParams::fs_GetParams(Parameters);
			Params.m_WorkingDirectory = _ServerRoot;
			Params.m_bMergeEnvironment = false;
			auto Environment = fg_GetSys()->f_Environment();
			for (auto iVariable = Environment.f_GetIterator(); iVariable; ++iVariable)
			{
				if (!iVariable.f_GetKey().f_StartsWithNoCase("P4"))
					Params.m_Environment[iVariable.f_GetKey()] = *iVariable;
			}
			Params.m_Environment["P4JOURNAL"] = _ServerRoot / "journal";
			Params.m_Environment["P4LOG"] = _ServerRoot / "log";

			return Params;
		}

		// f_Info runs the wrapper's feature detection.
		void fg_FixtureConnect(CPerforceClientThrow &o_Client, CStr const &_Password)
		{
			o_Client.f_Login(_Password);
			o_Client.f_NoThrow().f_Info();
		}

		// Every setting the fixture's sessions use is set here, so ambient P4 settings (the environment, P4CONFIG files, the enviro
		// file and the registry) don't change them. Tickets and learned settings stay in the fixture, out of the user's files and
		// registry and apart between parallel suites. Without _Client the session's client is one the fixture never creates, in
		// place of p4's default, the host's name.
		CPerforceClient::CConnectionInfo fg_FixtureConnection(CStr const &_FixtureRoot, CStr const &_Port, CStr const &_User, CStr const &_Client = {})
		{
			CPerforceClient::CConnectionInfo Info;
			Info.m_Server = _Port;
			Info.m_User = _User;
			Info.m_Client = _Client ? _Client : CStr("PerforceTestServer_NoClient");
			Info.m_Host = "PerforceTestServer";
			Info.m_bIgnorePasswordSetting = true; // Only the fixture's password and tickets
			Info.m_WorkingDirectory = _FixtureRoot;
			Info.m_ConfigName = CStr(); // No P4CONFIG files
			Info.m_TicketFile = _FixtureRoot / "p4tickets";
			Info.m_EnviroFile = _FixtureRoot / "p4enviro";
			Info.m_Charset = "none"; // Untranslated; the login turns to UTF-8 with a Unicode server
			Info.m_CommandCharset = CStr(); // No P4COMMANDCHARSET, which would translate a session with P4CHARSET none, or stop it
			Info.m_IgnoreFiles = "unset"; // Submits add what they are given
			Info.m_ClientPath = "unset"; // The fixture is outside the user's workspaces
			return Info;
		}
	}

	// Empty when not found, which tests report as a skip: Perforce doesn't ship p4d for every platform.
	CStr CPerforceTestServer::fs_FindServerExecutable(CStr const &_Name)
	{
		// fg_FindExecutable returns the name unchanged when it finds nothing.
		CStr Executable = NProcess::NPlatform::fg_FindExecutable(_Name);
		if (!NFile::CFile::fs_IsPathAbsolute(Executable) || !NFile::CFile::fs_FileExists(Executable, NFile::EFileAttrib_File))
			return {};

		return Executable;
	}

	// Serves "<_Root>/p4d". The fixture user is created by its first command.
	TCFuture<void> CPerforceTestServer::f_Start(CStr _Root, CStr _User)
	{
		co_return co_await fp_Start(_Root, _User, false);
	}

	// As f_Start, with a server in Unicode mode, whose sessions are translated: the fixture's sessions log in as UTF-8 ones.
	TCFuture<void> CPerforceTestServer::f_StartUnicode(CStr _Root, CStr _User)
	{
		co_return co_await fp_Start(_Root, _User, true);
	}

	TCFuture<void> CPerforceTestServer::fp_Start(CStr _Root, CStr _User, bool _bUnicode)
	{
		auto CheckDestroy = co_await f_CheckDestroyedOnResume();

		mp_ServerExecutable = fs_FindServerExecutable();
		if (mp_ServerExecutable.f_IsEmpty())
			co_return DMibErrorInstance("p4d was not found in PATH");

		mp_FixtureRoot = _Root;
		mp_ServerRoot = _Root / "p4d";
		mp_User = _User;
		NFile::CFile::fs_CreateDirectory(mp_ServerRoot);

		// A random base port keeps parallel test processes apart. A busy port shows as the server exiting or never answering.
		CStr Unique = NCryptography::CUniversallyUniqueIdentifier(NCryptography::EUniversallyUniqueIdentifierGenerate_Random)
			.f_GetAsString(NCryptography::EUniversallyUniqueIdentifierFormat_AlphaNum)
		;
		uint32 Hash = fg_StrHashDJB2(Unique.f_GetStr());

		// Tells this server from another p4d already listening on the port, which the probe reaches when ours fails to bind.
		mp_ServerID = "Fixture" + Unique;
		co_await NProcess::CProcessLaunchActor::fs_LaunchSimple
			(
				NProcess::CProcessLaunchActor::CSimpleLaunch
				(
					fg_ServerLaunchParams(mp_ServerExecutable, mp_ServerRoot, {"-xD", mp_ServerID})
					, NProcess::CProcessLaunchActor::ESimpleLaunchFlag_GenerateExceptionOnNonZeroExitCode
				)
			)
		;

		if (_bUnicode)
		{
			co_await NProcess::CProcessLaunchActor::fs_LaunchSimple
				(
					NProcess::CProcessLaunchActor::CSimpleLaunch
					(
						fg_ServerLaunchParams(mp_ServerExecutable, mp_ServerRoot, {"-xi"})
						, NProcess::CProcessLaunchActor::ESimpleLaunchFlag_GenerateExceptionOnNonZeroExitCode
					)
				)
			;
		}

		bool bStarted = false;
		for (umint iAttempt = 0; iAttempt < 20 && !bStarted; ++iAttempt)
		{
			mp_Port = "127.0.0.1:{}"_f << (20000 + ((Hash + uint32(iAttempt) * 733) % 40000));
			bStarted = co_await fp_LaunchServer(mp_Port);
		}

		if (!bStarted)
			co_return DMibErrorInstance("Failed to start a p4d test server");

		// A new database is secure by default (security level 4, no user auto-creation). The fixtures rely on an open server, so
		// that is relaxed on the initialized database.
		co_await fp_StopServer();

		for (CStr const &Configurable : {CStr("security=0"), CStr("dm.user.noautocreate=0"), CStr("run.users.authorize=0")})
		{
			co_await NProcess::CProcessLaunchActor::fs_LaunchSimple
				(
					NProcess::CProcessLaunchActor::CSimpleLaunch
					(
						fg_ServerLaunchParams(mp_ServerExecutable, mp_ServerRoot, {"-c", "set {}"_f << Configurable})
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

	// False when the server exits or doesn't answer in time, which is what a busy port looks like.
	TCFuture<bool> CPerforceTestServer::fp_LaunchServer(CStr _Port)
	{
		auto CheckDestroy = co_await f_CheckDestroyedOnResume();

		NProcess::CProcessLaunchParams Params = fg_ServerLaunchParams(mp_ServerExecutable, mp_ServerRoot, {"-p", _Port});
		Params.m_bCreateNewProcessGroup = true; // On Windows the stop that fp_StopServer waits for needs its own process group

		TCPromise<void> ExitPromise;

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

		CStr Port = _Port;
		CStr User = mp_User;
		CStr ServerID = mp_ServerID;
		for (umint iProbe = 0; iProbe < 100; ++iProbe)
		{
			if (ExitPromise.f_IsSet())
				break;

			auto BlockingActorCheckout = fg_BlockingActor();
			bool bReady = co_await
				(
					g_Dispatch(BlockingActorCheckout) / [FixtureRoot = mp_FixtureRoot, Port, User, ServerID]() -> bool
					{
						// A service on the port that never answers is given up on.
						auto Connection = fg_FixtureConnection(FixtureRoot, Port, User);
						Connection.m_Timeout = 10.0;
						CPerforceClient Client(Connection);
						return Client.f_Info() && Client.f_GetLastInfo("ServerID") == ServerID;
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

		// Destroying the launch actor waits for p4d to exit, which settles the database before a restart. The exit notification
		// never comes once the subscription is gone.
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
				/ [FixtureRoot = mp_FixtureRoot, Port, User, Password, Depot = _Depot]()
				{
					CPerforceClientThrow Client(fg_FixtureConnection(FixtureRoot, Port, User));
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
				/ [FixtureRoot = mp_FixtureRoot, Port, User, Password, Stream = _Stream]()
				{
					CPerforceClientThrow Client(fg_FixtureConnection(FixtureRoot, Port, User));
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

	// Clients created from the template bind their own streams; _Stream is the template's own binding.
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
				/ [FixtureRoot = mp_FixtureRoot, Port, User, Password, Name = _Name, Stream = _Stream, Root = _Root]()
				{
					CPerforceClientThrow Client(fg_FixtureConnection(FixtureRoot, Port, User));
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

	// Adds or edits the files through a temporary workspace and returns the submitted changelist. The paths are literal: an @,
	// #, % or * is part of the name.
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
				g_Dispatch(BlockingActorCheckout) / [FixtureRoot = mp_FixtureRoot, Port, User, Password, Stream = _Stream, Files = _Files, Description = _Description, WorkRoot, ClientName]
				() -> int64
				{
					CPerforceClientThrow Client(fg_FixtureConnection(FixtureRoot, Port, User, ClientName));
					fg_FixtureConnect(Client, Password);

					CPerforceClient::CClient WorkspaceSpec;
					WorkspaceSpec.m_Root = WorkRoot;
					WorkspaceSpec.m_Description = "Perforce test submit workspace.";
					WorkspaceSpec.m_Options = fg_FixtureClientOptions();
					WorkspaceSpec.m_Stream = Stream;
					Client.f_SetClient(ClientName, WorkspaceSpec);

					// Edits start from the depot contents. Syncing an empty stream fails harmlessly.
					Client.f_NoThrow().f_Sync("//...");

					for (auto const &File : Files)
					{
						CStr Local = WorkRoot / File.m_Path;
						NFile::CFile::fs_CreateDirectoryForFile(Local);
						NFile::CFile::fs_WriteStringToFile(Local, File.m_Contents, false);

						CStr DepotPath = "{}/{}"_f << Stream << CPerforceClient::fs_EscapeFileName(File.m_Path);
						if (Client.f_NoThrow().f_FileExistsInDepotNotDeleted(DepotPath))
							Client.f_OpenForEdit(CPerforceClient::fs_EscapeFileName(Local));
						else
							Client.f_Add(Local, true);
					}

					Client.f_Submit(CPerforceClient::fs_EscapeFileName(WorkRoot) / "...", Description);

					// The stream's head can be another submit's by now.
					CStr Submitted = Client.f_NoThrow().f_GetLastInfo("submittedChange");
					if (Submitted.f_IsEmpty())
						DMibError("The submit reported no submitted changelist");

					return Submitted.f_ToInt(int64(0));
				}
				% "Failed to submit fixture files"
			)
		;
	}

	// Makes the server require authentication.
	TCFuture<void> CPerforceTestServer::f_SetUserPassword(CStr _Password)
	{
		auto CheckDestroy = co_await f_CheckDestroyedOnResume();

		CStr Port = mp_Port;
		CStr User = mp_User;

		auto BlockingActorCheckout = fg_BlockingActor();
		co_await
			(
				g_Dispatch(BlockingActorCheckout)
				/ [FixtureRoot = mp_FixtureRoot, Port, User, CurrentPassword = mp_Password, Password = _Password]()
				{
					CPerforceClientThrow Client(fg_FixtureConnection(FixtureRoot, Port, User));
					Client.f_Login(CurrentPassword);
					Client.f_NoThrow().f_Info();

					// A server with a password needs a protections table, here granting the fixture user everything.
					Client.f_SetProtections({CStr("super user {} * //..."_f << User)});
					Client.f_SetUserPassword(User, Password, CurrentPassword);
				}
				% "Failed to set the fixture user password"
			)
		;

		mp_Password = _Password;

		co_return {};
	}

	TCFuture<CPerforceClient::CConnectionInfo> CPerforceTestServer::f_GetConnectionInfo(CStr _Client)
	{
		co_return fg_FixtureConnection(mp_FixtureRoot, mp_Port, mp_User, _Client);
	}

	// The result has "Exists" false when the client doesn't exist; a query that fails throws.
	TCFuture<CJsonSorted> CPerforceTestServer::f_GetClientSpec(CStr _ClientName)
	{
		auto CheckDestroy = co_await f_CheckDestroyedOnResume();

		CStr Port = mp_Port;
		CStr User = mp_User;
		CStr Password = mp_Password;

		auto BlockingActorCheckout = fg_BlockingActor();
		co_return co_await
			(
				g_Dispatch(BlockingActorCheckout) / [FixtureRoot = mp_FixtureRoot, Port, User, Password, ClientName = _ClientName]() -> CJsonSorted
				{
					CPerforceClientThrow Client(fg_FixtureConnection(FixtureRoot, Port, User));
					fg_FixtureConnect(Client, Password);

					CJsonSorted Result(EJsonType_Object);

					// Only a listing without the client means it is missing: f_GetClient returns false for a failed query too.
					bool bExists = !Client.f_GetClients(ClientName).f_IsEmpty();
					Result["Exists"] = bExists;
					if (!bExists)
						return Result;

					CPerforceClient::CClient Spec;
					if (!Client.f_NoThrow().f_GetClient(ClientName, Spec))
						Client.f_ThrowLastError();

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
