// Copyright © 2015 Hansoft AB
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#pragma once

#include <Mib/Container/Registry>

class ClientApi;

namespace NMib::NPerforce
{
	class CPerforceClient
	{
	public:
		enum EAction
		{
			EAction_Unknown,
			EAction_Add,
			EAction_Delete,
			EAction_Edit,
			EAction_Integrate,
			EAction_Branch,
		};

		struct CIntegrationResult
		{
			CIntegrationResult()
				: m_Action(EAction_Unknown)
			{
			}
			EAction m_Action;
			CStr m_From;
			CStr m_To;
			zuint32 m_StartFromRev;
			zuint32 m_EndFromRev;
			bool operator < (CIntegrationResult const &_Right) const
			{
				if (m_Action < _Right.m_Action)
					return true;
				else if (m_Action > _Right.m_Action)
					return false;
				if (m_From < _Right.m_From)
					return true;
				else if (m_From > _Right.m_From)
					return false;
				if (m_To < _Right.m_To)
					return true;
				else if (m_To > _Right.m_To)
					return false;
				if (m_StartFromRev < _Right.m_StartFromRev)
					return true;
				else if (m_StartFromRev > _Right.m_StartFromRev)
					return false;
				if (m_EndFromRev < _Right.m_EndFromRev)
					return true;
				else if (m_EndFromRev > _Right.m_EndFromRev)
					return false;

				return false;
			}
		};

		struct CConnectionInfo
		{
			CStr m_Server;
			CStr m_User;
			CStr m_Client;
			CStr m_Host;
			CStr m_TrustedCertificateDigest;
			zbool m_bDisableTagging;
		};

		class CChangeList
		{
		public:
			uint32 m_ChangeID;
			uint64 m_Date;
			zbint m_bHasShelvedFiles;
			CStr m_PerforceDate;
			CStr m_Client;
			CStr m_User;
			CStr m_Status;
			CStr m_Description;
			TCVector<CStr> m_Jobs;
			struct CFile
			{
				CStr m_Name;
			};

			TCVector<CFile> m_Files;
		};

		class CFileRev
		{
		public:
			CFileRev()
				: m_Action(EAction_Unknown)
			{
			}
			zint32 m_ChangeList;
			zint32 m_Revision;

			EAction m_Action;
			zuint64 m_Time;
			CStr m_User;
			CStr m_Client;
			CStr m_Comment;

			class CRevDesc
			{
			public:
				enum EHow
				{
					EHow_Unknown,
					EHow_CopyFrom,
					EHow_EditFrom,
					EHow_MergeFrom,
					EHow_CopyInto,
					EHow_EditInto,
					EHow_MergeInto,
				};
				CRevDesc()
					: m_How(EHow_Unknown)
				{
				}
				EHow m_How;
				CStr m_File;
				zint32 m_StartRev;
				zint32 m_EndRev;
			};

			TCVector<CRevDesc> m_RevDescs;
		};

		class CFile
		{
		public:
			CStr m_File;
			TCVector<CFileRev> m_Revisions;
		};

		struct CFileRevision
		{
			CStr m_File;
			zint32 m_ChangeList;
			zint32 m_Revision;

			EAction m_Action;
			zuint64 m_Time;

			CFileRevision()
				: m_Action(EAction_Unknown)
			{
			}
		};

		class CFileRevisions
		{
		public:
			TCVector<CFile> m_Files;
		};

		class CFix
		{
		public:
			CStr m_Job;
			zuint32 m_ChangeNumber;
			zuint64 m_Date;
			CStr m_User;
			CStr m_Client;
			CStr m_Status;
			zuint64 m_PerforceGUID;
			aint f_Compare(const CFix &_Other) const
			{
				if (m_Job > _Other.m_Job)
					return 1;
				else if (m_Job < _Other.m_Job)
					return -1;
				if (m_ChangeNumber > _Other.m_ChangeNumber)
					return 1;
				else if (m_ChangeNumber < _Other.m_ChangeNumber)
					return -1;
				if (m_Date > _Other.m_Date)
					return 1;
				else if (m_Date < _Other.m_Date)
					return -1;
				if (m_User > _Other.m_User)
					return 1;
				else if (m_User < _Other.m_User)
					return -1;
				if (m_Client > _Other.m_Client)
					return 1;
				else if (m_Client < _Other.m_Client)
					return -1;
				if (m_Status > _Other.m_Status)
					return 1;
				else if (m_Status < _Other.m_Status)
					return -1;
				if (m_PerforceGUID > _Other.m_PerforceGUID)
					return 1;
				else if (m_PerforceGUID < _Other.m_PerforceGUID)
					return -1;
				return 0;
			}
			bint operator < (const CFix &_Other) const
			{
				return f_Compare(_Other) < 0;
			}
			bint operator > (const CFix &_Other) const
			{
				return f_Compare(_Other) > 0;
			}
			bint operator == (const CFix &_Other) const
			{
				return f_Compare(_Other) == 0;
			}
			bint operator != (const CFix &_Other) const
			{
				return f_Compare(_Other) != 0;
			}
		};

