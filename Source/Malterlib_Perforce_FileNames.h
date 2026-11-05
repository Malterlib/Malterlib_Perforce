// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#pragma once

#include <Mib/Core/Core>

class Enviro; // NOLINT(malterlib-type-naming): the name in the Perforce API

namespace NMib::NPerforce
{
	enum EPathType
	{
		EPathType_Missing
		, EPathType_File
		, EPathType_Directory
		, EPathType_Special // A FIFO, socket or device, which blocks or fails a reader
	};

	EPathType fg_GetPathType(NStr::CStr const &_Path);
	NStr::CStr fg_DirectoryPrefix(NStr::CStr const &_Directory);
	NStr::CStr fg_PerforcePath(NStr::CStr const &_Path);
	NStr::CStr fg_MalterlibPath(NStr::CStr const &_Path);
	NStr::CStr fg_ConvertCharset(NStr::CStr const &_Str, int _Charset, bool _bToCharset);

	// An encoding of the strings the Perforce API takes and gives.
	enum EPerforceEncoding
	{
		EPerforceEncoding_UTF8       // UTF-8, as Malterlib's strings are; outside Windows the bytes of names as they are
		, EPerforceEncoding_CodePage // Windows' active code page, which the API's file functions take a name in that isn't Unicode
	};

	// The charsets of a p4 run and the encoding of its file names, as fg_PerforceCharsets selects them.
	struct CPerforceCharsets
	{
		int m_Command = 0;   // The CharSetApi charset of arguments, output and dialogs (SetTrans' output and dialog), NOCONV untranslated
		int m_Content = 0;   // The charset of files' contents (SetTrans' content)
		int m_FileNames = 0; // The charset of file names (SetTrans' fnames): the settings' (Enviro::SetCharSet) and GlobalCharSet
#ifdef DPlatformFamily_Windows
		EPerforceEncoding m_FileSystem = EPerforceEncoding_CodePage;
#else
		EPerforceEncoding m_FileSystem = EPerforceEncoding_UTF8;
#endif
		NStr::CStr m_Error;  // Why p4 stops, as for an unknown charset; the charsets are then untranslated
		bool m_bTranslated = false;
	};

	EPerforceEncoding fg_FileNameEncoding(int _FileNamesCharset);
	CPerforceCharsets fg_PerforceCharsets(ch8 const *_pCharset, ch8 const *_pCommandCharset, ch8 const *_pLearnedCharset, Enviro *_pEnviro);
	NStr::CStr fg_DecodePerforceString(NStr::CAnsiStr const &_Bytes, EPerforceEncoding _Encoding);
	NStr::CAnsiStr fg_EncodePerforceString(NStr::CStr const &_String, EPerforceEncoding _Encoding);

#ifdef DPlatformFamily_Windows
	// Makes the Perforce API name files in a CharSetApi charset, like UTF-8, as Malterlib's paths are, on this thread only while it
	// lives.
	struct CFileNameCharset
	{
		explicit CFileNameCharset(int _Charset);
		~CFileNameCharset();
	};

	EPerforceEncoding fg_APIFileNameEncoding();
	NStr::CStr fg_WindowsFullPath(NStr::CStr const &_Path);
#endif
}
