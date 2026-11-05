// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#pragma once

#include <Mib/Core/Core>
#include <Mib/Container/Map>
#include <Mib/Container/Set>
#include <Mib/Container/Vector>
#include <Mib/Storage/UniquePointer>

namespace NMib::NPerforce
{
	// The P4IGNORE matching of p4 add and reconcile, through the Perforce API's matcher, without a server. Thread safe.
	struct CPerforceIgnore
	{
		struct CSettings
		{
			NStr::CStr m_IgnoreNames; // ';'-separated file names, looked for in every directory above a path, and paths
			NStr::CStr m_ConfigName;  // P4CONFIG, whose files are always excluded, or empty
			NStr::CStr m_EnviroFile;  // The enviro file the settings were read with, also while missing, or empty

			NStr::CStr f_GetConfigFile(NStr::CStr const &_Directory) const;
		};

		static NContainer::TCVector<NStr::CStr> fs_GetEnvironmentVariables();
		static CSettings fs_GetSettings(NStr::CStr const &_Directory, NContainer::TCMap<NStr::CStr, NStr::CStr> const &_Environment);
		static NStr::CStr fs_ResolveIgnoreNames(NStr::CStr const &_IgnoreNames, NStr::CStr const &_Directory);

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
