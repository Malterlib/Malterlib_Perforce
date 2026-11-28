// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#pragma once

#include <Mib/Container/Registry>
#include <Mib/Storage/UniquePointer>

#include "Malterlib_Perforce_Ignore.h"

class ClientApi; // NOLINT(malterlib-type-naming): the name in the Perforce API
class Enviro; // NOLINT(malterlib-type-naming): the name in the Perforce API

namespace NMib::NPerforce
{
	struct CPerforceClient
	{
		enum EAction
		{
			EAction_Unknown
			, EAction_Add
			, EAction_Delete
			, EAction_Edit
			, EAction_Integrate
			, EAction_Branch
		};

		struct CIntegrationResult
		{
			CIntegrationResult();

			auto operator <=> (CIntegrationResult const &_Right) const noexcept = default;

			EAction m_Action;
			NStr::CStr m_From;
			NStr::CStr m_To;
			uint32 m_StartFromRev = 0;
			uint32 m_EndFromRev = 0;
		};

		struct CConnectionInfo
		{
			NStr::CStr m_Server;
			NStr::CStr m_User;
			NStr::CStr m_Client;
			NStr::CStr m_Host;
			NStr::CStr m_TrustedCertificateDigest;
			NStr::CStr m_WorkingDirectory;                     // Whose P4CONFIG files give what the connection leaves unset; the current directory when empty
			NStr::CStr m_TicketFile;
			NStr::CStr m_EnviroFile;
			NStr::CStr m_Charset;                              // Overrides P4CHARSET, like "none" or "utf8"
			NStorage::TCOptional<NStr::CStr> m_CommandCharset; // Overrides P4COMMANDCHARSET, as p4's -Q; empty for none
			NStr::CStr m_IgnoreFiles;                          // Overrides P4IGNORE; "unset" for none
			NStr::CStr m_ClientPath;                           // Overrides P4CLIENTPATH; "unset" for no restriction, which clears P4INITROOT too
			NStorage::TCOptional<NStr::CStr> m_ConfigName;     // Overrides P4CONFIG, whose files in m_WorkingDirectory and above it give settings; empty for none

			// The environment the settings are read from in place of the process's
			NStorage::TCOptional<NContainer::TCMap<NStr::CStr, NStr::CStr>> m_Environment;

			fp64 m_Timeout = 0.0;                  // Seconds the server may take to answer, or zero for no limit. The API's blocking TCP connect is not bounded
			bool m_bDisableTagging = false;
			bool m_bIgnorePasswordSetting = false; // P4PASSWD is not read: only the password given to f_Login and the tickets are used
			NStr::CStr m_ConfigRoot;               // The highest directory whose P4CONFIG file gives settings, or empty for any, as p4 reads them
		};

		struct CChangeList
		{
			struct CFile
			{
				NStr::CStr m_Name;
			};

			uint32 m_ChangeID = 0;
			uint64 m_Date = 0;
			bool m_bHasShelvedFiles = false;
			NStr::CStr m_PerforceDate;
			NStr::CStr m_Client;
			NStr::CStr m_User;
			NStr::CStr m_Status;
			NStr::CStr m_Description;
			NContainer::TCVector<NStr::CStr> m_Jobs;
			NContainer::TCVector<CFile> m_Files;
		};

		struct CFileRev
		{
			struct CRevDesc
			{
				enum EHow
				{
					EHow_Unknown
					, EHow_CopyFrom
					, EHow_EditFrom
					, EHow_MergeFrom
					, EHow_CopyInto
					, EHow_EditInto
					, EHow_MergeInto
				};

				CRevDesc();

				EHow m_How;
				NStr::CStr m_File;
				int32 m_StartRev = 0;
				int32 m_EndRev = 0;
			};

			CFileRev();

			int32 m_ChangeList = 0;
			int32 m_Revision = 0;

			EAction m_Action;
			uint64 m_Time = 0;
			NStr::CStr m_User;
			NStr::CStr m_Client;
			NStr::CStr m_Comment;

			NContainer::TCVector<CRevDesc> m_RevDescs;
		};

		struct CFile
		{
			NStr::CStr m_File;
			NContainer::TCVector<CFileRev> m_Revisions;
		};

		struct CFileRevision
		{
			CFileRevision();

			NStr::CStr m_File;
			zint32 m_ChangeList;
			zint32 m_Revision;

			EAction m_Action;
			zuint64 m_Time;
		};

		struct CFileRevisions
		{
			NContainer::TCVector<CFile> m_Files;
		};

		struct CFix
		{
			auto operator <=> (CFix const &_Other) const noexcept = default;

			NStr::CStr m_Job;
			uint32 m_ChangeNumber = 0;
			uint64 m_Date = 0;
			NStr::CStr m_User;
			NStr::CStr m_Client;
			NStr::CStr m_Status;
			uint64 m_PerforceGUID = 0;
		};

		struct CJob
		{
			uint64 m_Date = 0;
			NStr::CStr m_Status;
			NStr::CStr m_User;
		};

		struct CDescription
		{
			struct CFile
			{
				CFile();

				NStr::CStr m_Name;
				EAction m_Action;
			};

			NStr::CStr m_User;
			NStr::CStr m_ChangelistStatus;
			NContainer::TCVector<NStr::CStr> m_Jobs;
			NContainer::TCVector<NStr::CStr> m_JobStatuses;
			NContainer::TCVector<CFile> m_Files;
		};

		struct CStream
		{
			NStr::CStr m_Update;
			NStr::CStr m_Access;
			NStr::CStr m_Owner;
			NStr::CStr m_Name;

			NStr::CStr m_Type;
			NStr::CStr m_Description;

			NContainer::TCVector<NStr::CStr> m_Options;
			NContainer::TCVector<NStr::CStr> m_Paths;
			NContainer::TCVector<NStr::CStr> m_Remapped;
			NContainer::TCVector<NStr::CStr> m_Ignored;

			NStr::CStr m_Parent;
			NStr::CStr m_BaseParent;

			bool m_bFirmerThanParent = false;
			bool m_bInheritParentView = true;
		};

		// The line ends that the read of a workspace file turned into the LFs that end the lines of the text p4 diff compared.
		enum class EComparedLineEnds
		{
			mc_None     // The bytes as they are: an LF ends a line, also that of a CRLF, and a lone CR ends none
			, mc_CR     // LineEnd mac: every CR was read as an LF, so a CRLF ends two lines
			, mc_CRLF   // LineEnd win, or local on Windows: a CRLF was read as an LF, and a lone CR ends no line
			, mc_LFCRLF // LineEnd share: a CRLF and a lone CR were each read as an LF
		};

		// The lines a chunk of differences replaces, as a unified diff without context heads its hunk: the first line on either side,
		// one-based, which is the line in front of which the chunk stands where the side has no lines, and the number of lines.
		struct CDiffRange
		{
			umint m_iOldFirst = 0;
			umint m_nOld = 0;
			umint m_iNewFirst = 0;
			umint m_nNew = 0;
		};

