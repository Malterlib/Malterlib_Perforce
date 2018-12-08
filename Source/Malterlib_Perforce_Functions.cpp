// Copyright © 2015 Hansoft AB 
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "Malterlib_Perforce_Functions.h"

#include <Mib/Process/StdIn>
#include <Mib/Process/ProcessLaunch>

CPerforceFunctions::CPerforceFunctions(TCUniquePointer<CPerforceClientThrow> &_pClient)
	: m_pClient(_pClient)
{
}

CPerforceClient::CStream const &CPerforceFunctions::f_GetStreamCached(CStr const &_Stream)
{
	auto pStream = m_StreamCache.f_FindEqual(_Stream);
	if (pStream)
		return *pStream;
	
	auto &Stream = m_StreamCache[_Stream];
	
	Stream = m_pClient->f_GetStream(_Stream);
	
	return Stream;
}

CStr CPerforceFunctions::f_GetStreamRootParent(CStr const &_Stream)
{
	auto *pStream = &f_GetStreamCached(_Stream);
	
	CStr Parent = pStream->m_Parent;
	if (Parent == "none")
		Parent = CStr();
	
	while (!pStream->m_Parent.f_IsEmpty() && pStream->m_Parent != "none")
	{
		Parent = pStream->m_Parent;
		pStream = &f_GetStreamCached(Parent);
	}
	
	return Parent;
}

TCUniquePointer<CPerforceClientThrow> &CPerforceFunctions::f_GetClientPtr()
{
	return m_pClient;
}

CPerforceClientThrow &CPerforceFunctions::f_GetClient()
{
	return *m_pClient;
}

CStr CPerforceFunctions::fs_GetCommonPath(CStr const &_First, CStr const &_Second, CStr &_oFirstSuffix, CStr &_oSecondSuffix)
{
	ch8 const *pParse0 = _First;
	ch8 const *pParse1 = _Second;

	ch8 const *pParse0Start = pParse0;
	
	while (*pParse0 && *pParse1)
	{
		if (*pParse0 != *pParse1)
			break;
		++pParse0;
		++pParse1;
	}
	
	_oFirstSuffix = _First.f_Extract(pParse0 - pParse0Start);
	_oSecondSuffix = _Second.f_Extract(pParse0 - pParse0Start);
	return _First.f_Left(pParse0 - pParse0Start);
}

CStr CPerforceFunctions::f_GetCurrentStream() const
{
	CStr CurrentStream;
	m_pClient->f_GetClient
		(
			m_pClient->f_GetClient()
			, [&](CStr const &_Key, CStr const &_Value)
			{
				if (_Key == "Stream")
					CurrentStream = _Value;
			}
		)
	;
	if (CurrentStream.f_IsEmpty())
		DError("Could not find a stream for the current workspace");
	return CurrentStream;
}

