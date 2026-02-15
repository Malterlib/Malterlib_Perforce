// Copyright © 2015 Hansoft AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "Malterlib_Perforce_Wrapper.h"

#if defined(DPlatformFamily_Windows)
	#include <Mib/Core/PlatformSpecific/WindowsRegistry>
#endif

#undef SetPort

#ifdef DCompiler_MSVC
#pragma warning(push,2)
#endif
#include "clientapi.h"
#include "i18napi.h"
#include "enviro.h"
#include "hostenv.h"
#include "errornum.h"
#ifdef DCompiler_MSVC
#pragma warning(pop)
#endif

namespace NMib::NPerforce
{
	class CPerforceClient::CP4Client : public ClientUser, public KeepAlive
	{
	public:

		CPerforceClient *m_pClient;
		CP4Client(CPerforceClient *_pClient)
		{
			m_pClient = _pClient;
		}

		virtual int	IsAlive() override
		{
			return !m_bAbortOperation;
		}

		bool m_bAbortOperation = false;

		CStr m_OutputText;
		CStr m_OutputTextRaw;
		TCVector<CStr> m_Infos;
		TCFunction<void (CStr const &)> m_OnInfo;
		TCFunction<void (CStr const &)> m_OnText;
		bool m_bError;
		CStr m_LastError;
		TCFunction<CStr (CStr const &, Error *_pError)> m_fOnPrompt;

		void OutputText( const char *data, int length ) override;
		void InputData( StrBuf *strbuf, Error *e ) override;
		void HandleError( Error *err ) override;
		void Message( Error *err ) override;
		void OutputError( const char *errBuf ) override;
		CStr GetInfo(CStr const &_Name);
		void OutputInfo( char level, const char *data ) override;
		void OutputBinary( const char *data, int length ) override;
		void OutputStat( StrDict *varList ) override;
		CStr m_PromtOverride;
		void Prompt( const StrPtr &msg, StrBuf &rsp, int noEcho, Error *e ) override;
		void SetOutputCharset(int) override;
		void ErrorPause( char *errBuf, Error *e ) override;
		void Edit( FileSys *f1, Error *e ) override;
		void Diff( FileSys *f1, FileSys *f2, int doPage, char *diffFlags, Error *e ) override;
		void Merge( FileSys *base, FileSys *leg1, FileSys *leg2, FileSys *result, Error *e ) override;
		int	Resolve( ClientMerge *m, Error *e ) override;
		void Help( const char *const *help ) override;
		FileSys	*File( FileSysType type ) override;
		void Finished() override;
		void Clear() override;
	};

	void CPerforceClient::CP4Client::OutputText( const char *data, int length )
	{
		CStr Temp;
		Temp.f_AddStr(data, length);
		if (m_OnText)
		{
			m_OnText(Temp);
			return;
		}

		m_OutputTextRaw += Temp;
		Temp = m_pClient->f_DecodeStr(Temp);
		m_OutputText += Temp;
	//	return ClientUser::OutputText( data );
	}

	void CPerforceClient::CP4Client::InputData( StrBuf *strbuf, Error *e )
	{
		if (m_PromtOverride != "")
		{
			CStr Temp = m_pClient->f_EncodeStr(m_PromtOverride);
			strbuf->Set(Temp, Temp.f_GetLen());
			m_PromtOverride = "";
		}
		else
		return ClientUser::InputData( strbuf, e );
	//			DConOut("P4: InputData" DNewLine, 0);
	}

	void CPerforceClient::CP4Client::HandleError( Error *err )
	{
	//	DDTrace("P4: HandleError: {}\r\n", err->GetGeneric());
		return ClientUser::HandleError( err );
	}

	void CPerforceClient::CP4Client::Message( Error *err )
	{
		if (err->GetSeverity() > E_INFO)
		{
	//				DConOut("P4: Message(error) = {}" DNewLine, err->GetSeverity());
	//				err->Dump("P4Error");
		}
		return ClientUser::Message( err );
	}

	void CPerforceClient::CP4Client::OutputError( const char *errBuf )
	{
		m_bError = true;
		m_LastError += m_pClient->f_DecodeStr(errBuf) + "\r\n";
	//			DConOut("P4: OutputError: {}" DNewLine, errBuf);
	//	return ClientUser::OutputError( errBuf );
	}

	CStr CPerforceClient::CP4Client::GetInfo(CStr const &_Name)
	{
		mint nInfos = m_Infos.f_GetLen();
		CStr ToFind = _Name + " ";
		for (mint i = 0; i < nInfos; ++i)
		{
			if (m_Infos[i].f_Find(ToFind) == 0)
			{
				return m_Infos[i].f_Extract(ToFind.f_GetLen());
			}
		}
		return "";
	}

	void CPerforceClient::CP4Client::OutputInfo( char level, const char *data )
	{
		(void)level;
		CStr Data = m_pClient->f_DecodeStr(data);
		if (m_OnInfo)
			m_OnInfo(CStr(Data));
		else
			m_Infos.f_Insert(CStr(Data));

		if(strstr(data, "fixes associated with it and can't be deleted"))
		{
			m_bError = true;
			m_LastError += Data + "\r\n";
		}

		//		DConOut("P4: OutputInfo: {}" DNewLine, data);
		if(strstr(data, "can't edit exclusive file already opened"))
			m_bError = true;
	}

	void CPerforceClient::CP4Client::OutputBinary( const char *data, int length )
	{
		CStr Temp;
		Temp.f_AddStr(data, length);
		if (m_OnText)
		{
			m_OnText(Temp);
			return;
		}

		m_OutputTextRaw += Temp;
		Temp = m_pClient->f_DecodeStr(Temp);
		m_OutputText += Temp;
	//			DConOut("P4: OutputBinary" DNewLine, 0);
	//	return ClientUser::OutputBinary( data, length );
	}


	void CPerforceClient::CP4Client::OutputStat( StrDict *varList )
	{
	//			DConOut("P4: OutputStat" DNewLine, 0);
		return ClientUser::OutputStat( varList );
	}

	void CPerforceClient::CP4Client::Prompt( const StrPtr &msg, StrBuf &rsp, int noEcho, Error *e )
	{
	//			DConOut("P4: Prompt" DNewLine, 0);
		if (m_fOnPrompt)
		{
			CStr PromptResult = m_fOnPrompt(CStr(msg.Value(), msg.Length()), e);
			CStr Temp = m_pClient->f_EncodeStr(PromptResult);
			rsp.Set(Temp, Temp.f_GetLen());
		}
		else if (m_PromtOverride != "")
		{
			CStr Temp = m_pClient->f_EncodeStr(m_PromtOverride);
			rsp.Set(Temp, Temp.f_GetLen());
			m_PromtOverride = "";
		}
		else
			return ClientUser::Prompt( msg, rsp, noEcho, e );
	}

	void CPerforceClient::CP4Client::ErrorPause( char *errBuf, Error *e )
	{
	//			DConOut("P4: ErrorPause" DNewLine, 0);
		return ClientUser::ErrorPause( errBuf, e );
	}

	void CPerforceClient::CP4Client::SetOutputCharset(int _Charset)
	{
	//			DConOut("P4: SetOutputCharset" DNewLine, 0);
		return ClientUser::SetOutputCharset( _Charset);
	}

	void CPerforceClient::CP4Client::Edit( FileSys *f1, Error *e )
	{
	//			DConOut("P4: Edit" DNewLine, 0);
		return ClientUser::Edit( f1, e );
	}

	void CPerforceClient::CP4Client::Diff( FileSys *f1, FileSys *f2, int doPage, char *diffFlags, Error *e )
	{
	//			DConOut("P4: Diff" DNewLine, 0);
		return ClientUser::Diff( f1, f2, doPage, diffFlags, e );
	}

	void CPerforceClient::CP4Client::Merge( FileSys *base, FileSys *leg1, FileSys *leg2, FileSys *result, Error *e )
	{
	//			DConOut("P4: Merge" DNewLine, 0);
		return ClientUser::Merge( base, leg1, leg2, result, e );
	}

	int	CPerforceClient::CP4Client::Resolve( ClientMerge *m, Error *e )
	{
	//			DConOut("P4: Resolve" DNewLine, 0);
		return ClientUser::Resolve( m, e );
	}

	void CPerforceClient::CP4Client::Help( const char *const *help )
	{
	//			DConOut("P4: Help" DNewLine, 0);
		return ClientUser::Help( help );
	}

	FileSys	*CPerforceClient::CP4Client::File( FileSysType type )
	{
	//			DConOut("P4: File" DNewLine, 0);
		return ClientUser::File( type );
	}

	void CPerforceClient::CP4Client::Finished()
	{
	//			DConOut("P4: Finished" DNewLine, 0);
		return ClientUser::Finished();
	}

	void CPerforceClient::CP4Client::Clear()
	{
		m_Infos.f_Clear();
		m_bError = false;
		m_LastError.f_Clear();
		m_PromtOverride.f_Clear();
		m_bAbortOperation = false;
	}

	CPerforceClient::CPerforceClient(CStr const &_Server, CStr const &_User, CStr const &_Client, CStr const &_Host)
	{
		m_bUTF8 = false;
		m_pClient = nullptr;
		m_pAPI = nullptr;
		m_pClient = fg_ConstructObject<CP4Client>(NMemory::CDefaultAllocator(), this);
		m_pAPI = fg_ConstructObject<ClientApi>(NMemory::CDefaultAllocator());
		Error P4error;

		if (m_pAPI)
		{
			if (_Server != "")
				m_pAPI->SetPort(f_EncodeStr(_Server));
			m_pAPI->SetProtocol("tag", "");
			//m_pAPI->SetProtocol("api", "58");

			m_pAPI->Init( &P4error );
			m_pAPI->SetProg( "Malterlib Perforce Wrapper" );

			m_ConnectionInfo.m_Server = _Server;
			m_ConnectionInfo.m_User = _User;
			m_ConnectionInfo.m_Host = _Host;
			m_ConnectionInfo.m_Client = _Client;
		}
	}

	CPerforceClient::EAction fg_ConvertAction(CStr const &_Action)
	{
		if (_Action == "add")
			return CPerforceClient::EAction_Add;
		else if (_Action == "delete")
			return CPerforceClient::EAction_Delete;
		else if (_Action == "edit")
			return CPerforceClient::EAction_Edit;
		else if (_Action == "integrate")
			return CPerforceClient::EAction_Integrate;
		else if (_Action == "branch")
			return CPerforceClient::EAction_Branch;
		else
			return CPerforceClient::EAction_Unknown;
	}

	CPerforceClient::CPerforceClient(CPerforceClient::CConnectionInfo const& _Info)
	{
		m_bUTF8 = false;
		m_pClient = nullptr;
		m_pAPI = nullptr;
		m_pClient = fg_ConstructObject<CP4Client>(NMemory::CDefaultAllocator(), this);
		m_pAPI = fg_ConstructObject<ClientApi>(NMemory::CDefaultAllocator());
		Error P4error;

		if (m_pAPI)
		{
			if (_Info.m_Server != "")
				m_pAPI->SetPort(f_EncodeStr(_Info.m_Server));
			if (!_Info.m_bDisableTagging)
				m_pAPI->SetProtocol("tag", "");
			//m_pAPI->SetProtocol("api", "58");

			m_pAPI->Init( &P4error );
			if (P4error.IsError())
			{
				StrBuf Buffer;
				P4error.Fmt(&Buffer);
				m_InitError = Buffer.Text();
			}
			m_pAPI->SetBreak(m_pClient);
			m_pAPI->SetProg( "Malterlib Perforce Wrapper" );

			m_ConnectionInfo = _Info;
		}
	}


	CStr CPerforceClient::f_GetLastFunction() const
	{
		return m_LastFunction;
	}

	CStr CPerforceClient::f_GetLastError() const
	{
		if (!m_LastError.f_IsEmpty())
			return m_LastError.f_Trim();
		if (m_pClient)
			return m_pClient->m_LastError.f_Trim();

		return "";
	}

	#define	DCheckApi(_Function) \
			m_LastError.f_Clear();\
			auto fOnError = [&]{m_LastFunction = _Function ;};\
			if (!m_InitError.f_IsEmpty())\
			{\
				m_LastError = m_InitError;\
				fOnError();\
				return false;\
			}\
			if (!m_pAPI || m_pAPI->Dropped())\
			{\
				m_LastError = "The Perforce client was dropped.";\
				fOnError();\
				return false;\
			}\
			m_pClient->Clear();\
			m_pClient->m_OnInfo.f_Clear();\
			m_pClient->m_OnText.f_Clear();

	void CPerforceClient::fp_Run(const char *_pFunc, TCVector<CStr> const &_Arguments)
	{
		TCVector<CStr> Arguments;
		for (auto iArg = _Arguments.f_GetIterator(); iArg; ++iArg)
			Arguments.f_Insert(f_EncodeStr(*iArg));
		{
			TCVector<ch8 *> CommandPtrs;
			for (auto iCommand = Arguments.f_GetIterator(); iCommand; ++iCommand)
				CommandPtrs.f_Insert((ch8 *)iCommand->f_GetStr());
			m_pAPI->SetArgv( CommandPtrs.f_GetLen(), CommandPtrs.f_GetArray());
		}

	#if 0
		CStr ToOutput;
		ToOutput = CStr::CFormat("p4 {}") << _pFunc;
		for (auto iArg = _Arguments.f_GetIterator(); iArg; ++iArg)
			ToOutput += CStr::CFormat(" {}") << *iArg;
		DConOutRaw(ToOutput);
	#endif

		m_pAPI->SetVar("enableStreams");
		m_pAPI->Run(_pFunc, m_pClient);
	}

	void CPerforceClient::fp_Run(const char *func)
	{
		m_pAPI->SetVar("enableStreams");
		m_pAPI->Run(func, m_pClient);
	}

	bool CPerforceClient::f_Login(CStr const &_Password, CStr const &_WorkingDir)
	{
		DCheckApi("Login");

		if (!_WorkingDir.f_IsEmpty())
		{
			CStr CurrentDir = _WorkingDir;
			m_pAPI->SetCwd(CurrentDir);
		}
		else
		{
			CStr CurrentDir = NFile::CFile::fs_GetCurrentDirectory();
			m_pAPI->SetCwd(CurrentDir);
		}

		m_bUTF8 = false;
		m_pAPI->SetCharset("");
		m_pAPI->SetTrans(CharSetApi::NOCONV);
		if (m_ConnectionInfo.m_User != "")
			m_pAPI->SetUser(f_EncodeStr(m_ConnectionInfo.m_User));
		if (m_ConnectionInfo.m_Host != "")
			m_pAPI->SetHost(f_EncodeStr(m_ConnectionInfo.m_Host));
		if (m_ConnectionInfo.m_Client != "")
			m_pAPI->SetClient(f_EncodeStr(m_ConnectionInfo.m_Client));

		bool bTriedTrust = false;

		while (true)
		{
			DCheckApi("Login");
			char const * CommandsProtect[] = {"-m", nullptr};
			m_pAPI->SetArgv( 0, (char* const*)CommandsProtect);
			fp_Run("protects");

			if (m_pClient->m_bError && m_pClient->m_LastError.f_Find("Unicode server permits only unicode enabled clients.") >= 0)
			{
				m_pAPI->SetCharset("utf8");
				m_pAPI->SetTrans(CharSetApi::UTF_8);
				m_bUTF8 = true;
				if (m_ConnectionInfo.m_User != "")
					m_pAPI->SetUser(f_EncodeStr(m_ConnectionInfo.m_User));
				if (m_ConnectionInfo.m_Host != "")
					m_pAPI->SetHost(f_EncodeStr(m_ConnectionInfo.m_Host));
				if (m_ConnectionInfo.m_Client != "")
					m_pAPI->SetClient(f_EncodeStr(m_ConnectionInfo.m_Client));
				continue;
			}
			else if
				(
					m_pClient->m_bError
					&&
					(
						m_pClient->m_LastError.f_StartsWith("Perforce password (P4PASSWD) invalid or unset.")
						|| m_pClient->m_LastError.f_StartsWith("Your session has expired, please login again.")
					)
					&& _Password != ""
				)
			{
				DCheckApi("Login");
				char const * Commands[] = {nullptr};

				m_pAPI->SetArgv( 0, (char* const*)Commands );
				m_pAPI->SetPassword(_Password.f_GetStr());
				m_pClient->m_PromtOverride = _Password;
				fp_Run("login");
				m_pClient->m_PromtOverride.f_Clear();
				if (m_pClient->m_bError)
				{
					if (m_pClient->m_LastError.f_Find("Unicode server permits only unicode enabled clients.") >= 0)
						continue;
					fOnError();
					return false;
				}
				continue;
			}
			else if (m_pClient->m_bError && m_pClient->m_LastError.f_Find("To allow connection use the 'p4 trust' command.") >= 0)
			{
				if (bTriedTrust)
				{
					fOnError();
					return false;
				}
				DCheckApi("Trust");
				char const * Commands[] = {nullptr};
				m_pAPI->SetArgv( 0, (char* const*)Commands);
				bTriedTrust = true;
				m_pClient->m_fOnPrompt = [&] (CStr const &_Message, Error *_pError) inline_never -> CStr
					{
						if (!_pError || _pError->GetGeneric() != EV_COMM)
							return "no";

						auto pDictonary = _pError->GetDict();
						if (!pDictonary)
							return "no";

						auto pCertHash = pDictonary->GetVar("key");
						if (!pCertHash)
							return "no";

						CStr CertHash(pCertHash->Value(), pCertHash->Length());

						if (CertHash != m_ConnectionInfo.m_TrustedCertificateDigest)
							return "no";

						return "yes";
					}
				;
				fp_Run("trust");
				m_pClient->m_OnText.f_Clear();
				m_pClient->m_fOnPrompt.f_Clear();

				if (m_pClient->m_bError)
				{
					fOnError();
					return false;
				}
				continue;
			}
			else if (m_pClient->m_bError)
			{
				fOnError();
				return false;
			}

			break;
		}

		{
			char const * Commands[] = {0};
			m_pAPI->SetArgv( 0, (char* const*)Commands );
			fp_Run("info");

			if (m_pClient->m_bError)
			{
				fOnError();
				return false;
			}
			else
			{
				m_ActiveHost = m_pClient->GetInfo("clientHost");
				m_ActiveClient = m_pClient->GetInfo("clientName");
				m_ActiveUser = m_pClient->GetInfo("userName");
				m_ActiveVersion = m_pClient->GetInfo("serverVersion");

				CStr Program;
				CStr Platform;
				uint64 VersionYear = 3000;
				uint32 MinorVersion = 1;
				uint64 Revision = TCLimitsInt<uint64>::mc_Max;
				CStr Date;

				(CStr::CParse("{}/{}/{}.{}/{} ({})") >> Program >> Platform >> VersionYear >> MinorVersion >> Revision >> Date).f_Parse(m_ActiveVersion);

				m_bSupportsParentView = Revision >= 2006716;
			}
		}

		m_ConnectionInfo.m_Host = f_DecodeStr(m_pAPI->GetHost().Value());
		return true;
	}

	bool CPerforceClient::f_GetSecurityLevel(CStr &_SecurityLevel)
	{
		DCheckApi("GetSecurityLevel");

		char const * Commands[] = {"-m", nullptr};
		m_pAPI->SetArgv( 1, (char* const*)Commands );
	//	m_pClient->m_PromtOverride = _Password;
		fp_Run("protects");
		if (m_pClient->m_bError)
		{
			fOnError();
			return false;
		}
		else
		{

			mint nInfos = m_pClient->m_Infos.f_GetLen();
			for (mint i = 0; i < nInfos; ++i)
			{
				aint nParse = 0;
				(CStr::CParse("permMax {}") >> _SecurityLevel).f_Parse(m_pClient->m_Infos[0], nParse);
				if (nParse)
					break;
			}
			return true;
		}
	}


	bool CPerforceClient::f_Dropped()
	{
		if(!m_pAPI)
			return true;
		int64 ChangeList;
		f_GetHeadChangelist(ChangeList, CStr()); // Kickstart with a cheap command
		if (m_pAPI->Dropped())
			return true;
		return false;
	}

	CPerforceClient::~CPerforceClient()
	{
		Error P4error;
		if (m_pAPI)
		{
			m_pAPI->Dropped();
			m_pAPI->Final( &P4error );
			fg_DeleteObject(NMemory::CDefaultAllocator(), m_pAPI);
		}
		if (m_pClient)
			fg_DeleteObject(NMemory::CDefaultAllocator(), m_pClient);
	}

	bool CPerforceClient::f_GetStreamDepots(TCVector<CStr> &_Depots)
	{
		DCheckApi("GetStreamDepots");
		char const * Commands[] = {""};
		m_pAPI->SetArgv( 0, (char* const*)Commands );
		fp_Run("depots");
		if (m_pClient->m_bError)
		{
			fOnError();
			return false;
		}
		else
		{
			CStr Name;
			for (mint i = 0; i < m_pClient->m_Infos.f_GetLen(); ++i)
			{
				const CStr &Info = m_pClient->m_Infos[i];
				if (!Info.f_IsEmpty())
				{
					CStr Command;
					CStr Data;
					(CStr::CParse("{} {}") >> Command >> Data).f_Parse(Info);
					if (Command == "name")
						Name = Data;
					else if (Command == "type" && Data == "stream")
						_Depots.f_Insert(Name);
				}
			}
		}

		return true;
	}

	bool CPerforceClient::f_FileExistsInDepot(CStr const &_File)
	{
		DCheckApi(CStr::CFormat("FileExistsInDepot({})") << _File);
		CStr Temp = f_EncodeStr(_File);
		char const * Commands[] = {(ch8 *)Temp.f_GetStr()};
		m_pAPI->SetArgv( 1, (char* const*)Commands );
		fp_Run("fstat");
		if (m_pClient->m_bError)
		{
			CStr Error = f_GetLastError();
			if (Error.f_Find(" - no such file(s).") >= 0)
			{
				m_LastError.f_Clear();
				m_pClient->m_LastError.f_Clear();
				return false;
			}
			fOnError();
			return false;
		}
		else
		{
			int bFound = false;
			for (mint i = 0; i < m_pClient->m_Infos.f_GetLen(); ++i)
			{
				if (m_pClient->m_Infos[i].f_CmpNoCase("headAction ", 11) == 0)
				{
					bFound = true;
					break;
				}
			}
			return bFound;
		}

		return false;
	}


	bool CPerforceClient::f_FileExistsInDepotNotDeleted(CStr const &_File)
	{
		DCheckApi(CStr::CFormat("FileExistsInDepotNotDeleted({})") << _File);
		CStr Temp = f_EncodeStr(_File);
		char const * Commands[] = {(ch8 *)Temp.f_GetStr()};
		m_pAPI->SetArgv( 1, (char* const*)Commands );
		fp_Run("fstat");
		if (m_pClient->m_bError)
		{
			CStr Error = f_GetLastError();
			if (Error.f_Find(" - no such file(s).") >= 0)
			{
				m_LastError.f_Clear();
				m_pClient->m_LastError.f_Clear();
				return false;
			}
			fOnError();
			return false;
		}
		else
		{
			int bFound = false;
			for (mint i = 0; i < m_pClient->m_Infos.f_GetLen(); ++i)
			{
				if (m_pClient->m_Infos[i].f_CmpNoCase("headAction ", 11) == 0)
				{
					if (m_pClient->m_Infos[i].f_GetLen() > 11)
						if (NStr::fg_StrCmpNoCase(m_pClient->m_Infos[i].f_GetStr() + 11, "delete", 6) == 0)
							break;
					bFound = true;
					break;
				}
			}
			return bFound;
		}
	}