		struct CFileDiff
		{
			NStr::CStr m_File;                                                 // The workspace file p4 diff compares, or the second depot file and revision p4 diff2 compares
			NStr::CStr m_DepotFile;                                            // From p4 diff: the depot file whose m_Revision it compares the workspace file with
			uint32 m_Revision = 0;                                             // From p4 diff's record of the file, empty with m_DepotFile where p4 sent none
			NStr::CStr m_Status;                                               // From p4 diff2: identical, content, types, left only or right only
			NStr::CStr m_Type1;                                                // From p4 diff2: the type of the first revision, empty where there is none
			NStr::CStr m_Type2;                                                // From p4 diff2: the type of the second revision, empty where there is none
			NContainer::TCVector<CDiffRange> m_Ranges;                         // The chunks of differences, in order
			EComparedLineEnds m_ComparedLineEnds = EComparedLineEnds::mc_None; // From p4 diff: the line ends the read of the workspace file turned into LFs
			bool m_bConverted = false;                                         // From p4 diff: the workspace file was compared converted to UTF-8, else as its bytes
			bool m_bBinary = false;                                            // Compared as binary, which gives no differences
		};

		struct CFileStats
		{
			CFileStats();

			NStr::CStr m_DepotFile;
			NStr::CStr m_ClientFile;
			bool m_bIsMapped = false;
			EAction m_HeadAction;
			NStr::CStr m_HeadType;
			NStr::CStr m_HeadTime;
			uint32 m_HeadRev = 0;
			uint64 m_HeadChange = 0;      // 64-bit on a server that allows it, sys.types.allow64
			uint64 m_HeadModTime = 0;
			uint32 m_HaveRev = 0;
			NStr::CStr m_Action;          // What the workspace has the file opened for, like 'edit', 'add' or 'move/delete'; empty where it is not opened
			NStr::CStr m_Type;            // The type the file is opened as
			NStr::CStr m_ResolveBaseFile; // Where a content resolve from the file itself is still to do, the file, whose m_ResolveBaseRev its changes are based on
			uint32 m_ResolveBaseRev = 0;
			NStr::CStr m_MovedFile;       // Of a file opened for a move, the depot file at its other end: 'move/add' from it or 'move/delete' to it
		};

		struct CMapping
		{
			NStr::CStr m_From;
			NStr::CStr m_To;
			bool m_bNegative = false;
		};

		struct CBranchSpec
		{
			NStr::CStr m_UpdateTime;
			NStr::CStr m_AccessTime;
			NStr::CStr m_Owner;
			NStr::CStr m_Description;
			NContainer::TCVector<NStr::CStr> m_Options;
			NContainer::TCVector<CMapping> m_View;
		};

		struct CClient
		{
			NStr::CStr m_UpdateTime;
			NStr::CStr m_AccessTime;
			NStr::CStr m_Owner;
			NStr::CStr m_Host;
			NStr::CStr m_Description;
			NStr::CStr m_Root;
			NStr::CStr m_AltRoot0;
			NStr::CStr m_AltRoot1;
			NStr::CStr m_LineEndings;
			NStr::CStr m_Stream;
			NContainer::TCVector<NStr::CStr> m_Options;
			NContainer::TCVector<CMapping> m_View;
		};

		struct CMergeError
		{
			NStr::CStr m_Path;
			NStr::CStr m_Error;
		};

		struct CUnresolvedFile
		{
			NStr::CStr m_ClientFile;
			NStr::CStr m_FromFile;
			NStr::CStr m_ResolveType; // What is to resolve, like content, filetype, move or delete
			uint32 m_StartFromRev = 0;
			uint32 m_EndFromRev = 0;
		};

		struct COpenedFile
		{
			NStr::CStr m_DepotFile;
			NStr::CStr m_Change; // The changelist the file is opened in: default, or its number
			NStr::CStr m_Action; // What the file is opened for, like 'edit', 'add' or 'move/delete'
		};

		CPerforceClient(NStr::CStr const &_Server = {}, NStr::CStr const &_User = {}, NStr::CStr const &_Client = {}, NStr::CStr const &_Host = {});
		CPerforceClient(CConnectionInfo const &_Info);
		~CPerforceClient();

		NStr::CStr const &f_GetHost() const;
		NStr::CStr const &f_GetServer() const;
		NStr::CStr const &f_GetUser() const;
		NStr::CStr const &f_GetClient() const;
		NStr::CStr f_GetActiveServer();
		NStr::CStr f_GetActiveUser();
		NStr::CStr f_GetActiveClient();
		NStr::CStr f_GetActiveHost();
		CConnectionInfo const &f_GetConnectionInfo() const;

		NStr::CStr f_GetLastError() const;
		NStr::CStr f_GetLastFunction() const;
		NStr::CStr f_GetLastInfo(NStr::CStr const &_Name) const;
		bool f_Login(NStr::CStr const &_Password, NStr::CStr const &_WorkingDir = {});
		bool f_GetSecurityLevel(NStr::CStr &_SecurityLevel);
		bool f_GetLoginTicket(NStr::CStr const &_Password, bool _bAllHosts, NStr::CStr &o_Ticket);
		bool f_CreateDepot(NStr::CStr const &_Name, NStr::CStr const &_Type);
		bool f_SetUserPassword(NStr::CStr const &_User, NStr::CStr const &_Password, NStr::CStr const &_OldPassword = {});
		bool f_SetClient(NStr::CStr const &_ClientName, CClient const &_Client);
		bool f_SetProtections(NContainer::TCVector<NStr::CStr> const &_Lines);
		bool f_SetTypeMap(NContainer::TCVector<NStr::CStr> const &_Lines);
		bool f_Dropped();

		bool f_IsUTF8() const;
		NStr::CStr f_EncodeStr(NStr::CStr const &_Str);
		NStr::CStr f_DecodeStr(NStr::CStr const &_Str);