		class CJob
		{
		public:
			zuint64 m_Date;
			CStr m_Status;
			CStr m_User;
		};

		class CDescription
		{
		public:
			CStr m_User;
			CStr m_ChangelistStatus;
			TCVector<CStr> m_Jobs;
			TCVector<CStr> m_JobStatuses;
			struct CFile
			{
				CStr m_Name;
				EAction m_Action;
				CFile()
					: m_Action(EAction_Unknown)
				{
				}
			};

			TCVector<CFile> m_Files;
		};

		struct CStream
		{
			CStr m_Update;
			CStr m_Access;
			CStr m_Owner;
			CStr m_Name;

			CStr m_Type;
			CStr m_Description;

			TCVector<CStr> m_Options;
			TCVector<CStr> m_Paths;
			TCVector<CStr> m_Remapped;
			TCVector<CStr> m_Ignored;

			CStr m_Parent;
			CStr m_BaseParent;

			zbool m_bFirmerThanParent;
		};

		struct CFileStats
		{
			CFileStats()
				: m_HeadAction(EAction_Unknown)
			{
			}
			CStr m_DepotFile;
			CStr m_ClientFile;
			zbint m_bIsMapped;
			EAction m_HeadAction;
			CStr m_HeadType;
			CStr m_HeadTime;
			zuint32 m_HeadRev;
			zuint32 m_HeadChange;
			zuint64 m_HeadModTime;
			zuint32 m_HaveRev;
		};

		struct CMapping
		{
			CStr m_From;
			CStr m_To;
			zbool m_bNegative;
		};

		struct CBranchSpec
		{
			CStr m_UpdateTime;
			CStr m_AccessTime;
			CStr m_Owner;
			CStr m_Description;
			TCVector<CStr> m_Options;
			TCVector<CMapping> m_View;
		};

		struct CClient
		{
			CStr m_UpdateTime;
			CStr m_AccessTime;
			CStr m_Owner;
			CStr m_Host;
			CStr m_Description;
			CStr m_Root;
			CStr m_AltRoot0;
			CStr m_AltRoot1;
			CStr m_LineEndings;
			CStr m_Stream;
			TCVector<CStr> m_Options;
			TCVector<CMapping> m_View;
		};

		struct CMergeError
		{
			CStr m_Path;
			CStr m_Error;
		};

	private:

		class CP4Client;

		CStr m_InitError;
		CStr m_LastError;
		CStr m_LastFunction;
		CP4Client *m_pClient;
		ClientApi *m_pAPI;
		bint m_bUTF8;
		TCUniquePointer<CPerforceClient> m_pNonTaggedClient;

		CStr m_ActiveHost;
		CStr m_ActiveClient;
		CStr m_ActiveUser;

		CConnectionInfo m_ConnectionInfo;

		void fp_Run( const char *func);
		void fp_Run( const char *func, TCVector<CStr> const &_Arguments);


		void fp_ReadMergeResult(TCVector<CIntegrationResult> &_oIntegrated, TCVector<CStr> &_oMustSync, bool _bInverted, TCVector<CMergeError> &_oErrors);
		bint fp_CreatePatchNonTagged(CStr const &_Branch, bool _bFullContext, CStr &_oPatch);
	public:

		CPerforceClient(CStr const &_Server = CStr(), CStr const &_User = CStr(), CStr const &_Client = CStr(), CStr const &_Host = CStr());
		CPerforceClient(CConnectionInfo const& _Info);
		~CPerforceClient();

