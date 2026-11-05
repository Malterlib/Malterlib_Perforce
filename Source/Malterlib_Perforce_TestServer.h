// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#pragma once

#include <Mib/Concurrency/ConcurrencyManager>
#include <Mib/Encoding/Json>
#include <Mib/Process/ProcessLaunchActor>

#include "Malterlib_Perforce_Wrapper.h"

namespace NMib::NPerforce
{
	// A throwaway p4d on 127.0.0.1 over a fixture directory. Its sessions never use the ambient P4 environment.
	struct CPerforceTestServer : public NConcurrency::CActor
	{
		struct CFile
		{
			NStr::CStr m_Path; // Relative to the stream
			NStr::CStr m_Contents;
		};

		static NStr::CStr fs_FindServerExecutable(NStr::CStr const &_Name = "p4d");

		NConcurrency::TCFuture<void> f_Start(NStr::CStr _Root, NStr::CStr _User);
		NConcurrency::TCFuture<NStr::CStr> f_GetPort();

		NConcurrency::TCFuture<void> f_CreateStreamDepot(NStr::CStr _Depot);
		NConcurrency::TCFuture<void> f_CreateMainlineStream(NStr::CStr _Stream);
		NConcurrency::TCFuture<void> f_CreateTemplateClient(NStr::CStr _Name, NStr::CStr _Stream);
		NConcurrency::TCFuture<void> f_CreateClient(NStr::CStr _Name, NStr::CStr _Stream, NStr::CStr _Root);
		NConcurrency::TCFuture<int64> f_SubmitFiles(NStr::CStr _Stream, NContainer::TCVector<CFile> _Files, NStr::CStr _Description);
		NConcurrency::TCFuture<void> f_SetUserPassword(NStr::CStr _Password);
		NConcurrency::TCFuture<CPerforceClient::CConnectionInfo> f_GetConnectionInfo(NStr::CStr _Client);
		NConcurrency::TCFuture<NEncoding::CJsonSorted> f_GetClientSpec(NStr::CStr _ClientName);

	private:
		NConcurrency::TCFuture<void> fp_Destroy() override;

		NConcurrency::TCFuture<bool> fp_LaunchServer(NStr::CStr _Port);
		NConcurrency::TCFuture<void> fp_StopServer();

		NStr::CStr mp_FixtureRoot;
		NStr::CStr mp_ServerRoot;
		NStr::CStr mp_ServerID;
		NStr::CStr mp_ServerExecutable;
		NStr::CStr mp_Port;
		NStr::CStr mp_User;
		NStr::CStr mp_Password;
		umint mp_nSubmitWorkspaces = 0;
		NConcurrency::TCActor<NProcess::CProcessLaunchActor> mp_LaunchActor;
		NConcurrency::CActorSubscription mp_LaunchSubscription;
	};
}