		bool f_FileExistsInDepot(NStr::CStr const &_File);
		bool f_FileExistsInDepotNotDeleted(NStr::CStr const &_File);
		bool f_FileExistsInChangeList(NStr::CStr const &_File);
		bool f_FileExists(NStr::CStr const &_File);
		bool f_CanOpenForEdit(NStr::CStr const &_File);
		bool f_RemoveFromClient(NStr::CStr const &_File);
		bool f_Sync(NStr::CStr const &_File, NContainer::TCVector<NStr::CStr> &_Synced, NContainer::TCVector<NStr::CStr> &_Removed, bool _bPretend = false);
		auto f_Sync
			(
				NStr::CStr const &_File
				, NFunction::TCFunction<bool (int64 _TotalBytes, int64 _SyncedBytes)> const &_fProgress = NFunction::TCFunction<bool (int64 _TotalBytes, int64 _SyncedBytes)>()
				, bool _bForce = false
				, NContainer::TCVector<NStr::CStr> const &_MoreFiles = NContainer::TCVector<NStr::CStr>()
			)
			-> bool
		;
		bool f_GetHeadChangelist(int64 &_Changelist, NStr::CStr const &_Path);
		bool f_GetHeadChangelistUnsafe(int64 &_Changelist);
		bool f_Files(NStr::CStr const &_Search, NContainer::TCVector<NStr::CStr> &_Existing);
		bool f_Files(NStr::CStr const &_Search, NContainer::TCVector<NStr::CStr> &_Existing, NContainer::TCVector<NStr::CStr> &_Deleted);
		bool f_ClientFiles(NStr::CStr const &_Search, NContainer::TCVector<NStr::CStr> &_Existing);
		bool f_GetFileStatClientFiles(NContainer::TCVector<NStr::CStr> const &_Arguments, NContainer::TCVector<NStr::CStr> &o_ClientFiles);
		auto f_GetFileStatFilesUnder(NStr::CStr const &_LocalRoot, NContainer::TCVector<NStr::CStr> const &_Arguments, NContainer::TCVector<NStr::CStr> &o_Files) -> bool;
		auto f_GetFileStatFilesUnder
			(
				NStr::CStr const &_LocalRoot
				, NContainer::TCVector<NStr::CStr> const &_Arguments
				, NContainer::TCVector<NStr::CStr> &o_Files
				, NContainer::TCVector<CFileStats> &o_Stats
			)
			-> bool
		;
		bool f_GetJobSpec(NStr::CStr &_JobSpec);
		bool f_GetJobs(NContainer::CRegistry &_Jobs, NStr::CStr const &_JobView);
		bool f_GetTriggers(NStr::CStr &_Triggers);
		bool f_GetUsers(NContainer::CRegistry &_Users);
		bool f_GetDepotPath(NStr::CStr const &_ClientPath, NStr::CStr &_DepotPath, bool _bReturnImportedStream = true);
		bool f_GetClientPath(NStr::CStr const &_DepotPath, NStr::CStr &_ClientPath);
		bool f_GetWorkspacePath(NStr::CStr const &_DepotPath, NStr::CStr &_WorkspacePath);
		bool f_GetTextFileContents(NStr::CStr const &_Path, NStr::CStr &_Contents);
		bool f_GetTextFileContentsIfExists(NStr::CStr const &_Path, NStorage::TCOptional<NStr::CStr> &o_Contents);
		bool f_GetFileContents(NStr::CStr const &_Path, bool _bExpandKeywords, NContainer::CByteVector &o_Contents);
		bool f_DiffOpenedFiles(NContainer::TCVector<NStr::CStr> const &_Files, NContainer::TCVector<CFileDiff> &o_Diffs);
		bool f_DiffRevisions(NStr::CStr const &_File1, NStr::CStr const &_File2, CFileDiff &o_Diff, bool _bAnyTypes = false);
		bool f_GetStreams(NContainer::TCVector<NStr::CStr> &_Streams);
		bool f_GetStreamDepots(NContainer::TCVector<NStr::CStr> &_Depots);

		bool f_GetChangelist(uint32 _ChangeList, CChangeList &_Ret);
		bool f_SetChangelistOwner(uint32 _ChangeList, NStr::CStr const &_User);
		bool f_SetChangelistClient(uint32 _ChangeList, NStr::CStr const &_Client, bool _bForce = false);
		bool f_SetChangelistDescription(uint32 _ChangeList, NStr::CStr const &_Description, bool _bForce = false);

		auto f_GetChangelists(NStr::CStr const &_Path, NContainer::TCVector<CChangeList> &_Ret, bool _bIncludeIntegrated, NStr::CStr const &_Workspace = {}, NStr::CStr const &_Status = {})
			-> bool
		;

		bool f_GetFileRevisions(NStr::CStr const &_File, CFileRevisions &_Revisions);
		bool f_GetFileRevisions(NContainer::TCVector<NStr::CStr> const &_Files, CFileRevisions &_Revisions);

		bool f_ChangeExists(CFix const &_Fix);

		bool f_GetFixes(NStr::CStr const &_Job, NContainer::TCVector<CFix> &_Fixes, uint64 _PerforceGUID);
		bool f_AddFixes(NStr::CStr const &_Job, NContainer::TCVector<uint32> const &_Fixes, NStr::CStr const &_Status);
		bool f_RemoveFixes(NStr::CStr const &_Job, NContainer::TCVector<uint32> const &_Fixes);
		bool f_SetJobSpec(NStr::CStr const &_JobSpec);
		bool f_SetJob(NStr::CStr const &_Job);
		bool f_GetJob(NStr::CStr const &_Job, CJob &_Ret);
		bool f_SetTriggers(NStr::CStr const &_Triggers);
		bool f_JobExists(NStr::CStr const &_Job);
		bool f_DeleteJob(NStr::CStr const &_Job);
		bool f_Describe(uint32 _Changelist, CDescription &_Description);
		bool f_DescribeShelved(uint32 _Changelist, CDescription &_Description);
		bool f_Info();
		bool f_GetUserName(NStr::CStr &_UserName);
		bool f_GetClientName(NStr::CStr &_ClientName);
		bool f_GetClientRoot(NStr::CStr &_Root);
		bool f_OpenForEdit(NStr::CStr const &_File);
		bool f_OpenForEdit(NStr::CStr const &_File, NStr::CStr const &_Type);
		bool f_Submit(NStr::CStr const &_File, NStr::CStr const &_Comment, NStr::CStr const &_Job = {});
		bool f_Revert(NStr::CStr const &_File, bool _bOnlyIfUnchanged = false);
		// Changelist 0 selects the current client's default changelist.
		bool f_RevertChangelist(uint32 _Changelist, bool _bOnlyIfUnchanged = false, bool _bDeleteAddedFiles = false);
		bool f_Add(NStr::CStr const &_File, bool _bLiteral = false);
		bool f_Delete(NStr::CStr const &_File);
		bool f_Move(NStr::CStr const &_From, NStr::CStr const &_To);
		bool f_OpenForEditOrMakeWritable(NStr::CStr const &_File);
		bool f_TryOpenForEdit(NStr::CStr const &_File);
		bool f_OpenForWrite(NStr::CStr const &_File, bool *o_pRefused = nullptr);

		bool f_CreateChangelist(NStr::CStr const &_Comment, NContainer::TCVector<NStr::CStr> const &_Jobs, NContainer::TCVector<NStr::CStr> const &_Files, uint32 &_ChangeList);

		bool f_FindHeadFiles(NStr::CStr const &_Pattern, NContainer::TCVector<CFileRevision> &_Files);

		bool f_GetClients(NStr::CStr const &_SearchPattern, NContainer::TCVector<NStr::CStr> &_Clients);