		CStr const &f_GetHost() const
		{
			return m_ConnectionInfo.m_Host;
		}

		CStr const &f_GetServer() const
		{
			return m_ConnectionInfo.m_Server;
		}

		CStr const &f_GetUser() const
		{
			return m_ConnectionInfo.m_User;
		}

		CStr const &f_GetClient() const
		{
			return m_ConnectionInfo.m_Client;
		}

		CConnectionInfo const &f_GetConnectionInfo() const
		{
			return m_ConnectionInfo;
		}


		CStr f_GetLastError() const;
		CStr f_GetLastFunction() const;
		bint f_Login(CStr const &_Password, CStr const &_WorkingDir = CStr());
		bint f_GetSecurityLevel(CStr &_SecurityLevel);
		bint f_Dropped();

		bint f_IsUTF8() const
		{
			return m_bUTF8;
		}
		CStr f_EncodeStr(CStr const &_Str);
		CStr f_DecodeStr(CStr const &_Str);

		bint f_FileExistsInDepot(CStr const &_File);
		bint f_FileExistsInDepotNotDeleted(CStr const &_File);
		bint f_FileExistsInChangeList(CStr const &_File);
		bint f_FileExists(CStr const &_File);
		bint f_CanOpenForEdit(CStr const &_File);
		bint f_RemoveFromClient(CStr const &_File);
		bint f_Sync(CStr const &_File, TCVector<CStr> &_Synced, TCVector<CStr> &_Removed, bint _bPretend = false);
		bint f_Sync(CStr const &_File, TCFunction<bool (int64 _TotalBytes, int64 _SyncedBytes)> const &_Progress = TCFunction<bool (int64 _TotalBytes, int64 _SyncedBytes)>(), bool _bForce = false, TCVector<CStr> const &_MoreFiles = TCVector<CStr>());
		bint f_GetHeadChangelist(int64& _Changelist, CStr const& _Path);
		bint f_GetHeadChangelistUnsafe(int64& _Changelist);
		bint f_Files(CStr const &_Search, TCVector<CStr> &_Existing);
		bint f_Files(CStr const &_Search, TCVector<CStr> &_Existing, TCVector<CStr> &_Deleted);
		bint f_ClientFiles(CStr const &_Search, TCVector<CStr> &_Existing);
		bint f_GetJobSpec(CStr &_JobSpec);
		bint f_GetJobs(CRegistry &_Jobs, CStr const &_JobView);
		bint f_GetTriggers(CStr &_Triggers);
		bint f_GetUsers(CRegistry &_Users);
		bint f_GetDepotPath(CStr const& _ClientPath, CStr &_DepotPath, bool _bReturnImportedStream = true);
		bint f_GetClientPath(CStr const& _DepotPath, CStr &_ClientPath);
		bint f_GetWorkspacePath(CStr const& _DepotPath, CStr &_WorkspacePath);
		bint f_GetTextFileContents(CStr const& _Path, CStr &_Contents);
		bint f_GetStreams(TCVector<CStr> &_Streams);
		bint f_GetStreamDepots(TCVector<CStr> &_Depots);


		bint f_GetChangelist(uint32 _ChangeList, CChangeList &_Ret);
		bint f_SetChangelistOwner(uint32 _ChangeList, CStr const &_User);
		bint f_SetChangelistClient(uint32 _ChangeList, CStr const &_Client, bool _bForce = false);
		bint f_SetChangelistDescription(uint32 _ChangeList, CStr const &_Description, bool _bForce = false);

		bint f_GetChangelists(CStr const &_Path, TCVector<CChangeList> &_Ret, bint _bIncludeIntegrated, CStr const &_Workspace = CStr(), CStr const &_Status = CStr());

		bint f_GetFileRevisions(CStr const &_File, CFileRevisions &_Revisions);
		bint f_GetFileRevisions(TCVector<CStr> const &_Files, CFileRevisions &_Revisions);

		bint f_ChangeExists(const CFix &_Fix);