CStr CPerforceFunctions::f_GetStrippedRoot()
{
	CStr CurrentHost;
	CStr CurrentRoot;
	CStr CurrentStream;
	m_pClient->f_GetClient
		(
			m_pClient->f_GetClient()
			, [&](CStr const &_Key, CStr const &_Value)
			{
				if (_Key == "Host")
					CurrentHost = _Value;
				if (_Key == "Root")
					CurrentRoot = _Value;
				if (_Key == "Stream")
					CurrentStream = _Value;
			}
		)
	;

	if (CurrentHost.f_IsEmpty())
		DError(fg_Format("Failed to find host in old workspace: {}", m_pClient->f_GetClient()));
	if (CurrentRoot.f_IsEmpty())
		DError(fg_Format("Failed to find root in old workspace: {}", m_pClient->f_GetClient()));
	if (CurrentStream.f_IsEmpty())
		DError(fg_Format("Failed to find stream in old workspace: {}", m_pClient->f_GetClient()));
	
	CPerforceClient::CStream StreamInfo;
	StreamInfo = m_pClient->f_GetStream(CurrentStream);
	
	{
		CStr CurrentDepot = fs_GetDepot(CurrentStream);
		TCVector<CStr> ExtraStreamNames;
		CRegistryPreserveAndOrder_CStr Registry = CPerforceFunctions::fs_GetRegistry(StreamInfo);
		if (StreamInfo.m_Type == "task" || !Registry.f_GetValue("UniqueName", "").f_IsEmpty())
		{
			CPerforceClient::CStream ParentStream = m_pClient->f_GetStream(StreamInfo.m_Parent);
			Registry = CPerforceFunctions::fs_GetRegistry(ParentStream);
			ExtraStreamNames.f_Insert("/" + CurrentDepot + "/" + ParentStream.m_Name);
		}
		else
		{
			ExtraStreamNames.f_Insert("/" + CurrentDepot + "/" + StreamInfo.m_Name);
		}
		
		auto pOldNames = Registry.f_GetChildNoPath("OldNames");
		if (pOldNames)
		{
			for (auto iName = pOldNames->f_GetChildIterator("Name"); iName && iName->f_GetName() == "Name"; ++iName)
				ExtraStreamNames.f_Insert("/" + CurrentDepot + "/" + iName->f_GetThisValue());
		}
		
		for (auto iExtra = ExtraStreamNames.f_GetIterator(); iExtra; ++iExtra)
		{
			aint iFind = CurrentRoot.f_FindReverse(*iExtra);
			if (iFind >= 0)
			{
				CurrentRoot = CurrentRoot.f_Left(iFind);
				break;
			}
		}
	}
	return CurrentRoot;
}

CStr CPerforceFunctions::fs_GetDepot(CStr const &_Stream)
{
	if (_Stream.f_IsEmpty())
		return CStr();
	CStr Ret;
	aint nParsed = 0;
	(CStr::CParse("//{}/") >> Ret).f_Parse(_Stream, nParsed);
	DRequire(nParsed == 1);
	
	return Ret;		
}

CStr CPerforceFunctions::fs_GetStream(CStr const &_Stream)
{
	CStr Depot;
	CStr Stream;
	aint nParsed = 0;
	(CStr::CParse("//{}/{}/") >> Depot >> Stream).f_Parse(_Stream, nParsed);
	if (nParsed == 1)
		(CStr::CParse("//{}/{}") >> Depot >> Stream).f_Parse(_Stream, nParsed);

	DRequire(nParsed == 2);
	
	return fg_Format("//{}/{}", Depot, Stream);
}

CStr CPerforceFunctions::f_GetFullRoot(CStr const &_Stream, CStr const &_StrippedRoot)
{
	CStr Root = _StrippedRoot;
	if (Root.f_IsEmpty())
		Root = f_GetStrippedRoot();
	
	CPerforceClient::CStream StreamInfo = m_pClient->f_GetStream(_Stream);
	
	CStr CurrentDepot = fs_GetDepot(_Stream);
	CStr ExtraStreamName;

	CRegistryPreserveAndOrder_CStr Registry = CPerforceFunctions::fs_GetRegistry(StreamInfo);
	if (StreamInfo.m_Type == "task" || !Registry.f_GetValue("UniqueName", "").f_IsEmpty())
	{
		CPerforceClient::CStream Stream = m_pClient->f_GetStream(StreamInfo.m_Parent);
		ExtraStreamName = "/" + CurrentDepot + "/" + Stream.m_Name;
	}
	else
		ExtraStreamName = "/" + CurrentDepot + "/" + StreamInfo.m_Name;
	return Root + ExtraStreamName;
}