		auto f_GetClients
			(
				NStr::CStr const &_SearchPattern
				, NStr::CStr const &_Stream
				, NStr::CStr const &_User
				, NFunction::TCFunction<void (NStr::CStr const &_Client, NStr::CStr const &_Key, NStr::CStr const &_Value)> const &_fProcessor
			)
			-> bool
		;

		bool f_GetClient(NStr::CStr const &_ClientName, NStr::CStr &_Contents);
		bool f_GetClient(NStr::CStr const &_ClientName, NFunction::TCFunction<void (NStr::CStr const &_Key, NStr::CStr const &_Value)> const &_fProcessor);
		bool f_GetClient(NStr::CStr const &_Client, CPerforceClient::CClient &o_Client);

		bool f_ShelveChangelist(uint32 _Changelist, bool _bReplaceFiles, bool _bForce, NContainer::TCVector<NStr::CStr> const &_Files);
		bool f_MoveToChangelist(NContainer::TCVector<NStr::CStr> const &_Files, uint32 _ChangeList);
		// Destination 0 selects the current client's default changelist.
		bool f_UnshelveInto(uint32 _SourceChangelist, uint32 _DestinationChangelist, NStr::CStr const &_FileSpec = {});
		bool f_UnshelveWithBranch(uint32 _SourceChangelist, NStr::CStr const &_BranchMapping, uint32 _DestinationChangeList);
		bool f_SubmitChangelist(uint32 _Changelist, bool _bSubmitShelved, uint32 &_FinalChangelist);
		bool f_DeleteChangelist(uint32 _Changelist, bool _bForce = false);
		bool f_RemoveJobsFromChangelist(uint32 _Changelist, NContainer::TCVector<NStr::CStr> const &_Jobs);
		bool f_DeleteShelvedFile(uint32 _Changelist, NStr::CStr const &_File, bool _bForce = false);
		bool f_ResolveSafe(NStr::CStr const &_File, uint32 _Changelist = 0);
		bool f_ResolveAutomatic(NStr::CStr const &_File, uint32 _Changelist = 0, bool _bRequireResolved = false);
		bool f_GetUnresolvedFiles(NStr::CStr const &_File, uint32 _Changelist, NContainer::TCVector<CUnresolvedFile> &o_Files);
		bool f_ResolveMine(NStr::CStr const &_File, uint32 _Changelist = 0);

		auto f_CopyStream
			(
				NStr::CStr const &_FromStream
				, NStr::CStr const &_ToStream
				, bool _bPretend
				, NContainer::TCVector<CIntegrationResult> &o_Integrated
				, NContainer::TCVector<NStr::CStr> &o_MustSync
				, NContainer::TCVector<CMergeError> &o_Errors
				, NStr::CStr const &_ToFileSpec = {}
			)
			-> bool
		;
		auto f_MergeStream
			(
				NStr::CStr const &_FromStream
				, NStr::CStr const &_ToStream
				, bool _bPretend
				, NContainer::TCVector<CIntegrationResult> &o_Integrated
				, NContainer::TCVector<NStr::CStr> &o_MustSync
				, NContainer::TCVector<CMergeError> &o_Errors
				, NStr::CStr const &_ToFileSpec = {}
			)
			-> bool
		;
		auto f_IntegrateStream
			(
				NStr::CStr const &_FromStream
				, NStr::CStr const &_ToStream
				, bool _bPretend
				, NContainer::TCVector<CIntegrationResult> &o_Integrated
				, NContainer::TCVector<NStr::CStr> &o_MustSync
				, NContainer::TCVector<CMergeError> &o_Errors
				, NStr::CStr const &_ToFileSpec = {}
			)
			-> bool
		;
		auto f_IntegrateFiles
			(
				NStr::CStr const &_FromFiles
				, NStr::CStr const &_ToFiles
				, bool _bPretend
				, bool _bEnableBaseless
				, NContainer::TCVector<CIntegrationResult> &o_Integrated
				, NContainer::TCVector<NStr::CStr> &o_MustSync
				, NContainer::TCVector<CMergeError> &o_Errors
			)
			-> bool
		;

		auto f_CopyStreamToParent
			(
				NStr::CStr const &_FromStream
				, bool _bPretend
				, NContainer::TCVector<CIntegrationResult> &o_Integrated
				, NContainer::TCVector<NStr::CStr> &o_MustSync
				, NContainer::TCVector<CMergeError> &o_Errors
				, NStr::CStr const &_ToFileSpec = {}
			)
			-> bool
		;
		auto f_MergeStreamToParent
			(
				NStr::CStr const &_FromStream
				, bool _bPretend
				, NContainer::TCVector<CIntegrationResult> &o_Integrated
				, NContainer::TCVector<NStr::CStr> &o_MustSync
				, NContainer::TCVector<CMergeError> &o_Errors
				, NStr::CStr const &_ToFileSpec = {}
			)
			-> bool
		;
		auto f_IntegrateStreamToParent
			(
				NStr::CStr const &_FromStream
				, bool _bPretend
				, NContainer::TCVector<CIntegrationResult> &o_Integrated
				, NContainer::TCVector<NStr::CStr> &o_MustSync
				, NContainer::TCVector<CMergeError> &o_Errors
				, NStr::CStr const &_ToFileSpec = {}
			)
			-> bool
		;

		auto f_CopyStreamFromParent
			(
				NStr::CStr const &_ToStream
				, bool _bPretend
				, NContainer::TCVector<CIntegrationResult> &o_Integrated
				, NContainer::TCVector<NStr::CStr> &o_MustSync
				, NContainer::TCVector<CMergeError> &o_Errors
				, NStr::CStr const &_ToFileSpec = {}
			)
			-> bool
		;
		auto f_MergeStreamFromParent
			(
				NStr::CStr const &_ToStream
				, bool _bPretend
				, NContainer::TCVector<CIntegrationResult> &o_Integrated
				, NContainer::TCVector<NStr::CStr> &o_MustSync
				, NContainer::TCVector<CMergeError> &o_Errors
				, NStr::CStr const &_ToFileSpec = {}
			)
			-> bool
		;
		auto f_IntegrateStreamFromParent
			(
				NStr::CStr const &_ToStream
				, bool _bPretend
				, NContainer::TCVector<CIntegrationResult> &o_Integrated
				, NContainer::TCVector<NStr::CStr> &o_MustSync
				, NContainer::TCVector<CMergeError> &o_Errors
				, NStr::CStr const &_ToFileSpec = {}
			)
			-> bool
		;

		bool f_CreatePatch(NStr::CStr const &_Branch, bool _bFullContext, NStr::CStr &o_Patch);

		bool f_GetStream(NStr::CStr const &_StreamName, CStream &_Stream);

		bool f_DeleteStream(NStr::CStr const &_StreamName);
		bool f_OpenStreamSpec();

		bool f_Obliterate(NStr::CStr const &_Path);

		bool f_StreamExists(NStr::CStr const &_StreamName);

		bool f_SetStream(NStr::CStr const &_StreamName, CStream const &_Stream);