	bool CPerforceClient::f_FileExistsInChangeList(CStr const &_File)
	{
		DCheckApi(CStr::CFormat("FileExistsInChangeList({})") << _File);
		CStr Temp = f_EncodeStr(_File);
		char const * Commands[] = {(ch8 *)Temp.f_GetStr()};
		m_pAPI->SetArgv( 1, (char* const*)Commands );
		fp_Run("fstat");
		if (m_pClient->m_bError)
		{
			fOnError();
			return false;
		}
		else
		{
			int bFound = false;
			for (mint i = 0; i < m_pClient->m_Infos.f_GetLen(); ++i)
			{
				if (NStr::fg_StrCmpNoCase(m_pClient->m_Infos[i].f_GetStr(), "change ", 7) == 0)
				{
					bFound = true;
					break;
				}
			}
			return bFound;
		}
	}

	bool CPerforceClient::f_FileExists(CStr const &_File)
	{
		DCheckApi(CStr::CFormat("FileExists({})") << _File);
		CStr Temp = f_EncodeStr(_File);
		char const * Commands[] = {(ch8 *)Temp.f_GetStr()};
		m_pAPI->SetArgv( 1, (char* const*)Commands );
		fp_Run("fstat");
		if (m_pClient->m_bError)
		{
			if (m_pClient->m_LastError.f_FindReverse(" - no such file(s).") >= 0)
			{
				m_pClient->m_LastError = CStr();
				return false;
			}

			fOnError();
			return false;
		}
		else
		{
			return true;
		}
	}

	bool CPerforceClient::f_CanOpenForEdit(CStr const &_File)
	{
		return f_FileExistsInDepot(_File);
	}

	bool CPerforceClient::f_RemoveFromClient(CStr const &_File)
	{
		DCheckApi(CStr::CFormat("RemoveFromClient({})") << _File);
		CStr File = _File + "@0";
		CStr Temp = f_EncodeStr(File);
		char const * Commands[] = {(ch8 *)Temp.f_GetStr()};
		m_pAPI->SetArgv( 1, (char* const*)Commands );
		fp_Run("sync");
		if (m_pClient->m_bError)
		{
			fOnError();
			return false;
		}
		else
		{
			return true;
		}
	}

	bool CPerforceClient::f_Sync(CStr const &_File, TCVector<CStr> &_Synced, TCVector<CStr> &_Removed, bool _bPretend)
	{
		DCheckApi(CStr::CFormat("Sync({})") << _File);
		CStr Temp = f_EncodeStr(_File);
		if (_bPretend)
		{
			ch8 const * Commands[] = {"-n", (ch8 *)Temp.f_GetStr()};
			m_pAPI->SetArgv( 2, (char* const*)Commands );
			fp_Run("sync");
		}
		else
		{
			char const * Commands[] = {(ch8 *)Temp.f_GetStr()};
			m_pAPI->SetArgv( 1, (char* const*)Commands );
			fp_Run("sync");
		}
		{
			mint nInfo = m_pClient->m_Infos.f_GetLen();
			CStr ToFindClient = "clientFile ";
			CStr ToFindAction = "action ";
			CStr LastFile;
			for (mint i = 0; i < nInfo; ++i)
			{
				CStr Info = m_pClient->m_Infos[i];
				aint iFind = Info.f_Find(ToFindClient);
				if (iFind == 0)
				{
					LastFile = Info.f_Extract(ToFindClient.f_GetLen());
				}
				iFind = Info.f_Find(ToFindAction);
				if (iFind == 0 && LastFile != "")
				{
					if (Info.f_Extract(ToFindAction.f_GetLen()) != "deleted")
						_Synced.f_Insert(LastFile.f_ReplaceChar('\\', '/'));
					else
						_Removed.f_Insert(LastFile.f_ReplaceChar('\\', '/'));
					LastFile = "";
				}
			}
		}
		if (m_pClient->m_bError)
		{
			fOnError();
			return false;
		}
		return true;
	}


	bool CPerforceClient::f_Sync(CStr const &_File, TCFunction<bool (int64 _TotalBytes, int64 _SyncedBytes)> const &_Progress, bool _bForce, TCVector<CStr> const &_MoreFiles)
	{
		DCheckApi(CStr::CFormat("{}") << _File);

		TCVector<CStr> Commands;

		if (_bForce)
			Commands.f_Insert("-f");

		if (!_File.f_IsEmpty())
			Commands.f_Insert(_File);

		for (auto &File : _MoreFiles)
			Commands.f_Insert(File);

		int64 TotalFileSize = 0;
		int64 SyncedFileSize = 0;
		if (_Progress)
		{
			m_pClient->m_OnInfo
				= [&](CStr const &_Info)
				{
					CStr Command;
					CStr Data;
					(CStr::CParse("{} {}") >> Command >> Data).f_Parse(_Info);
					bool bDoProgress = false;
					if (Command == "totalFileSize")
					{
						TotalFileSize = Data.f_ToInt(int64(0));
						bDoProgress = true;
					}
					else if (Command == "fileSize")
					{
						SyncedFileSize += Data.f_ToInt(int64(0));
						bDoProgress = true;
					}

					if (bDoProgress)
					{
						if (!_Progress(TotalFileSize, SyncedFileSize))
							m_pClient->m_bAbortOperation = true;
					}
					//DTrace("{}\r\n", _Info);
				}
			;
		}

		fp_Run("sync", Commands);
		if (m_pClient->m_bError)
		{
			if (m_pClient->m_LastError.f_FindNoCase(" - file(s) up-to-date.") != -1)
				return true;
		}

		if (m_pClient->m_bError)
		{
			fOnError();
			return false;
		}
		return true;
	}

	bool CPerforceClient::f_GetHeadChangelistUnsafe(int64& _Changelist)
	{
		DCheckApi("GetHeadChangelistUnsafe");

		ch8 const * Commands[] = {"change"};

		m_pAPI->SetArgv( 1, (char* const*)Commands );

		fp_Run("counter");
		if (m_pClient->m_bError)
		{
			fOnError();
			return false;
		}

		mint nInfo = m_pClient->m_Infos.f_GetLen();
		for (mint i = 0; i < nInfo; ++i)
		{
			const CStr &Info = m_pClient->m_Infos[i];
			CStr Command;
			CStr Data;
			(CStr::CParse("{} {}") >> Command >> Data).f_Parse(Info);
			if (Command == "value")
			{
				_Changelist = Data.f_ToInt(int64(0));
				return true;
			}
		}

		fOnError();
		return false;
	}

	bool CPerforceClient::f_GetHeadChangelist(int64& _Changelist, CStr const& _Path)
	{
		DCheckApi(CStr::CFormat("GetHeadChangelist({})") << _Path);

		ch8 const * Commands[] = {"-s", "submitted", "-m", "1", nullptr};
		Commands[4] = (ch8*)_Path.f_GetStr();

		if (_Path.f_IsEmpty())
			m_pAPI->SetArgv( 4, (char* const*)Commands );
		else
			m_pAPI->SetArgv( 5, (char* const*)Commands );

		fp_Run("changes");
		if (m_pClient->m_bError)
		{
			fOnError();
			return false;
		}

		mint nInfo = m_pClient->m_Infos.f_GetLen();
		for (mint i = 0; i < nInfo; ++i)
		{
			const CStr &Info = m_pClient->m_Infos[i];
			CStr Command;
			CStr Data;
			(CStr::CParse("{} {}") >> Command >> Data).f_Parse(Info);
			if (Command == "change")
			{
				_Changelist = Data.f_ToInt(int64(0));
				return true;
			}
		}

		fOnError();
		return false;
	}


	bool CPerforceClient::f_Files(CStr const &_Search, TCVector<CStr> &_Existing)
	{
		DCheckApi(CStr::CFormat("Files({})") << _Search);
		CStr Temp = f_EncodeStr(_Search);
		char const * Commands[] = {(ch8 *)Temp.f_GetStr()};
		m_pAPI->SetArgv( 1, (char* const*)Commands );
		fp_Run("files");
		if (m_pClient->m_bError)
		{
			fOnError();
			return false;
		}
		else
		{
			mint nInfo = m_pClient->m_Infos.f_GetLen();
			CStr LastFile, Action;
			for (mint i = 0; i < nInfo; ++i)
			{
				CStr const&CurLine = m_pClient->m_Infos[i];

				if (CurLine.f_Find("depotFile ") == 0)
				{
					LastFile = CurLine.f_Extract(10).f_ReplaceChar('\\', '/');
				}
				else if (CurLine.f_Find("action ") == 0)
				{
					Action = CurLine.f_Extract(7);
					if
						(
							Action.f_CmpNoCase("add") == 0
							|| Action.f_CmpNoCase("edit") == 0
							|| Action.f_CmpNoCase("branch") == 0
							|| Action.f_CmpNoCase("integrate") == 0
						)
					{
						_Existing.f_Insert(LastFile);
					}
				}
			}
			return true;
		}
	}

	bool CPerforceClient::f_Files(CStr const &_Search, TCVector<CStr> &_Existing, TCVector<CStr> &_Deleted)
	{
		DCheckApi(CStr::CFormat("Files({})") << _Search);
		CStr Temp = f_EncodeStr(_Search);
		char const * Commands[] = {(ch8 *)Temp.f_GetStr()};
		m_pAPI->SetArgv( 1, (char* const*)Commands );
		fp_Run("files");
		if (m_pClient->m_bError)
		{
			fOnError();
			return false;
		}
		else
		{
			mint nInfo = m_pClient->m_Infos.f_GetLen();
			CStr LastFile, Action;
			for (mint i = 0; i < nInfo; ++i)
			{
				CStr const&CurLine = m_pClient->m_Infos[i];

				if (CurLine.f_Find("depotFile ") == 0)
				{
					LastFile = CurLine.f_Extract(10).f_ReplaceChar('\\', '/');
				}
				else if (CurLine.f_Find("action ") == 0)
				{
					Action = CurLine.f_Extract(7);
					if (Action.f_CmpNoCase("add") == 0 ||
						Action.f_CmpNoCase("edit") == 0 ||
						Action.f_CmpNoCase("integrate") == 0 ||
						Action.f_CmpNoCase("branch") == 0)
						_Existing.f_Insert(LastFile);
					else if (Action.f_CmpNoCase("delete") == 0)
						_Deleted.f_Insert(LastFile);
				}
			}
			return true;
		}
	}

	bool CPerforceClient::f_ClientFiles(CStr const &_Search, TCVector<CStr> &_Existing)
	{
		DCheckApi(CStr::CFormat("{}") << _Search);
		CStr Temp = f_EncodeStr(_Search);
		char const * Commands[] = {(ch8 *)Temp.f_GetStr()};
		m_pAPI->SetArgv( 1, (char* const*)Commands );
		fp_Run("fstat");
		if (m_pClient->m_bError)
		{
			fOnError();
			return false;
		}
		else
		{
			mint nInfo = m_pClient->m_Infos.f_GetLen();
			CStr ToFindClient = "clientFile ";
			CStr ToFindAction = "headAction ";
			CStr LastFile;
			for (mint i = 0; i < nInfo; ++i)
			{
				CStr Info = m_pClient->m_Infos[i];
				aint iFind = Info.f_Find(ToFindClient);
				if (iFind == 0)
				{
					LastFile = Info.f_Extract(ToFindClient.f_GetLen());
				}
				iFind = Info.f_Find(ToFindAction);
				if (iFind == 0 && LastFile != "")
				{
					if (Info.f_Extract(ToFindAction.f_GetLen()) != "delete")
						_Existing.f_Insert(LastFile.f_ReplaceChar('\\', '/'));
					LastFile = "";
				}
			}
			return true;
		}
	}

	bool CPerforceClient::f_SetChangelistOwner(uint32 _ChangeList, CStr const &_User)
	{
		DCheckApi(CStr::CFormat("SetChangelistOwner({}, {})") << _ChangeList << _User);

		TCVector<CStr> Commands;
		Commands.f_Insert("-f");
		Commands.f_Insert("-U");
		Commands.f_Insert(_User);
		Commands.f_Insert(CStr::fs_ToStr(_ChangeList));

		fp_Run("change", Commands);

		if (m_pClient->m_bError)
		{
			fOnError();
			return false;
		}
		return true;
	}

	bool CPerforceClient::fp_MutateChangelist(uint32 _ChangeList, CStr const &_Operation, bool _bForce, TCFunction<bool (CStr &o_NewDesc, CStr const &_Key, CStr const &_Data)> &&_fMutator)
	{
		DCheckApi(CStr::CFormat("{}({})") << _Operation << _ChangeList);

		TCVector<CStr> Commands;
		Commands.f_Insert("-o");

		if (_ChangeList != 0)
			Commands.f_Insert(CStr::fs_ToStr(_ChangeList));

		fp_Run("change", Commands);

		CStr ChangeListContents;
		ChangeListContents += CStr::CFormat("Change:	{}{\n}") << _ChangeList;
		ChangeListContents += CStr::CFormat("{\n}");

		if (m_pClient->m_bError)
		{
			fOnError();
			return false;
		}
		else
		{
			mint nInfo = m_pClient->m_Infos.f_GetLen();

			for (mint i = 0; i < nInfo; ++i)
			{
				const CStr &Info = m_pClient->m_Infos[i];
				if (Info.f_IsEmpty())
					continue;

				CStr Command;
				CStr Data;

				(CStr::CParse("{} {}") >> Command >> Data).f_Parse(Info);
//				DConOut("    {}={}" DNewLine, Command << Data);

//				DDTrace("{} = {}\n", Command << Data);
				if (_fMutator(ChangeListContents, Command, Data))
					continue;

				if (Command == "User")
				{
					ChangeListContents += CStr::CFormat("User:	{}{\n}") << Data;
					ChangeListContents += CStr::CFormat("{\n}");
				}
				else if (Command == "Client")
				{
					ChangeListContents += CStr::CFormat("Client:	{}{\n}") << Data;
					ChangeListContents += CStr::CFormat("{\n}");
				}
				else if (Command == "Status")
				{
					ChangeListContents += CStr::CFormat("Status:	{}{\n}") << Data;
					ChangeListContents += CStr::CFormat("{\n}");
				}
				else if (Command == "Type")
				{
					ChangeListContents += CStr::CFormat("Type:	{}{\n}") << Data;
					ChangeListContents += CStr::CFormat("{\n}");
				}
				else if (Command == "Description")
				{
					ChangeListContents += CStr::CFormat("Description:{\n}");
					ChangeListContents += fs_FixLineStartingTabs(Data);
					ChangeListContents += CStr::CFormat("{\n}");
				}
				else if (Command.f_StartsWith("Jobs"))
				{
					if (Command == "Jobs0")
						ChangeListContents += CStr::CFormat("Jobs:{\n}");
					ChangeListContents += CStr::CFormat("\t{}{\n}") << Data;
				}
				else if (Command.f_StartsWith("Files"))
				{
					if (Command == "Files0")
						ChangeListContents += CStr::CFormat("Files:{\n}");
					ChangeListContents += CStr::CFormat("\t{}{\n}") << Data;
				}
			}
		}
		{
			DCheckApi(CStr::CFormat("{}({})") << _Operation << _ChangeList);

			m_pClient->m_PromtOverride = ChangeListContents;

			Commands.f_Clear();
			Commands.f_Insert("-i");
			if (_bForce)
				Commands.f_Insert("-f");

			fp_Run("change", Commands);

			if (m_pClient->m_bError)
			{
				fOnError();
				return false;
			}
			return true;
		}
	}

	bool CPerforceClient::f_SetChangelistDescription(uint32 _ChangeList, CStr const &_Description, bool _bForce)
	{
		return fp_MutateChangelist
			(
				_ChangeList
				, "SetChangelistDescription"
				, _bForce
				, [&](CStr &o_NewDesc, CStr const &_Key, CStr const &_Data)
				{
					if (_Key == "Description")
					{
						o_NewDesc += CStr::CFormat("Description:{\n}");
						o_NewDesc += fs_FixLineStartingTabs(_Description);
						o_NewDesc += CStr::CFormat("{\n}");
						return true;
					}
					return false;
				}
			)
		;
	}

	bool CPerforceClient::f_SetChangelistClient(uint32 _ChangeList, CStr const &_Client, bool _bForce)
	{
		return fp_MutateChangelist
			(
				_ChangeList
				, "SetChangelistClient"
				, _bForce
				, [&](CStr &o_NewDesc, CStr const &_Key, CStr const &_Data)
				{
					if (_Key == "Client")
					{
						o_NewDesc += CStr::CFormat("Client:	{}{\n}") << _Client;
						o_NewDesc += CStr::CFormat("{\n}");
						return true;
					}
					return false;
				}
			)
		;
	}

	bool CPerforceClient::f_GetChangelist(uint32 _ChangeList, CChangeList &_Ret)
	{
		_Ret.m_ChangeID = _ChangeList;
		DCheckApi(CStr::CFormat("GetChangelist({})") << _ChangeList);

		TCVector<CStr> Commands;
		Commands.f_Insert("-o");

		if (_ChangeList != 0)
			Commands.f_Insert(CStr::fs_ToStr(_ChangeList));

		fp_Run("change", Commands);

		if (m_pClient->m_bError)
		{
			fOnError();
			return false;
		}
		else
		{
			mint nInfo = m_pClient->m_Infos.f_GetLen();
			CChangeList::CFile *pLastFile = nullptr;

			for (mint i = 0; i < nInfo; ++i)
			{
				const CStr &Info = m_pClient->m_Infos[i];
				if (Info.f_IsEmpty())
					continue;
				CStr Command;
				CStr Data;

				(CStr::CParse("{} {}") >> Command >> Data).f_Parse(Info);

				if (Command == "Date")
				{
					_Ret.m_Date = Data.f_ToInt(uint64(0));
					_Ret.m_PerforceDate = Data;
				}
				else if (Command == "User")
				{
					_Ret.m_User = Data;
				}
				else if (Command == "Client")
				{
					_Ret.m_Client = Data;
				}
				else if (Command == "Status")
				{
					_Ret.m_Status = Data;
				}
				else if (Command == "Description")
				{
					_Ret.m_Description = Data;
				}
				else if (Command.f_StartsWith("Jobs"))
				{
					_Ret.m_Jobs.f_Insert(Data);
				}
				else if (Command.f_StartsWith("Files"))
				{
					pLastFile = &_Ret.m_Files.f_Insert();
					pLastFile->m_Name = Data;
				}
			}
			return true;
		}
	}

	bool CPerforceClient::f_GetChangelists(CStr const &_Path, TCVector<CChangeList> &_Ret, bool _bIncludeIntegrated, CStr const &_Workspace, CStr const &_Status)
	{
		DCheckApi(CStr::CFormat("GetChangelists({}, {}, {}, {})") << _Path << _bIncludeIntegrated << _Workspace << _Status);
		TCVector<CStr> Commands;
		if (!_Workspace.f_IsEmpty())
		{
			Commands.f_Insert("-c");
			Commands.f_Insert(f_EncodeStr(_Workspace));
		}
		if (_bIncludeIntegrated)
			Commands.f_Insert("-i");

		Commands.f_Insert("-l"); // Full text

		if (!_Status.f_IsEmpty())
		{
			Commands.f_Insert("-s");
			Commands.f_Insert(f_EncodeStr(_Status));
		}

		if (!_Path.f_IsEmpty())
			Commands.f_Insert(_Path);

		fp_Run("changes", Commands);
		if (m_pClient->m_bError)
		{
			fOnError();
			return false;
		}
		else
		{
			mint nInfo = m_pClient->m_Infos.f_GetLen();
			CChangeList *pCurrentChange = nullptr;
			for (mint i = 0; i < nInfo; ++i)
			{
				const CStr &Info = m_pClient->m_Infos[i];
				if (Info.f_IsEmpty())
					continue;
				CStr Command;
				CStr Data;
				(CStr::CParse("{} {}") >> Command >> Data).f_Parse(Info);

				if (Command == "change")
				{
					pCurrentChange = &_Ret.f_Insert();
					pCurrentChange->m_ChangeID = Data.f_ToInt(uint32(0));
				}

				if (!pCurrentChange)
					continue;

				else if (Command == "shelved")
					pCurrentChange->m_bHasShelvedFiles = true;
				else if (Command == "time" && pCurrentChange)
				{
					pCurrentChange->m_Date = Data.f_ToInt(uint64(0));
					pCurrentChange->m_PerforceDate = Data;
				}
				else if (Command == "user" && pCurrentChange)
					pCurrentChange->m_User = Data;
				else if (Command == "client" && pCurrentChange)
					pCurrentChange->m_Client = Data;
				else if (Command == "status" && pCurrentChange)
					pCurrentChange->m_Status = Data;
				else if (Command == "desc")
					pCurrentChange->m_Description = Data;
			}
			return true;
		}
	}

	bool CPerforceClient::f_ChangeExists(const CFix &_Fix)
	{
		DCheckApi(CStr::CFormat("ChangeExists({})") << _Fix.m_ChangeNumber);
		CChangeList ChangeList;
		bool bRet = f_GetChangelist(_Fix.m_ChangeNumber, ChangeList);
		m_pClient->m_LastError.f_Clear();
		m_LastError.f_Clear();
		return bRet;
	}

	bool CPerforceClient::f_AddFixes(CStr const &_Job, const TCVector<uint32> &_Fixes, CStr const &_Status)
	{
		mint nFixes = _Fixes.f_GetLen();
		for (mint i = 0; i < nFixes; ++i)
		{
			CStr Status = f_EncodeStr(_Status);
			CStr Temp0 = CStr::fs_ToStr(_Fixes[i]);
			CStr Temp1 = f_EncodeStr(_Job);
			ch8 const * Commands[] = {"-s", (ch8 *)Status.f_GetStr(), "-c", (ch8 *)Temp0.f_GetStr(), (ch8 *)Temp1.f_GetStr()};

			DCheckApi(CStr::CFormat("AddFixes({}, {}, {})") << _Job << _Fixes[i] << _Status);
			m_pAPI->SetArgv( 5, (char* const*)Commands );
			fp_Run("fix");
			if (m_pClient->m_bError)
			{
				fOnError();
				return false;
			}
		}
		return true;
	}

	bool CPerforceClient::f_RemoveFixes(CStr const &_Job, const TCVector<uint32> &_Fixes)
	{
		mint nFixes = _Fixes.f_GetLen();
		for (mint i = 0; i < nFixes; ++i)
		{
			CStr Temp0 = CStr::fs_ToStr(_Fixes[i]);
			CStr Temp1 = f_EncodeStr(_Job);
			ch8 const * Commands[] = {"-d", "-c", (ch8 *)Temp0.f_GetStr(), (ch8 *)Temp1.f_GetStr()};

			DCheckApi(CStr::CFormat("RemoveFixes({}, {})") << _Job << _Fixes[i]);
			m_pAPI->SetArgv( 4, (char* const*)Commands );
			fp_Run("fix");
			if (m_pClient->m_bError)
			{
				fOnError();
				return false;
			}
		}
		return true;
	}