		bint f_GetFixes(CStr const &_Job, TCVector<CFix> &_Fixes, uint64 _PerforceGUID);
		bint f_AddFixes(CStr const &_Job, const TCVector<uint32> &_Fixes, CStr const &_Status);
		bint f_RemoveFixes(CStr const &_Job, const TCVector<uint32> &_Fixes);
		bint f_SetJobSpec(const CStr &_JobSpec);
		bint f_SetJob(const CStr &_Job);
		bint f_GetJob(const CStr &_Job, CJob &_Ret);
		bint f_SetTriggers(const CStr &_Triggers);
		bint f_JobExists(const CStr &_Job);
		bint f_DeleteJob(const CStr &_Job);
		bint f_Describe(uint32 _Changelist, CDescription &_Description);
		bint f_DescribeShelved(uint32 _Changelist, CDescription &_Description);
		bint f_Info();
		bint f_GetUserName(CStr &_UserName);
		bint f_GetClientName(CStr &_ClientName);
		bint f_GetClientRoot(CStr &_Root);
		bint f_OpenForEdit(CStr const &_File);
		bint f_Submit(CStr const &_File, CStr const &_Comment, CStr const &_Job = "");
		bint f_Revert(CStr const &_File, bool _bOnlyIfUnchanged = false);
		bint f_RevertChangelist(uint32 _Changelist, bool _bOnlyIfUnchanged = false);
		bint f_Add(CStr const &_File);
		bint f_Delete(CStr const &_File);
		bint f_OpenForEditOrMakeWritable(CStr const &_File);
		bint f_TryOpenForEdit(CStr const &_File);

		bint f_CreateChangelist(CStr const &_Comment, TCVector<CStr> const &_Jobs, TCVector<CStr> const &_Files, uint32 &_ChangeList);

		bint f_FindHeadFiles(CStr const& _Pattern, TCVector<CFileRevision> &_lFiles);

		bint f_GetClients(CStr const &_SearchPattern, TCVector<CStr> &_Clients);

		bint f_GetClients(CStr const &_SearchPattern, CStr const &_Stream, CStr const &_User, TCFunction<void (CStr const &_Client, CStr const &_Key, CStr const &_Value)> const &_Processor);

		bint f_GetClient(CStr const &_ClientName, CStr &_Contents);
		bint f_GetClient(CStr const &_ClientName, TCFunction<void (CStr const &_Key, CStr const &_Value)> const &_Processor);
		bint f_GetClient(CStr const &_Client, CPerforceClient::CClient &_oClient);


		bint f_ShelveChangelist(uint32 _Changelist, bool _bReplaceFiles, bool _bForce, TCVector<CStr> const &_Files);
		bint f_MoveToChangelist(TCVector<CStr> const &_Files, uint32 _ChangeList);
		bint f_UnshelveInto(uint32 _SourceChangelist, uint32 _DestinationChangelist);
		bint f_UnshelveWithBranch(uint32 _SourceChangelist, CStr const &_BranchMapping, uint32 _DestinationChangeList);
		bint f_SubmitChangelist(uint32 _Changelist, bool _bSubmitShelved, uint32 &_FinalChangelist);
		bint f_DeleteChangelist(uint32 _Changelist, bool _bForce = false);
		bint f_RemoveJobsFromChangelist(uint32 _Changelist, TCVector<CStr> const &_Jobs);
		bint f_DeleteShelvedFile(uint32 _Changelist, CStr const &_File, bool _bForce = false);
		bint f_ResolveSafe(CStr const &_File, uint32 _Changelist = 0);
		bint f_ResolveAutomatic(CStr const &_File, uint32 _Changelist = 0);
		bint f_ResolveMine(CStr const &_File, uint32 _Changelist = 0);

		bint f_CopyStream(CStr const &_FromStream, CStr const &_ToStream, bool _bPretend, TCVector<CIntegrationResult> &_oIntegrated, TCVector<CStr> &_oMustSync, TCVector<CMergeError> &_oErrors, CStr _ToFileSpec = CStr());
		bint f_MergeStream(CStr const &_FromStream, CStr const &_ToStream, bool _bPretend, TCVector<CIntegrationResult> &_oIntegrated, TCVector<CStr> &_oMustSync, TCVector<CMergeError> &_oErrors, CStr _ToFileSpec = CStr());
		bint f_IntegrateStream(CStr const &_FromStream, CStr const &_ToStream, bool _bPretend, TCVector<CIntegrationResult> &_oIntegrated, TCVector<CStr> &_oMustSync, TCVector<CMergeError> &_oErrors, CStr _ToFileSpec = CStr());
		bint f_IntegrateFiles(CStr const &_FromFiles, CStr const &_ToFiles, bool _bPretend, bool _bEnableBaseless, TCVector<CIntegrationResult> &_oIntegrated, TCVector<CStr> &_oMustSync, TCVector<CMergeError> &_oErrors);