CStr CPerforceFunctions::f_GetClientForStream(CStr const &_Stream, CSwitchResult &_oOldStream, bool _bQuiet)
{
	for (int i = 0; i < 2; ++i)
	{
		CStr CurrentHost = m_pClient->f_GetHost();
		CStr CurrentUser = m_pClient->f_GetUser();
		
		TCSet<CStr> MatchedClients;
		m_pClient->f_GetClients
			(
				CStr()
				, _Stream
				, CurrentUser
				, [&](CStr const &_Client, CStr const &_Key, CStr const &_Value)
				{
					if (_Key == "Host" && _Value == CurrentHost)
						MatchedClients[_Client];
				}
			)
		;
		
		if (MatchedClients.f_IsEmpty())
		{
			if (i == 0)
			{
				_oOldStream = CPerforceFunctions::fs_SwitchStream(*this, _Stream, false, _bQuiet);
				continue;
			}
			else
				DError(fg_Format("No matching destination workspace found for stream '{}'. Have you created a workspace for this stream, or do you need to switch task streams?", _Stream));
		}
		
		if (MatchedClients.f_GetLen() > 1)
		{
			CStr Matching;
		
			for (auto iMatching = MatchedClients.f_GetIterator(); iMatching; ++iMatching)
				fg_AddStrSep(Matching, *iMatching, ", ");
			
			DError(fg_Format("Found multiple matching workspaces. This is not supported: {}", Matching));
		}
		
		return *MatchedClients.f_FindAny();
	}
	return CStr();
}

CRegistryPreserveAndOrder_CStr CPerforceFunctions::fs_GetRegistry(CPerforceClient::CStream const &_Stream)
{
	CRegistryPreserveAndOrder_CStr Registry;
	if (!_Stream.m_Description.f_IsEmpty() && !_Stream.m_Description.f_StartsWith("Created by"))
		Registry.f_ParseStr(_Stream.m_Description);
	return Registry;
}