	bool CPerforceClient::f_GetFileRevisions(TCVector<CStr> const &_Files, CFileRevisions &_Revisions)
	{
		DCheckApi(_Files.f_GetLen() == 1 ? (CStr::CFormat("GetFileRevisions({})") << _Files.f_GetFirst()).f_GetStr() : CStr("GetFileRevisions"));

		TCVector<CStr> Args;
		Args.f_Insert("-l");
		Args.f_Insert(_Files);
		fp_Run("filelog", Args);

		if (m_pClient->m_bError)
		{
			fOnError();
			return false;
		}
		else
		{
			mint nInfo = m_pClient->m_Infos.f_GetLen();
	//		aint iFix = 0;
			CFile *pCurrentFile = nullptr;
			CFileRev *pCurrentRev = nullptr;
			for (mint i = 0; i < nInfo; ++i)
			{
				const CStr &Info = m_pClient->m_Infos[i];
	//			DDTrace("{}\n", Info);

				CStr Command;
				CStr Data;
				(CStr::CParse("{} {}") >> Command >> Data).f_Parse(Info);

				if (Command == "depotFile")
				{
					pCurrentFile = &_Revisions.m_Files.f_Insert();
					pCurrentFile->m_File = Data;
				}
				else if (Command.f_Find("rev") == 0 && pCurrentFile)
				{
					pCurrentRev = &pCurrentFile->m_Revisions.f_Insert();
					pCurrentRev->m_Revision = Data.f_ToInt(int32(-1));
				}
				else if (Command.f_Find("change") == 0 && pCurrentRev)
				{
					pCurrentRev->m_ChangeList = Data.f_ToInt(int32(-1));
				}
				else if (Command.f_Find("action") == 0 && pCurrentRev)
				{
					pCurrentRev->m_Action = fg_ConvertAction(Data);
				}
				else if (Command.f_Find("time") == 0 && pCurrentRev)
				{
					pCurrentRev->m_Time = Data.f_ToInt(uint64(0));
				}
				else if (Command.f_Find("user") == 0 && pCurrentRev)
				{
					pCurrentRev->m_User = Data;
				}
				else if (Command.f_Find("client") == 0 && pCurrentRev)
				{
					pCurrentRev->m_Client = Data;
				}
				else if (Command.f_Find("desc") == 0 && pCurrentRev)
				{
					pCurrentRev->m_Comment = Data;
				}
				else if (Command.f_Find("how") == 0 && pCurrentRev)
				{
					CStr Index = fg_GetStrSep(Command, ",");
					int32 iIndex = fg_Clamp(Command.f_ToInt(int32(0)), 0, 1000);
					pCurrentRev->m_RevDescs.f_SetAtLeastLen(iIndex + 1, 0);

					CFileRev::CRevDesc::EHow How;
					if (Data == "copy from")
						How = CFileRev::CRevDesc::EHow_CopyFrom;
					else if (Data == "edit from")
						How = CFileRev::CRevDesc::EHow_EditFrom;
					else if (Data == "merge from")
						How = CFileRev::CRevDesc::EHow_MergeFrom;
					else if (Data == "copy into")
						How = CFileRev::CRevDesc::EHow_CopyInto;
					else if (Data == "edit into")
						How = CFileRev::CRevDesc::EHow_EditInto;
					else if (Data == "merge into")
						How = CFileRev::CRevDesc::EHow_MergeInto;
					else
						How = CFileRev::CRevDesc::EHow_Unknown;
					pCurrentRev->m_RevDescs[iIndex].m_How = How;
				}
				else if (Command.f_Find("file") == 0 && pCurrentRev)
				{
					CStr Index = fg_GetStrSep(Command, ",");
					int32 iIndex = fg_Clamp(Command.f_ToInt(int32(0)), 0, 1000);
					pCurrentRev->m_RevDescs.f_SetAtLeastLen(iIndex + 1, 0);
					pCurrentRev->m_RevDescs[iIndex].m_File = Data;
				}
				else if (Command.f_Find("srev") == 0 && pCurrentRev)
				{
					CStr Index = fg_GetStrSep(Command, ",");
					int32 iIndex = fg_Clamp(Command.f_ToInt(int32(0)), 0, 1000);
					pCurrentRev->m_RevDescs.f_SetAtLeastLen(iIndex + 1, 0);
					pCurrentRev->m_RevDescs[iIndex].m_StartRev = Data.f_Extract(1).f_ToInt(int32(-1));
				}
				else if (Command.f_Find("erev") == 0 && pCurrentRev)
				{
					CStr Index = fg_GetStrSep(Command, ",");
					int32 iIndex = fg_Clamp(Command.f_ToInt(int32(0)), 0, 1000);
					pCurrentRev->m_RevDescs.f_SetAtLeastLen(iIndex + 1, 0);
					pCurrentRev->m_RevDescs[iIndex].m_EndRev = Data.f_Extract(1).f_ToInt(int32(-1));
				}
			}
			return true;
		}
	}

	bool CPerforceClient::f_GetFileRevisions(CStr const &_File, CFileRevisions &_Revisions)
	{
		TCVector<CStr> Files;
		Files.f_Insert(_File);
		return f_GetFileRevisions(Files, _Revisions);
	}

	bool CPerforceClient::f_GetFixes(CStr const &_Job, TCVector<CFix> &_Fixes, uint64 _PerforceGUID)
	{
		DCheckApi(CStr::CFormat("GetFixes({})") << _Job);
		CStr Temp = f_EncodeStr(_Job);
		char const * Commands[] = {"-j", (ch8 *)Temp.f_GetStr()};

		if (_Job.f_IsEmpty())
			m_pAPI->SetArgv( 0, (char* const*)Commands );
		else
			m_pAPI->SetArgv( 2, (char* const*)Commands );

		fp_Run("fixes");
		if (m_pClient->m_bError)
		{
			fOnError();
			return false;
		}
		else
		{
			mint nInfo = m_pClient->m_Infos.f_GetLen();
			aint iFix = 0;
			for (mint i = 0; i < nInfo; ++i)
			{
				const CStr &Info = m_pClient->m_Infos[i];
				if (Info.f_IsEmpty())
				{

					_Fixes.f_SetAtLeastLen(iFix + 1, 0);
					_Fixes[iFix].m_PerforceGUID = _PerforceGUID;
					++iFix;
				}
				else
				{
					CStr Command;
					CStr Data;
					(CStr::CParse("{} {}") >> Command >> Data).f_Parse(Info);
					if (Command == "Job")
					{
						_Fixes.f_SetAtLeastLen(iFix + 1, 0);
						_Fixes[iFix].m_Job = Data;
					}
					else if (Command == "Change")
					{
						_Fixes.f_SetAtLeastLen(iFix + 1, 0);
						_Fixes[iFix].m_ChangeNumber = Data.f_ToInt(uint32(0));
					}
					else if (Command == "Date")
					{
						_Fixes.f_SetAtLeastLen(iFix + 1, 0);
						_Fixes[iFix].m_Date = Data.f_ToInt(uint64(0));
					}
					else if (Command == "User")
					{
						_Fixes.f_SetAtLeastLen(iFix + 1, 0);
						_Fixes[iFix].m_User = Data;
					}
					else if (Command == "Client")
					{
						_Fixes.f_SetAtLeastLen(iFix + 1, 0);
						_Fixes[iFix].m_Client = Data;
					}
					else if (Command == "Status")
					{
						_Fixes.f_SetAtLeastLen(iFix + 1, 0);
						_Fixes[iFix].m_Status = Data;
					}
				}
			}
			return true;
		}
	}

	CStr CPerforceClient::fs_FixSpecialChars(CStr const &_In)
	{
		return _In.f_ReplaceChar('\"', '\'');
	}

	CStr CPerforceClient::fs_FixWhiteSpace(CStr const &_In)
	{
		const ch8 *pParse = _In;
		CStr Ret;
		CStr LastLine;
		bool bOnlyWhiteSpace = true;
		while (*pParse)
		{
			if (*pParse == '\n')
			{
				if (bOnlyWhiteSpace)
				{
					Ret.f_AddStr("\r\n");
				}
				else
				{
					LastLine.f_AddStr("\r\n");
					Ret += LastLine;
				}
				bOnlyWhiteSpace = true;
				LastLine.f_Clear();
				++pParse;
				continue;
			}
			else if (*pParse == '\r')
			{
				if (bOnlyWhiteSpace)
				{
					Ret.f_AddStr("\r\n");
				}
				else
				{
					LastLine.f_AddStr("\r\n");
					Ret += LastLine;
				}
				bOnlyWhiteSpace = true;
				LastLine.f_Clear();
				++pParse;
				if (*pParse == '\n')
					++pParse;
				continue;
			}
			else
			{
				if (!NStr::fg_CharIsWhiteSpace(*pParse))
					bOnlyWhiteSpace = false;
				LastLine.f_AddChar(*pParse);
			}

			++pParse;
		}
		if (!LastLine.f_IsEmpty())
		{
			if (bOnlyWhiteSpace)
			{
				Ret.f_AddStr("\r\n");
			}
			else
			{
				LastLine.f_AddStr("\r\n");
				Ret += LastLine;
			}
			LastLine.f_Clear();
		}

		return Ret;
	}


	CStr CPerforceClient::fs_ActionToStr(EAction _Action)
	{
		switch (_Action)
		{
		case EAction_Unknown:
			return "Unknown";
		case EAction_Add:
			return "Add";
		case EAction_Delete:
			return "Delete";
		case EAction_Edit:
			return "Edit";
		case EAction_Integrate:
			return "Integrate";
		case EAction_Branch:
			return "Branch";
		}

		return "Unknown";
	}

	CStr CPerforceClient::fs_FixLineStartingTabs(CStr const &_In)
	{
		CStr Tmp1 = _In.f_Replace("\r\n", "\n");
		CStr Tmp2 = Tmp1.f_Replace("\r", "\n");
		return "\t" + Tmp2.f_Replace("\n", DMibNewLine "\t");
	}

	bool CPerforceClient::f_GetJobs(CRegistry &_Jobs, CStr const &_JobView)
	{
		DCheckApi(CStr::CFormat("GetJobs({})") << _JobView);
		CStr Temp = f_EncodeStr(_JobView);
		ch8 const * Commands[] = {"-e", (ch8 *)Temp.f_GetStr()};
		if (_JobView.f_IsEmpty())
			m_pAPI->SetArgv( 0, (char* const*)Commands );
		else
			m_pAPI->SetArgv( 2, (char* const*)Commands );
		fp_Run("jobs");
		if (m_pClient->m_bError)
		{
			fOnError();
			return false;
		}
		else
		{
			mint nInfo = m_pClient->m_Infos.f_GetLen();
			CRegistry *pCurrentJob = nullptr;
			CRegistry CurrentTemp;
			for (mint i = 0; i < nInfo; ++i)
			{
				const CStr &Info = m_pClient->m_Infos[i];
	//			DDTrace("{}\n", Info);
				if (Info.f_IsEmpty())
				{
					pCurrentJob = nullptr;
				}
				else
				{
					CStr Command;
					CStr Data;
					(CStr::CParse("{} {}") >> Command >> Data).f_Parse(Info);
	#if 1
	//				Data = Data.f_TrimRight();
	#else
					aint iFind = Data.f_FindCharsReverse("\n\r");
					aint iFind2 = Data.f_FindChars("\n\r");
					if (iFind == Data.f_GetLen() - 1 && iFind2 == iFind)
					{
						if (Data[iFind] == '\n' && iFind > 0 && Data[iFind-1] == '\r')
						{
							--iFind;
						}
						Data.f_SetAt(iFind, 0);
						Data.f_SetModified();
					}

	#endif
					Data = fs_FixWhiteSpace(Data);

					Data = Data.f_TrimRight();
	/*				aint iFind = Data.f_FindReverse("\r\n");
					if (iFind == Data.f_GetLen() - 2)
					{
						Data = Data.f_Left(Data.f_GetLen() - 2);
					}*/
	/*				Data = Data.f_Replace("\r\n", "\n");
					Data = Data.f_Replace("\n", "\r\n");
					mint CurrentLen = 0;
					while (CurrentLen != Data.f_GetLen())
					{
						CurrentLen = Data.f_GetLen();
						Data = Data.f_Replace(" \r\n", "\r\n");
					}*/


					if (pCurrentJob)
					{
						pCurrentJob->f_SetValue(Command, Data);
					}
					else
					{
						if (Command == "Job")
						{
							pCurrentJob = _Jobs.f_CreateChild(Data);
							pCurrentJob->f_Merge(CurrentTemp);
							CurrentTemp.f_Clear();
						}
						else
							CurrentTemp.f_SetValue(Command, Data);
					}
				}
			}

			return true;
		}
	}

	bool CPerforceClient::f_GetJobSpec(CStr &_JobSpec)
	{
		DCheckApi("GetJobSpec");
		ch8 const * Commands[] = {"-o"};
		m_pAPI->SetArgv( 1, (char* const*)Commands );
		fp_Run("jobspec");
		if (m_pClient->m_bError)
		{
			fOnError();
			return false;
		}
		else
		{
			mint nInfo = m_pClient->m_Infos.f_GetLen();
			for (mint i = 0; i < nInfo; ++i)
			{
				_JobSpec += m_pClient->m_Infos[i] + "\n";
			}

			return true;
		}
	}

	bool CPerforceClient::f_GetStreams(TCVector<CStr> &_Streams)
	{
		DCheckApi("GetStreams");
		char const * Commands[] = {""};
		m_pAPI->SetArgv( 0, (char* const*)Commands );
		fp_Run("streams");
		if (m_pClient->m_bError)
		{
			fOnError();
			return false;
		}
		else
		{
			for (auto Iter = m_pClient->m_Infos.f_GetIterator(); Iter; ++Iter)
			{
				auto &Info = *Iter;
				CStr Command;
				CStr Data;
				(CStr::CParse("{} {}") >> Command >> Data).f_Parse(Info);
				if (Command == "Stream")
					_Streams.f_Insert(Data);
			}

			return true;
		}


	}

	bool CPerforceClient::f_GetTriggers(CStr &_Triggers)
	{
		DCheckApi("GetTriggers");
		char const * Commands[] = {"-o"};
		m_pAPI->SetArgv( 1, (char* const*)Commands );
		fp_Run("triggers");
		if (m_pClient->m_bError)
		{
			fOnError();
			return false;
		}
		else
		{
			mint nInfo = m_pClient->m_Infos.f_GetLen();
			for (mint i = 0; i < nInfo; ++i)
			{
				_Triggers += m_pClient->m_Infos[i] + "\n";
			}

			return true;
		}
	}

	bool CPerforceClient::f_GetUsers(CRegistry &_Users)
	{
		DCheckApi("GetUsers");
		fp_Run("users");
		if (m_pClient->m_bError)
		{
			fOnError();
			return false;
		}
		else
		{
			mint nInfo = m_pClient->m_Infos.f_GetLen();
			CRegistry *pCurrentUser = nullptr;
			CRegistry CurrentTemp;

			for (mint i = 0; i < nInfo; ++i)
			{
				const CStr &Info = m_pClient->m_Infos[i];
				if (Info.f_IsEmpty())
				{
					pCurrentUser = nullptr;
				}
				else
				{
					CStr Command;
					CStr Data;
					(CStr::CParse("{} {}") >> Command >> Data).f_Parse(Info);

					aint iFind = Data.f_FindCharsReverse("\n\r");
					aint iFind2 = Data.f_FindChars("\n\r");
					if (iFind == Data.f_GetLen() - 1 && iFind2 == iFind)
					{
						if (Data[iFind] == '\n' && iFind > 0 && Data[iFind-1] == '\r')
						{
							--iFind;
						}
						Data.f_SetAt(iFind, 0);
						Data.f_SetModified();
					}

					Data = fs_FixWhiteSpace(Data);

					Data = Data.f_TrimRight();

					if (pCurrentUser)
					{
						pCurrentUser->f_SetValue(Command, Data);
					}
					else
					{
						if (Command == "User")
						{
							pCurrentUser = _Users.f_CreateChild(Data);
							pCurrentUser->f_Merge(CurrentTemp);
							CurrentTemp.f_Clear();
						}
						else
							CurrentTemp.f_SetValue(Command, Data);
					}
				}
			}
			return true;
		}
	}

	bool CPerforceClient::f_ResolveSafe(CStr const &_File, uint32 _Changelist)
	{
		DCheckApi(CStr::CFormat("ResolveSafe({})") << _File);

		TCVector<CStr> Arguments;

		Arguments.f_Insert("-as");
		if (_Changelist != 0)
		{
			Arguments.f_Insert("-c");
			Arguments.f_Insert(CStr::fs_ToStr(_Changelist));
		}

		if (!_File.f_IsEmpty())
			Arguments.f_Insert(_File);

		fp_Run("resolve", Arguments);

		if (m_pClient->m_bError)
		{
			fOnError();
			return false;
		}
		return true;
	}

	void CPerforceClient::fp_ReadMergeResult(TCVector<CIntegrationResult> &_oIntegrated, TCVector<CStr> &_oMustSync, bool _bInverted, TCVector<CMergeError> &_oErrors)
	{
		CIntegrationResult *pLast = nullptr;

		for (CStr const &CurInfo : m_pClient->m_Infos)
		{

			aint iFind = CurInfo.f_FindReverse(" - must sync before integrating.");
			if (iFind >= 0)
			{
				CStr ToSync = CurInfo.f_Left(iFind);
				_oMustSync.f_Insert(ToSync);
				continue;
			}

			CStr Command;
			CStr Data;
			(CStr::CParse("{} {}") >> Command >> Data).f_Parse(CurInfo);

			if (Command == "depotFile")
			{
				pLast = &_oIntegrated.f_Insert();
				pLast->m_To = Data;
			}
			if (pLast)
			{
				if (Command == "action")
				{
					pLast->m_Action = fg_ConvertAction(Data);
					if (_bInverted)
					{
						if (pLast->m_Action == EAction_Delete)
							pLast->m_Action = EAction_Add;
						else if (pLast->m_Action == EAction_Add)
							pLast->m_Action = EAction_Delete;
					}
				}
				else if (Command == "fromFile")
					pLast->m_From = Data;
				else if (Command == "startFromRev")
					pLast->m_StartFromRev = Data.f_ToInt(uint32(0));
				else if (Command == "endFromRev")
					pLast->m_EndFromRev = Data.f_ToInt(uint32(0));
			}
			else
			{
				auto &Error = _oErrors.f_Insert();
				Error.m_Path = Command;
				Error.m_Error = Data;
				if (Error.m_Error.f_StartsWith("- "))
					Error.m_Error = Error.m_Error.f_Extract(2);
			}
		}
	}

	bool CPerforceClient::f_CopyStream(CStr const &_FromStream, CStr const &_ToStream, bool _bPretend, TCVector<CIntegrationResult> &_oIntegrated, TCVector<CStr> &_oMustSync, TCVector<CMergeError> &_oErrors, CStr _ToFileSpec)
	{
		DCheckApi(CStr::CFormat("CopyStream({}, {}, {})") << _FromStream << _ToStream << _bPretend);

		TCVector<CStr> Arguments;

		Arguments.f_Insert("-S");
		Arguments.f_Insert(_ToStream);
		Arguments.f_Insert("-r");
		Arguments.f_Insert("-P");
		Arguments.f_Insert(_FromStream);
		if (_bPretend)
			Arguments.f_Insert("-n");
		if (!_ToFileSpec.f_IsEmpty())
			Arguments.f_Insert(_ToFileSpec);

		fp_Run("copy", Arguments);

		fp_ReadMergeResult(_oIntegrated, _oMustSync, true, _oErrors);

		if (m_pClient->m_bError)
		{
			fOnError();
			return false;
		}
		return true;
	}

	bool CPerforceClient::f_MergeStream(CStr const &_FromStream, CStr const &_ToStream, bool _bPretend, TCVector<CIntegrationResult> &_oIntegrated, TCVector<CStr> &_oMustSync, TCVector<CMergeError> &_oErrors, CStr _ToFileSpec)
	{
		DCheckApi(CStr::CFormat("MergeStream({}, {}, {})") << _FromStream << _ToStream << _bPretend);

		TCVector<CStr> Arguments;

		Arguments.f_Insert("-S");
		Arguments.f_Insert(_ToStream);
		Arguments.f_Insert("-r");
		Arguments.f_Insert("-P");
		Arguments.f_Insert(_FromStream);
		if (_bPretend)
			Arguments.f_Insert("-n");
		if (!_ToFileSpec.f_IsEmpty())
			Arguments.f_Insert(_ToFileSpec);

		fp_Run("merge", Arguments);

		fp_ReadMergeResult(_oIntegrated, _oMustSync, true, _oErrors);

		if (m_pClient->m_bError)
		{
			fOnError();
			return false;
		}
		return true;
	}

	bool CPerforceClient::f_IntegrateFiles(CStr const &_FromFiles, CStr const &_ToFiles, bool _bPretend, bool _bEnableBaseless, TCVector<CIntegrationResult> &_oIntegrated, TCVector<CStr> &_oMustSync, TCVector<CMergeError> &_oErrors)
	{
		DCheckApi(CStr::CFormat("IntegrateFiles({}, {}, {})") << _FromFiles << _ToFiles << _bPretend);

		TCVector<CStr> Arguments;

		if (_bPretend)
			Arguments.f_Insert("-n");

		if (_bEnableBaseless)
			Arguments.f_Insert("-i");

		Arguments.f_Insert("-t");

		Arguments.f_Insert(_FromFiles);
		Arguments.f_Insert(_ToFiles);

		fp_Run("integrate", Arguments);

		fp_ReadMergeResult(_oIntegrated, _oMustSync, false, _oErrors);

		if (m_pClient->m_bError)
		{
			fOnError();
			return false;
		}
		return true;
	}