		bool f_PopulateStream(NStr::CStr const &_StreamName);

		bool f_FindStreams(NStr::CStr const &_SearchQuery, NContainer::TCVector<NStr::CStr> &o_Streams, NContainer::TCVector<NStr::CStr> const &_StreamSpecs = {});
		bool f_FindStreamsByViewMatch(NContainer::TCVector<NStr::CStr> const &_Views, NContainer::TCVector<NStr::CStr> &o_Streams);

		bool f_GetOpened(NStr::CStr const &_Path, NStr::CStr const &_Client, NContainer::TCVector<NStr::CStr> &o_Opened);
		bool f_GetOpenedFiles(NStr::CStr const &_Path, NStr::CStr const &_Client, NContainer::TCVector<COpenedFile> &o_Opened);

		bool f_SwitchWorkspaceStream(NStr::CStr const &_Workspace, NStr::CStr const &_Stream);
		bool f_DeleteWorkspace(NStr::CStr const &_Workspace);

		bool f_GetEnvVar(NStr::CStr const &_Var, NStr::CStr &_Value);
		bool f_SetEnvVar(NStr::CStr const &_Var, NStr::CStr const &_Value);

		bool f_FileStats(NStr::CStr const &_File, CFileStats &_Stats);
		bool f_OpenedFileStats(NStr::CStr const &_File, CFileStats &_Stats);
		bool f_HaveFileStats(NStr::CStr const &_File, CFileStats &_Stats);
		bool f_DepotFileStats(NStr::CStr const &_File, CFileStats &_Stats);
		bool f_GetRevisionTypes(NContainer::TCVector<NStr::CStr> const &_Revisions, NContainer::TCVector<NStr::CStr> &o_Types);

		auto f_CreateStreamClient
			(
				NStr::CStr const &_ClientName
				, NStr::CStr const &_Root
				, NStr::CStr const &_AltRoot
				, NStr::CStr const &_Template
				, NStr::CStr const &_Stream
				, NContainer::TCVector<NStr::CStr> const *_pOptions = nullptr
			)
			-> bool
		;
		bool f_CreateClient(NStr::CStr const &_ClientName, NStr::CStr const &_Root, NStr::CStr const &_AltRoot, NStr::CStr const &_Template);
		auto f_UpdateStreamClient
			(
				NStr::CStr const &_ClientName
				, NStr::CStr const &_Root
				, NStr::CStr const &_AltRoot
				, NStr::CStr const &_Template
				, NStr::CStr const &_Stream
				, NContainer::TCVector<NStr::CStr> const *_pOptions = nullptr
			)
			-> bool
		;
		bool f_UpdateClient(NStr::CStr const &_ClientName, NStr::CStr const &_Root, NStr::CStr const &_AltRoot, NStr::CStr const &_Template);

		bool f_CreateBranch(NStr::CStr const &_Name, CBranchSpec const &_BranchSpec);
		bool f_GetBranchForStreams(NStr::CStr const &_From, NStr::CStr const &_To, CPerforceClient::CBranchSpec &o_Branch);

		bool f_DeleteBranch(NStr::CStr const &_Name);

		static NStr::CStr fs_FixWhiteSpace(NStr::CStr const &_In);
		static NStr::CStr fs_FixLineStartingTabs(NStr::CStr const &_In);
		static NStr::CStr fs_FixSpecialChars(NStr::CStr const &_In);
		static NStr::CStr fs_EscapeFileName(NStr::CStr const &_FileName);
		static NStr::CStr fs_UnescapeFileName(NStr::CStr const &_FileName);

		static NStr::CStr fs_ActionToStr(EAction _Action);

		static bool fs_HasP4Config(NStr::CStr const &_Path);
		static auto fs_HasP4Config
			(
				NStr::CStr const &_Path
				, NContainer::TCMap<NStr::CStr, NStr::CStr> const &_Environment
				, CPerforceIgnore::CRegistrySettings const &_Registry
				, NStr::CStr const &_ConfigRoot
			)
			-> bool
		;
		static CConnectionInfo fs_GetConnectionInfo(NStr::CStr const &_Directory);
		static auto fs_GetConnectionInfo(NStr::CStr const &_Directory, CPerforceIgnore::CSettings const &_Settings, NContainer::TCMap<NStr::CStr, NStr::CStr> const &_Environment)
			-> CConnectionInfo
		;

	private:
		struct CP4Client;

		void fp_Run(char const *_pFunc);
		void fp_Run(char const *_pFunc, NContainer::TCVector<NStr::CStr> const &_Arguments);
		void fp_CheckDropped();
		bool fp_FileStatClientFiles(NContainer::TCVector<NStr::CStr> const &_Arguments, NContainer::TCVector<NStr::CStr> &o_ClientFiles, NContainer::TCVector<CFileStats> *o_pStats);
		auto fp_GetFileStatFilesUnder
			(
				NStr::CStr const &_LocalRoot
				, NContainer::TCVector<NStr::CStr> const &_Arguments
				, NContainer::TCVector<NStr::CStr> &o_Files
				, NContainer::TCVector<CFileStats> *o_pStats
			)
			-> bool
		;
		void fp_SetFileNames(NStr::CStr const &_WorkingDirectory);
		void fp_SetTrans(int _Charset, int _ContentCharset, NStr::CStr const &_WorkingDirectory);
		NStr::CAnsiStr fp_EncodeFileName(NStr::CStr const &_Name) const;
		void fp_SetClientPath();
		void fp_SetIdentity();
		static int fsp_FilterSettingsFile(char const *_pPath, int _Charset, char const *_pDirectory, void *_pContext);

		void fp_ReadMergeResult
			(
				NContainer::TCVector<CIntegrationResult> &o_Integrated
				, NContainer::TCVector<NStr::CStr> &o_MustSync
				, bool _bInverted
				, NContainer::TCVector<CMergeError> &o_Errors
			)
		;
		bool fp_GetNonTaggedClient();
		bool fp_CreatePatchNonTagged(NStr::CStr const &_Branch, bool _bFullContext, NStr::CStr &o_Patch);
		bool fp_DiffRevisionsNonTagged(NStr::CStr const &_File1, NStr::CStr const &_File2, CFileDiff &o_Diff, bool _bAnyTypes);
		bool fp_FileStats(NContainer::TCVector<NStr::CStr> const &_Options, NStr::CStr const &_File, CFileStats &_Stats);

		auto fp_MutateChangelist
			(
				uint32 _ChangeList
				, NStr::CStr const &_Operation
				, bool _bForce
				, NFunction::TCFunction<bool (NStr::CStr &o_NewDesc, NStr::CStr const &_Key, NStr::CStr const &_Data)> &&_fMutator
			)
			-> bool
		;