void CPerforceFunctions::fs_InitializeStream(CStr const &_Stream, CPerforceFunctions &_Functions)
{
	auto &pClient = _Functions.f_GetClientPtr();
	CStr StreamName = _Stream;
	
	CStr P4Port = pClient->f_GetServer();
	CStr P4User = pClient->f_GetUser();
	CStr P4Client = pClient->f_GetClient();
	
	CPerforceFunctions &Functions = _Functions;
	
	CStr CurrentHost;
	CStr CurrentStream;
	pClient->f_GetClient
		(
			P4Client
			, [&](CStr const &_Key, CStr const &_Value)
			{
				if (_Key == "Host")
					CurrentHost = _Value;
				if (_Key == "Stream")
					CurrentStream = _Value;
			}
		)
	;

	if (CurrentHost.f_IsEmpty())
		DError(fg_Format("Failed to find host in old workspace: {}", P4Client));
	if (CurrentStream.f_IsEmpty())
		DError(fg_Format("Failed to find stream in old workspace: {}", P4Client));

	CPerforceClient::CStream Stream = _Functions.f_GetStreamCached(StreamName);
	
	CPerforceClient::CClient SourceClientInfo = pClient->f_GetClientScruct(P4Client);

	CRegistryPreserveAndOrder_CStr Registry = CPerforceFunctions::fs_GetRegistry(Stream);
	CStr ParentStream;
	if (Stream.m_Type == "task" || !Registry.f_GetValue("UniqueName", "").f_IsEmpty())
	{
		ParentStream = Stream.m_Parent;
		Stream = _Functions.f_GetStreamCached(Stream.m_Parent);
	}
	else
		ParentStream = StreamName;
	
	
	CStr CurrentRoot;
	if (Stream.m_Remapped.f_IsEmpty())
		CurrentRoot = Functions.f_GetFullRoot(StreamName);
	else
		CurrentRoot = Functions.f_GetStrippedRoot();
	
	TCSet<CStr> ConsideredStreams;
	
	// Find all task streams
	TCVector<CStr> Streams = pClient->f_FindStreams(fg_Format("Parent={}", ParentStream));
	
	for (auto &Stream : Streams)
	{
		CPerforceClient::CStream StreamInfo = _Functions.f_GetStreamCached(Stream);
		
		CRegistryPreserveAndOrder_CStr Registry = CPerforceFunctions::fs_GetRegistry(StreamInfo);
		
		if (StreamInfo.m_Type == "task" || !Registry.f_GetValue("UniqueName", "").f_IsEmpty())
			ConsideredStreams[Stream];
	}
	
	ConsideredStreams[ParentStream];
	
	TCSet<CStr> MatchedClientsHost;
	TCSet<CStr> MatchedClientsStream;
	pClient->f_GetClients
		(
			CStr()
			, CStr()
			, P4User
			, [&](CStr const &_Client, CStr const &_Key, CStr const &_Value)
			{
				if (_Key == "Host")
				{
					if (_Value == CurrentHost)
						MatchedClientsHost[_Client];
				}
				else if (_Key == "Stream")
				{
					if (ConsideredStreams.f_FindEqual(_Value))
						MatchedClientsStream[_Client];
				}
			}
		)
	;
	
	TCVector<CStr> MatchedClients;
	
	for (auto &Client : MatchedClientsHost)
	{
		if (MatchedClientsStream.f_FindEqual(Client))
			MatchedClients.f_Insert(Client);
	}
	
	CStr Clients;
	for (auto &Client : MatchedClients)
	{
		fg_AddStrSep(Clients, Client, ", ");
	}
	
	if (MatchedClients.f_GetLen() > 1)
		DError(fg_Format("Found more than one client that matches your host and stream(s): {}", Clients));
	
	DConOut("Workspace root: {}{\n}", CurrentRoot);
	
	CStr ClientName;
	{
		CStr Depot = fs_GetDepot(ParentStream);
		CStr ShortHost = fg_GetStrSep(CurrentHost, ".");
		CStr CleanStreamName = Stream.m_Name.f_Replace(" (", ".").f_Replace("(", "").f_Replace(")", "").f_Replace(" ", "_").f_Replace("/", ".");
		
		ClientName = fg_Format("{}_{}_{}_{}", P4User, ShortHost, Depot, CleanStreamName);
		
	}
	TCVector<CStr> Options = SourceClientInfo.m_Options;
	
	if (Registry.f_GetValueNoPath("AlwaysWritable", "") == "true")
	{
		auto iNoAllWrite = Options.f_Contains("noallwrite");
		if (iNoAllWrite >= 0)
			Options.f_Remove(iNoAllWrite);
		if (Options.f_Contains("allwrite") < 0)
			Options.f_Insert("allwrite");
	}
	CStr Client;
	if (MatchedClients.f_IsEmpty())
	{
		DConOut("Create new client: {}{\n}", ClientName);
		pClient->f_CreateStreamClient(ClientName, CurrentRoot, CStr(), P4Client, StreamName, &Options);
		Client = ClientName;
	}
	else
	{
		Client = MatchedClients[0];
		
		auto ClientInfo = pClient->f_GetClientScruct(Client);
		
		if (ClientInfo.m_Root != CurrentRoot || Client != ClientName || ClientInfo.m_Options != Options)
		{
			// We need to reconcile and check for checked out files
		
			TCUniquePointer<CPerforceClientThrow> pClient;
		
			CPerforceClient::CConnectionInfo ConnectionInfo;
			ConnectionInfo.m_Server = P4Port;
			ConnectionInfo.m_User = P4User;
			ConnectionInfo.m_Client = Client;
		
			pClient = fg_Construct(ConnectionInfo);
			pClient->f_Login(CStr());
			
			CStr RootPath;
			try
			{
				RootPath = CFile::fs_GetPath(pClient->f_GetClientPath("//....BranchRoot"));
			}
			catch (CException const &)
			{
				RootPath = ClientInfo.m_Root;
			}
			
#ifdef DPlatformFamily_Windows
			CStr ReconcileScript = CFile::fs_AppendPath(RootPath, "_Reconcile.bat");
#else
			CStr ReconcileScript = CFile::fs_AppendPath(RootPath, "_Reconcile.sh");
#endif
			
			if (CFile::fs_FileExists(ReconcileScript))
			{
				DConOut("Running reconcile script{\n}", 0);
				
				CStr StdOut;
				CStr StdErr;
				uint32 ExitCode;
				CProcessLaunchParams Params;
				Params.m_Environment["P4PORT"] = P4Port;
				Params.m_Environment["P4USER"] = P4User;
				Params.m_Environment["P4CLIENT"] = Client;
				if (pClient->f_IsUTF8())
					Params.m_Environment["P4CHARSET"] = "utf8";
				else
					Params.m_Environment["P4CHARSET"] = "";
				Params.m_WorkingDirectory = RootPath;
				if (!CProcessLaunch::fs_LaunchBlock(ReconcileScript, CStr(), StdOut, StdErr, ExitCode, Params))
				{
					ExitCode = 5;
				}
				
				DConOutRaw(StdOut);
				DConErrOutRaw(StdErr);

				if (ExitCode != 0)
					DError("Reconcile script failed, aborting");
			}
			
			if (!pClient->f_GetOpened(CStr(), CStr()).f_IsEmpty())
				DError(fg_Format("Workspace {} has opened files, aborting", Client));
			
			TCVector<CPerforceClient::CChangeList> ShelvedChangelists = pClient->f_GetChangelists(CStr(), false, Client, "pending");
			
			if (Client != ClientName)
			{
				pClient->f_CreateStreamClient(ClientName, CurrentRoot, CStr(), P4Client, StreamName);
				
				// Move over shelved changelists to new client
				for (auto iShelved = ShelvedChangelists.f_GetIterator(); iShelved; ++iShelved)
					pClient->f_SetChangelistClient(iShelved->m_ChangeID, ClientName);

				// Desync old client
				pClient->f_NoThrow().f_Sync("//...@0");
				
				pClient->f_DeleteWorkspace(Client);
				Client = ClientName;
			}
			else
			{
				// Just desync old root
				pClient->f_NoThrow().f_Sync("//...@0");
			}
		}		
		
		pClient->f_UpdateStreamClient(Client, CurrentRoot, CStr(), P4Client, StreamName, &Options);
		DConOut("Updated old client: {}{\n}", Client);
	}

	{
		TCUniquePointer<CPerforceClientThrow> pClient;
		
		CPerforceClient::CConnectionInfo ConnectionInfo;
		ConnectionInfo.m_Server = P4Port;
		ConnectionInfo.m_User = P4User;
		ConnectionInfo.m_Client = Client;
		
		pClient = fg_Construct(ConnectionInfo);
		pClient->f_Login(CStr());

		CStr Charset;
		if (pClient->f_IsUTF8())
			Charset = "utf8";
		else
			Charset = "";
		
		CStr P4Config = pClient->f_GetEnvVar("P4CONFIG");
		
		if (P4Config != ".p4config")
		{
			DConOut("Setting P4CONFIG to .p4config{\n}", 0);
			pClient->f_SetEnvVar("P4CONFIG", ".p4config");
		}
		
		CStr ConfigFile;
		try
		{
			pClient->f_Sync("//....BranchRoot", fg_Default(), true);
			CPerforceClient::CFileStats Stats = pClient->f_FileStats(CurrentRoot + "/....BranchRoot");
			
			if (Stats.m_ClientFile.f_IsEmpty())
				DError("Failed to find branch root file (.BranchRoot)");
			
			ConfigFile = CFile::fs_AppendPath(CFile::fs_GetPath(Stats.m_ClientFile), ".p4config");
		}
		catch (CException const &)
		{
			ConfigFile = CFile::fs_AppendPath(CurrentRoot, ".p4config");
		}		

		DConOut("ConfigFile: {}{\n}", ConfigFile);
		
		CStr ConfigContents;
		
		fg_AppendFormat(ConfigContents, "P4PORT={}{\n}", P4Port);
		fg_AppendFormat(ConfigContents, "P4USER={}{\n}", P4User);
		fg_AppendFormat(ConfigContents, "P4CLIENT={}{\n}", Client);
		fg_AppendFormat(ConfigContents, "P4CHARSET={}{\n}", Charset);
		
		CByteVector FileContents;
		CFile::fs_WriteStringToVector(FileContents, ConfigContents, false);
		
		CFile::fs_CreateDirectory(CFile::fs_GetPath(ConfigFile));
		if (CFile::fs_CopyFileDiff(FileContents, ConfigFile, CTime::fs_NowUTC()))
			DConOut("Updated .p4config at '{}'{\n}", ConfigFile);
	}		
}