	bool CPerforceClient::f_CopyStreamToParent(CStr const &_FromStream, bool _bPretend, TCVector<CIntegrationResult> &_oIntegrated, TCVector<CStr> &_oMustSync, TCVector<CMergeError> &_oErrors, CStr _ToFileSpec)
	{
		DCheckApi(CStr::CFormat("CopyStreamToParent({}, {})") << _FromStream << _bPretend);

		TCVector<CStr> Arguments;

		Arguments.f_Insert("-S");
		Arguments.f_Insert(_FromStream);
		if (_bPretend)
			Arguments.f_Insert("-n");
		if (!_ToFileSpec.f_IsEmpty())
			Arguments.f_Insert(_ToFileSpec);

		fp_Run("copy", Arguments);

		fp_ReadMergeResult(_oIntegrated, _oMustSync, false, _oErrors);

		if (m_pClient->m_bError)
		{
			fOnError();
			return false;
		}
		return true;
	}
	bool CPerforceClient::f_MergeStreamToParent(CStr const &_FromStream, bool _bPretend, TCVector<CIntegrationResult> &_oIntegrated, TCVector<CStr> &_oMustSync, TCVector<CMergeError> &_oErrors, CStr _ToFileSpec)
	{
		DCheckApi(CStr::CFormat("MergeStreamToParent({}, {})") << _FromStream << _bPretend);

		TCVector<CStr> Arguments;

		Arguments.f_Insert("-S");
		Arguments.f_Insert(_FromStream);
		if (_bPretend)
			Arguments.f_Insert("-n");
		if (!_ToFileSpec.f_IsEmpty())
			Arguments.f_Insert(_ToFileSpec);

		fp_Run("merge", Arguments);

		fp_ReadMergeResult(_oIntegrated, _oMustSync, false, _oErrors);

		if (m_pClient->m_bError)
		{
			fOnError();
			return false;
		}
		return true;
	}
	bool CPerforceClient::f_IntegrateStreamToParent(CStr const &_FromStream, bool _bPretend, TCVector<CIntegrationResult> &_oIntegrated, TCVector<CStr> &_oMustSync, TCVector<CMergeError> &_oErrors, CStr _ToFileSpec)
	{
		DCheckApi(CStr::CFormat("IntegrateStreamToParent({}, {})") << _FromStream << _bPretend);

		TCVector<CStr> Arguments;

		Arguments.f_Insert("-S");
		Arguments.f_Insert(_FromStream);
		if (_bPretend)
			Arguments.f_Insert("-n");

		Arguments.f_Insert("-t");
		if (!_ToFileSpec.f_IsEmpty())
			Arguments.f_Insert(_ToFileSpec);

		fp_Run("integrate", Arguments);

		fp_ReadMergeResult(_oIntegrated, _oMustSync, false, _oErrors);

		if (m_pClient->m_bError)
		{
			fOnError();
			return false;
		}
		return true;
	}

	bool CPerforceClient::f_CopyStreamFromParent(CStr const &_ToStream, bool _bPretend, TCVector<CIntegrationResult> &_oIntegrated, TCVector<CStr> &_oMustSync, TCVector<CMergeError> &_oErrors, CStr _ToFileSpec)
	{
		DCheckApi(CStr::CFormat("CopyStreamFromParent({}, {})") << _ToStream << _bPretend);

		TCVector<CStr> Arguments;

		Arguments.f_Insert("-r");
		Arguments.f_Insert("-S");
		Arguments.f_Insert(_ToStream);
		if (_bPretend)
			Arguments.f_Insert("-n");
		if (!_ToFileSpec.f_IsEmpty())
			Arguments.f_Insert(_ToFileSpec);

		fp_Run("copy", Arguments);

		fp_ReadMergeResult(_oIntegrated, _oMustSync, true, _oErrors);

		if (m_pClient->m_bError)
		{
			fOnError();
			return false;
		}
		return true;
	}
	bool CPerforceClient::f_MergeStreamFromParent(CStr const &_ToStream, bool _bPretend, TCVector<CIntegrationResult> &_oIntegrated, TCVector<CStr> &_oMustSync, TCVector<CMergeError> &_oErrors, CStr _ToFileSpec)
	{
		DCheckApi(CStr::CFormat("MergeStreamFromParent({}, {})") << _ToStream << _bPretend);

		TCVector<CStr> Arguments;

		Arguments.f_Insert("-r");
		Arguments.f_Insert("-S");
		Arguments.f_Insert(_ToStream);
		if (_bPretend)
			Arguments.f_Insert("-n");
		if (!_ToFileSpec.f_IsEmpty())
			Arguments.f_Insert(_ToFileSpec);

		fp_Run("merge", Arguments);

		fp_ReadMergeResult(_oIntegrated, _oMustSync, true, _oErrors);

		if (m_pClient->m_bError)
		{
			fOnError();
			return false;
		}
		return true;
	}

	bool CPerforceClient::fp_CreatePatchNonTagged(CStr const &_Branch, bool _bFullContext, CStr &_oPatch)
	{
		DCheckApi(CStr::CFormat("CreatePatch({})") << _Branch);

		if (!m_ConnectionInfo.m_bDisableTagging)
		{
			m_LastError = "CreatePatch requires an untagged client";
			fOnError();
			return false;
		}

		TCVector<CStr> Arguments;

		Arguments.f_Insert("-t");
		Arguments.f_Insert("-u");
		if (_bFullContext)
			Arguments.f_Insert("-du999999");
		Arguments.f_Insert("-b");
		Arguments.f_Insert(_Branch);

		m_pClient->m_OnText
			= [&](CStr const &_Info)
			{
				_oPatch += _Info;
			}
		;

		m_pClient->m_OnInfo
			= [&](CStr const &_Info)
			{
				//DConOut("Info: {}", _Info);
				CStr To = _Info;
				CStr From = fg_GetStrLineSep(To);

				auto iFind = From.f_FindCharReverse('\t');
				if (iFind >= 0)
					From = From.f_Left(iFind);
				iFind = To.f_FindCharReverse('\t');
				if (iFind >= 0)
					To = To.f_Left(iFind);

				CStr ToStripped;

				if (To.f_StartsWith("+++ "))
					ToStripped = To.f_Extract(4);
				else
					ToStripped = To;

				_oPatch += CStr::CFormat("Index: {}\n") << ToStripped;
				_oPatch += "===================================================================\n";
				_oPatch += From;
				_oPatch += "\n";
				_oPatch += To;
				_oPatch += "\n";
			}
		;

		fp_Run("diff2", Arguments);

		if (m_pClient->m_bError)
		{
			fOnError();
			return false;
		}

		return true;
	}

	bool CPerforceClient::f_CreatePatch(CStr const &_Branch, bool _bFullContext, CStr &_oPatch)
	{
		DCheckApi(CStr::CFormat("CreatePatch({})") << _Branch);

		if (!m_pNonTaggedClient)
		{
			auto ConnectionInfo = f_GetConnectionInfo();
			ConnectionInfo.m_bDisableTagging = true;
			m_pNonTaggedClient = fg_Construct(ConnectionInfo);
			m_pNonTaggedClient->f_Login(CStr());
		}

		if (!m_pNonTaggedClient->fp_CreatePatchNonTagged(_Branch, _bFullContext, _oPatch))
		{
			CStr Error = m_pNonTaggedClient->f_GetLastError();
			if (Error != "No source file(s) in branch view." && Error != "No file(s) to diff.")
			{
				m_LastError = Error;
				fOnError();
				return false;
			}
		}

		TCVector<CStr> Arguments;

		Arguments.f_Insert("-q");
		Arguments.f_Insert("-t");
		Arguments.f_Insert("-b");
		Arguments.f_Insert(_Branch);

		CStr Status;
		struct CChange
		{
			CStr m_From;
			CStr m_FromType;
			CStr m_To;
			CStr m_ToType;
			CStr m_Status;
			CStr m_Type;
		};
		TCVector<CChange> Changes;
		CChange *pLastChange = nullptr;
		m_pClient->m_OnInfo
			= [&](CStr const &_Info)
			{
				CStr Command;
				CStr Data;
				(CStr::CParse("{} {}") >> Command >> Data).f_Parse(_Info);

				if (Command == "status")
				{
					pLastChange = &Changes.f_Insert();
					pLastChange->m_Status = Data;
				}
				if (pLastChange)
				{
					if (Command == "depotFile")
						pLastChange->m_From = Data;
					else if (Command == "depotFile2")
						pLastChange->m_To = Data;
					else if (Command == "type")
						pLastChange->m_FromType = Data;
					else if (Command == "type2")
						pLastChange->m_ToType = Data;
				}
			}
		;

		fp_Run("diff2", Arguments);

		if (m_pClient->m_bError)
		{
			fOnError();
			return false;
		}
		CTime Now = CTime::fs_NowUTC();
		CTimeConvert::CDateTime DateTime;
		CTimeConvert(Now).f_ExtractDateTime(DateTime);

		CStr TimeFormat = fg_Format
			(
				"{}-{sf0,sj2}-{sf0,sj2} {sf0,sj2}:{sf0,sj2}:{sf0,sj2}.000000000 0000"
				, DateTime.m_Year
				, DateTime.m_Month
				, DateTime.m_DayOfMonth
				, DateTime.m_Hour
				, DateTime.m_Minute
				, DateTime.m_Second
			)
		;

		for (auto &Change : Changes)
		{
			if (Change.m_Status == "left only")
			{
				// Deleted file
				CStr Contents;
				if (Change.m_FromType.f_Find("text") >= 0)
				{
					f_GetTextFileContents(Change.m_From, Contents);
				}

				mint nLines = 0;
				ch8 const *pContents = Contents;
				while (*pContents)
				{
					fg_ParseToEndOfLine(pContents);
					fg_ParseEndOfLine(pContents);
					++nLines;
				}

				_oPatch += CStr::CFormat("Index: {}\n") << Change.m_From;
				_oPatch += "===================================================================\n";
				_oPatch += CStr::CFormat("--- {}\n") << Change.m_From;
				_oPatch += "+++ /dev/null\n";
				_oPatch += CStr::CFormat("@@ -1,{} +0,0 @@\n") << nLines;
				pContents = Contents;
				while (*pContents)
				{
					auto pStart = pContents;
					fg_ParseToEndOfLine(pContents);
					fg_ParseEndOfLine(pContents);
					++nLines;
					_oPatch += "-";
					_oPatch.f_AddStr(pStart, pContents - pStart);
				}
			}
			else if (Change.m_Status == "right only")
			{
				// Added file
				if (pLastChange->m_ToType.f_Find("text") >= 0)
				{
					// We can only add non-binary files
					CStr Contents;
					if (f_GetTextFileContents(Change.m_To, Contents))
					{
						mint nLines = 0;
						ch8 const *pContents = Contents;
						while (*pContents)
						{
							fg_ParseToEndOfLine(pContents);
							fg_ParseEndOfLine(pContents);
							++nLines;
						}

						_oPatch += CStr::CFormat("Index: {}\n") << Change.m_To;
						_oPatch += "===================================================================\n";
						_oPatch += "--- /dev/null\n";
						_oPatch += CStr::CFormat("+++ {}\n") << Change.m_To;
						_oPatch += CStr::CFormat("@@ -0,0 +1,{} @@\n") << nLines;
						pContents = Contents;
						while (*pContents)
						{
							auto pStart = pContents;
							fg_ParseToEndOfLine(pContents);
							fg_ParseEndOfLine(pContents);
							++nLines;
							_oPatch += "+";
							_oPatch.f_AddStr(pStart, pContents - pStart);
						}
					}
				}
			}
			// All other changes are handled by the unified diff
		}

		return true;
	}

	bool CPerforceClient::f_IntegrateStreamFromParent(CStr const &_ToStream, bool _bPretend, TCVector<CIntegrationResult> &_oIntegrated, TCVector<CStr> &_oMustSync, TCVector<CMergeError> &_oErrors, CStr _ToFileSpec)
	{
		DCheckApi(CStr::CFormat("IntegrateStreamFromParent({}, {})") << _ToStream << _bPretend);

		TCVector<CStr> Arguments;

		Arguments.f_Insert("-r");
		Arguments.f_Insert("-S");
		Arguments.f_Insert(_ToStream);
		if (_bPretend)
			Arguments.f_Insert("-n");
		if (!_ToFileSpec.f_IsEmpty())
			Arguments.f_Insert(_ToFileSpec);

		Arguments.f_Insert("-t");

		fp_Run("integrate", Arguments);

		fp_ReadMergeResult(_oIntegrated, _oMustSync, true, _oErrors);

		if (m_pClient->m_bError)
		{
			fOnError();
			return false;
		}
		return true;
	}

	bool CPerforceClient::f_IntegrateStream(CStr const &_FromStream, CStr const &_ToStream, bool _bPretend, TCVector<CIntegrationResult> &_oIntegrated, TCVector<CStr> &_oMustSync, TCVector<CMergeError> &_oErrors, CStr _ToFileSpec)
	{
		DCheckApi(CStr::CFormat("IntegrateStream({}, {}, {})") << _FromStream << _ToStream << _bPretend);

		TCVector<CStr> Arguments;

		Arguments.f_Insert("-S");
		Arguments.f_Insert(_ToStream);
		Arguments.f_Insert("-r");
		Arguments.f_Insert("-P");
		Arguments.f_Insert(_FromStream);
		if (_bPretend)
			Arguments.f_Insert("-n");
		if (!_ToFileSpec.f_IsEmpty())
			Arguments.f_Insert(_ToFileSpec);

		Arguments.f_Insert("-t");
	//	Arguments.f_Insert("-Rs");
	//	Arguments.f_Insert("-v");

		fp_Run("integrate", Arguments);

		fp_ReadMergeResult(_oIntegrated, _oMustSync, true, _oErrors);

		if (m_pClient->m_bError)
		{
			fOnError();
			return false;
		}
		return true;
	}

	bool CPerforceClient::f_ResolveAutomatic(CStr const &_File, uint32 _Changelist)
	{
		DCheckApi(CStr::CFormat("ResolveAutomatic({})") << _File);

		TCVector<CStr> Arguments;

		Arguments.f_Insert("-am");

		if (_Changelist != 0)
		{
			Arguments.f_Insert("-c");
			Arguments.f_Insert(CStr::fs_ToStr(_Changelist));
		}

		if (!_File.f_IsEmpty())
			Arguments.f_Insert(_File);

		fp_Run("resolve", Arguments);

		if (m_pClient->m_bError)
		{
			if (f_GetLastError() != "No file(s) to resolve.")
			{
				fOnError();
				return false;
			}
		}
		return true;
	}

	bool CPerforceClient::f_ResolveMine(CStr const &_File, uint32 _Changelist)
	{
		DCheckApi(CStr::CFormat("ResolveMine({})") << _File);

		TCVector<CStr> Arguments;

		Arguments.f_Insert("-ay");

		if (_Changelist != 0)
		{
			Arguments.f_Insert("-c");
			Arguments.f_Insert(CStr::fs_ToStr(_Changelist));
		}

		if (!_File.f_IsEmpty())
			Arguments.f_Insert(_File);

		fp_Run("resolve", Arguments);

		if (m_pClient->m_bError)
		{
			if (f_GetLastError() != "No file(s) to resolve.")
			{
				fOnError();
				return false;
			}
		}
		return true;
	}


	bool CPerforceClient::f_FileStats(CStr const &_File, CFileStats &_Stats)
	{
		DCheckApi(CStr::CFormat("FileStats({})") << _File);

		TCVector<CStr> Arguments;

		Arguments.f_Insert("-m");
		Arguments.f_Insert("1");
		Arguments.f_Insert(_File);

		fp_Run("fstat", Arguments);

		for (CStr const &CurInfo : m_pClient->m_Infos)
		{
			CStr Command;
			CStr Data;
			(CStr::CParse("{} {}") >> Command >> Data).f_Parse(CurInfo);

			if (Command == "depotFile")
				_Stats.m_DepotFile = Data;
			else if (Command == "clientFile")
				_Stats.m_ClientFile = Data;
			else if (Command == "isMapped")
				_Stats.m_bIsMapped = Data != "";
			else if (Command == "headAction")
				_Stats.m_HeadAction = fg_ConvertAction(Data);
			else if (Command == "headType")
				_Stats.m_HeadType = Data;
			else if (Command == "headTime")
				_Stats.m_HeadTime = Data;
			else if (Command == "headRev")
				_Stats.m_HeadRev = Data.f_ToInt(uint32(0));
			else if (Command == "headChange")
				_Stats.m_HeadChange = Data.f_ToInt(uint32(0));
			else if (Command == "headModTime")
				_Stats.m_HeadModTime = Data.f_ToInt(uint64(0));
			else if (Command == "haveRev")
				_Stats.m_HaveRev = Data.f_ToInt(uint32(0));
		}

		if (m_pClient->m_bError)
		{
			fOnError();
			return false;
		}
		return true;
	}

	#if defined(DPlatformFamily_Windows)

	bool CPerforceClient::f_GetEnvVar(CStr const &_Var, CStr &_Value)
	{
		DCheckApi(CStr::CFormat("f_GetEnvVar({})") << _Var);

		NMib::NPlatform::CWin32_Registry Registry(NMib::NPlatform::CWin32_Registry::ERegRoot_CurrentUser, "Software\\Perforce\\Environment");
		if (Registry.f_ValueExists("", _Var))
			_Value = Registry.f_Read_Str("", _Var);

		return true;
	}
	bool CPerforceClient::f_SetEnvVar(CStr const &_Var, CStr const &_Value)
	{
		DCheckApi(CStr::CFormat("f_SetEnvVar({}, {})") << _Var << _Value);

		NMib::NPlatform::CWin32_Registry Registry(NMib::NPlatform::CWin32_Registry::ERegRoot_CurrentUser, "Software\\Perforce\\Environment");
		Registry.f_Write("", _Var, _Value);

		return true;
	}
	#else

	bool CPerforceClient::f_GetEnvVar(CStr const &_Var, CStr &_Value)
	{
		DCheckApi(CStr::CFormat("f_GetEnvVar({})") << _Var);

		Enviro env;
		CStr EncodedVar = f_EncodeStr(_Var);
		auto pValue = env.Get(EncodedVar);
		if (pValue)
			_Value = f_DecodeStr(pValue);

		return true;
	}
	bool CPerforceClient::f_SetEnvVar(CStr const &_Var, CStr const &_Value)
	{
		DCheckApi(CStr::CFormat("f_SetEnvVar({}, {})") << _Var << _Value);

		Enviro	env;
		CStr EncodedVar = f_EncodeStr(_Var);
		CStr EncodedValue = f_EncodeStr(_Value);
		Error ReturnedError;
		env.Set(EncodedVar, EncodedValue, &ReturnedError);
		if (ReturnedError.Test())
			m_pClient->Message(&ReturnedError);

		if (m_pClient->m_bError)
		{
			fOnError();
			return false;
		}
		return true;
	}
	#endif

	bool CPerforceClient::f_GetOpened(CStr const &_Path, CStr const &_Client, TCVector<CStr> &_oOpened)
	{
		DCheckApi(CStr::CFormat("GetOpened({}, {})") << _Path << _Client);

		TCVector<CStr> Arguments;

		if (!_Client.f_IsEmpty())
		{
			Arguments.f_Insert("-C");
			Arguments.f_Insert(_Client);
		}
		if (!_Path.f_IsEmpty())
			Arguments.f_Insert(_Path);

		fp_Run("opened", Arguments);

		for (CStr const &CurInfo : m_pClient->m_Infos)
		{
			CStr Command;
			CStr Data;
			(CStr::CParse("{} {}") >> Command >> Data).f_Parse(CurInfo);

			if (Command == "depotFile")
				_oOpened.f_Insert(Data);
		}

		if (m_pClient->m_bError)
		{
			fOnError();
			return false;
		}
		return true;
	}

	bool CPerforceClient::f_FindStreams(CStr const &_SearchQuery, TCVector<CStr> &_oStreams, TCVector<CStr> const &_StreamSpecs)
	{
		DCheckApi(CStr::CFormat("FindStreams({})") << _SearchQuery);

		TCVector<CStr> Arguments;

		if (_SearchQuery)
		{
			Arguments.f_Insert("-F");
			Arguments.f_Insert(_SearchQuery);
		}
		Arguments.f_Insert(_StreamSpecs);

		fp_Run("streams", Arguments);

		for (CStr const &CurInfo : m_pClient->m_Infos)
		{
			CStr Command;
			CStr Data;
			(CStr::CParse("{} {}") >> Command >> Data).f_Parse(CurInfo);
			if (Command == "Stream")
				_oStreams.f_Insert(Data);
		}

		if (m_pClient->m_bError)
		{
			fOnError();
			return false;
		}
		return true;
	}

	bool CPerforceClient::f_FindStreamsByViewMatch(TCVector<CStr> const &_Views, TCVector<CStr> &o_Streams)
	{
		DCheckApi(CStr::CFormat("FindStreamsByViewMatch({vs})") << _Views);

		TCVector<CStr> Arguments;

		for (auto &View : _Views)
		{
			Arguments.f_Insert("--viewmatch");
			Arguments.f_Insert(View);
		}

		fp_Run("streams", Arguments);

		for (CStr const &CurInfo : m_pClient->m_Infos)
		{
			CStr Command;
			CStr Data;
			(CStr::CParse("{} {}") >> Command >> Data).f_Parse(CurInfo);
			if (Command == "Stream")
				o_Streams.f_Insert(Data);
		}

		if (m_pClient->m_bError)
		{
			fOnError();
			return false;
		}
		return true;
	}

	bool CPerforceClient::f_SwitchWorkspaceStream(CStr const &_Workspace, CStr const &_Stream)
	{
		DCheckApi(CStr::CFormat("SwitchWorkspaceStream({}, {})") << _Workspace << _Stream);

		TCVector<CStr> Arguments;

		Arguments.f_Insert("-s");
		Arguments.f_Insert("-S");
		Arguments.f_Insert(_Stream);
		Arguments.f_Insert(_Workspace);

		fp_Run("client", Arguments);

		if (m_pClient->m_bError)
		{
			fOnError();
			return false;
		}
		return true;
	}

	bool CPerforceClient::f_DeleteWorkspace(CStr const &_Workspace)
	{
		DCheckApi(CStr::CFormat("DeleteWorkspace({})") << _Workspace);

		TCVector<CStr> Arguments;

		Arguments.f_Insert("-d");
		Arguments.f_Insert(_Workspace);

		fp_Run("client", Arguments);

		if (m_pClient->m_bError)
		{
			fOnError();
			return false;
		}
		return true;
	}


	bool CPerforceClient::f_PopulateStream(CStr const &_StreamName)
	{
		DCheckApi(CStr::CFormat("PopulateStream({})") << _StreamName);

		TCVector<CStr> Arguments;

		Arguments.f_Insert("-r");
		Arguments.f_Insert("-S");
		Arguments.f_Insert(_StreamName);

		fp_Run("populate", Arguments);

		if (m_pClient->m_bError)
		{
			fOnError();
			return false;
		}
		return true;
	}