		bint f_CopyStreamToParent(CStr const &_FromStream, bool _bPretend, TCVector<CIntegrationResult> &_oIntegrated, TCVector<CStr> &_oMustSync, TCVector<CMergeError> &_oErrors, CStr _ToFileSpec = CStr());
		bint f_MergeStreamToParent(CStr const &_FromStream, bool _bPretend, TCVector<CIntegrationResult> &_oIntegrated, TCVector<CStr> &_oMustSync, TCVector<CMergeError> &_oErrors, CStr _ToFileSpec = CStr());
		bint f_IntegrateStreamToParent(CStr const &_FromStream, bool _bPretend, TCVector<CIntegrationResult> &_oIntegrated, TCVector<CStr> &_oMustSync, TCVector<CMergeError> &_oErrors, CStr _ToFileSpec = CStr());

		bint f_CopyStreamFromParent(CStr const &_ToStream, bool _bPretend, TCVector<CIntegrationResult> &_oIntegrated, TCVector<CStr> &_oMustSync, TCVector<CMergeError> &_oErrors, CStr _ToFileSpec = CStr());
		bint f_MergeStreamFromParent(CStr const &_ToStream, bool _bPretend, TCVector<CIntegrationResult> &_oIntegrated, TCVector<CStr> &_oMustSync, TCVector<CMergeError> &_oErrors, CStr _ToFileSpec = CStr());
		bint f_IntegrateStreamFromParent(CStr const &_ToStream, bool _bPretend, TCVector<CIntegrationResult> &_oIntegrated, TCVector<CStr> &_oMustSync, TCVector<CMergeError> &_oErrors, CStr _ToFileSpec = CStr());

		bint f_CreatePatch(CStr const &_Branch, bool _bFullContext, CStr &_oPatch);

		bint f_GetStream(CStr const &_StreamName, CStream &_Stream);

		bint f_DeleteStream(CStr const &_StreamName);

		bint f_Obliterate(CStr const &_Path);

		bint f_StreamExists(CStr const &_StreamName);

		bint f_SetStream(CStr const &_StreamName, CStream const &_Stream);

		bint f_PopulateStream(CStr const &_StreamName);

		bint f_FindStreams(CStr const &_SearchQuery, TCVector<CStr> &_oStreams);

		bint f_GetOpened(CStr const &_Path, CStr const &_Client, TCVector<CStr> &_oOpened);

		bint f_SwitchWorkspaceStream(CStr const &_Workspace, CStr const &_Stream);
		bint f_DeleteWorkspace(CStr const &_Workspace);

		bint f_GetEnvVar(CStr const &_Var, CStr &_Value);
		bint f_SetEnvVar(CStr const &_Var, CStr const &_Value);


		bint f_FileStats(CStr const &_File, CFileStats &_Stats);

		bint f_CreateStreamClient(CStr const &_ClientName, CStr const &_Root, CStr const &_AltRoot, CStr const &_Template, CStr const &_Stream, TCVector<CStr> const *_pOptions = nullptr);
		bint f_CreateClient(CStr const &_ClientName, CStr const &_Root, CStr const &_AltRoot, CStr const &_Template);
		bint f_UpdateStreamClient(CStr const &_ClientName, CStr const &_Root, CStr const &_AltRoot, CStr const &_Template, CStr const &_Stream, TCVector<CStr> const *_pOptions = nullptr);
		bint f_UpdateClient(CStr const &_ClientName, CStr const &_Root, CStr const &_AltRoot, CStr const &_Template);

		bint f_CreateBranch(CStr const &_Name, CBranchSpec const &_BranchSpec);
		bint f_GetBranchForStreams(CStr const &_From, CStr const &_To, CPerforceClient::CBranchSpec &_oBranch);


		bint f_DeleteBranch(CStr const &_Name);