CPerforceFunctions::CSwitchResult CPerforceFunctions::fs_SwitchStream(CPerforceFunctions &_Functions, CStr const &_StreamName, bool _bDoSync, bool _bQuiet)
{
	CStr StreamName = _StreamName;
	auto &pClient = _Functions.f_GetClientPtr();
	
	CStr P4Port = pClient->f_GetServer();
	CStr P4Client = pClient->f_GetClient();
	CStr P4User = pClient->f_GetUser();
	
	CStr CurrentHost;
	CStr CurrentRoot;
	pClient->f_GetClient
		(
			P4Client
			, [&](CStr const &_Key, CStr const &_Value)
			{
				if (_Key == "Host")
					CurrentHost = _Value;
				if (_Key == "Root")
					CurrentRoot = _Value;
			}
		)
	;
	
	if (CurrentHost.f_IsEmpty())
		DError(fg_Format("Failed to find host in old workspace: {}", P4Client));
	if (CurrentRoot.f_IsEmpty())
		DError(fg_Format("Failed to find root in old workspace: {}", CurrentRoot));

	CPerforceClient::CStream Stream = _Functions.f_GetStreamCached(StreamName);
	
	CRegistryPreserveAndOrder_CStr Registry = CPerforceFunctions::fs_GetRegistry(Stream);
	CStr ParentStream;
	if (Stream.m_Type == "task" || !Registry.f_GetValue("UniqueName", "").f_IsEmpty())
		ParentStream = Stream.m_Parent;
	else
		ParentStream = StreamName;

	TCSet<CStr> ConsideredStreams;

	// Find all task streams
	TCVector<CStr> Streams = pClient->f_FindStreams(fg_Format("Parent={}", ParentStream));

	for (auto &Stream : Streams)
	{
		CPerforceClient::CStream StreamInfo = _Functions.f_GetStreamCached(Stream);
		
		CRegistryPreserveAndOrder_CStr Registry = CPerforceFunctions::fs_GetRegistry(StreamInfo);
				
		if (StreamInfo.m_Type == "task" || !Registry.f_GetValue("UniqueName", "").f_IsEmpty())
			ConsideredStreams[Stream];
	}
	
	ConsideredStreams[ParentStream];

	CStr Client;
	for (int i = 0; i < 2; ++i)
	{
		TCSet<CStr> MatchedClientsHost;
		TCSet<CStr> MatchedClientsStream;
		pClient->f_GetClients
			(
				CStr()
				, CStr()
				, P4User
				, [&](CStr const &_Client, CStr const &_Key, CStr const &_Value)
				{
					if (_Key == "Host")
					{
						if (_Value == CurrentHost)
						{
							MatchedClientsHost[_Client];
						}
					}
					else if (_Key == "Stream")
					{
						if (ConsideredStreams.f_FindEqual(_Value))
						{
							MatchedClientsStream[_Client];
						}
					}
				}
			)
		;
		
		TCVector<CStr> MatchedClients;
		
		for (auto &Client : MatchedClientsHost)
		{
			if (MatchedClientsStream.f_FindEqual(Client))
				MatchedClients.f_Insert(Client);
		}
		
		CStr Clients;
		for (auto &Client : MatchedClients)
		{
			fg_AddStrSep(Clients, Client, ", ");
		}
		
		if (MatchedClients.f_GetLen() > 1)
			DError(fg_Format("Found more than one client that matches your host and stream(s): {}", Clients));
	
		if (MatchedClients.f_IsEmpty())
		{
			if (i == 0)
			{
				CPerforceFunctions Functions(pClient);
				fs_InitializeStream(_StreamName, Functions);
				continue;
			}
			
			DError(fg_Format("No suitable workspace found to switch for stream {}", _StreamName));
		}

		Client = MatchedClients[0];
	}
	
	CStr CurrentStream;
	pClient->f_GetClient
		(
			Client
			, [&](CStr const &_Key, CStr const &_Value)
			{
				if (_Key == "Stream")
					CurrentStream = _Value;
			}
		)
	;
	
	if (CurrentStream == StreamName)
	{
		if (!_bQuiet)
			DConOut("Workspace: {} is already has the correct stream '{}' set{\n}", Client << StreamName);
		CSwitchResult Result;
		Result.m_Workspace = Client;
		return Result;
	}
	
	if (!_bQuiet)
		DConOut("Switching workspace: {}{\n}", Client);
	CPerforceClient::CStream CurrentStreamInfo = _Functions.f_GetStreamCached(CurrentStream);

	//DConOut("\tFrom\t{}{\n}", CurrentStream);
	if (!_bQuiet)
		DConOut("\tFrom\t//{}/{}{\n}", CPerforceFunctions::fs_GetDepot(CurrentStream) << CurrentStreamInfo.m_Name);
	//DConOut("\tTo\t\t{}{\n}", StreamName);
	if (!_bQuiet)
		DConOut("\tTo\t\t//{}/{}{\n}", CPerforceFunctions::fs_GetDepot(StreamName) << Stream.m_Name);

	pClient->f_SwitchWorkspaceStream(Client, StreamName);

	if (_bDoSync)
	{

		TCUniquePointer<CPerforceClientThrow> pClient;

		CPerforceClient::CConnectionInfo ConnectionInfo;
		ConnectionInfo.m_Server = P4Port;
		ConnectionInfo.m_User = P4User;
		ConnectionInfo.m_Client = Client;
		
		pClient = fg_Construct(ConnectionInfo);
		pClient->f_Login(CStr());
		
		CClock Timer;
		Timer.f_Start();
		fp64 NextUpdate = Timer.f_GetTime() + 0.5;
		
		CBlockingStdInReader StdInReader;
		
		DConOut("Syncing workspace to new stream{\n}", 0);
		pClient->f_Sync
			(
				"//..."
				, [&](int64 _TotalBytes, int64 _SyncedBytes) -> bool
				{
					fp64 Now = Timer.f_GetTime();
					
					if (Now > NextUpdate)
					{
						if (_TotalBytes > 0)
							DConOut("{sj12} bytes synced ({fe1} %){\n}", _SyncedBytes << (fp64(_SyncedBytes) / fp64(_TotalBytes)) * 100.0);
						else
							DConOut("{sj12} bytes synced{\n}", _SyncedBytes);
							
						NextUpdate = Now + 5.0;
					}

					// Abort
					CStr Data = StdInReader.f_TryReadLine();
					if (Data.f_CmpNoCase("a") == 0 || Data.f_CmpNoCase("abort") == 0)
						return false;
					
					return true;
				}
			)
		;
	}

	CSwitchResult Result;
	Result.m_Workspace = Client;
	Result.m_OldStream = CurrentStream;
	return Result;
}