		NStr::CStr mp_InitError;
		NStr::CStr mp_LastError;
		NStr::CStr mp_LastFunction;
		CP4Client *mp_pClient;
		ClientApi *mp_pAPI;
		Enviro *mp_pEnviro = nullptr; // The API's settings, when the connection's overrides are applied before it reads them
		NStr::CStr mp_ConfigRoot;     // The connection's m_ConfigRoot, which the settings' filter needs before the connection is kept
		bool mp_bUTF8 = false;
		int mp_CommandCharset = 0;    // The CharSetApi charset of the strings the API takes and gives, NOCONV for none
		int mp_FileNamesCharset = 0;  // The charset of file names (SetTrans' fnames), which decides how the API takes them, NOCONV for none
		int mp_ContentCharset = 0;    // The charset the settings select for the contents of files, which a login turning to UTF-8 keeps, NOCONV for none
		NStorage::TCUniquePointer<CPerforceClient> mp_pNonTaggedClient;

		NStr::CStr mp_LoginDirectory; // The directory the session logged in in, whose settings it has
		NStr::CStr mp_ActiveHost;
		NStr::CStr mp_ActiveClient;
		NStr::CStr mp_ActiveUser;
		NStr::CStr mp_ActiveVersion;

		CConnectionInfo mp_ConnectionInfo;

		bool mp_bSupportsParentView = false;
	};

	struct CPerforceClientThrow
	{
		CPerforceClientThrow(NStr::CStr const &_Server = {}, NStr::CStr const &_User = {}, NStr::CStr const &_Client = {}, NStr::CStr const &_Host = {});
		CPerforceClientThrow(CPerforceClient::CConnectionInfo const &_Info);

		void f_ThrowLastError() const;
		CPerforceClient &f_NoThrow();

		NStr::CStr const &f_GetHost() const;
		NStr::CStr const &f_GetServer() const;
		NStr::CStr const &f_GetUser() const;
		NStr::CStr const &f_GetClient() const;
		NStr::CStr f_GetActiveServer();
		NStr::CStr f_GetActiveUser();
		NStr::CStr f_GetActiveClient();
		NStr::CStr f_GetActiveHost();
		CPerforceClient::CConnectionInfo const &f_GetConnectionInfo() const;

		bool f_IsUTF8();
		void f_Login(NStr::CStr const &_Password, NStr::CStr const &_WorkingDir = {});
		NStr::CStr f_GetSecurityLevel();
		NStr::CStr f_GetLoginTicket(NStr::CStr const &_Password, bool _bAllHosts);
		void f_CreateDepot(NStr::CStr const &_Name, NStr::CStr const &_Type);
		void f_SetUserPassword(NStr::CStr const &_User, NStr::CStr const &_Password, NStr::CStr const &_OldPassword = {});
		void f_SetClient(NStr::CStr const &_ClientName, CPerforceClient::CClient const &_Client);
		void f_SetProtections(NContainer::TCVector<NStr::CStr> const &_Lines);
		void f_SetTypeMap(NContainer::TCVector<NStr::CStr> const &_Lines);

		bool f_FileExistsInDepot(NStr::CStr const &_File);
		bool f_FileExistsInDepotNotDeleted(NStr::CStr const &_File);
		bool f_FileExistsInChangeList(NStr::CStr const &_File);
		bool f_FileExists(NStr::CStr const &_File);
		bool f_CanOpenForEdit(NStr::CStr const &_File);

		void f_RemoveFromClient(NStr::CStr const &_File);
		void f_Sync(NStr::CStr const &_File, NContainer::TCVector<NStr::CStr> &_Synced, NContainer::TCVector<NStr::CStr> &_Removed, bool _bPretend = false);
		void f_Sync
			(
				NStr::CStr const &_File
				, NFunction::TCFunction<bool (int64 _TotalBytes, int64 _SyncedBytes)> const &_fProgress = NFunction::TCFunction<bool (int64 _TotalBytes, int64 _SyncedBytes)>()
				, bool _bForce = false
				, NContainer::TCVector<NStr::CStr> const &_MoreFiles = NContainer::TCVector<NStr::CStr>()
			)
		;
		int64 f_GetHeadChangelist(NStr::CStr const &_Path);
		int64 f_GetHeadChangelistUnsafe();
		NContainer::TCVector<NStr::CStr> f_Files(NStr::CStr const &_Search);
		NContainer::TCVector<NStr::CStr> f_Files(NStr::CStr const &_Search, NContainer::TCVector<NStr::CStr> &_Deleted);
		NContainer::TCVector<NStr::CStr> f_ClientFiles(NStr::CStr const &_Search);
		NContainer::TCVector<NStr::CStr> f_GetFileStatClientFiles(NContainer::TCVector<NStr::CStr> const &_Arguments);
		NContainer::TCVector<NStr::CStr> f_GetFileStatFilesUnder(NStr::CStr const &_LocalRoot, NContainer::TCVector<NStr::CStr> const &_Arguments);
		auto f_GetFileStatFilesUnder(NStr::CStr const &_LocalRoot, NContainer::TCVector<NStr::CStr> const &_Arguments, NContainer::TCVector<CPerforceClient::CFileStats> &o_Stats)
			-> NContainer::TCVector<NStr::CStr>
		;
		NStr::CStr f_GetJobSpec();
		NContainer::CRegistry f_GetJobs(NStr::CStr const &_JobView);
		NStr::CStr f_GetTriggers();
		NContainer::CRegistry f_GetUsers();
		NStr::CStr f_GetDepotPath(NStr::CStr const &_ClientPath, bool _bReturnImportedStream = true);
		NStr::CStr f_GetClientPath(NStr::CStr const &_DepotPath);

		NStr::CStr f_GetWorkspacePath(NStr::CStr const &_DepotPath);
		NStr::CStr f_GetTextFileContents(NStr::CStr const &_Path);
		NStorage::TCOptional<NStr::CStr> f_GetTextFileContentsIfExists(NStr::CStr const &_Path);
		NContainer::CByteVector f_GetFileContents(NStr::CStr const &_Path, bool _bExpandKeywords = true);
		NContainer::TCVector<CPerforceClient::CFileDiff> f_DiffOpenedFiles(NContainer::TCVector<NStr::CStr> const &_Files);
		CPerforceClient::CFileDiff f_DiffRevisions(NStr::CStr const &_File1, NStr::CStr const &_File2, bool _bAnyTypes = false);
		NContainer::TCVector<NStr::CStr> f_GetStreams();
		NContainer::TCVector<NStr::CStr> f_GetStreamDepots();

		CPerforceClient::CChangeList f_GetChangelist(uint32 _ChangeList);
		void f_SetChangelistOwner(uint32 _ChangeList, NStr::CStr const &_User);
		void f_SetChangelistClient(uint32 _ChangeList, NStr::CStr const &_Client, bool _bForce = false);
		void f_SetChangelistDescription(uint32 _ChangeList, NStr::CStr const &_Description, bool _bForce = false);

		auto f_GetChangelists(NStr::CStr const &_Path, bool _bIncludeIntegrated, NStr::CStr const &_Workspace = {}, NStr::CStr const &_Status = {})
			-> NContainer::TCVector<CPerforceClient::CChangeList>
		;