		static CStr fs_FixWhiteSpace(CStr const &_In);
		static CStr fs_FixLineStartingTabs(CStr const &_In);
		static CStr fs_FixSpecialChars(CStr const &_In);

		static CStr fs_ActionToStr(EAction _Action);

	private:
		bint fp_MutateChangelist(uint32 _ChangeList, CStr const &_Operation, bool _bForce, TCFunction<bool (CStr &o_NewDesc, CStr const &_Key, CStr const &_Data)> &&_fMutator);

	};

	class CPerforceClientThrow
	{
		CPerforceClient mp_Client;
		void fp_Throw(bool _bResult) const;
	public:
		CPerforceClientThrow(CStr const &_Server = CStr(), CStr const &_User = CStr(), CStr const &_Client = CStr(), CStr const &_Host = CStr());
		CPerforceClientThrow(CPerforceClient::CConnectionInfo const& _Info);

		void f_ThrowLastError() const;
		CPerforceClient &f_NoThrow()
		{
			return mp_Client;
		}

		CStr const &f_GetHost() const;
		CStr const &f_GetServer() const;
		CStr const &f_GetUser() const;
		CStr const &f_GetClient() const;
		CPerforceClient::CConnectionInfo const &f_GetConnectionInfo() const;

		bool f_IsUTF8();
		void f_Login(CStr const &_Password, CStr const &_WorkingDir = CStr());
		CStr f_GetSecurityLevel();

		bool f_FileExistsInDepot(CStr const &_File);
		bool f_FileExistsInDepotNotDeleted(CStr const &_File);
		bool f_FileExistsInChangeList(CStr const &_File);
		bool f_FileExists(CStr const &_File);
		bool f_CanOpenForEdit(CStr const &_File);

		void f_RemoveFromClient(CStr const &_File);
		void f_Sync(CStr const &_File, TCVector<CStr> &_Synced, TCVector<CStr> &_Removed, bint _bPretend = false);
		void f_Sync(CStr const &_File, TCFunction<bool (int64 _TotalBytes, int64 _SyncedBytes)> const &_Progress = TCFunction<bool (int64 _TotalBytes, int64 _SyncedBytes)>(), bool _bForce = false, TCVector<CStr> const &_MoreFiles = TCVector<CStr>());
		int64 f_GetHeadChangelist(CStr const& _Path);
		int64 f_GetHeadChangelistUnsafe();
		TCVector<CStr> f_Files(CStr const &_Search);
		TCVector<CStr> f_Files(CStr const &_Search, TCVector<CStr> &_Deleted);
		TCVector<CStr> f_ClientFiles(CStr const &_Search);
		CStr f_GetJobSpec();
		CRegistry f_GetJobs(CStr const &_JobView);
		CStr f_GetTriggers();
		CRegistry f_GetUsers();
		CStr f_GetDepotPath(CStr const& _ClientPath, bool _bReturnImportedStream = true);
		CStr f_GetClientPath(CStr const& _DepotPath);

		CStr f_GetWorkspacePath(CStr const& _DepotPath);
		CStr f_GetTextFileContents(CStr const& _Path);
		TCVector<CStr> f_GetStreams();
		TCVector<CStr> f_GetStreamDepots();

		CPerforceClient::CChangeList f_GetChangelist(uint32 _ChangeList);
		void f_SetChangelistOwner(uint32 _ChangeList, CStr const &_User);
		void f_SetChangelistClient(uint32 _ChangeList, CStr const &_Client, bool _bForce = false);
		void f_SetChangelistDescription(uint32 _ChangeList, CStr const &_Description, bool _bForce = false);

		TCVector<CPerforceClient::CChangeList> f_GetChangelists(CStr const &_Path, bint _bIncludeIntegrated, CStr const &_Workspace = CStr(), CStr const &_Status = CStr());

		CPerforceClient::CFileRevisions f_GetFileRevisions(CStr const &_File);
		CPerforceClient::CFileRevisions f_GetFileRevisions(TCVector<CStr> const &_Files);


		bool f_ChangeExists(const CPerforceClient::CFix &_Fix);