TCSet<CStr> CPerforceFunctions::f_GetDisabledCreate(CPerforceClient::CStream const &_Stream)
{
	TCSet<CStr> Ret;
	
	auto fl_AddRegistry
		= [&](CRegistryPreserveAndOrder_CStr const &_Registry, CStr const &_Property)
		{
			auto pDisableCreate = _Registry.f_GetChildNoPath(_Property);
			if (pDisableCreate)
			{
				for (auto iDisable = pDisableCreate->f_GetChildIterator("Stream"); iDisable && iDisable->f_GetName() == "Stream"; ++iDisable)
					Ret[iDisable->f_GetThisValue()];
			}
		}
	;

	CRegistryPreserveAndOrder_CStr Registry = CPerforceFunctions::fs_GetRegistry(_Stream);
	fl_AddRegistry(Registry, "DisableCreate");
	fl_AddRegistry(Registry, "DisableCreateInherit");
	
	CStr Parent = _Stream.m_Parent;
	
	while (!Parent.f_IsEmpty() && Parent != "none")
	{
		CPerforceClient::CStream Stream = f_GetStreamCached(Parent);
	
		CRegistryPreserveAndOrder_CStr ParentRegistry = fs_GetRegistry(Stream);
		fl_AddRegistry(ParentRegistry, "DisableCreateInherit");
		Parent = Stream.m_Parent;
	}
	
	return Ret;
}