	bool CPerforceClient::f_SetStream(CStr const &_StreamName, CStream const &_Stream)
	{
		DCheckApi(CStr::CFormat("SetStream({})") << _StreamName);

		CStr StreamSpec;
		StreamSpec += CStr::CFormat("Stream:	{}\n") << _StreamName;
		StreamSpec += "\n";
		StreamSpec += CStr::CFormat("Name:	{}\n") << _Stream.m_Name;
		StreamSpec += "\n";
		StreamSpec += CStr::CFormat("Parent:	{}\n") << _Stream.m_Parent;
		StreamSpec += "\n";
		if (m_bSupportsParentView)
		{
			StreamSpec += CStr::CFormat("ParentView:	{}\n") << (_Stream.m_bInheritParentView ? "inherit" : "noinherit");
			StreamSpec += "\n";
		}
		StreamSpec += CStr::CFormat("Type:	{}\n") << _Stream.m_Type;
		StreamSpec += "\n";
		CStr Owner = _Stream.m_Owner;
		if (Owner.f_IsEmpty())
			Owner = m_ConnectionInfo.m_User;
		StreamSpec += CStr::CFormat("Owner:	{}\n") << Owner;
		StreamSpec += "\n";
		if (!_Stream.m_Description.f_IsEmpty())
		{
			StreamSpec += "Description:\n";
			CStr Description = _Stream.m_Description;
			while (!Description.f_IsEmpty())
			{
				CStr Line = fg_GetStrLineSep(Description);
				StreamSpec += CStr::CFormat("	{}\n") << Line;
			}
			StreamSpec += "\n";
		}
		if (!_Stream.m_Options.f_IsEmpty())
		{
			StreamSpec += "Options:";
			for (auto iOption = _Stream.m_Options.f_GetIterator(); iOption; ++iOption)
				StreamSpec += CStr::CFormat(" {}") << *iOption;
			StreamSpec += "\n";
			StreamSpec += "\n";
		}
		if (!_Stream.m_Paths.f_IsEmpty())
		{
			StreamSpec += "Paths:\n";
			for (auto iPath = _Stream.m_Paths.f_GetIterator(); iPath; ++iPath)
				StreamSpec += CStr::CFormat("	{}\n") << *iPath;
			StreamSpec += "\n";
		}
		if (!_Stream.m_Remapped.f_IsEmpty())
		{
			StreamSpec += "Remapped:\n";
			for (auto iIgnore = _Stream.m_Remapped.f_GetIterator(); iIgnore; ++iIgnore)
				StreamSpec += CStr::CFormat("	{}\n") << *iIgnore;
			StreamSpec += "\n";
		}
		if (!_Stream.m_Ignored.f_IsEmpty())
		{
			StreamSpec += "Ignored:\n";
			for (auto iIgnore = _Stream.m_Ignored.f_GetIterator(); iIgnore; ++iIgnore)
				StreamSpec += CStr::CFormat("	{}\n") << *iIgnore;
			StreamSpec += "\n";
		}

		TCVector<CStr> Arguments;

		Arguments.f_Insert("-i");

		m_pClient->m_PromtOverride = StreamSpec;

		fp_Run("stream", Arguments);

		if (m_pClient->m_bError)
		{
			fOnError();
			return false;
		}
		return true;
	}

	bool CPerforceClient::f_StreamExists(CStr const &_StreamName)
	{
		DCheckApi(CStr::CFormat("StreamExists({})") << _StreamName);
		TCVector<CStr> Arguments;

		Arguments.f_Insert(_StreamName);

		fp_Run("streams", Arguments);

		for (CStr const &CurInfo : m_pClient->m_Infos)
		{
			CStr Command;
			CStr Data;
			(CStr::CParse("{} {}") >> Command >> Data).f_Parse(CurInfo);
			if (Command == "Stream" && Data == _StreamName)
				return true;
		}

		m_pClient->m_LastError.f_Clear();
		m_LastError.f_Clear();

		return false;
	}

	bool CPerforceClient::f_Obliterate(CStr const &_Path)
	{
		DCheckApi(CStr::CFormat("Obliterate({})") << _Path);

		TCVector<CStr> Arguments;

		Arguments.f_Insert("-y");
		Arguments.f_Insert("-b");
		Arguments.f_Insert(_Path);

		fp_Run("obliterate", Arguments);

		if (m_pClient->m_bError)
		{
			fOnError();
			return false;
		}
		return true;
	}

	bool CPerforceClient::f_DeleteStream(CStr const &_StreamName)
	{
		DCheckApi(CStr::CFormat("DeleteStream({})") << _StreamName);

		TCVector<CStr> Arguments;

		Arguments.f_Insert("-d");
		Arguments.f_Insert(_StreamName);

		fp_Run("stream", Arguments);

		if (m_pClient->m_bError)
		{
			fOnError();
			return false;
		}
		return true;
	}

	bool CPerforceClient::f_GetStream(CStr const &_StreamName, CStream &_Stream)
	{
		DCheckApi(CStr::CFormat("f_GetStream({})") << _StreamName);

		TCVector<CStr> Arguments;

		Arguments.f_Insert("-o");
		Arguments.f_Insert(_StreamName);

		fp_Run("stream", Arguments);

		for (CStr const &CurInfo : m_pClient->m_Infos)
		{
			CStr Command;
			CStr Data;
			(CStr::CParse("{} {}") >> Command >> Data).f_Parse(CurInfo);

			if (Command == "Update")
				_Stream.m_Update = Data;
			else if (Command == "Access")
				_Stream.m_Access = Data;
			else if (Command == "Owner")
				_Stream.m_Owner = Data;
			else if (Command == "Name")
				_Stream.m_Name = Data;
			else if (Command == "Parent")
				_Stream.m_Parent = Data;
			else if (Command == "ParentView")
				_Stream.m_bInheritParentView = Data == "inherit";
			else if (Command == "baseParent")
				_Stream.m_BaseParent = Data;
			else if (Command == "Type")
				_Stream.m_Type = Data;
			else if (Command == "Description")
				_Stream.m_Description = Data;
			else if (Command == "Options")
			{
				while (!Data.f_IsEmpty())
					_Stream.m_Options.f_Insert(fg_GetStrSep(Data, " "));
			}
			else if (Command.f_StartsWith("Paths"))
				_Stream.m_Paths.f_Insert(Data);
			else if (Command.f_StartsWith("Ignored"))
				_Stream.m_Ignored.f_Insert(Data);
			else if (Command.f_StartsWith("Remapped"))
				_Stream.m_Remapped.f_Insert(Data);
			else if (Command.f_StartsWith("firmerThanParent"))
				_Stream.m_bFirmerThanParent = Data == "true";
		}

		if (m_pClient->m_bError)
		{
			fOnError();
			return false;
		}
		return true;
	}

	bool CPerforceClient::f_DeleteShelvedFile(uint32 _Changelist, CStr const &_File, bool _bForce)
	{
		DCheckApi(CStr::CFormat("DeleteShelvedFile({}, {}, {})") << _Changelist << _File << _bForce);

		TCVector<CStr> Arguments;

		if (_bForce)
			Arguments.f_Insert("-f");
		Arguments.f_Insert("-d");
		Arguments.f_Insert("-c");
		Arguments.f_Insert(CStr::fs_ToStr(_Changelist));
		if (!_File.f_IsEmpty())
			Arguments.f_Insert(_File);

		fp_Run("shelve", Arguments);

		if (m_pClient->m_bError)
		{
			fOnError();
			return false;
		}
		return true;
	}

	bool CPerforceClient::f_RemoveJobsFromChangelist(uint32 _Changelist, TCVector<CStr> const &_Jobs)
	{
		DCheckApi(CStr::CFormat("RemoveJobsFromChangelist({})") << _Changelist);

		TCVector<CStr> Arguments;

		Arguments.f_Insert("-d");
		Arguments.f_Insert("-c");
		Arguments.f_Insert(CStr::fs_ToStr(_Changelist));
		Arguments.f_Insert(_Jobs);

		fp_Run("fix", Arguments);

		if (m_pClient->m_bError)
		{
			fOnError();
			return false;
		}
		return true;
	}

	bool CPerforceClient::f_DeleteChangelist(uint32 _Changelist, bool _bForce)
	{
		DCheckApi(CStr::CFormat("DeleteChangelist({}, {})") << _Changelist << _bForce);

		TCVector<CStr> Arguments;

		if (_bForce)
			Arguments.f_Insert("-f");
		Arguments.f_Insert("-d");
		Arguments.f_Insert(CStr::fs_ToStr(_Changelist));

		fp_Run("change", Arguments);

		if (m_pClient->m_bError)
		{
			fOnError();
			return false;
		}
		return true;
	}

	bool CPerforceClient::f_SubmitChangelist(uint32 _Changelist, bool _bSubmitShelved, uint32 &_FinalChangelist)
	{
		DCheckApi(CStr::CFormat("SubmitChangelist({})") << _Changelist);

		TCVector<CStr> Arguments;

		if (_bSubmitShelved)
			Arguments.f_Insert("-e");
		else
			Arguments.f_Insert("-c");
		Arguments.f_Insert(CStr::fs_ToStr(_Changelist));

		fp_Run("submit", Arguments);

		if (m_pClient->m_bError)
		{
			fOnError();
			return false;
		}
		for (CStr const &CurInfo : m_pClient->m_Infos)
		{
			CStr Command;
			CStr Data;
			(CStr::CParse("{} {}") >> Command >> Data).f_Parse(CurInfo);

			if (Command == "submittedChange")
				_FinalChangelist = Data.f_ToInt(uint32(0));
		}

		return true;
	}

	bool CPerforceClient::f_UnshelveWithBranch(uint32 _SourceChangelist, CStr const &_BranchMapping, uint32 _DestinationChangeList)
	{
		DCheckApi(CStr::CFormat("UnshelveWithBranch({}, {})") << _SourceChangelist << _BranchMapping);

		TCVector<CStr> Arguments;
		Arguments.f_Insert("-s");
		Arguments.f_Insert(CStr::fs_ToStr(_SourceChangelist));

		Arguments.f_Insert("-b");
		Arguments.f_Insert(_BranchMapping);

		if (_DestinationChangeList != 0)
		{
			Arguments.f_Insert("-c");
			Arguments.f_Insert(CStr::fs_ToStr(_DestinationChangeList));
		}

		fp_Run("unshelve", Arguments);

		if (m_pClient->m_bError)
		{
			fOnError();
			return false;
		}
		CStr Errors;
		for (CStr const &CurInfo : m_pClient->m_Infos)
		{
			CStr Command;
			CStr Data;
			(CStr::CParse("{} {}") >> Command >> Data).f_Parse(CurInfo);

			if (Data.f_StartsWith("- can't unshelve"))
				fg_AddStrSep(Errors, Data, "\r\n");
		}
		if (!Errors.f_IsEmpty())
		{
			m_LastError = Errors;
			fOnError();
			return false;
		}
		return true;
	}

	bool CPerforceClient::f_UnshelveInto(uint32 _SourceChangelist, uint32 _DestinationChangelist)
	{
		DCheckApi(CStr::CFormat("UnshelveInto({}, {})") << _SourceChangelist << _DestinationChangelist);

		TCVector<CStr> Arguments;
		Arguments.f_Insert("-s");
		Arguments.f_Insert(CStr::fs_ToStr(_SourceChangelist));

		Arguments.f_Insert("-c");
		Arguments.f_Insert(CStr::fs_ToStr(_DestinationChangelist));

		fp_Run("unshelve", Arguments);

		if (m_pClient->m_bError)
		{
			fOnError();
			return false;
		}
		CStr Errors;
		for (CStr const &CurInfo : m_pClient->m_Infos)
		{
			CStr Command;
			CStr Data;
			(CStr::CParse("{} {}") >> Command >> Data).f_Parse(CurInfo);

			if (Data.f_StartsWith("- can't unshelve"))
				fg_AddStrSep(Errors, Data, "\r\n");
		}
		if (!Errors.f_IsEmpty())
		{
			m_LastError = Errors;
			fOnError();
			return false;
		}
		return true;
	}

	bool CPerforceClient::f_MoveToChangelist(TCVector<CStr> const &_Files, uint32 _ChangeList)
	{
		DCheckApi(CStr::CFormat("MoveToChangelist({})") << _ChangeList);

		TCVector<CStr> Arguments;

		Arguments.f_Insert("-c");
		Arguments.f_Insert(CStr::fs_ToStr(_ChangeList));
		Arguments.f_Insert(_Files);

		fp_Run("reopen", Arguments);

		if (m_pClient->m_bError)
		{
			fOnError();
			return false;
		}
		return true;
	}

	bool CPerforceClient::f_ShelveChangelist(uint32 _Changelist, bool _bReplaceFiles, bool _bForce, TCVector<CStr> const &_Files)
	{
		DCheckApi(CStr::CFormat("ShelveChangelist({}, {}, {})") << _Changelist << _bReplaceFiles << _bForce);

		TCVector<CStr> Arguments;
		if (_bReplaceFiles)
			Arguments.f_Insert("-r");

		if (_bForce)
			Arguments.f_Insert("-f");

		Arguments.f_Insert("-c");
		Arguments.f_Insert(CStr::fs_ToStr(_Changelist));
		Arguments.f_Insert(_Files);

		fp_Run("shelve", Arguments);

		if (m_pClient->m_bError)
		{
			fOnError();
			return false;
		}
		return true;
	}

	bool CPerforceClient::f_GetClients(CStr const &_SearchPattern, CStr const &_Stream, CStr const &_User, TCFunction<void (CStr const &_Client, CStr const &_Key, CStr const &_Value)> const &_Processor)
	{
		DCheckApi(CStr::CFormat("GetClients({}, {}, {})") << _SearchPattern << _Stream << _User);

		TCVector<CStr> Arguments;
		if (!_SearchPattern.f_IsEmpty())
		{
			Arguments.f_Insert("-e");
			Arguments.f_Insert(_SearchPattern);
		}

		if (!_Stream.f_IsEmpty())
		{
			Arguments.f_Insert("-S");
			Arguments.f_Insert(_Stream);
		}

		if (!_User.f_IsEmpty())
		{
			Arguments.f_Insert("-u");
			Arguments.f_Insert(_User);
		}

		fp_Run("clients", Arguments);

		if (m_pClient->m_bError)
		{
			fOnError();
			return false;
		}

		CStr CurrentClient;
		for (auto Iter = m_pClient->m_Infos.f_GetIterator(); Iter; ++Iter)
		{
			auto &Info = *Iter;
			CStr Command;
			CStr Data;
			(CStr::CParse("{} {}") >> Command >> Data).f_Parse(Info);
			if (Command == "client")
				CurrentClient = Data;

			if (!CurrentClient.f_IsEmpty())
				_Processor(CurrentClient, Command, Data);
		}
		return true;
	}

	bool CPerforceClient::f_GetClients(CStr const &_SearchPattern, TCVector<CStr> &_Clients)
	{
		DCheckApi(CStr::CFormat("GetClients({})") << _SearchPattern);

		char const * Commands[] = {"-e", (ch8*)_SearchPattern.f_GetStr()};

		if (_SearchPattern.f_IsEmpty())
			m_pAPI->SetArgv( 0, (char* const*)Commands);
		else
			m_pAPI->SetArgv( 2, (char* const*)Commands);

		fp_Run("clients");

		if (m_pClient->m_bError)
		{
			fOnError();
			return false;
		}

		for (auto Iter = m_pClient->m_Infos.f_GetIterator(); Iter; ++Iter)
		{
			auto &Info = *Iter;
			CStr Command;
			CStr Data;
			(CStr::CParse("{} {}") >> Command >> Data).f_Parse(Info);
			if (Command == "client")
				_Clients.f_Insert(Data);
		}
		return true;
	}

	bool CPerforceClient::f_GetClient(CStr const &_ClientName, TCFunction<void (CStr const &_Key, CStr const &_Value)> const &_Processor)
	{
		TCVector<CStr> Clients;
		if (!f_GetClients(_ClientName, Clients) || Clients.f_IsEmpty())
		{
			if (f_GetLastError().f_IsEmpty())
				m_LastError = "Client not found";
			return false;
		}

		DCheckApi(CStr::CFormat("GetClient({})") << _ClientName);

		char const * Commands[] = {"-o", nullptr};
		Commands[1] = (ch8*)_ClientName.f_GetStr();

		m_pAPI->SetArgv( 2, (char* const*)Commands);

		fp_Run("client");
		if (m_pClient->m_bError)
		{
			fOnError();
			return false;
		}

		for (auto Iter = m_pClient->m_Infos.f_GetIterator(); Iter; ++Iter)
		{
			CStr Command;
			CStr Data;
			(CStr::CParse("{} {}") >> Command >> Data).f_Parse(*Iter);
			_Processor(Command, Data);
		}
		return true;
	}

	bool CPerforceClient::f_GetClient(CStr const &_ClientName, CStr &_Contents)
	{
		TCVector<CStr> Clients;
		if (!f_GetClients(_ClientName, Clients) || Clients.f_IsEmpty())
		{
			if (f_GetLastError().f_IsEmpty())
				m_LastError = "Client not found";
			return false;
		}

		DCheckApi(CStr::CFormat("GetClient({})") << _ClientName);

		char const * Commands[] = {"-o", nullptr};
		Commands[1] = (ch8*)_ClientName.f_GetStr();

		m_pAPI->SetArgv( 2, (char* const*)Commands);

		fp_Run("client");
		if (m_pClient->m_bError)
		{
			fOnError();
			return false;
		}

		for (auto Iter = m_pClient->m_Infos.f_GetIterator(); Iter; ++Iter)
		{
			CStr Command;
			CStr Data;
			_Contents += *Iter;
			_Contents += "\r\n";
		}
		return true;
	}


	bool CPerforceClient::f_UpdateStreamClient(CStr const &_ClientName, CStr const &_Root, CStr const &_AltRoot, CStr const &_Template, CStr const &_Stream, TCVector<CStr> const *_pOptions)
	{
		CStr ClientData;
		CStr Dummy;
		if (!f_GetClient(_ClientName, Dummy))
			return false;

		CStr LastCommand;
		TCVector<CStr> ExtraTags;
		for (auto Iter = m_pClient->m_Infos.f_GetIterator(); Iter; ++Iter)
		{
			auto &Info = *Iter;
			CStr Command;
			CStr Data;
			(CStr::CParse("{} {}") >> Command >> Data).f_Parse(Info);
			if (Command == "Client")
			{
				ClientData += "Client:\r\n";
				ClientData += "\t";
				ClientData += _ClientName;
				ClientData += "\r\n";
				ClientData += "Stream:\r\n";
				ClientData += "\t";
				ClientData += _Stream;
				ClientData += "\r\n";
				if (!_Root.f_IsEmpty())
				{
					ClientData += "Root:\r\n";
					ClientData += "\t";
					ClientData += _Root;
					ClientData += "\r\n";
				}
				if (!_AltRoot.f_IsEmpty())
				{
					ClientData += "AltRoots:\r\n";
					ClientData += "\t";
					ClientData += _AltRoot;
					ClientData += "\r\n";
				}
				if (!m_ConnectionInfo.m_Host.f_IsEmpty())
					ClientData += CStr::CFormat("Host:\t{}\r\n\r\n") << m_ConnectionInfo.m_Host;
				if (!m_ConnectionInfo.m_User.f_IsEmpty())
					ClientData += CStr::CFormat("Owner:\t{}\r\n\r\n") << m_ConnectionInfo.m_User;

				if (_pOptions)
				{
					ClientData += "Options:";
					for (auto &Option : *_pOptions)
						ClientData += CStr::CFormat(" {}") << Option;
					ClientData += "\r\n";
				}

			}
			else if (Command == "Update")
				;
			else if (Command == "Root" && !_Root.f_IsEmpty())
				;
			else if (Command.f_StartsWith("AltRoots"))
				;
			else if (Command == "Access")
				;
			else if (Command == "Host")
				;
			else if (Command == "Options" && _pOptions)
				;
			else if (Command == "Owner")
				;
			else if (Command == "Stream")
				;
			else if (Command.f_StartsWith("View"))
				;
			else if (Command.f_StartsWith("ChangeView"))
				;
			else if (Command.f_StartsWith("extraTag"))
			{
				ExtraTags.f_Insert(Data);
			}
			else if (!Command.f_IsEmpty())
			{
				bool bExtraTag = false;
				for (auto const &ExtraTag : ExtraTags)
				{
					if (Command.f_StartsWith(ExtraTag))
					{
						bExtraTag = true;
						break;
					}
				}
				if (!bExtraTag)
				{
					if (LastCommand != Command)
					{
						ClientData += Command;
						ClientData += ":\r\n";
					}
					ClientData += "\t";
					ClientData += Data;
					ClientData += "\r\n";
				}
			}
			LastCommand = Command;
		}

		DCheckApi(CStr::CFormat("UpdateStreamClient({}, {}, {}, {}, {})") << _ClientName << _Root << _AltRoot << _Template << _Stream);

		char const * Commands[] = {"-i", nullptr};
		m_pClient->m_PromtOverride = ClientData;
		m_pAPI->SetArgv( 1, (char* const*)Commands );
		fp_Run("client");
		if (m_pClient->m_bError)
		{
			fOnError();
			return false;
		}

		return true;
	}

	bool CPerforceClient::f_GetBranchForStreams(CStr const &_From, CStr const &_To, CPerforceClient::CBranchSpec &_oBranch)
	{
		DCheckApi(CStr::CFormat("GetBranchForStreams({}, {})") << _From << _To);

		TCVector<CStr> Arguments;
		Arguments.f_Insert("-P");
		Arguments.f_Insert(_To);
		Arguments.f_Insert("-S");
		Arguments.f_Insert(_From);
		Arguments.f_Insert("-o");
		Arguments.f_Insert("Dummy");

		fp_Run("branch", Arguments);

		if (m_pClient->m_bError)
		{
			fOnError();
			return false;
		}

		for (auto Iter = m_pClient->m_Infos.f_GetIterator(); Iter; ++Iter)
		{
			auto &Info = *Iter;
			CStr Command;
			CStr Data;
			(CStr::CParse("{} {}") >> Command >> Data).f_Parse(Info);

			if (Command.f_StartsWith("View"))
			{
				auto &Mapping = _oBranch.m_View.f_Insert();
				Mapping.m_From = fg_GetStrSepEscaped(Data, " ");
				Mapping.m_To = fg_GetStrSepEscaped(Data, " ");
				if (Mapping.m_From.f_StartsWith("-"))
				{
					Mapping.m_bNegative = true;
					Mapping.m_From = Mapping.m_From.f_Extract(1);
				}
			}
		}

		return true;
	}

	bool CPerforceClient::f_GetClient(CStr const &_Client, CPerforceClient::CClient &_oClient)
	{
		TCVector<CStr> Clients;
		if (!f_GetClients(_Client, Clients) || Clients.f_IsEmpty())
		{
			if (f_GetLastError().f_IsEmpty())
				m_LastError = "Client not found";
			return false;
		}

		DCheckApi(CStr::CFormat("GetClient({})") << _Client);

		TCVector<CStr> Arguments;
		Arguments.f_Insert("-o");
		Arguments.f_Insert(_Client);

		fp_Run("client", Arguments);

		if (m_pClient->m_bError)
		{
			fOnError();
			return false;
		}

		for (auto Iter = m_pClient->m_Infos.f_GetIterator(); Iter; ++Iter)
		{
			auto &Info = *Iter;
			CStr Command;
			CStr Data;
			(CStr::CParse("{} {}") >> Command >> Data).f_Parse(Info);

			if (Command == "Update")
				_oClient.m_UpdateTime = Data;
			else if (Command == "Access")
				_oClient.m_AccessTime = Data;
			else if (Command == "Owner")
				_oClient.m_Owner = Data;
			else if (Command == "Host")
				_oClient.m_Host = Data;
			else if (Command == "Description")
				_oClient.m_Description = Data;
			else if (Command == "Root")
				_oClient.m_Root = Data;
			else if (Command == "AltRoot0")
				_oClient.m_AltRoot0 = Data;
			else if (Command == "AltRoot1")
				_oClient.m_AltRoot1 = Data;
			else if (Command == "Options")
			{
				while (!Data.f_IsEmpty())
					_oClient.m_Options.f_Insert(fg_GetStrSep(Data, " "));
			}
			else if (Command == "LineEnd")
				_oClient.m_LineEndings = Data;
			else if (Command == "Stream")
				_oClient.m_Stream = Data;
			else if (Command.f_StartsWith("View"))
			{
				auto &Mapping = _oClient.m_View.f_Insert();
				Mapping.m_From = fg_GetStrSepEscaped(Data, " ");
				Mapping.m_To = fg_GetStrSepEscaped(Data, " ");
				if (Mapping.m_From.f_StartsWith("-"))
				{
					Mapping.m_bNegative = true;
					Mapping.m_From = Mapping.m_From.f_Extract(1);
				}
			}
		}

		return true;
	}


