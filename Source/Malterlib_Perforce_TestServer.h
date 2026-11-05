// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#pragma once

#include <Mib/Concurrency/ConcurrencyManager>
#include <Mib/Encoding/Json>
#include <Mib/Process/ProcessLaunchActor>
#include <Mib/Storage/Optional>

namespace NMib::NPerforce
{
	/// A throwaway Perforce server for tests: a p4d on 127.0.0.1 over a fixture directory, with helpers to create depots,
	/// streams and clients and to submit files. Its sessions use the fixture user and never the ambient P4 environment.
	struct CPerforceTestServer : public NConcurrency::CActor
	{
		/// One file of a fixture submit: a stream-relative path and its contents.
		struct CFile
		{
			NStr::CStr m_Path;
			NStr::CStr m_Contents;
		};

		/// The path of the p4d executable, found in PATH, or empty when there is none. Tests that need a server report a
		/// skip when it is empty, since Perforce doesn't ship p4d for every platform.
		static NStr::CStr fs_FindServerExecutable();

		/// Starts the server over "<_Root>/p4d". The fixture user is created by its first command.
		NConcurrency::TCFuture<void> f_Start(NStr::CStr _Root, NStr::CStr _User);

		/// The server address, "127.0.0.1:<port>", for P4PORT.
		NConcurrency::TCFuture<NStr::CStr> f_GetPort();

		NConcurrency::TCFuture<void> f_CreateStreamDepot(NStr::CStr _Depot);
		NConcurrency::TCFuture<void> f_CreateMainlineStream(NStr::CStr _Stream);
		/// A template client that other clients are created from, with build-friendly options (allwrite, clobber, ...).
		/// _Stream is only the template's own binding; clients created from it bind their own stream.
		NConcurrency::TCFuture<void> f_CreateTemplateClient(NStr::CStr _Name, NStr::CStr _Stream);
		/// Creates or updates a stream client rooted at _Root, for tests that work in a workspace themselves.
		NConcurrency::TCFuture<void> f_CreateClient(NStr::CStr _Name, NStr::CStr _Stream, NStr::CStr _Root);
		/// Submits files to a stream through a temporary fixture workspace, adding new files and editing existing ones,
		/// and returns the submitted changelist number.
		NConcurrency::TCFuture<int64> f_SubmitFiles(NStr::CStr _Stream, NContainer::TCVector<CFile> _Files, NStr::CStr _Description);
		/// Sets the fixture user's password, which turns the open server into one that requires authentication.
		NConcurrency::TCFuture<void> f_SetUserPassword(NStr::CStr _Password);
		/// Reads a client spec from the server. The result has "Exists" false when the client doesn't exist.
		NConcurrency::TCFuture<NEncoding::CJsonSorted> f_GetClientSpec(NStr::CStr _ClientName);

	private:
		NConcurrency::TCFuture<void> fp_Destroy() override;

		/// Launches `p4d -r <root> -p <_Port>` and waits until it answers `p4 info`. False on timeout or early exit, which
		/// is what a busy port looks like.
		NConcurrency::TCFuture<bool> fp_LaunchServer(NStr::CStr _Port);
		/// Stops the running server process and waits for it to exit.
		NConcurrency::TCFuture<void> fp_StopServer();

		NStr::CStr mp_FixtureRoot;
		NStr::CStr mp_ServerRoot;
		NStr::CStr mp_ServerExecutable;
		NStr::CStr mp_Port;
		NStr::CStr mp_User;
		NStr::CStr mp_Password;
		umint mp_nSubmitWorkspaces = 0;
		NConcurrency::TCActor<NProcess::CProcessLaunchActor> mp_LaunchActor;
		NConcurrency::CActorSubscription mp_LaunchSubscription;
		NStorage::TCOptional<NConcurrency::TCPromise<void>> mp_ExitPromise;
	};
}