void CPerforceFunctions::fpr_GetStreamOwned(CStr const &_Stream, COwnedStreams &_oOwned, bool _bReversed)
{
	CPerforceClient::CStream Stream = f_GetStreamCached(_Stream);
	
	auto Registry = fs_GetRegistry(Stream);
	
	auto pOwned = Registry.f_GetChildNoPath("OwnedStreams");
	if (pOwned)
	{
		for (auto iOwned = pOwned->f_GetChildIterator(); iOwned; ++iOwned)
		{
			CStr From;
			CStr To;
			if (_bReversed)
			{
				From = iOwned->f_GetThisValue();
				To = iOwned->f_GetName();
			}
			else
			{
				From = iOwned->f_GetName();
				To = iOwned->f_GetThisValue();
			}
			
			if (From.f_IsEmpty())
				From = To;
			
			CStr RecurseValue = iOwned->f_GetThisValue();
			fpr_GetStreamOwned(RecurseValue, _oOwned, _bReversed);
			_oOwned.f_AddStream(From, To);
		}
	}		
}

CPerforceFunctions::COwnedStreams CPerforceFunctions::f_GetStreamOwned(CStr const &_Stream, bool _bReversed)
{
	COwnedStreams Ret;
	fpr_GetStreamOwned(_Stream, Ret, _bReversed);
	
	return Ret;
}