	bool CPerforceClient::f_CreateBranch(CStr const &_Name, CBranchSpec const &_BranchSpec)
	{
		DCheckApi(CStr::CFormat("CreateBranch({})") << _Name);

		CStr Contents;
		fg_AppendFormat(Contents, "Branch: {}{\n}", _Name);
		fg_AppendFormat(Contents, "{\n}");
		if (!_BranchSpec.m_Owner.f_IsEmpty())
		{
			fg_AppendFormat(Contents, "Owner:  Erik_Olofsson{\n}", _BranchSpec.m_Owner);
			fg_AppendFormat(Contents, "{\n}");
		}
		if (!_BranchSpec.m_Description.f_IsEmpty())
		{
			fg_AppendFormat(Contents, "Description:{\n}");
			CStr Description = _BranchSpec.m_Description;
			while (!Description.f_IsEmpty())
			{
				CStr Line = fg_GetStrLineSep(Description);
				fg_AppendFormat(Contents, "	{}{\n}", Line);
			}
			fg_AppendFormat(Contents, "{\n}");
		}
		if (!_BranchSpec.m_Options.f_IsEmpty())
		{
			fg_AppendFormat(Contents, "Options:");
			for (auto iOption = _BranchSpec.m_Options.f_GetIterator(); iOption; ++iOption)
				fg_AppendFormat(Contents, " {}", *iOption);
			fg_AppendFormat(Contents, "{\n}");
			fg_AppendFormat(Contents, "{\n}");
		}
		if (!_BranchSpec.m_View.f_IsEmpty())
		{
			fg_AppendFormat(Contents, "View:{\n}");
			for (auto iView = _BranchSpec.m_View.f_GetIterator(); iView; ++iView)
			{
				if (iView->m_bNegative)
					fg_AppendFormat(Contents, "\t{} {}{\n}", ("-" + iView->m_From).f_EscapeStr(), iView->m_To.f_EscapeStr());
				else
					fg_AppendFormat(Contents, "\t{} {}{\n}", iView->m_From.f_EscapeStr(), iView->m_To.f_EscapeStr());
			}
			fg_AppendFormat(Contents, "{\n}");
		}

		TCVector<CStr> Arguments;
		Arguments.f_Insert("-i");

		m_pClient->m_PromtOverride = Contents;

		fp_Run("branch", Arguments);

		if (m_pClient->m_bError)
		{
			fOnError();
			return false;
		}
		return true;
	}

	bool CPerforceClient::f_DeleteBranch(CStr const &_Name)
	{
		DCheckApi(CStr::CFormat("CreateBranch({})") << _Name);

		TCVector<CStr> Arguments;
		Arguments.f_Insert("-d");
		Arguments.f_Insert(_Name);

		fp_Run("branch", Arguments);

		if (m_pClient->m_bError)
		{
			fOnError();
			return false;
		}
		return true;
	}

	bool CPerforceClient::f_UpdateClient(CStr const &_ClientName, CStr const &_Root, CStr const &_AltRoot, CStr const &_Template)
	{
		CStr ClientData;
		CStr Dummy;
		if (!f_GetClient(_ClientName, Dummy))
			return false;

		CStr LastCommand;
		TCVector<CStr> ExtraTags;
		for (auto Iter = m_pClient->m_Infos.f_GetIterator(); Iter; ++Iter)
		{
			auto &Info = *Iter;
			CStr Command;
			CStr Data;

			(CStr::CParse("{} {}") >> Command >> Data).f_Parse(Info);

			if (Command.f_StartsWith("View"))
				Command = "View";

			if (Command == "Client")
			{
				ClientData += "Client:\r\n";
				ClientData += "\t";
				ClientData += _ClientName;
				ClientData += "\r\n";
				if (!_Root.f_IsEmpty())
				{
					ClientData += "Root:\r\n";
					ClientData += "\t";
					ClientData += _Root;
					ClientData += "\r\n";
				}
				if (!_AltRoot.f_IsEmpty())
				{
					ClientData += "AltRoots:\r\n";
					ClientData += "\t";
					ClientData += _AltRoot;
					ClientData += "\r\n";
				}
				if (!m_ConnectionInfo.m_Host.f_IsEmpty())
					ClientData += CStr::CFormat("Host:\t{}\r\n\r\n") << m_ConnectionInfo.m_Host;
				if (!m_ConnectionInfo.m_User.f_IsEmpty())
					ClientData += CStr::CFormat("Owner:\t{}\r\n\r\n") << m_ConnectionInfo.m_User;
			}
			else if (Command == "Update")
				;
			else if (Command == "Root" && !_Root.f_IsEmpty())
				;
			else if (Command.f_StartsWith("AltRoots"))
				;
			else if (Command == "Access")
				;
			else if (Command == "Host")
				;
			else if (Command == "Owner")
				;
			else if (Command == "Stream")
				;
			else if (Command.f_StartsWith("extraTag"))
			{
				ExtraTags.f_Insert(Data);
			}
			else if (!Command.f_IsEmpty())
			{
				bool bExtraTag = false;
				for (auto const &ExtraTag : ExtraTags)
				{
					if (Command.f_StartsWith(ExtraTag))
					{
						bExtraTag = true;
						break;
					}
				}
				if (!bExtraTag)
				{
					if (LastCommand != Command)
					{
						ClientData += Command;
						ClientData += ":\r\n";
					}
					ClientData += "\t";
					ClientData += Data;
					ClientData += "\r\n";
				}
			}
			LastCommand = Command;
		}

		DCheckApi(CStr::CFormat("UpdateClient({}, {}, {}, {})") << _ClientName << _Root << _AltRoot << _Template);

		char const * Commands[] = {"-i", nullptr};
		m_pClient->m_PromtOverride = ClientData;
		m_pAPI->SetArgv( 1, (char* const*)Commands );
		fp_Run("client");
		if (m_pClient->m_bError)
		{
			fOnError();
			return false;
		}

		return true;
	}

	bool CPerforceClient::f_CreateStreamClient(CStr const &_ClientName, CStr const &_Root, CStr const &_AltRoot, CStr const &_Template, CStr const &_Stream, TCVector<CStr> const *_pOptions)
	{
		CStr ClientData;
		CStr Dummy;
		if (!f_GetClient(_Template, Dummy))
			return false;

		CStr LastCommand;
		TCVector<CStr> ExtraTags;
		for (auto Iter = m_pClient->m_Infos.f_GetIterator(); Iter; ++Iter)
		{
			auto &Info = *Iter;
			CStr Command;
			CStr Data;
			(CStr::CParse("{} {}") >> Command >> Data).f_Parse(Info);
			if (Command == "Client")
			{
				ClientData += "Client:\r\n";
				ClientData += "\t";
				ClientData += _ClientName;
				ClientData += "\r\n";
				ClientData += "Stream:\r\n";
				ClientData += "\t";
				ClientData += _Stream;
				ClientData += "\r\n";
				if (!_Root.f_IsEmpty())
				{
					ClientData += "Root:\r\n";
					ClientData += "\t";
					ClientData += _Root;
					ClientData += "\r\n";
				}
				if (!_AltRoot.f_IsEmpty())
				{
					ClientData += "AltRoots:\r\n";
					ClientData += "\t";
					ClientData += _AltRoot;
					ClientData += "\r\n";
				}
				if (!m_ConnectionInfo.m_Host.f_IsEmpty())
					ClientData += CStr::CFormat("Host:\t{}\r\n\r\n") << m_ConnectionInfo.m_Host;
				if (!m_ConnectionInfo.m_User.f_IsEmpty())
					ClientData += CStr::CFormat("Owner:\t{}\r\n\r\n") << m_ConnectionInfo.m_User;
				if (_pOptions)
				{
					ClientData += "Options:";
					for (auto &Option : *_pOptions)
						ClientData += CStr::CFormat(" {}") << Option;
					ClientData += "\r\n";
				}

			}
			else if (Command == "Update")
				;
			else if (Command == "Root" && !_Root.f_IsEmpty())
				;
			else if (Command.f_StartsWith("AltRoots"))
				;
			else if (Command == "Access")
				;
			else if (Command == "Options" && _pOptions)
				;
			else if (Command == "Host")
				;
			else if (Command == "Owner")
				;
			else if (Command == "Stream")
				;
			else if (Command.f_StartsWith("View"))
				;
			else if (Command.f_StartsWith("ChangeView"))
				;
			else if (Command.f_StartsWith("extraTag"))
			{
				ExtraTags.f_Insert(Data);
			}
			else if (!Command.f_IsEmpty())
			{
				bool bExtraTag = false;
				for (auto const &ExtraTag : ExtraTags)
				{
					if (Command.f_StartsWith(ExtraTag))
					{
						bExtraTag = true;
						break;
					}
				}
				if (!bExtraTag)
				{
					if (LastCommand != Command)
					{
						ClientData += Command;
						ClientData += ":\r\n";
					}
					ClientData += "\t";
					ClientData += Data;
					ClientData += "\r\n";
				}
			}
			LastCommand = Command;
		}

		DCheckApi(CStr::CFormat("CreateStreamClient({}, {}, {}, {}, {})") << _ClientName << _Root << _AltRoot << _Template << _Stream);

		char const * Commands[] = {"-i", nullptr};
		m_pClient->m_PromtOverride = ClientData;
		m_pAPI->SetArgv( 1, (char* const*)Commands );
		fp_Run("client");
		if (m_pClient->m_bError)
		{
			fOnError();
			return false;
		}

		return true;
	}

	bool CPerforceClient::f_CreateClient(CStr const &_ClientName, CStr const &_Root, CStr const &_AltRoot, CStr const &_Template)
	{
		CStr ClientData;
		CStr Dummy;
		if (!f_GetClient(_Template, Dummy))
			return false;

		CStr LastCommand;
		TCVector<CStr> ExtraTags;
		for (auto Iter = m_pClient->m_Infos.f_GetIterator(); Iter; ++Iter)
		{
			auto &Info = *Iter;
			CStr Command;
			CStr Data;

			(CStr::CParse("{} {}") >> Command >> Data).f_Parse(Info);

			if (Command.f_StartsWith("View"))
				Command = "View";

			if (Command == "Client")
			{
				ClientData += "Client:\r\n";
				ClientData += "\t";
				ClientData += _ClientName;
				ClientData += "\r\n";
				if (!_Root.f_IsEmpty())
				{
					ClientData += "Root:\r\n";
					ClientData += "\t";
					ClientData += _Root;
					ClientData += "\r\n";
				}
				if (!_AltRoot.f_IsEmpty())
				{
					ClientData += "AltRoots:\r\n";
					ClientData += "\t";
					ClientData += _AltRoot;
					ClientData += "\r\n";
				}
				if (!m_ConnectionInfo.m_Host.f_IsEmpty())
					ClientData += CStr::CFormat("Host:\t{}\r\n\r\n") << m_ConnectionInfo.m_Host;
				if (!m_ConnectionInfo.m_User.f_IsEmpty())
					ClientData += CStr::CFormat("Owner:\t{}\r\n\r\n") << m_ConnectionInfo.m_User;
			}
			else if (Command == "Update")
				;
			else if (Command == "Root" && !_Root.f_IsEmpty())
				;
			else if (Command.f_StartsWith("AltRoots"))
				;
			else if (Command == "Host")
				;
			else if (Command == "Owner")
				;
			else if (Command == "Access")
				;
			else if (Command == "Stream")
				;
			else if (Command.f_StartsWith("extraTag"))
			{
				ExtraTags.f_Insert(Data);
			}
			else if (!Command.f_IsEmpty())
			{
				bool bExtraTag = false;
				for (auto const &ExtraTag : ExtraTags)
				{
					if (Command.f_StartsWith(ExtraTag))
					{
						bExtraTag = true;
						break;
					}
				}
				if (!bExtraTag)
				{
					if (LastCommand != Command)
					{
						ClientData += Command;
						ClientData += ":\r\n";
					}
					ClientData += "\t";
					if (Command == "View")
						ClientData += Data.f_Replace(_Template, _ClientName);
					else
						ClientData += Data;
					ClientData += "\r\n";
				}
			}
			LastCommand = Command;
		}

		DCheckApi(CStr::CFormat("CreateClient({}, {}, {}, {})") << _ClientName << _Root << _AltRoot << _Template);

		char const * Commands[] = {"-i", nullptr};
		m_pClient->m_PromtOverride = ClientData;
		m_pAPI->SetArgv( 1, (char* const*)Commands );
		fp_Run("client");
		if (m_pClient->m_bError)
		{
			fOnError();
			return false;
		}

		return true;
	}

	bool CPerforceClient::f_GetWorkspacePath(CStr const& _DepotPath, CStr &_WorkspacePath)
	{
		DCheckApi(CStr::CFormat("GetWorkspacePath({})") << _DepotPath);
		char const * Commands[] = {nullptr};
		Commands[0] = (ch8*)_DepotPath.f_GetStr();
		m_pAPI->SetArgv( 1, (char* const*)Commands );
		fp_Run("where");
		bool bRemove = false;
		if (m_pClient->m_bError)
		{
			DCheckApi(CStr::CFormat("GetWorkspacePath({})") << _DepotPath);
			bRemove = true;
			CStr Path = _DepotPath + "/{CEF99DB4-836A-4495-B6DF-4C5FBFE7B443}";
			Commands[0] = (ch8*)Path.f_GetStr();
			m_pAPI->SetArgv( 1, (char* const*)Commands );
			fp_Run("where");
			if (m_pClient->m_bError)
			{
				fOnError();
				return false;
			}
		}

		mint nInfo = m_pClient->m_Infos.f_GetLen();
		CStr CurInfo;
		for (mint iI = 0; iI < nInfo; iI++)
		{
			CurInfo = m_pClient->m_Infos[iI];

			CStr Command;
			CStr Data;
			(CStr::CParse("{} {}") >> Command >> Data).f_Parse(CurInfo);

			if (Command == "clientFile")
			{
				if (bRemove)
					_WorkspacePath = Data.f_Left(Data.f_GetLen() - 39);
				else
					_WorkspacePath = Data;
				return true;
			}
		}

		m_LastError = "Depot file not found";
		fOnError();
		return false;
	}

	bool CPerforceClient::f_GetClientPath(CStr const& _DepotPath, CStr &_ClientPath)
	{
		DCheckApi(CStr::CFormat("GetClientPath({})") << _DepotPath);

		char const * Commands[] = {nullptr};
		Commands[0] = (ch8*)_DepotPath.f_GetStr();
		m_pAPI->SetArgv( 1, (char* const*)Commands );
		fp_Run("where");
		bool bRemove = false;
		if (m_pClient->m_bError)
		{
			DCheckApi(CStr::CFormat("GetClientPath({})") << _DepotPath);
			bRemove = true;
			CStr Path = _DepotPath + "/{CEF99DB4-836A-4495-B6DF-4C5FBFE7B443}";
			Commands[0] = (ch8*)Path.f_GetStr();
			m_pAPI->SetArgv( 1, (char* const*)Commands );
			fp_Run("where");
			if (m_pClient->m_bError)
			{
				fOnError();
				return false;
			}
		}

		mint nInfo = m_pClient->m_Infos.f_GetLen();
		CStr CurInfo;
		for (mint iI = 0; iI < nInfo; iI++)
		{
			CurInfo = m_pClient->m_Infos[iI];

			CStr Command;
			CStr Data;
			(CStr::CParse("{} {}") >> Command >> Data).f_Parse(CurInfo);

			if (Command == "path")
			{
				if (bRemove)
					_ClientPath = NFile::CFile::fs_GetMalterlibPath(Data.f_Left(Data.f_GetLen() - 39));
				else
					_ClientPath = NFile::CFile::fs_GetMalterlibPath(Data);
				return true;
			}
		}

		m_LastError = "Depot file not found";
		fOnError();
		return false;
	}

	bool CPerforceClient::f_GetDepotPath(CStr const& _ClientPath, CStr &_DepotPath, bool _bReturnImportedStream)
	{
		DCheckApi(CStr::CFormat("GetDepotPath({}, {})") << _ClientPath << _bReturnImportedStream);

		char const * Commands[] = {nullptr};
		Commands[0] = (ch8*)_ClientPath.f_GetStr();
		m_pAPI->SetArgv( 1, (char* const*)Commands );
		fp_Run("where");
		bool bRemove = false;
		if (m_pClient->m_bError)
		{
			DCheckApi(CStr::CFormat("GetDepotPath({}, {})") << _ClientPath << _bReturnImportedStream);
			bRemove = true;
			CStr Path = _ClientPath + "/{CEF99DB4-836A-4495-B6DF-4C5FBFE7B443}";
			Commands[0] = (ch8*)Path.f_GetStr();
			m_pAPI->SetArgv( 1, (char* const*)Commands );
			fp_Run("where");
			if (m_pClient->m_bError)
			{
				fOnError();
				return false;
			}
		}

		mint nInfo = m_pClient->m_Infos.f_GetLen();
		CStr CurInfo;
		bool bRet = false;
		for (mint iI = 0; iI < nInfo; iI++)
		{
			CurInfo = m_pClient->m_Infos[iI];

			CStr Command;
			CStr Data;
			(CStr::CParse("{} {}") >> Command >> Data).f_Parse(CurInfo);

			if (Command == "depotFile")
			{
				if (bRemove)
					_DepotPath = Data.f_Left(Data.f_GetLen() - 39);
				else
					_DepotPath = Data;
				bRet = true;
				if (!_bReturnImportedStream)
					break;
			}
		}

		if (!bRet)
		{
			m_LastError = "Client path not found";
			fOnError();
		}
		return bRet;
	}


	bool CPerforceClient::f_GetTextFileContents(CStr const &_Path, CStr &_Contents)
	{
		DCheckApi(CStr::CFormat("GetTextFileContents({})") << _Path);

		char const * Commands[] = {"-q", nullptr};
		Commands[1] = (ch8*)_Path.f_GetStr();
		m_pAPI->SetArgv( 2, (char* const*)Commands );
		m_pClient->m_OutputText.f_Clear();
		m_pClient->m_OutputTextRaw.f_Clear();
		fp_Run("print");

		CByteVector Temp;
		Temp.f_Insert((uint8 *)m_pClient->m_OutputTextRaw.f_GetStr(), m_pClient->m_OutputTextRaw.f_GetLen());

		CStr Result = NFile::CFile::fs_ReadStringFromVector(Temp);

		if (m_pClient->m_bError)
		{
			fOnError();
			return false;
		}

		_Contents = Result;

		return true;
	}

	bool CPerforceClient::f_JobExists(const CStr &_Job)
	{
		DCheckApi(CStr::CFormat("JobExists({})") << _Job);
		CStr Temp = CStr("job=") + f_EncodeStr(_Job);
		char const * Commands[] = {"-e", (ch8 *)Temp.f_GetStr()};
		m_pAPI->SetArgv( 2, (char* const*)Commands );
		fp_Run("jobs");

		if (m_pClient->m_bError)
		{
			fOnError();
			return false;
		}
		else
		{
			return m_pClient->m_Infos.f_GetLen() > 0;
		}
	}

	bool CPerforceClient::f_SetJob(const CStr &_Job)
	{
		DCheckApi(CStr::CFormat("SetJob({})") << _Job);
		m_pClient->m_PromtOverride = _Job;
		char const * Commands[] = {"-i", "-f"};
		m_pAPI->SetArgv( 2, (char* const*)Commands );
		fp_Run("job");

		if (m_pClient->m_bError)
		{
			fOnError();
			return false;
		}
		else
		{
			return true;
		}
	}

	bool CPerforceClient::f_GetJob(const CStr &_Job, CJob &_Ret)
	{
		DCheckApi(CStr::CFormat("GetJob({})") << _Job);
		CStr Temp = CStr::fs_ToStr(_Job);
		char const * Commands[] = {"-o", (ch8 *)Temp.f_GetStr()};

		m_pAPI->SetArgv( 2, (char* const*)Commands );
		fp_Run("job");
		if (m_pClient->m_bError)
		{
			fOnError();
			return false;
		}
		else
		{
			mint nInfo = m_pClient->m_Infos.f_GetLen();
			for (mint i = 0; i < nInfo; ++i)
			{
				const CStr &Info = m_pClient->m_Infos[i];
				if (!Info.f_IsEmpty())
				{
					CStr Command;
					CStr Data;
					(CStr::CParse("{} {}") >> Command >> Data).f_Parse(Info);

					if (Command == "Date")
					{
						_Ret.m_Date = Data.f_ToInt(uint64(0));
					}
					else if (Command == "User")
					{
						_Ret.m_User = Data;
					}
					else if (Command == "Status")
					{
						_Ret.m_Status = Data;
					}
				}
			}
			return true;
		}
	}

	bool CPerforceClient::f_DeleteJob(const CStr &_Job)
	{
		DCheckApi(CStr::CFormat("DeleteJob({})") << _Job);
		CStr Temp = f_EncodeStr(_Job);
		char const * Commands[] = {"-d", (ch8 *)Temp.f_GetStr()};
		m_pAPI->SetArgv( 2, (char* const*)Commands );
		fp_Run("job");

		if (m_pClient->m_bError)
		{
			fOnError();
			return false;
		}
		else
		{
			return true;
		}
	}

	bool CPerforceClient::f_DescribeShelved(uint32 _Changelist, CDescription &_Ret)
	{
		DCheckApi(CStr::CFormat("DescribeShelved({})") << _Changelist);

		TCVector<CStr> Arguments;
		Arguments.f_Insert("-s");
		Arguments.f_Insert("-S");
		Arguments.f_Insert(CStr::fs_ToStr(_Changelist));

		fp_Run("describe", Arguments);
		if (m_pClient->m_bError)
		{
			fOnError();
			return false;
		}
		else
		{
			CDescription::CFile *pLastFile = nullptr;
			mint nInfo = m_pClient->m_Infos.f_GetLen();
			for (mint i = 0; i < nInfo; ++i)
			{
				const CStr &Info = m_pClient->m_Infos[i];
				if (!Info.f_IsEmpty())
				{
					CStr Command;
					CStr Data;
					(CStr::CParse("{} {}") >> Command >> Data).f_Parse(Info);

					if (Command == "user")
					{
						_Ret.m_User = Data;
					}
					else if (Command == "status")
					{
						_Ret.m_ChangelistStatus = Data;
					}
					else if (Command.f_StartsWith("jobstat"))
					{
						_Ret.m_JobStatuses.f_Insert(Data);
					}
					else if (Command.f_StartsWith("job"))
					{
						_Ret.m_Jobs.f_Insert(Data);
					}
					else if (Command.f_StartsWith("depotFile"))
					{
						pLastFile = &_Ret.m_Files.f_Insert();
						pLastFile->m_Name = Data;
					}
					else if (Command.f_StartsWith("action"))
					{
						if (pLastFile)
							pLastFile->m_Action = fg_ConvertAction(Data);
					}

				}
			}
			return true;
		}
	}