		TCVector<CPerforceClient::CFix> f_GetFixes(CStr const &_Job, uint64 _PerforceGUID);
		void f_AddFixes(CStr const &_Job, const TCVector<uint32> &_Fixes, CStr const &_Status);
		void f_RemoveFixes(CStr const &_Job, const TCVector<uint32> &_Fixes);
		void f_SetJobSpec(const CStr &_JobSpec);
		void f_SetJob(const CStr &_Job);
		CPerforceClient::CJob f_GetJob(const CStr &_Job);
		void f_SetTriggers(const CStr &_Triggers);
		bool f_JobExists(const CStr &_Job);
		void f_DeleteJob(const CStr &_Job);
		CPerforceClient::CDescription f_Describe(uint32 _Changelist);
		CPerforceClient::CDescription f_DescribeShelved(uint32 _Changelist);
		void f_Info();
		CStr f_GetUserName();
		CStr f_GetClientName();
		CStr f_GetClientRoot();
		void f_OpenForEdit(CStr const &_File);
		void f_Submit(CStr const &_File, CStr const &_Comment, CStr const &_Job = "");
		void f_Revert(CStr const &_File, bool _bOnlyIfUnchanged = false);
		void f_RevertChangelist(uint32 _Changelist, bool _bOnlyIfUnchanged = false);
		void f_Add(CStr const &_File);
		void f_Delete(CStr const &_File);
		void f_OpenForEditOrMakeWritable(CStr const &_File);
		void f_TryOpenForEdit(CStr const &_File);

		uint32 f_CreateChangelist(CStr const &_Comment, TCVector<CStr> const &_Jobs, TCVector<CStr> const &_Files);

		TCVector<CPerforceClient::CFileRevision> f_FindHeadFiles(CStr const& _Pattern);

		TCVector<CStr> f_GetClients(CStr const &_SearchPattern);

		void f_GetClients(CStr const &_SearchPattern, CStr const &_Stream, CStr const &_User, TCFunction<void (CStr const &_Client, CStr const &_Key, CStr const &_Value)> const &_Processor);

		CStr f_GetClient(CStr const &_ClientName);
		void f_GetClient(CStr const &_ClientName, TCFunction<void (CStr const &_Key, CStr const &_Value)> const &_Processor);
		CPerforceClient::CClient f_GetClientScruct(CStr const &_Client);

		void f_ShelveChangelist(uint32 _Changelist, bool _bReplaceFiles, bool _bForce, TCVector<CStr> const &_Files);
		void f_MoveToChangelist(TCVector<CStr> const &_Files, uint32 _ChangeList);
		void f_UnshelveInto(uint32 _SourceChangelist, uint32 _DestinationChangelist);
		void f_UnshelveWithBranch(uint32 _SourceChangelist, CStr const &_BranchMapping, uint32 _DestinationChangeList);
		uint32 f_SubmitChangelist(uint32 _Changelist, bool _bSubmitShelved);
		void f_DeleteChangelist(uint32 _Changelist, bool _bForce = false);
		void f_RemoveJobsFromChangelist(uint32 _Changelist, TCVector<CStr> const &_Jobs);
		void f_DeleteShelvedFile(uint32 _Changelist, CStr const &_File, bool _bForce = false);
		void f_ResolveSafe(CStr const &_File, uint32 _Changelist = 0);
		void f_ResolveAutomatic(CStr const &_File, uint32 _Changelist = 0);
		void f_ResolveMine(CStr const &_File, uint32 _Changelist = 0);

		TCVector<CPerforceClient::CIntegrationResult> f_CopyStream(CStr const &_FromStream, CStr const &_ToStream, bool _bPretend,  TCVector<CStr> &_oMustSync, TCVector<CPerforceClient::CMergeError> &_oErrors, CStr _ToFileSpec = CStr());
		TCVector<CPerforceClient::CIntegrationResult> f_MergeStream(CStr const &_FromStream, CStr const &_ToStream, bool _bPretend, TCVector<CStr> &_oMustSync, TCVector<CPerforceClient::CMergeError> &_oErrors, CStr _ToFileSpec = CStr());
		TCVector<CPerforceClient::CIntegrationResult> f_IntegrateStream(CStr const &_FromStream, CStr const &_ToStream, bool _bPretend, TCVector<CStr> &_oMustSync, TCVector<CPerforceClient::CMergeError> &_oErrors, CStr _ToFileSpec = CStr());
		TCVector<CPerforceClient::CIntegrationResult> f_IntegrateFiles(CStr const &_FromFiles, CStr const &_ToFiles, bool _bPretend, bool _bEnableBaseless, TCVector<CStr> &_oMustSync, TCVector<CPerforceClient::CMergeError> &_oErrors);