CStr CPerforceFunctions::f_GetPatchPrefix(CPerforceClient::CStream const &_Stream)
{
	CRegistryPreserveAndOrder_CStr Registry = CPerforceFunctions::fs_GetRegistry(_Stream);
	
	CStr Parent = _Stream.m_Parent;
	
	CStr Prefix = Registry.f_GetValue("PatchPrefix", "");
	
	if (!Prefix.f_IsEmpty())
		return Prefix;
	
	while (!Parent.f_IsEmpty() && Parent != "none")
	{
		CPerforceClient::CStream Stream = f_GetStreamCached(Parent);
	
		CRegistryPreserveAndOrder_CStr ParentRegistry = fs_GetRegistry(Stream);
		CStr Prefix = ParentRegistry.f_GetValue("PatchPrefix", "");
		if (!Prefix.f_IsEmpty())
			return Prefix;
		Parent = Stream.m_Parent;
	}
	
	return CStr();
}


CPerforce_TemporaryStreamSwitcher::CPerforce_TemporaryStreamSwitcher(CPerforceFunctions &_Functions)
	: mp_Functions(_Functions)
{
}

CPerforce_TemporaryStreamSwitcher::~CPerforce_TemporaryStreamSwitcher()
{
	for (auto &Stream : mp_OriginalStreams)
	{
		try
		{
			CPerforceFunctions::fs_SwitchStream(mp_Functions, Stream, false);
		}
		catch (NException::CException const &_Exception)
		{
			DConOut("Failed to switch back original stream ({}): {}", Stream << _Exception.f_GetErrorStr());
		}
	}
}

CStr CPerforce_TemporaryStreamSwitcher::f_GetClientForStream(CStr const &_StreamName, bool _bQuite)
{
	CPerforceFunctions::CSwitchResult SwitchResult;
	CStr DestinationWorkspace = mp_Functions.f_GetClientForStream(_StreamName, SwitchResult, _bQuite);
	
	if (!SwitchResult.m_OldStream.f_IsEmpty())
	{
		if (!mp_OriginalStreams.f_FindEqual(SwitchResult.m_Workspace))
			mp_OriginalStreams[SwitchResult.m_Workspace] = SwitchResult.m_OldStream;
	}
	return DestinationWorkspace;
}