	bool CPerforceClient::f_Describe(uint32 _Changelist, CDescription &_Ret)
	{
		DCheckApi(CStr::CFormat("Describe({})") << _Changelist);
		CStr Temp = CStr::fs_ToStr(_Changelist);
		char const * Commands[] = {"-s", (ch8 *)Temp.f_GetStr()};

		m_pAPI->SetArgv( 2, (char* const*)Commands );
		fp_Run("describe");
		if (m_pClient->m_bError)
		{
			fOnError();
			return false;
		}
		else
		{
			CDescription::CFile *pLastFile = nullptr;

			mint nInfo = m_pClient->m_Infos.f_GetLen();
			for (mint i = 0; i < nInfo; ++i)
			{
				const CStr &Info = m_pClient->m_Infos[i];
				if (!Info.f_IsEmpty())
				{
					CStr Command;
					CStr Data;
					(CStr::CParse("{} {}") >> Command >> Data).f_Parse(Info);

					if (Command == "user")
					{
						_Ret.m_User = Data;
					}
					else if (Command == "status")
					{
						_Ret.m_ChangelistStatus = Data;
					}
					else if (Command.f_Find("jobstat") == 0)
					{
						_Ret.m_JobStatuses.f_Insert(Data);
					}
					else if (Command.f_Find("job") == 0)
					{
						_Ret.m_Jobs.f_Insert(Data);
					}
					else if (Command.f_StartsWith("depotFile"))
					{
						pLastFile = &_Ret.m_Files.f_Insert();
						pLastFile->m_Name = Data;
					}
					else if (Command.f_StartsWith("action"))
					{
						if (pLastFile)
							pLastFile->m_Action = fg_ConvertAction(Data);
					}
				}
			}
			return true;
		}


	}

	bool CPerforceClient::f_SetJobSpec(const CStr &_JobSpec)
	{
		DCheckApi("SetJobSpec");
		m_pClient->m_PromtOverride = _JobSpec;
		char const * Commands[] = {"-i"};
		m_pAPI->SetArgv( 1, (char* const*)Commands );
		fp_Run("jobspec");

		if (m_pClient->m_bError)
		{
			fOnError();
			return false;
		}
		else
		{
			return true;
		}
	}

	bool CPerforceClient::f_SetTriggers(const CStr &_Triggers)
	{
		DCheckApi("SetTriggers");
		m_pClient->m_PromtOverride = _Triggers;
		char const * Commands[] = {"-i"};
		m_pAPI->SetArgv( 1, (char* const*)Commands );
		fp_Run("triggers");

		if (m_pClient->m_bError)
		{
			fOnError();
			return false;
		}
		else
		{
			return true;
		}
	}

	bool CPerforceClient::f_Info()
	{
		DCheckApi("Info");
		char const * Commands[] = {0};
		m_pAPI->SetArgv( 0, (char* const*)Commands );
		fp_Run("info");
		if (m_pClient->m_bError)
		{
			fOnError();
			return false;
		}
		else
		{
			return true;
		}
	}

	bool CPerforceClient::f_GetUserName(CStr &_UserName)
	{
		DCheckApi(CStr::CFormat("GetUserName({})") << _UserName);
		char const * Commands[] = {0};
		m_pAPI->SetArgv( 0, (char* const*)Commands );
		fp_Run("info");

		if (m_pClient->m_bError)
		{
			fOnError();
			return false;
		}
		else
		{
			_UserName = m_pClient->GetInfo("userName");
			return true;
		}
	}

	bool CPerforceClient::f_GetClientName(CStr &_ClientName)
	{
		DCheckApi("GetClientName");
		char const * Commands[] = {0};
		m_pAPI->SetArgv( 0, (char* const*)Commands );
		fp_Run("info");

		if (m_pClient->m_bError)
		{
			fOnError();
			return false;
		}
		else
		{
			_ClientName = m_pClient->GetInfo("clientName");
			return true;
		}
	}

	bool CPerforceClient::f_GetClientRoot(CStr &_Root)
	{
		DCheckApi("GetClientRoot");
		char const * Commands[] = {0};
		m_pAPI->SetArgv( 0, (char* const*)Commands );
		fp_Run("info");

		if (m_pClient->m_bError)
		{
			fOnError();
			return false;
		}
		else
		{
			_Root = m_pClient->GetInfo("clientRoot");
			return true;
		}
	}

	bool CPerforceClient::f_OpenForEdit(CStr const &_File)
	{
		DCheckApi(CStr::CFormat("OpenForEdit({})") << _File);
		char const * Commands[] = {(ch8 *)_File.f_GetStr()};
		m_pAPI->SetArgv( 1, (char* const*)Commands );
		fp_Run("edit");
		if (m_pClient->m_bError)
		{
			fOnError();
			return false;
		}
		else
		{
			return true;
		}
	}

	bool CPerforceClient::f_CreateChangelist(CStr const &_Comment, TCVector<CStr> const &_Jobs, TCVector<CStr> const &_Files, uint32 &_ChangeList)
	{
		DCheckApi("CreateChangelist");

		CStr ChangeListContents;

		ChangeListContents += "Change:	new\r\n";
		ChangeListContents += "\r\n";
		ChangeListContents += CStr::CFormat("Client:	{}\r\n") << m_ActiveClient;
		ChangeListContents += "\r\n";
		ChangeListContents += CStr::CFormat("User:	{}\r\n") << m_ActiveUser;
		ChangeListContents += "\r\n";
		ChangeListContents += "Status:	new\r\n";
		ChangeListContents += "\r\n";
		ChangeListContents += "Description:\r\n";
		ChangeListContents += fs_FixLineStartingTabs(_Comment);
		ChangeListContents += "\r\n";
		ChangeListContents += "\r\n";
		if (!_Jobs.f_IsEmpty())
		{
			ChangeListContents += "Jobs:\r\n";
			for (auto iJob = _Jobs.f_GetIterator(); iJob; ++iJob)
				ChangeListContents += CStr::CFormat("\t{}\r\n") << *iJob;
			ChangeListContents += "\r\n";
		}
		if (!_Files.f_IsEmpty())
		{
			ChangeListContents += "Files:\r\n";
			for (auto iFile = _Files.f_GetIterator(); iFile; ++iFile)
				ChangeListContents += CStr::CFormat("\t{}\r\n") << *iFile;
			ChangeListContents += "\r\n";
		}

		m_pClient->m_PromtOverride = ChangeListContents;

		char const * Commands[] = {"-i"};
		m_pAPI->SetArgv( 1, (char* const*)Commands );
		fp_Run("change");

		if (m_pClient->m_bError)
		{
			fOnError();
			return false;
		}
		else
		{
			for (CStr const &CurInfo : m_pClient->m_Infos)
			{
				CStr Command;
				CStr Data;
				(CStr::CParse("{} {}") >> Command >> Data).f_Parse(CurInfo);
				if (Command == "Change")
					_ChangeList = Data.f_ToInt(uint32(0));
				//DConOut("{}={}", Command << Data);
			}
			return true;
		}
	}

	bool CPerforceClient::f_Submit(CStr const &_File, CStr const &_Comment, CStr const &_Job)
	{
		DCheckApi(CStr::CFormat("Submit({}, {})") << _File << _Job);

		if (_Job.f_IsEmpty())
		{
			char const * Commands[] = {"-d", (ch8*)_Comment.f_GetStr(), (ch8 *)_File.f_GetStr()};
			m_pAPI->SetArgv( 3, (char* const*)Commands );
			fp_Run("submit");
			if (m_pClient->m_bError)
			{
				fOnError();
				return false;
			}
			else
			{
				return true;
			}
		}
		else
		{
			CStr Form =
				"Change:	new\r\n"
				"\r\n"
				"Client:	{}\r\n"
				"\r\n"
				"User:	{}\r\n"
				"\r\n"
				"Status:	new\r\n"
				"\r\n"
				"Description:\r\n"
				"{}\r\n"
				"\r\n"
				"Jobs:\r\n"
				"\t{} same"
				"\r\n"
				"Files:\r\n"
				"\t{}";

			CStr FileDepotPath;
			bool bExistsInDepot = f_GetDepotPath(_File, FileDepotPath);
			if (!bExistsInDepot)
				return false;

			m_pClient->m_PromtOverride = CStr::CFormat(Form) << m_ActiveClient << m_ActiveUser << fs_FixLineStartingTabs(_Comment) << _Job << FileDepotPath;

			CStr JobName = f_EncodeStr(_Job);
			char const * Commands[] = {"-i", "-s", "same"};
			m_pAPI->SetArgv( 3, (char* const*)Commands );
			fp_Run("submit");

			if (m_pClient->m_bError)
			{
				fOnError();
				return false;
			}
			else
			{
				return true;
			}
		}
	}

	bool CPerforceClient::f_RevertChangelist(uint32 _Changelist, bool _bOnlyIfUnchanged)
	{
		DCheckApi(CStr::CFormat("RevertChangelist({}, {})") << _Changelist << _bOnlyIfUnchanged);

		TCVector<CStr> Arguments;
		if (_bOnlyIfUnchanged)
			Arguments.f_Insert("-a");
		Arguments.f_Insert("-c");
		Arguments.f_Insert(CStr::fs_ToStr(_Changelist));
		if (!_bOnlyIfUnchanged)
			Arguments.f_Insert("//...");

		fp_Run("revert", Arguments);
		if (m_pClient->m_bError)
		{
			fOnError();
			return false;
		}
		else
		{
			return true;
		}
	}


	bool CPerforceClient::f_Revert(CStr const &_File, bool _bOnlyIfUnchanged)
	{
		DCheckApi(CStr::CFormat("Revert({}, {})") << _File << _bOnlyIfUnchanged);

		TCVector<CStr> Arguments;
		if (_bOnlyIfUnchanged)
			Arguments.f_Insert("-a");
		Arguments.f_Insert(_File);

		fp_Run("revert", Arguments);
		if (m_pClient->m_bError)
		{
			fOnError();
			return false;
		}
		else
		{
			return true;
		}
	}




	bool CPerforceClient::f_Add(CStr const &_File)
	{
		DCheckApi(CStr::CFormat("Add({})") << _File);
		char const * Commands[] = {(ch8 *)_File.f_GetStr()};
		m_pAPI->SetArgv( 1, (char* const*)Commands );
		fp_Run("add");
		if (m_pClient->m_bError)
		{
			fOnError();
			return false;
		}
		else
		{
			return true;
		}
	}

	bool CPerforceClient::f_Delete(CStr const &_File)
	{
		DCheckApi(CStr::CFormat("Delete({})") << _File);
		TCVector<CStr> Commands;
		Commands.f_Insert(_File);
		fp_Run("delete", Commands);
		if (m_pClient->m_bError)
		{
			fOnError();
			return false;
		}
		else
		{
			return true;
		}
	}

	bool CPerforceClient::f_OpenForEditOrMakeWritable(CStr const &_File)
	{
		bool bRet = true;
		if (NFile::CFile::fs_FileExists(_File))
		{
			if (f_CanOpenForEdit(_File))
			{
				f_OpenForEdit(_File);
			}

			NFile::CFile::fs_MakeFileWritable(_File);
		}

		return bRet;
	}

	bool CPerforceClient::f_TryOpenForEdit(CStr const &_File)
	{
		bool bRet = true;
		if (NFile::CFile::fs_FileExists(_File))
		{
			if (f_CanOpenForEdit(_File))
			{
				f_OpenForEdit(_File);
			}

			if (!NFile::CFile::fs_IsFileWritable(_File))
				bRet = false;
		}
		return bRet;
	}

	bool CPerforceClient::f_FindHeadFiles(CStr const &_Pattern, TCVector<CFileRevision> &_lFiles)
	{
		DCheckApi(CStr::CFormat("FindHeadFiles({})") << _Pattern);
		char const * Commands[] = {nullptr};
		Commands[0] = (ch8*)_Pattern.f_GetStr();
		m_pAPI->SetArgv( 1, (char* const*)Commands );
		fp_Run("files");
		if (m_pClient->m_bError)
		{
			fOnError();
			return false;
		}

		mint nInfo = m_pClient->m_Infos.f_GetLen();
		CStr CurInfo;
		CFileRevision* pCurRev = nullptr;
		for (mint iI = 0; iI < nInfo; iI++)
		{
			CurInfo = m_pClient->m_Infos[iI];

			CStr Command;
			CStr Data;
			(CStr::CParse("{} {}") >> Command >> Data).f_Parse(CurInfo);

			if (Command == "depotFile")
			{
				pCurRev = &_lFiles.f_Insert();
				pCurRev->m_File = Data;
			}
			else if (pCurRev)
			{
				if (Command == "rev")
				{
					pCurRev->m_Revision = Data.f_ToInt((int32)0);
				}
				else if (Command == "change")
				{
					pCurRev->m_ChangeList = Data.f_ToInt((uint32)0);
				}
				else if (Command == "action")
				{
					pCurRev->m_Action = fg_ConvertAction(Data);
				}
				else if (Command == "time")
				{
					pCurRev->m_Time = Data.f_ToInt((uint64)0);
				}
			}
		}

		return true;

		/*
		DTraceRaw("f_FindHeadFiles:\n");
		mint nInfo = m_pClient->m_Infos.f_GetLen();
		for (mint iI = 0; iI < nInfo; ++iI)
		{
			DTrace("{}\n", m_pClient->m_Infos[iI].f_GetStr());
		}

		return false;
		*/
	}

	CStr CPerforceClient::f_EncodeStr(CStr const &_Str)
	{
		if (m_bUTF8)
			return _Str;

		CAnsiStr Temp;
		NMib::NStr::NPlatform::fg_SystemEncodeAnsiStr(_Str, Temp, '?');;

		CStr Out;
		Out.f_AddStr(Temp.f_GetStr(), Temp.f_GetLen());
		return Out;
	}

	CStr CPerforceClient::f_DecodeStr(CStr const &_Str)
	{
		if (m_bUTF8)
			return _Str;
		else
		{
			CAnsiStr Temp;
			Temp.f_AddStr(_Str.f_GetStr(), _Str.f_GetLen());
			CStr Out;
			try
			{
				NMib::NStr::NPlatform::fg_SystemDecodeAnsiStr(Temp, Out);;
			}
			catch (CException const &)
			{
				return _Str; // No conversion performed
			}
			return Out;
		}
	}


	CPerforceClientThrow::CPerforceClientThrow(CStr const &_Server, CStr const &_User, CStr const &_Client, CStr const &_Host)
		: mp_Client(_Server, _User, _Client, _Host)
	{
	}
	CPerforceClientThrow::CPerforceClientThrow(CPerforceClient::CConnectionInfo const& _Info)
		: mp_Client(_Info)
	{
	}

	void CPerforceClientThrow::f_ThrowLastError() const
	{
		fp_Throw(false);
	}

	void CPerforceClientThrow::fp_Throw(bool _bResult) const
	{
		if (!_bResult)
		{
	//		while (true)
	//			NSys::fg_Thread_Sleep(1.0);
			DError(fg_Format("Perforce function '{}' failed with: {}", mp_Client.f_GetLastFunction(), mp_Client.f_GetLastError()));
		}
	}

	CStr const &CPerforceClientThrow::f_GetHost() const
	{
		return mp_Client.f_GetHost();
	}

	CStr const &CPerforceClientThrow::f_GetServer() const
	{
		return mp_Client.f_GetServer();
	}

	CStr const &CPerforceClientThrow::f_GetUser() const
	{
		return mp_Client.f_GetUser();
	}

	CStr const &CPerforceClientThrow::f_GetClient() const
	{
		return mp_Client.f_GetClient();
	}

	CPerforceClient::CConnectionInfo const &CPerforceClientThrow::f_GetConnectionInfo() const
	{
		return mp_Client.f_GetConnectionInfo();
	}

	bool CPerforceClientThrow::f_IsUTF8()
	{
		return mp_Client.f_IsUTF8();
	}

	void CPerforceClientThrow::f_Login(CStr const &_Password, CStr const &_WorkingDir)
	{
		fp_Throw(mp_Client.f_Login(_Password, _WorkingDir));
	}
	CStr CPerforceClientThrow::f_GetSecurityLevel()
	{
		CStr Ret;
		fp_Throw(mp_Client.f_GetSecurityLevel(Ret));
		return Ret;
	}

	bool CPerforceClientThrow::f_FileExistsInDepot(CStr const &_File)
	{
		bool bRet = mp_Client.f_FileExistsInDepot(_File);
		CStr Error = mp_Client.f_GetLastError();
		if (!Error.f_IsEmpty())
			fp_Throw(false);
		return bRet;
	}
	bool CPerforceClientThrow::f_FileExistsInDepotNotDeleted(CStr const &_File)
	{
		bool bRet = mp_Client.f_FileExistsInDepotNotDeleted(_File);
		CStr Error = mp_Client.f_GetLastError();
		if (!Error.f_IsEmpty())
			fp_Throw(false);
		return bRet;
	}
	bool CPerforceClientThrow::f_FileExistsInChangeList(CStr const &_File)
	{
		bool bRet = mp_Client.f_FileExistsInChangeList(_File);
		CStr Error = mp_Client.f_GetLastError();
		if (!Error.f_IsEmpty())
			fp_Throw(false);
		return bRet;
	}
	bool CPerforceClientThrow::f_FileExists(CStr const &_File)
	{
		bool bRet = mp_Client.f_FileExists(_File);
		CStr Error = mp_Client.f_GetLastError();
		if (!Error.f_IsEmpty())
			fp_Throw(false);
		return bRet;
	}
	bool CPerforceClientThrow::f_CanOpenForEdit(CStr const &_File)
	{
		bool bRet = mp_Client.f_CanOpenForEdit(_File);
		CStr Error = mp_Client.f_GetLastError();
		if (!Error.f_IsEmpty())
			fp_Throw(false);
		return bRet;
	}

	void CPerforceClientThrow::f_RemoveFromClient(CStr const &_File)
	{
		fp_Throw(mp_Client.f_RemoveFromClient(_File));
	}
	void CPerforceClientThrow::f_Sync(CStr const &_File, TCVector<CStr> &_Synced, TCVector<CStr> &_Removed, bool _bPretend)
	{
		fp_Throw(mp_Client.f_Sync(_File, _Synced, _Removed, _bPretend));
	}
	void CPerforceClientThrow::f_Sync(CStr const &_File, TCFunction<bool (int64 _TotalBytes, int64 _SyncedBytes)> const &_Progress, bool _bForce, TCVector<CStr> const &_MoreFiles)
	{
		fp_Throw(mp_Client.f_Sync(_File, _Progress, _bForce, _MoreFiles));
	}
	int64 CPerforceClientThrow::f_GetHeadChangelist(CStr const& _Path)
	{
		int64 Ret;
		fp_Throw(mp_Client.f_GetHeadChangelist(Ret, _Path));
		return Ret;
	}
	int64 CPerforceClientThrow::f_GetHeadChangelistUnsafe()
	{
		int64 Ret;
		fp_Throw(mp_Client.f_GetHeadChangelistUnsafe(Ret));
		return Ret;
	}
	TCVector<CStr> CPerforceClientThrow::f_Files(CStr const &_Search)
	{
		TCVector<CStr> Ret;
		fp_Throw(mp_Client.f_Files(_Search, Ret));
		return Ret;
	}
	TCVector<CStr> CPerforceClientThrow::f_Files(CStr const &_Search, TCVector<CStr> &_Deleted)
	{
		TCVector<CStr> Ret;
		fp_Throw(mp_Client.f_Files(_Search, Ret, _Deleted));
		return Ret;
	}
	TCVector<CStr> CPerforceClientThrow::f_ClientFiles(CStr const &_Search)
	{
		TCVector<CStr> Ret;
		fp_Throw(mp_Client.f_ClientFiles(_Search, Ret));
		return Ret;
	}
	CStr CPerforceClientThrow::f_GetJobSpec()
	{
		CStr Ret;
		fp_Throw(mp_Client.f_GetJobSpec(Ret));
		return Ret;
	}
	CRegistry CPerforceClientThrow::f_GetJobs(CStr const &_JobView)
	{
		CRegistry Ret;
		fp_Throw(mp_Client.f_GetJobs(Ret, _JobView));
		return Ret;
	}
	CStr CPerforceClientThrow::f_GetTriggers()
	{
		CStr Ret;
		fp_Throw(mp_Client.f_GetTriggers(Ret));
		return Ret;
	}
	CRegistry CPerforceClientThrow::f_GetUsers()
	{
		CRegistry Ret;
		fp_Throw(mp_Client.f_GetUsers(Ret));
		return Ret;
	}
	CStr CPerforceClientThrow::f_GetDepotPath(CStr const& _ClientPath, bool _bReturnImportedStream)
	{
		CStr Ret;
		fp_Throw(mp_Client.f_GetDepotPath(_ClientPath, Ret, _bReturnImportedStream));
		return Ret;
	}
	CStr CPerforceClientThrow::f_GetClientPath(CStr const& _DepotPath)
	{
		CStr Ret;
		fp_Throw(mp_Client.f_GetClientPath(_DepotPath, Ret));
		return Ret;
	}
	CStr CPerforceClientThrow::f_GetWorkspacePath(CStr const& _DepotPath)
	{
		CStr Ret;
		fp_Throw(mp_Client.f_GetWorkspacePath(_DepotPath, Ret));
		return Ret;
	}
	CStr CPerforceClientThrow::f_GetTextFileContents(CStr const& _Path)
	{
		CStr Ret;
		fp_Throw(mp_Client.f_GetTextFileContents(_Path, Ret));
		return Ret;
	}
	TCVector<CStr> CPerforceClientThrow::f_GetStreams()
	{
		TCVector<CStr> Ret;
		fp_Throw(mp_Client.f_GetStreams(Ret));
		return Ret;
	}
	TCVector<CStr> CPerforceClientThrow::f_GetStreamDepots()
	{
		TCVector<CStr> Ret;
		fp_Throw(mp_Client.f_GetStreamDepots(Ret));
		return Ret;
	}

	CPerforceClient::CChangeList CPerforceClientThrow::f_GetChangelist(uint32 _ChangeList)
	{
		CPerforceClient::CChangeList Ret;
		fp_Throw(mp_Client.f_GetChangelist(_ChangeList, Ret));
		return Ret;
	}

	void CPerforceClientThrow::f_SetChangelistOwner(uint32 _ChangeList, CStr const &_User)
	{
		fp_Throw(mp_Client.f_SetChangelistOwner(_ChangeList, _User));
	}