		TCVector<CPerforceClient::CIntegrationResult> f_CopyStreamToParent(CStr const &_FromStream, bool _bPretend, TCVector<CStr> &_oMustSync, TCVector<CPerforceClient::CMergeError> &_oErrors, CStr _ToFileSpec = CStr());
		TCVector<CPerforceClient::CIntegrationResult> f_MergeStreamToParent(CStr const &_FromStream, bool _bPretend, TCVector<CStr> &_oMustSync, TCVector<CPerforceClient::CMergeError> &_oErrors, CStr _ToFileSpec = CStr());
		TCVector<CPerforceClient::CIntegrationResult> f_IntegrateStreamToParent(CStr const &_FromStream, bool _bPretend, TCVector<CStr> &_oMustSync, TCVector<CPerforceClient::CMergeError> &_oErrors, CStr _ToFileSpec = CStr());

		TCVector<CPerforceClient::CIntegrationResult> f_CopyStreamFromParent(CStr const &_ToStream, bool _bPretend, TCVector<CStr> &_oMustSync, TCVector<CPerforceClient::CMergeError> &_oErrors, CStr _ToFileSpec = CStr());
		TCVector<CPerforceClient::CIntegrationResult> f_MergeStreamFromParent(CStr const &_ToStream, bool _bPretend, TCVector<CStr> &_oMustSync, TCVector<CPerforceClient::CMergeError> &_oErrors, CStr _ToFileSpec = CStr());
		TCVector<CPerforceClient::CIntegrationResult> f_IntegrateStreamFromParent(CStr const &_ToStream, bool _bPretend, TCVector<CStr> &_oMustSync, TCVector<CPerforceClient::CMergeError> &_oErrors, CStr _ToFileSpec = CStr());

		CStr f_CreatePatch(CStr const &_Branch, bool _bFullContext);

		CPerforceClient::CStream f_GetStream(CStr const &_StreamName);

		void f_DeleteStream(CStr const &_StreamName);

		void f_Obliterate(CStr const &_Path);

		bool f_StreamExists(CStr const &_StreamName);

		void f_SetStream(CStr const &_StreamName, CPerforceClient::CStream const &_Stream);

		void f_PopulateStream(CStr const &_StreamName);

		TCVector<CStr> f_FindStreams(CStr const &_SearchQuery);

		TCVector<CStr> f_GetOpened(CStr const &_Path, CStr const &_Client);

		void f_SwitchWorkspaceStream(CStr const &_Workspace, CStr const &_Stream);
		void f_DeleteWorkspace(CStr const &_Workspace);

		CStr f_GetEnvVar(CStr const &_Var);
		void f_SetEnvVar(CStr const &_Var, CStr const &_Value);


		CPerforceClient::CFileStats f_FileStats(CStr const &_File);

		void f_CreateStreamClient(CStr const &_ClientName, CStr const &_Root, CStr const &_AltRoot, CStr const &_Template, CStr const &_Stream, TCVector<CStr> const *_pOptions = nullptr);
		void f_CreateClient(CStr const &_ClientName, CStr const &_Root, CStr const &_AltRoot, CStr const &_Template);
		void f_UpdateStreamClient(CStr const &_ClientName, CStr const &_Root, CStr const &_AltRoot, CStr const &_Template, CStr const &_Stream, TCVector<CStr> const *_pOptions = nullptr);
		void f_UpdateClient(CStr const &_ClientName, CStr const &_Root, CStr const &_AltRoot, CStr const &_Template);

		CPerforceClient::CBranchSpec f_GetBranchForStreams(CStr const &_From, CStr const &_To);

		void f_CreateBranch(CStr const &_Name, CPerforceClient::CBranchSpec const &_BranchSpec);
		void f_DeleteBranch(CStr const &_Name);

		static bool fs_GetFromP4Config(CStr const &_Path, CPerforceClientThrow &o_Client);
	};
}

#ifndef DMibPNoShortCuts
	using namespace NMib::NPerforce;
#endif
