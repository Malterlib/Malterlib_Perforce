// Copyright © 2015 Hansoft AB 
// Distributed under the MIT license, see license text in LICENSE.Malterlib

#include "Malterlib_Perforce_Wrapper.h"

namespace NMib::NPerforce
{
	struct CPerforceFunctions
	{
		struct COwnedStreams
		{
			TCLinkedList<TCTuple<CStr, CStr>> m_Streams;
			TCMap<CStr, CStr> m_StreamMap;
			void f_AddStream(CStr const &_FromStream, CStr const &_ToStream)
			{
				auto Created = m_StreamMap(_FromStream);
				if (Created.f_WasCreated())
				{
					m_Streams.f_InsertFirst(fg_Tuple(_FromStream, _ToStream));
					*Created = _ToStream;
				}
				else if (*Created != _ToStream)
					DError(fg_Format("Stream ownership error for '{}'. '{}' != '{}'", _FromStream, *Created, _ToStream));
			}
			
			COwnedStreams operator += (COwnedStreams const &_Other)
			{
				for (auto iStream = _Other.m_Streams.f_GetIterator(); iStream; ++iStream)
					f_AddStream(fg_Get<0>(*iStream), fg_Get<1>(*iStream));
				return *this;
			}
		};
		
	private:
		void fpr_GetStreamOwned(CStr const &_Stream, COwnedStreams &_oOwned, bool _bReversed);
	public:
		
		struct CSwitchResult
		{
			CStr m_Workspace;
			CStr m_OldStream;
		};
	  
		TCUniquePointer<CPerforceClientThrow> &m_pClient;
		TCMap<CStr, CPerforceClient::CStream> m_StreamCache;
		
		CPerforceFunctions(TCUniquePointer<CPerforceClientThrow> &_pClient);
		CPerforceClient::CStream const &f_GetStreamCached(CStr const &_Stream);
		TCUniquePointer<CPerforceClientThrow> &f_GetClientPtr();
		CPerforceClientThrow &f_GetClient();
		
		CStr f_GetStreamRootParent(CStr const &_Stream);
	  
		static CStr fs_GetCommonPath(CStr const &_First, CStr const &_Second, CStr &_oFirstSuffix, CStr &_oSecondSuffix);

		CStr f_GetCurrentStream() const;
		CStr f_GetStrippedRoot();
		static CStr fs_GetDepot(CStr const &_Stream);
		static CStr fs_GetStream(CStr const &_Stream);
		
		CStr f_GetFullRoot(CStr const &_Stream, CStr const &_StrippedRoot = CStr());
		

		CStr f_GetClientForStream(CStr const &_Stream, CSwitchResult &_oOldStream, bool _bQuiet = false);
		static CRegistryPreserveAndOrder_CStr fs_GetRegistry(CPerforceClient::CStream const &_Stream);
		static void fs_InitializeStream(CStr const &_Stream, CPerforceFunctions &_Functions);
		static CSwitchResult fs_SwitchStream(CPerforceFunctions &_Functions, CStr const &_StreamName, bool _bDoSync, bool _bQuiet = false);
		TCSet<CStr> f_GetDisabledCreate(CPerforceClient::CStream const &_Stream);
		CStr f_GetPatchPrefix(CPerforceClient::CStream const &_Stream);

		
		COwnedStreams f_GetStreamOwned(CStr const &_Stream, bool _bReversed);
	};

	class CPerforce_TemporaryStreamSwitcher
	{
	public:
		CPerforce_TemporaryStreamSwitcher(CPerforceFunctions &_Functions);
		~CPerforce_TemporaryStreamSwitcher();
		CStr f_GetClientForStream(CStr const &_StreamName, bool _bQuite = false);
		
	private:
		TCMap<CStr, CStr> mp_OriginalStreams;	
		CPerforceFunctions &mp_Functions;
	};
}

#ifndef DMibPNoShortCuts
	using namespace NMib::NPerforce;
#endif