	void CPerforceClientThrow::f_SetChangelistClient(uint32 _ChangeList, CStr const &_Client, bool _bForce)
	{
		fp_Throw(mp_Client.f_SetChangelistClient(_ChangeList, _Client, _bForce));
	}
	void CPerforceClientThrow::f_SetChangelistDescription(uint32 _ChangeList, CStr const &_Description, bool _bForce)
	{
		fp_Throw(mp_Client.f_SetChangelistDescription(_ChangeList, _Description, _bForce));
	}

	TCVector<CPerforceClient::CChangeList> CPerforceClientThrow::f_GetChangelists(CStr const &_Path, bool _bIncludeIntegrated, CStr const &_Workspace, CStr const &_Status)
	{
		TCVector<CPerforceClient::CChangeList> Ret;
		fp_Throw(mp_Client.f_GetChangelists(_Path, Ret, _bIncludeIntegrated, _Workspace, _Status));
		return Ret;
	}

	CPerforceClient::CFileRevisions CPerforceClientThrow::f_GetFileRevisions(CStr const &_File)
	{
		CPerforceClient::CFileRevisions Ret;
		fp_Throw(mp_Client.f_GetFileRevisions(_File, Ret));
		return Ret;
	}

	CPerforceClient::CFileRevisions CPerforceClientThrow::f_GetFileRevisions(TCVector<CStr> const &_Files)
	{
		CPerforceClient::CFileRevisions Ret;
		fp_Throw(mp_Client.f_GetFileRevisions(_Files, Ret));
		return Ret;
	}

	bool CPerforceClientThrow::f_ChangeExists(const CPerforceClient::CFix &_Fix)
	{
		bool bRet = mp_Client.f_ChangeExists(_Fix);
		CStr Error = mp_Client.f_GetLastError();
		if (!Error.f_IsEmpty())
			fp_Throw(false);
		return bRet;
	}

	TCVector<CPerforceClient::CFix> CPerforceClientThrow::f_GetFixes(CStr const &_Job, uint64 _PerforceGUID)
	{
		TCVector<CPerforceClient::CFix> Ret;
		fp_Throw(mp_Client.f_GetFixes(_Job, Ret, _PerforceGUID));
		return Ret;
	}
	void CPerforceClientThrow::f_AddFixes(CStr const &_Job, const TCVector<uint32> &_Fixes, CStr const &_Status)
	{
		fp_Throw(mp_Client.f_AddFixes(_Job, _Fixes, _Status));
	}
	void CPerforceClientThrow::f_RemoveFixes(CStr const &_Job, const TCVector<uint32> &_Fixes)
	{
		fp_Throw(mp_Client.f_RemoveFixes(_Job, _Fixes));
	}
	void CPerforceClientThrow::f_SetJobSpec(const CStr &_JobSpec)
	{
		fp_Throw(mp_Client.f_SetJobSpec(_JobSpec));
	}
	void CPerforceClientThrow::f_SetJob(const CStr &_Job)
	{
		fp_Throw(mp_Client.f_SetJob(_Job));
	}
	CPerforceClient::CJob CPerforceClientThrow::f_GetJob(const CStr &_Job)
	{
		CPerforceClient::CJob Ret;
		fp_Throw(mp_Client.f_GetJob(_Job, Ret));
		return Ret;
	}
	void CPerforceClientThrow::f_SetTriggers(const CStr &_Triggers)
	{
		fp_Throw(mp_Client.f_SetTriggers(_Triggers));
	}
	bool CPerforceClientThrow::f_JobExists(const CStr &_Job)
	{
		bool bRet = mp_Client.f_JobExists(_Job);
		CStr Error = mp_Client.f_GetLastError();
		if (!Error.f_IsEmpty())
			fp_Throw(false);
		return bRet;
	}
	void CPerforceClientThrow::f_DeleteJob(const CStr &_Job)
	{
		fp_Throw(mp_Client.f_DeleteJob(_Job));
	}
	CPerforceClient::CDescription CPerforceClientThrow::f_Describe(uint32 _Changelist)
	{
		CPerforceClient::CDescription Ret;
		fp_Throw(mp_Client.f_Describe(_Changelist, Ret));
		return Ret;
	}
	CPerforceClient::CDescription CPerforceClientThrow::f_DescribeShelved(uint32 _Changelist)
	{
		CPerforceClient::CDescription Ret;
		fp_Throw(mp_Client.f_DescribeShelved(_Changelist, Ret));
		return Ret;
	}
	void CPerforceClientThrow::f_Info()
	{
		fp_Throw(mp_Client.f_Info());
	}
	CStr CPerforceClientThrow::f_GetUserName()
	{
		CStr Ret;
		fp_Throw(mp_Client.f_GetUserName(Ret));
		return Ret;
	}
	CStr CPerforceClientThrow::f_GetClientName()
	{
		CStr Ret;
		fp_Throw(mp_Client.f_GetClientName(Ret));
		return Ret;
	}
	CStr CPerforceClientThrow::f_GetClientRoot()
	{
		CStr Ret;
		fp_Throw(mp_Client.f_GetClientRoot(Ret));
		return Ret;
	}
	void CPerforceClientThrow::f_OpenForEdit(CStr const &_File)
	{
		fp_Throw(mp_Client.f_OpenForEdit(_File));
	}
	void CPerforceClientThrow::f_Submit(CStr const &_File, CStr const &_Comment, CStr const &_Job)
	{
		fp_Throw(mp_Client.f_Submit(_File, _Comment, _Job));
	}
	void CPerforceClientThrow::f_Revert(CStr const &_File, bool _bOnlyIfUnchanged)
	{
		fp_Throw(mp_Client.f_Revert(_File, _bOnlyIfUnchanged));
	}
	void CPerforceClientThrow::f_RevertChangelist(uint32 _Changelist, bool _bOnlyIfUnchanged)
	{
		fp_Throw(mp_Client.f_RevertChangelist(_Changelist, _bOnlyIfUnchanged));
	}

	void CPerforceClientThrow::f_Add(CStr const &_File)
	{
		fp_Throw(mp_Client.f_Add(_File));
	}

	void CPerforceClientThrow::f_Delete(CStr const &_File)
	{
		fp_Throw(mp_Client.f_Delete(_File));
	}

	void CPerforceClientThrow::f_OpenForEditOrMakeWritable(CStr const &_File)
	{
		fp_Throw(mp_Client.f_OpenForEditOrMakeWritable(_File));
	}
	void CPerforceClientThrow::f_TryOpenForEdit(CStr const &_File)
	{
		fp_Throw(mp_Client.f_TryOpenForEdit(_File));
	}

	uint32 CPerforceClientThrow::f_CreateChangelist(CStr const &_Comment, TCVector<CStr> const &_Jobs, TCVector<CStr> const &_Files)
	{
		uint32 Ret = 0;
		fp_Throw(mp_Client.f_CreateChangelist(_Comment, _Jobs, _Files, Ret));
		return Ret;
	}

	TCVector<CPerforceClient::CFileRevision> CPerforceClientThrow::f_FindHeadFiles(CStr const& _Pattern)
	{
		TCVector<CPerforceClient::CFileRevision> Ret;
		fp_Throw(mp_Client.f_FindHeadFiles(_Pattern, Ret));
		return Ret;
	}

	TCVector<CStr> CPerforceClientThrow::f_GetClients(CStr const &_SearchPattern)
	{
		TCVector<CStr> Ret;
		fp_Throw(mp_Client.f_GetClients(_SearchPattern, Ret));
		return Ret;
	}

	void CPerforceClientThrow::f_GetClients(CStr const &_SearchPattern, CStr const &_Stream, CStr const &_User, TCFunction<void (CStr const &_Client, CStr const &_Key, CStr const &_Value)> const &_Processor)
	{
		fp_Throw(mp_Client.f_GetClients(_SearchPattern, _Stream, _User, _Processor));
	}

	CStr CPerforceClientThrow::f_GetClient(CStr const &_ClientName)
	{
		CStr Ret;
		fp_Throw(mp_Client.f_GetClient(_ClientName, Ret));
		return Ret;
	}

	CPerforceClient::CClient CPerforceClientThrow::f_GetClientScruct(CStr const &_Client)
	{
		CPerforceClient::CClient Ret;
		fp_Throw(mp_Client.f_GetClient(_Client, Ret));
		return Ret;
	}


	void CPerforceClientThrow::f_UnshelveWithBranch(uint32 _SourceChangelist, CStr const &_BranchMapping, uint32 _DestinationChangeList)
	{
		fp_Throw(mp_Client.f_UnshelveWithBranch(_SourceChangelist, _BranchMapping, _DestinationChangeList));
	}

	void CPerforceClientThrow::f_GetClient(CStr const &_ClientName, TCFunction<void (CStr const &_Key, CStr const &_Value)> const &_Processor)
	{
		fp_Throw(mp_Client.f_GetClient(_ClientName, _Processor));
	}

	void CPerforceClientThrow::f_ShelveChangelist(uint32 _Changelist, bool _bReplaceFiles, bool _bForce, TCVector<CStr> const &_Files)
	{
		fp_Throw(mp_Client.f_ShelveChangelist(_Changelist, _bReplaceFiles, _bForce, _Files));
	}

	void CPerforceClientThrow::f_MoveToChangelist(TCVector<CStr> const &_Files, uint32 _ChangeList)
	{
		fp_Throw(mp_Client.f_MoveToChangelist(_Files, _ChangeList));
	}

	void CPerforceClientThrow::f_UnshelveInto(uint32 _SourceChangelist, uint32 _DestinationChangelist)
	{
		fp_Throw(mp_Client.f_UnshelveInto(_SourceChangelist, _DestinationChangelist));
	}
	uint32 CPerforceClientThrow::f_SubmitChangelist(uint32 _Changelist, bool _bSubmitShelved)
	{
		uint32 Ret = 0;
		fp_Throw(mp_Client.f_SubmitChangelist(_Changelist, _bSubmitShelved, Ret));
		return Ret;
	}
	void CPerforceClientThrow::f_DeleteChangelist(uint32 _Changelist, bool _bForce)
	{
		fp_Throw(mp_Client.f_DeleteChangelist(_Changelist, _bForce));
	}
	void CPerforceClientThrow::f_RemoveJobsFromChangelist(uint32 _Changelist, TCVector<CStr> const &_Jobs)
	{
		fp_Throw(mp_Client.f_RemoveJobsFromChangelist(_Changelist, _Jobs));
	}
	void CPerforceClientThrow::f_DeleteShelvedFile(uint32 _Changelist, CStr const &_File, bool _bForce)
	{
		fp_Throw(mp_Client.f_DeleteShelvedFile(_Changelist, _File, _bForce));
	}
	void CPerforceClientThrow::f_ResolveSafe(CStr const &_File, uint32 _ChangeList)
	{
		fp_Throw(mp_Client.f_ResolveSafe(_File, _ChangeList));
	}
	void CPerforceClientThrow::f_ResolveAutomatic(CStr const &_File, uint32 _ChangeList)
	{
		fp_Throw(mp_Client.f_ResolveAutomatic(_File, _ChangeList));
	}
	void CPerforceClientThrow::f_ResolveMine(CStr const &_File, uint32 _ChangeList)
	{
		fp_Throw(mp_Client.f_ResolveMine(_File, _ChangeList));
	}

	TCVector<CPerforceClient::CIntegrationResult> CPerforceClientThrow::f_CopyStream(CStr const &_FromStream, CStr const &_ToStream, bool _bPretend,  TCVector<CStr> &_oMustSync, TCVector<CPerforceClient::CMergeError> &_oErrors, CStr _ToFileSpec)
	{
		TCVector<CPerforceClient::CIntegrationResult> Ret;
		fp_Throw(mp_Client.f_CopyStream(_FromStream, _ToStream, _bPretend, Ret, _oMustSync, _oErrors, _ToFileSpec));
		return Ret;
	}
	TCVector<CPerforceClient::CIntegrationResult> CPerforceClientThrow::f_MergeStream(CStr const &_FromStream, CStr const &_ToStream, bool _bPretend, TCVector<CStr> &_oMustSync, TCVector<CPerforceClient::CMergeError> &_oErrors, CStr _ToFileSpec)
	{
		TCVector<CPerforceClient::CIntegrationResult> Ret;
		fp_Throw(mp_Client.f_MergeStream(_FromStream, _ToStream, _bPretend, Ret, _oMustSync, _oErrors, _ToFileSpec));
		return Ret;
	}
	TCVector<CPerforceClient::CIntegrationResult> CPerforceClientThrow::f_IntegrateStream(CStr const &_FromStream, CStr const &_ToStream, bool _bPretend, TCVector<CStr> &_oMustSync, TCVector<CPerforceClient::CMergeError> &_oErrors, CStr _ToFileSpec)
	{
		TCVector<CPerforceClient::CIntegrationResult> Ret;
		fp_Throw(mp_Client.f_IntegrateStream(_FromStream, _ToStream, _bPretend, Ret, _oMustSync, _oErrors, _ToFileSpec));
		return Ret;
	}
	TCVector<CPerforceClient::CIntegrationResult> CPerforceClientThrow::f_IntegrateFiles(CStr const &_FromFiles, CStr const &_ToFiles, bool _bPretend, bool _bEnableBaseless, TCVector<CStr> &_oMustSync, TCVector<CPerforceClient::CMergeError> &_oErrors)
	{
		TCVector<CPerforceClient::CIntegrationResult> Ret;
		fp_Throw(mp_Client.f_IntegrateFiles(_FromFiles, _ToFiles, _bPretend, _bEnableBaseless, Ret, _oMustSync, _oErrors));
		return Ret;
	}

	TCVector<CPerforceClient::CIntegrationResult> CPerforceClientThrow::f_CopyStreamToParent(CStr const &_FromStream, bool _bPretend, TCVector<CStr> &_oMustSync, TCVector<CPerforceClient::CMergeError> &_oErrors, CStr _ToFileSpec)
	{
		TCVector<CPerforceClient::CIntegrationResult> Ret;
		fp_Throw(mp_Client.f_CopyStreamToParent(_FromStream, _bPretend, Ret, _oMustSync, _oErrors, _ToFileSpec));
		return Ret;
	}
	TCVector<CPerforceClient::CIntegrationResult> CPerforceClientThrow::f_MergeStreamToParent(CStr const &_FromStream, bool _bPretend, TCVector<CStr> &_oMustSync, TCVector<CPerforceClient::CMergeError> &_oErrors, CStr _ToFileSpec)
	{
		TCVector<CPerforceClient::CIntegrationResult> Ret;
		fp_Throw(mp_Client.f_MergeStreamToParent(_FromStream, _bPretend, Ret, _oMustSync, _oErrors, _ToFileSpec));
		return Ret;
	}
	TCVector<CPerforceClient::CIntegrationResult> CPerforceClientThrow::f_IntegrateStreamToParent(CStr const &_FromStream, bool _bPretend, TCVector<CStr> &_oMustSync, TCVector<CPerforceClient::CMergeError> &_oErrors, CStr _ToFileSpec)
	{
		TCVector<CPerforceClient::CIntegrationResult> Ret;
		fp_Throw(mp_Client.f_IntegrateStreamToParent(_FromStream, _bPretend, Ret, _oMustSync, _oErrors, _ToFileSpec));
		return Ret;
	}

	TCVector<CPerforceClient::CIntegrationResult> CPerforceClientThrow::f_CopyStreamFromParent(CStr const &_ToStream, bool _bPretend, TCVector<CStr> &_oMustSync, TCVector<CPerforceClient::CMergeError> &_oErrors, CStr _ToFileSpec)
	{
		TCVector<CPerforceClient::CIntegrationResult> Ret;
		fp_Throw(mp_Client.f_CopyStreamFromParent(_ToStream, _bPretend, Ret, _oMustSync, _oErrors, _ToFileSpec));
		return Ret;
	}
	TCVector<CPerforceClient::CIntegrationResult> CPerforceClientThrow::f_MergeStreamFromParent(CStr const &_ToStream, bool _bPretend, TCVector<CStr> &_oMustSync, TCVector<CPerforceClient::CMergeError> &_oErrors, CStr _ToFileSpec)
	{
		TCVector<CPerforceClient::CIntegrationResult> Ret;
		fp_Throw(mp_Client.f_MergeStreamFromParent(_ToStream, _bPretend, Ret, _oMustSync, _oErrors, _ToFileSpec));
		return Ret;
	}
	TCVector<CPerforceClient::CIntegrationResult> CPerforceClientThrow::f_IntegrateStreamFromParent(CStr const &_ToStream, bool _bPretend, TCVector<CStr> &_oMustSync, TCVector<CPerforceClient::CMergeError> &_oErrors, CStr _ToFileSpec)
	{
		TCVector<CPerforceClient::CIntegrationResult> Ret;
		fp_Throw(mp_Client.f_IntegrateStreamFromParent(_ToStream, _bPretend, Ret, _oMustSync, _oErrors, _ToFileSpec));
		return Ret;
	}

	CStr CPerforceClientThrow::f_CreatePatch(CStr const &_Branch, bool _bFullContext)
	{
		CStr Ret;
		fp_Throw(mp_Client.f_CreatePatch(_Branch, _bFullContext, Ret));
		return Ret;
	}

	CPerforceClient::CStream CPerforceClientThrow::f_GetStream(CStr const &_StreamName)
	{
		CPerforceClient::CStream Ret;
		fp_Throw(mp_Client.f_GetStream(_StreamName, Ret));
		return Ret;
	}

	void CPerforceClientThrow::f_DeleteStream(CStr const &_StreamName)
	{
		fp_Throw(mp_Client.f_DeleteStream(_StreamName));
	}

	void CPerforceClientThrow::f_Obliterate(CStr const &_Path)
	{
		fp_Throw(mp_Client.f_Obliterate(_Path));
	}

	bool CPerforceClientThrow::f_StreamExists(CStr const &_StreamName)
	{
		bool bRet = mp_Client.f_StreamExists(_StreamName);
		CStr Error = mp_Client.f_GetLastError();
		if (!Error.f_IsEmpty())
			fp_Throw(false);
		return bRet;
	}

	void CPerforceClientThrow::f_SetStream(CStr const &_StreamName, CPerforceClient::CStream const &_Stream)
	{
		fp_Throw(mp_Client.f_SetStream(_StreamName, _Stream));
	}

	void CPerforceClientThrow::f_PopulateStream(CStr const &_StreamName)
	{
		fp_Throw(mp_Client.f_PopulateStream(_StreamName));
	}

	TCVector<CStr> CPerforceClientThrow::f_FindStreams(CStr const &_SearchQuery, TCVector<CStr> const &_StreamSpecs)
	{
		TCVector<CStr> Ret;
		fp_Throw(mp_Client.f_FindStreams(_SearchQuery, Ret, _StreamSpecs));
		return Ret;
	}

	TCVector<CStr> CPerforceClientThrow::f_FindStreamsByViewMatch(TCVector<CStr> const &_Views)
	{
		TCVector<CStr> Ret;
		fp_Throw(mp_Client.f_FindStreamsByViewMatch(_Views, Ret));
		return Ret;
	}

	TCVector<CStr> CPerforceClientThrow::f_GetOpened(CStr const &_Path, CStr const &_Client)
	{
		TCVector<CStr> Ret;
		fp_Throw(mp_Client.f_GetOpened(_Path, _Client, Ret));
		return Ret;
	}

	void CPerforceClientThrow::f_SwitchWorkspaceStream(CStr const &_Workspace, CStr const &_Stream)
	{
		fp_Throw(mp_Client.f_SwitchWorkspaceStream(_Workspace, _Stream));
	}

	void CPerforceClientThrow::f_DeleteWorkspace(CStr const &_Workspace)
	{
		fp_Throw(mp_Client.f_DeleteWorkspace(_Workspace));
	}

	CStr CPerforceClientThrow::f_GetEnvVar(CStr const &_Var)
	{
		CStr Ret;
		fp_Throw(mp_Client.f_GetEnvVar(_Var, Ret));
		return Ret;
	}
	void CPerforceClientThrow::f_SetEnvVar(CStr const &_Var, CStr const &_Value)
	{
		fp_Throw(mp_Client.f_SetEnvVar(_Var, _Value));
	}

	CPerforceClient::CFileStats CPerforceClientThrow::f_FileStats(CStr const &_File)
	{
		CPerforceClient::CFileStats Ret;
		fp_Throw(mp_Client.f_FileStats(_File, Ret));
		return Ret;
	}

	void CPerforceClientThrow::f_CreateStreamClient(CStr const &_ClientName, CStr const &_Root, CStr const &_AltRoot, CStr const &_Template, CStr const &_Stream, TCVector<CStr> const *_pOptions)
	{
		fp_Throw(mp_Client.f_CreateStreamClient(_ClientName, _Root, _AltRoot, _Template, _Stream, _pOptions));
	}
	void CPerforceClientThrow::f_CreateClient(CStr const &_ClientName, CStr const &_Root, CStr const &_AltRoot, CStr const &_Template)
	{
		fp_Throw(mp_Client.f_CreateClient(_ClientName, _Root, _AltRoot, _Template));
	}
	void CPerforceClientThrow::f_UpdateStreamClient(CStr const &_ClientName, CStr const &_Root, CStr const &_AltRoot, CStr const &_Template, CStr const &_Stream, TCVector<CStr> const *_pOptions)
	{
		fp_Throw(mp_Client.f_UpdateStreamClient(_ClientName, _Root, _AltRoot, _Template, _Stream, _pOptions));
	}
	void CPerforceClientThrow::f_UpdateClient(CStr const &_ClientName, CStr const &_Root, CStr const &_AltRoot, CStr const &_Template)
	{
		fp_Throw(mp_Client.f_UpdateClient(_ClientName, _Root, _AltRoot, _Template));
	}

	CPerforceClient::CBranchSpec CPerforceClientThrow::f_GetBranchForStreams(CStr const &_From, CStr const &_To)
	{
		CPerforceClient::CBranchSpec Ret;
		fp_Throw(mp_Client.f_GetBranchForStreams(_From, _To, Ret));
		return Ret;
	}

	void CPerforceClientThrow::f_CreateBranch(CStr const &_Name, CPerforceClient::CBranchSpec const &_BranchSpec)
	{
		fp_Throw(mp_Client.f_CreateBranch(_Name, _BranchSpec));
	}

	void CPerforceClientThrow::f_DeleteBranch(CStr const &_Name)
	{
		fp_Throw(mp_Client.f_DeleteBranch(_Name));
	}

	bool CPerforceClientThrow::fs_GetFromP4Config(CStr const &_Path, CPerforceClientThrow &o_Client)
	{
		CStr P4Config = o_Client.f_GetEnvVar("P4CONFIG");

		if (P4Config.f_IsEmpty())
			return false;

		CStr Path = CFile::fs_GetPath(_Path);
		while (!Path.f_IsEmpty())
		{
			if (CFile::fs_FileExists(CFile::fs_AppendPath(Path, P4Config)))
			{
				// Perforce checkout
				o_Client.f_Login(CStr(), Path);
				return true;
			}

			Path = CFile::fs_GetPath(Path);
		}
		return false;
	}
}