		CPerforceClient::CFileRevisions f_GetFileRevisions(NStr::CStr const &_File);
		CPerforceClient::CFileRevisions f_GetFileRevisions(NContainer::TCVector<NStr::CStr> const &_Files);

		bool f_ChangeExists(CPerforceClient::CFix const &_Fix);

		NContainer::TCVector<CPerforceClient::CFix> f_GetFixes(NStr::CStr const &_Job, uint64 _PerforceGUID);
		void f_AddFixes(NStr::CStr const &_Job, NContainer::TCVector<uint32> const &_Fixes, NStr::CStr const &_Status);
		void f_RemoveFixes(NStr::CStr const &_Job, NContainer::TCVector<uint32> const &_Fixes);
		void f_SetJobSpec(NStr::CStr const &_JobSpec);
		void f_SetJob(NStr::CStr const &_Job);
		CPerforceClient::CJob f_GetJob(NStr::CStr const &_Job);
		void f_SetTriggers(NStr::CStr const &_Triggers);
		bool f_JobExists(NStr::CStr const &_Job);
		void f_DeleteJob(NStr::CStr const &_Job);
		CPerforceClient::CDescription f_Describe(uint32 _Changelist);
		CPerforceClient::CDescription f_DescribeShelved(uint32 _Changelist);
		void f_Info();
		NStr::CStr f_GetUserName();
		NStr::CStr f_GetClientName();
		NStr::CStr f_GetClientRoot();
		void f_OpenForEdit(NStr::CStr const &_File);
		void f_OpenForEdit(NStr::CStr const &_File, NStr::CStr const &_Type);
		void f_Submit(NStr::CStr const &_File, NStr::CStr const &_Comment, NStr::CStr const &_Job = {});
		void f_Revert(NStr::CStr const &_File, bool _bOnlyIfUnchanged = false);
		void f_RevertChangelist(uint32 _Changelist, bool _bOnlyIfUnchanged = false, bool _bDeleteAddedFiles = false);
		void f_Add(NStr::CStr const &_File, bool _bLiteral = false);
		void f_Delete(NStr::CStr const &_File);
		void f_Move(NStr::CStr const &_From, NStr::CStr const &_To);
		void f_OpenForEditOrMakeWritable(NStr::CStr const &_File);
		void f_TryOpenForEdit(NStr::CStr const &_File);
		void f_OpenForWrite(NStr::CStr const &_File, bool *o_pRefused = nullptr);

		uint32 f_CreateChangelist(NStr::CStr const &_Comment, NContainer::TCVector<NStr::CStr> const &_Jobs, NContainer::TCVector<NStr::CStr> const &_Files);

		NContainer::TCVector<CPerforceClient::CFileRevision> f_FindHeadFiles(NStr::CStr const &_Pattern);

		NContainer::TCVector<NStr::CStr> f_GetClients(NStr::CStr const &_SearchPattern);

		void f_GetClients
			(
				NStr::CStr const &_SearchPattern
				, NStr::CStr const &_Stream
				, NStr::CStr const &_User
				, NFunction::TCFunction<void (NStr::CStr const &_Client, NStr::CStr const &_Key, NStr::CStr const &_Value)> const &_fProcessor
			)
		;

		NStr::CStr f_GetClient(NStr::CStr const &_ClientName);
		void f_GetClient(NStr::CStr const &_ClientName, NFunction::TCFunction<void (NStr::CStr const &_Key, NStr::CStr const &_Value)> const &_fProcessor);
		CPerforceClient::CClient f_GetClientScruct(NStr::CStr const &_Client);

		void f_ShelveChangelist(uint32 _Changelist, bool _bReplaceFiles, bool _bForce, NContainer::TCVector<NStr::CStr> const &_Files);
		void f_MoveToChangelist(NContainer::TCVector<NStr::CStr> const &_Files, uint32 _ChangeList);
		void f_UnshelveInto(uint32 _SourceChangelist, uint32 _DestinationChangelist, NStr::CStr const &_FileSpec = {});
		void f_UnshelveWithBranch(uint32 _SourceChangelist, NStr::CStr const &_BranchMapping, uint32 _DestinationChangeList);
		uint32 f_SubmitChangelist(uint32 _Changelist, bool _bSubmitShelved);
		void f_DeleteChangelist(uint32 _Changelist, bool _bForce = false);
		void f_RemoveJobsFromChangelist(uint32 _Changelist, NContainer::TCVector<NStr::CStr> const &_Jobs);
		void f_DeleteShelvedFile(uint32 _Changelist, NStr::CStr const &_File, bool _bForce = false);
		void f_ResolveSafe(NStr::CStr const &_File, uint32 _Changelist = 0);
		void f_ResolveAutomatic(NStr::CStr const &_File, uint32 _Changelist = 0, bool _bRequireResolved = false);
		NContainer::TCVector<CPerforceClient::CUnresolvedFile> f_GetUnresolvedFiles(NStr::CStr const &_File, uint32 _Changelist = 0);
		void f_ResolveMine(NStr::CStr const &_File, uint32 _Changelist = 0);

		auto f_CopyStream
			(
				NStr::CStr const &_FromStream
				, NStr::CStr const &_ToStream
				, bool _bPretend
				, NContainer::TCVector<NStr::CStr> &o_MustSync
				, NContainer::TCVector<CPerforceClient::CMergeError> &o_Errors
				, NStr::CStr const &_ToFileSpec = {}
			)
			-> NContainer::TCVector<CPerforceClient::CIntegrationResult>
		;
		auto f_MergeStream
			(
				NStr::CStr const &_FromStream
				, NStr::CStr const &_ToStream
				, bool _bPretend
				, NContainer::TCVector<NStr::CStr> &o_MustSync
				, NContainer::TCVector<CPerforceClient::CMergeError> &o_Errors
				, NStr::CStr const &_ToFileSpec = {}
			)
			-> NContainer::TCVector<CPerforceClient::CIntegrationResult>
		;
		auto f_IntegrateStream
			(
				NStr::CStr const &_FromStream
				, NStr::CStr const &_ToStream
				, bool _bPretend
				, NContainer::TCVector<NStr::CStr> &o_MustSync
				, NContainer::TCVector<CPerforceClient::CMergeError> &o_Errors
				, NStr::CStr const &_ToFileSpec = {}
			)
			-> NContainer::TCVector<CPerforceClient::CIntegrationResult>
		;
		auto f_IntegrateFiles
			(
				NStr::CStr const &_FromFiles
				, NStr::CStr const &_ToFiles
				, bool _bPretend
				, bool _bEnableBaseless
				, NContainer::TCVector<NStr::CStr> &o_MustSync
				, NContainer::TCVector<CPerforceClient::CMergeError> &o_Errors
			)
			-> NContainer::TCVector<CPerforceClient::CIntegrationResult>
		;

