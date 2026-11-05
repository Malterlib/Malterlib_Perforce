// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#pragma once

#include <Mib/Core/Core>
#include <Mib/Container/Map>
#include <Mib/Container/Set>
#include <Mib/Container/Vector>
#include <Mib/File/File>
#include <Mib/Storage/UniquePointer>

namespace NMib::NPerforce
{
	// Directories identified as the file system identifies them, by the file they are, so every spelling the file system takes for
	// one is that one. A path that doesn't exist is identified by its nearest existing ancestor and the rest of it, compared
	// exactly.
	struct CPerforcePathSet
	{
		void f_Insert(NStr::CStr const &_Path);
		bool f_Contains(NStr::CStr const &_Path) const;

	private:
		struct CKey
		{
			auto operator <=> (CKey const &_Right) const noexcept = default;

			NFile::CUniqueFileIdentifier m_Existing; // The path's, or its nearest existing ancestor's: the volume and the file on it
			NStr::CStr m_Rest;                       // The rest of the path below that ancestor, empty when the path exists
		};

		static bool fsp_GetKey(NStr::CStr const &_Path, CKey &o_Key);

		NContainer::TCSet<CKey> mp_Keys;
	};

	// The P4IGNORE matching of p4 add and reconcile, through the Perforce API's matcher, without a server. Thread safe.
	struct CPerforceIgnore
	{
		// A setting's value as p4 has it: text, which p4 takes in its file names' encoding, as it reads its environment, like
		// the environment's values and Malterlib's paths, or the bytes of a settings file, which p4 takes as they are in that
		// encoding. Which encoding that is the settings' charsets decide.
		struct CSettingValue
		{
			CSettingValue() = default;
			CSettingValue(NStr::CStr const &_Text);
			CSettingValue(ch8 const *_pText);

			auto operator <=> (CSettingValue const &_Right) const noexcept = default;

			template <typename tf_CStr>
			void f_Format(tf_CStr &o_Str) const;

			static CSettingValue fs_FromBytes(NStr::CAnsiStr const &_Bytes);

			bool f_IsEmpty() const;
			NStr::CStr f_GetDescription() const;

			NStr::CStr m_Text;      // The value when it is text
			NStr::CAnsiStr m_Bytes; // The value when it is bytes
			bool m_bBytes = false;
		};

		// The settings files of a directory, for knowing whether it is a Perforce root and tracking what decides that.
		struct CSettingsFiles
		{
			NStr::CStr m_ConfigFile;                       // The directory's P4CONFIG file p4 opens when it is a regular file, else empty
			NContainer::TCSet<NStr::CStr> m_Dependencies; // The enviro file and every name p4 tries for P4CONFIG, resolved as opened
		};

		struct CSettings
		{
			NContainer::TCVector<CSettingValue> m_IgnoreNames; // File names, looked for in every directory above a path, and paths; none for P4IGNORE=unset
			CSettingValue m_ConfigName;                        // P4CONFIG or empty; as in p4, its files are excluded by a name or relative path, not an absolute one
			NStr::CStr m_EnviroFile;                        // The enviro file the settings were read with, also while missing, or empty
			NStr::CStr m_Charset;                           // P4CHARSET, or empty; the charsets decide on Windows how names and paths are encoded
			NStr::CStr m_CommandCharset;                    // P4COMMANDCHARSET, or empty
			NStr::CStr m_LearnedCharset;                    // Without P4CHARSET the charset p4 learned for P4PORT, P4_<port>_CHARSET, or empty
			NStr::CStr m_LearnedCharsetVariable;            // Without P4CHARSET the name of P4_<port>_CHARSET for P4PORT, or empty

			NStr::CStr f_GetConfigFile(NStr::CStr const &_Directory) const;
			CSettingsFiles f_GetSettingsFiles(NStr::CStr const &_Directory) const;
			NStr::CStr f_GetText(CSettingValue const &_Value) const;
		};

		static NContainer::TCVector<NStr::CStr> fs_GetEnvironmentVariables();
		static bool fs_IsLearnedCharsetVariable(NStr::CStr const &_Name);
		static CSettings fs_GetSettings(NStr::CStr const &_Directory, NContainer::TCMap<NStr::CStr, NStr::CStr> const &_Environment);
		static NContainer::TCVector<CSettingValue> fs_ResolveIgnoreNames(NStr::CStr const &_IgnoreNames, NStr::CStr const &_Directory);

		CPerforceIgnore(CSettings const &_Settings);
		~CPerforceIgnore();

		CPerforceIgnore(CPerforceIgnore const &_Other) = delete;
		CPerforceIgnore &operator = (CPerforceIgnore const &_Other) = delete;

		bool f_IsIgnored(NStr::CStr const &_Path, bool _bDirectory) const;

	private:
		struct CInternal;
		NStorage::TCUniquePointer<CInternal> mp_pInternal;
	};

	// The files of a Perforce root without git, as `git ls-files --cached --others --exclude-standard` gives a repository's.
	// Thread safe, without a server.
	struct CPerforceRootFiles
	{
		CPerforceRootFiles(NStr::CStr const &_Root, NContainer::TCSet<NStr::CStr> const &_OtherRepositories, CPerforceIgnore::CSettings const &_Settings);
		~CPerforceRootFiles();

		CPerforceRootFiles(CPerforceRootFiles const &_Other) = delete;
		CPerforceRootFiles &operator = (CPerforceRootFiles const &_Other) = delete;

		CPerforceIgnore::CSettings const &f_GetSettings() const;
		bool f_Contains(NStr::CStr const &_Path) const;
		NContainer::TCVector<NStr::CStr> f_List() const;
		NContainer::TCVector<NStr::CStr> f_ListExcluded(NContainer::TCVector<NStr::CStr> *o_pRuleFiles = nullptr) const;

	private:
		struct CInternal;
		NStorage::TCUniquePointer<CInternal> mp_pInternal;
	};
}

#include "Malterlib_Perforce_Ignore.hpp"
