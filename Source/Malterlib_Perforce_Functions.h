// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "Malterlib_Perforce_Wrapper.h"

namespace NMib::NPerforce
{
	struct CPerforceFunctions
	{
		struct COwnedStreams
		{
			void f_AddStream(NStr::CStr const &_FromStream, NStr::CStr const &_ToStream);
			COwnedStreams operator += (COwnedStreams const &_Other);

			NContainer::TCLinkedList<NStorage::TCTuple<NStr::CStr, NStr::CStr>> m_Streams;
			NContainer::TCMap<NStr::CStr, NStr::CStr> m_StreamMap;
		};

		struct CSwitchResult
		{
			NStr::CStr m_Workspace;
			NStr::CStr m_OldStream;
		};

		struct CStrippedRoot
		{
			NStr::CStr m_Root;
			NContainer::TCVector<NStr::CStr> m_StreamPaths;
			bool m_bStripped = false;
		};

		CPerforceFunctions(NStorage::TCUniquePointer<CPerforceClientThrow> &_pClient);
		CPerforceClient::CStream const &f_GetStreamCached(NStr::CStr const &_Stream);
		NStorage::TCUniquePointer<CPerforceClientThrow> &f_GetClientPtr();
		CPerforceClientThrow &f_GetClient();

		NStr::CStr f_GetStreamRootParent(NStr::CStr const &_Stream);

		static NStr::CStr fs_GetCommonPath(NStr::CStr const &_First, NStr::CStr const &_Second, NStr::CStr &o_FirstSuffix, NStr::CStr &o_SecondSuffix);

		NStr::CStr f_GetCurrentStream() const;
		CStrippedRoot f_GetStrippedRoot();
		static NStr::CStr fs_GetDepot(NStr::CStr const &_Stream);
		static NStr::CStr fs_GetStream(NStr::CStr const &_Stream);

		NStr::CStr f_GetFullRoot(NStr::CStr const &_Stream);
		NStr::CStr f_GetFullRoot(NStr::CStr const &_Stream, CStrippedRoot const &_StrippedRoot);

		static bool fs_UsesParentStreamPath(CPerforceClient::CStream const &_Stream);
		static NContainer::TCVector<NStr::CStr> fs_GetStreamPaths(NStr::CStr const &_Depot, CPerforceClient::CStream const &_Stream);
		static CStrippedRoot fs_StripRoot(NStr::CStr const &_Root, NContainer::TCVector<NStr::CStr> const &_StreamPaths, bool _bRemapped);
		static NStr::CStr fs_GetFullRoot(NStr::CStr const &_Client, CStrippedRoot const &_StrippedRoot, NStr::CStr const &_StreamPath);

		NStr::CStr f_GetClientForStream(NStr::CStr const &_Stream, CSwitchResult &o_OldStream, bool _bQuiet = false);
		static NContainer::CRegistryPreserveAll fs_GetRegistry(CPerforceClient::CStream const &_Stream);
		static void fs_InitializeStream(NStr::CStr const &_Stream, CPerforceFunctions &_Functions);
		static CSwitchResult fs_SwitchStream(CPerforceFunctions &_Functions, NStr::CStr const &_StreamName, bool _bDoSync, bool _bQuiet = false);
		NContainer::TCSet<NStr::CStr> f_GetDisabledCreate(CPerforceClient::CStream const &_Stream);
		NStr::CStr f_GetPatchPrefix(CPerforceClient::CStream const &_Stream);

		COwnedStreams f_GetStreamOwned(NStr::CStr const &_Stream, bool _bReversed);

		NStorage::TCUniquePointer<CPerforceClientThrow> &m_pClient;
		NContainer::TCMap<NStr::CStr, CPerforceClient::CStream> m_StreamCache;

	private:
		void fp_GetStreamOwned(NStr::CStr const &_Stream, COwnedStreams &o_Owned, bool _bReversed);
	};

	struct CPerforce_TemporaryStreamSwitcher
	{
		CPerforce_TemporaryStreamSwitcher(CPerforceFunctions &_Functions);
		~CPerforce_TemporaryStreamSwitcher();
		NStr::CStr f_GetClientForStream(NStr::CStr const &_StreamName, bool _bQuite = false);

	private:
		NContainer::TCMap<NStr::CStr, NStr::CStr> mp_OriginalStreams;
		CPerforceFunctions &mp_Functions;
	};
}

#ifndef DMibPNoShortCuts
using namespace NMib::NPerforce;
#endif