		auto f_CopyStreamToParent
			(
				NStr::CStr const &_FromStream
				, bool _bPretend
				, NContainer::TCVector<NStr::CStr> &o_MustSync
				, NContainer::TCVector<CPerforceClient::CMergeError> &o_Errors
				, NStr::CStr const &_ToFileSpec = {}
			)
			-> NContainer::TCVector<CPerforceClient::CIntegrationResult>
		;
		auto f_MergeStreamToParent
			(
				NStr::CStr const &_FromStream
				, bool _bPretend
				, NContainer::TCVector<NStr::CStr> &o_MustSync
				, NContainer::TCVector<CPerforceClient::CMergeError> &o_Errors
				, NStr::CStr const &_ToFileSpec = {}
			)
			-> NContainer::TCVector<CPerforceClient::CIntegrationResult>
		;
		auto f_IntegrateStreamToParent
			(
				NStr::CStr const &_FromStream
				, bool _bPretend
				, NContainer::TCVector<NStr::CStr> &o_MustSync
				, NContainer::TCVector<CPerforceClient::CMergeError> &o_Errors
				, NStr::CStr const &_ToFileSpec = {}
			)
			-> NContainer::TCVector<CPerforceClient::CIntegrationResult>
		;

		auto f_CopyStreamFromParent
			(
				NStr::CStr const &_ToStream
				, bool _bPretend
				, NContainer::TCVector<NStr::CStr> &o_MustSync
				, NContainer::TCVector<CPerforceClient::CMergeError> &o_Errors
				, NStr::CStr const &_ToFileSpec = {}
			)
			-> NContainer::TCVector<CPerforceClient::CIntegrationResult>
		;
		auto f_MergeStreamFromParent
			(
				NStr::CStr const &_ToStream
				, bool _bPretend
				, NContainer::TCVector<NStr::CStr> &o_MustSync
				, NContainer::TCVector<CPerforceClient::CMergeError> &o_Errors
				, NStr::CStr const &_ToFileSpec = {}
			)
			-> NContainer::TCVector<CPerforceClient::CIntegrationResult>
		;
		auto f_IntegrateStreamFromParent
			(
				NStr::CStr const &_ToStream
				, bool _bPretend
				, NContainer::TCVector<NStr::CStr> &o_MustSync
				, NContainer::TCVector<CPerforceClient::CMergeError> &o_Errors
				, NStr::CStr const &_ToFileSpec = {}
			)
			-> NContainer::TCVector<CPerforceClient::CIntegrationResult>
		;

		NStr::CStr f_CreatePatch(NStr::CStr const &_Branch, bool _bFullContext);

		CPerforceClient::CStream f_GetStream(NStr::CStr const &_StreamName);

		void f_DeleteStream(NStr::CStr const &_StreamName);
		void f_OpenStreamSpec();

		void f_Obliterate(NStr::CStr const &_Path);

		bool f_StreamExists(NStr::CStr const &_StreamName);

		void f_SetStream(NStr::CStr const &_StreamName, CPerforceClient::CStream const &_Stream);

		void f_PopulateStream(NStr::CStr const &_StreamName);

		NContainer::TCVector<NStr::CStr> f_FindStreams(NStr::CStr const &_SearchQuery, NContainer::TCVector<NStr::CStr> const &_StreamSpecs = {});
		NContainer::TCVector<NStr::CStr> f_FindStreamsByViewMatch(NContainer::TCVector<NStr::CStr> const &_Views);

		NContainer::TCVector<NStr::CStr> f_GetOpened(NStr::CStr const &_Path, NStr::CStr const &_Client);

		void f_SwitchWorkspaceStream(NStr::CStr const &_Workspace, NStr::CStr const &_Stream);
		void f_DeleteWorkspace(NStr::CStr const &_Workspace);

		NStr::CStr f_GetEnvVar(NStr::CStr const &_Var);
		void f_SetEnvVar(NStr::CStr const &_Var, NStr::CStr const &_Value);

		CPerforceClient::CFileStats f_FileStats(NStr::CStr const &_File);
		CPerforceClient::CFileStats f_OpenedFileStats(NStr::CStr const &_File);
		CPerforceClient::CFileStats f_HaveFileStats(NStr::CStr const &_File);
		CPerforceClient::CFileStats f_DepotFileStats(NStr::CStr const &_File);
		NContainer::TCVector<NStr::CStr> f_GetRevisionTypes(NContainer::TCVector<NStr::CStr> const &_Revisions);

		void f_CreateStreamClient
			(
				NStr::CStr const &_ClientName
				, NStr::CStr const &_Root
				, NStr::CStr const &_AltRoot
				, NStr::CStr const &_Template
				, NStr::CStr const &_Stream
				, NContainer::TCVector<NStr::CStr> const *_pOptions = nullptr
			)
		;
		void f_CreateClient(NStr::CStr const &_ClientName, NStr::CStr const &_Root, NStr::CStr const &_AltRoot, NStr::CStr const &_Template);
		void f_UpdateStreamClient
			(
				NStr::CStr const &_ClientName
				, NStr::CStr const &_Root
				, NStr::CStr const &_AltRoot
				, NStr::CStr const &_Template
				, NStr::CStr const &_Stream
				, NContainer::TCVector<NStr::CStr> const *_pOptions = nullptr
			)
		;
		void f_UpdateClient(NStr::CStr const &_ClientName, NStr::CStr const &_Root, NStr::CStr const &_AltRoot, NStr::CStr const &_Template);

		CPerforceClient::CBranchSpec f_GetBranchForStreams(NStr::CStr const &_From, NStr::CStr const &_To);

		void f_CreateBranch(NStr::CStr const &_Name, CPerforceClient::CBranchSpec const &_BranchSpec);
		void f_DeleteBranch(NStr::CStr const &_Name);

		static bool fs_GetFromP4Config(NStr::CStr const &_Path, CPerforceClientThrow &o_Client);
		static auto fs_GetFromP4Config
			(
				NStr::CStr const &_Path
				, NContainer::TCMap<NStr::CStr, NStr::CStr> const &_Environment
				, CPerforceIgnore::CRegistrySettings const &_Registry
				, NStr::CStr const &_ConfigRoot
			)
			-> NStorage::TCUniquePointer<CPerforceClientThrow>
		;
		static auto fs_GetFromP4ConfigForFile
			(
				NStr::CStr const &_Path
				, NContainer::TCMap<NStr::CStr, NStr::CStr> const &_Environment
				, CPerforceIgnore::CRegistrySettings const &_Registry
				, NStr::CStr const &_ConfigRoot
				, NStr::CStr &o_FileName
			)
			-> NStorage::TCUniquePointer<CPerforceClientThrow>
		;

	private:
		void fp_Throw(bool _bResult) const;

		CPerforceClient mp_Client;
	};
}

#ifndef DMibPNoShortCuts
using namespace NMib::NPerforce;
#endif
