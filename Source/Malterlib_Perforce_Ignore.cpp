// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "Malterlib_Perforce_Ignore.h"
#include "Malterlib_Perforce_FileNames.h"

#include <Mib/File/File>

#undef SetPort

#ifdef DCompiler_MSVC
#	pragma warning(push,2)
#endif
#include "clientapi.h"
#include "ignore.h"
#ifndef DPlatformFamily_Windows
#	include <unistd.h>
#endif
#ifdef DCompiler_MSVC
#	pragma warning(pop)
#endif

namespace NMib::NPerforce
{
	using namespace NStr;
	using namespace NFile;
	using namespace NContainer;

	struct CPerforceIgnore::CInternal
	{
		CMutual m_Lock;
		TCMap<CStr, NStorage::TCUniquePointer<Ignore>> m_Matchers; // Per directory, under m_Lock
		CStr m_IgnoreNames;
		CStr m_ConfigName;
	};

	struct CPerforceRootFiles::CInternal
	{
		CInternal(CStr const &_Root, TCSet<CStr> const &_OtherRepositories, CPerforceIgnore::CSettings const &_Settings);

		bool f_IsDirectoryOfRoot(CStr const &_Directory);
		void f_Walk(TCVector<CStr> *o_pFiles, TCVector<CStr> *o_pExcluded, TCSet<CStr> *o_pRuleFiles);

		CStr const m_Root;
		TCSet<CStr> const m_OtherRepositories;
		CPerforceIgnore::CSettings const m_Settings;
		CPerforceIgnore m_Ignore;
		CMutual m_Lock;
		TCMap<CStr, bool> m_Directories; // Whether each directory asked about is the root's, under m_Lock
	};

	namespace
	{
		// The Perforce API's matcher uses the platform's separator.
		CStr fg_PerforcePath(CStr const &_Path)
		{
#ifdef DPlatformFamily_Windows
			return _Path.f_ReplaceChar('/', '\\');
#else
			return _Path;
#endif
		}
	}

	namespace
	{
#ifdef DPlatformFamily_Windows
		constexpr ch8 const *gc_pHomeVariable = "USERPROFILE";
#else
		constexpr ch8 const *gc_pHomeVariable = "HOME";
#endif

		// The settings of a P4CONFIG or P4ENVIRO file as p4 reads them into o_Settings: a line sets the variable named up to its
		// first '=' to the rest, without the spaces ending it, and a variable already set keeps its value. In a P4CONFIG file
		// $configdir is the file's directory. A file that is missing or can't be read sets nothing.
		void fg_ReadSettingsFile(CStr const &_Path, bool _bConfigFile, TCMap<CStr, CStr> &o_Settings)
		{
			CStr Contents;
			try
			{
				if (!CFile::fs_FileExists(_Path, EFileAttrib_File))
					return;

				Contents = CFile::fs_ReadStringFromFile(_Path, true);
			}
			catch (CException const &)
			{
				return;
			}

			for (auto &Line : Contents.f_Split("\n"))
			{
				aint nLength = Line.f_GetLen();
				if (nLength && Line[nLength - 1] == '\r')
					--nLength;
				while (nLength && Line[nLength - 1] == ' ')
					--nLength;

				CStr Setting = Line.f_Left(nLength);
				auto iEquals = Setting.f_FindChar('=');
				if (iEquals < 0)
					continue;

				CStr Name = Setting.f_Left(iEquals);
				if (o_Settings.f_FindEqual(Name))
					continue;

				CStr Value = Setting.f_Extract(iEquals + 1);
				if (_bConfigFile)
					Value = Value.f_Replace("$configdir", fg_PerforcePath(CFile::fs_GetPath(_Path)));

				o_Settings[Name] = Value;
			}
		}
	}

	// The P4CONFIG file in the directory, whether it exists or not, or empty without P4CONFIG.
	CStr CPerforceIgnore::CSettings::f_GetConfigFile(CStr const &_Directory) const
	{
		if (!m_ConfigName)
			return {};

		return CFile::fs_GetExpandedPath(CFile::fs_GetMalterlibPath(m_ConfigName), _Directory);
	}

	// The variables of the environment fs_GetSettings reads.
	TCVector<CStr> CPerforceIgnore::fs_GetEnvironmentVariables()
	{
		return {"P4CONFIG", "P4IGNORE", "P4ENVIRO", gc_pHomeVariable};
	}

	// The settings p4 run in the directory uses, read from _Environment in place of the process's environment, the P4CONFIG files
	// and the enviro file, without the Windows registry. As in p4, a P4CONFIG file outranks the environment, which outranks the
	// enviro file, a variable set empty is unset and hides what ranks below it, and $home is the environment's home directory.
	// P4ENVIRO is the environment's, and P4CONFIG is not read from a P4CONFIG file. Without P4IGNORE the ignore files are p4's
	// default: .p4ignore and p4ignore.txt above a path and in the home directory.
	auto CPerforceIgnore::fs_GetSettings(CStr const &_Directory, TCMap<CStr, CStr> const &_Environment) -> CSettings
	{
		CSettings Settings;

		// The home directory is without a separator ending it, as p4 has it.
		auto *pHome = _Environment.f_FindEqual(gc_pHomeVariable);
		CStr Home = pHome ? *pHome : CStr();
		if (Home.f_EndsWith("/") || Home.f_EndsWith("\\"))
			Home = Home.f_Left(Home.f_GetLen() - 1);

		// The enviro file's default is the home directory's .p4enviro, which p4 has on Windows only when P4ENVIRO names it.
		if (auto *pEnviroFile = _Environment.f_FindEqual("P4ENVIRO"); pEnviroFile && *pEnviroFile)
			Settings.m_EnviroFile = CFile::fs_GetExpandedPath(CFile::fs_GetMalterlibPath(pEnviroFile->f_Replace("$home", Home)), _Directory);
#ifndef DPlatformFamily_Windows
		else if (pHome && *pHome)
			Settings.m_EnviroFile = CFile::fs_GetMalterlibPath(Home + "/.p4enviro");
#endif

		TCMap<CStr, CStr> EnviroSettings;
		if (Settings.m_EnviroFile)
			fg_ReadSettingsFile(Settings.m_EnviroFile, false, EnviroSettings);

		auto fGet = [&](TCMap<CStr, CStr> const *_pConfigSettings, ch8 const *_pName) -> CStr
			{
				CStr const *pValue = _pConfigSettings ? _pConfigSettings->f_FindEqual(_pName) : nullptr;
				if (!pValue)
					pValue = _Environment.f_FindEqual(_pName);
				if (!pValue)
					pValue = EnviroSettings.f_FindEqual(_pName);

				return pValue ? pValue->f_Replace("$home", Home) : CStr();
			}
		;

		Settings.m_ConfigName = fGet(nullptr, "P4CONFIG");

		// The P4CONFIG files of the directory and those above it, the nearest first.
		TCMap<CStr, CStr> ConfigSettings;
		if (Settings.m_ConfigName)
		{
			CStr ConfigName = CFile::fs_GetMalterlibPath(Settings.m_ConfigName);
			for (CStr Directory = _Directory; Directory;)
			{
				fg_ReadSettingsFile(CFile::fs_GetExpandedPath(ConfigName, Directory), true, ConfigSettings);

				CStr Parent = CFile::fs_GetPath(Directory);
				if (Parent == Directory)
					break;
				Directory = Parent;
			}
		}

		if (CStr IgnoreNames = fGet(&ConfigSettings, "P4IGNORE"))
			Settings.m_IgnoreNames = fs_ResolveIgnoreNames(IgnoreNames, _Directory);
		else if (pHome)
			Settings.m_IgnoreNames = fs_ResolveIgnoreNames(".p4ignore;p4ignore.txt;{}/.p4ignore;{}/p4ignore.txt"_f << Home << Home, _Directory);
		else
			Settings.m_IgnoreNames = ".p4ignore;p4ignore.txt";

		return Settings;
	}

	// The names of a P4IGNORE as p4 run in _Directory takes them, joined with ';'. The matcher opens a path from the process's
	// current directory, so relative paths are resolved against _Directory. As in the matcher, a list (split on ';', and ':'
	// outside Windows) gets the platform's separators while a single name is kept, and only a name with the platform's
	// separator is a path; any other is looked for in every directory above a path.
	CStr CPerforceIgnore::fs_ResolveIgnoreNames(CStr const &_IgnoreNames, CStr const &_Directory)
	{
#ifdef DPlatformFamily_Windows
		constexpr ch8 c_PathSeparator = '\\';
		bool bList = _IgnoreNames.f_FindChar(';') >= 0;
#else
		constexpr ch8 c_PathSeparator = '/';
		bool bList = _IgnoreNames.f_FindChars(";:") >= 0;
#endif
		TCVector<CStr> Names = {_IgnoreNames};
		if (bList)
		{
#ifdef DPlatformFamily_Windows
			Names = _IgnoreNames.f_ReplaceChar('/', '\\').f_Split(";");
#else
			Names = _IgnoreNames.f_ReplaceChar('\\', '/').f_ReplaceChar(':', ';').f_Split(";");
#endif
		}

		TCVector<CStr> Resolved;
		for (auto &Name : Names)
		{
			if (Name.f_IsEmpty())
				continue;

			if (Name.f_FindChar(c_PathSeparator) < 0)
			{
				Resolved.f_Insert(Name);
				continue;
			}

			CStr Path = Name.f_ReplaceChar('\\', '/');
#ifdef DPlatformFamily_Windows
			// \Config\ignore is on the directory's drive, where p4 runs, and D:Config\ignore on that drive is relative to the
			// directory. On another drive the path stays relative to that drive's current directory, which p4 shares.
			if (_Directory.f_GetLen() >= 2 && _Directory[1] == ':')
			{
				CStr Drive = _Directory.f_Left(2);
				if (Path.f_StartsWith("/") && !Path.f_StartsWith("//"))
					Path = Drive + Path;
				else if (Path.f_GetLen() >= 2 && Path[1] == ':' && (Path.f_GetLen() == 2 || Path[2] != '/'))
				{
					if (Path.f_Left(2).f_CmpNoCase(Drive) != 0)
					{
						Resolved.f_Insert(Name);
						continue;
					}
					Path = Path.f_Extract(2);
				}
			}
#endif
			if (CFile::fs_IsPathAbsolute(Path))
				Resolved.f_Insert(Path == Name.f_ReplaceChar('\\', '/') ? Name : fg_PerforcePath(Path));
			else
				Resolved.f_Insert(fg_PerforcePath(CFile::fs_GetExpandedPath(Path, _Directory)));
		}

		return CStr::fs_Join(Resolved, ";");
	}

	CPerforceIgnore::CPerforceIgnore(CSettings const &_Settings)
		: mp_pInternal(fg_Construct())
	{
		mp_pInternal->m_IgnoreNames = _Settings.m_IgnoreNames;
		mp_pInternal->m_ConfigName = _Settings.m_ConfigName;
	}

	CPerforceIgnore::~CPerforceIgnore() = default;

	// _Path is absolute. A directory matches as where p4 walks a workspace and leaves it out with everything below: a rule naming
	// the directory excludes it, while one naming what is below it, like "/BuildSystem/", excludes only the files.
	bool CPerforceIgnore::f_IsIgnored(CStr const &_Path, bool _bDirectory) const
	{
		auto &Internal = *mp_pInternal;
		CStr Path = fg_PerforcePath(_Path);
		StrRef PathRef(Path.f_GetStr());
		StrRef IgnoreNames(Internal.m_IgnoreNames.f_GetStr());
		char const *pConfigName = Internal.m_ConfigName.f_IsEmpty() ? nullptr : Internal.m_ConfigName.f_GetStr();

		DMibLock(Internal.m_Lock);
#ifdef DPlatformFamily_Windows
		CUTF8FileNames UTF8FileNames;
#endif
		// A matcher caches the last directory's rules assuming a walk, and answers wrongly when asked about other directories in
		// between, so each directory has its own.
		auto &pMatcher = Internal.m_Matchers[CFile::fs_GetPath(_Path)];
		if (!pMatcher)
			pMatcher = fg_Construct();

		if (_bDirectory)
			return pMatcher->RejectDir(PathRef, IgnoreNames, pConfigName) != 0;

		return pMatcher->Reject(PathRef, IgnoreNames, pConfigName) != 0;
	}

	CPerforceRootFiles::CInternal::CInternal(CStr const &_Root, TCSet<CStr> const &_OtherRepositories, CPerforceIgnore::CSettings const &_Settings)
		: m_Root(_Root)
		, m_OtherRepositories(_OtherRepositories)
		, m_Settings(_Settings)
		, m_Ignore(_Settings)
	{
	}

	// A directory is not the root's when it is another repository's, by location or by its .git, a symbolic link, or ignored.
	bool CPerforceRootFiles::CInternal::f_IsDirectoryOfRoot(CStr const &_Directory)
	{
		{
			DMibLock(m_Lock);
			if (auto *pKnown = m_Directories.f_FindEqual(_Directory))
				return *pKnown;
		}

		bool bOfRoot
			= !m_OtherRepositories.f_FindEqual(_Directory)
			&& !CFile::fs_FileExists(_Directory, EFileAttrib_Link)
			&& !CFile::fs_FileExists(_Directory / ".git", EFileAttrib_Directory | EFileAttrib_File)
			&& !m_Ignore.f_IsIgnored(_Directory, true)
		;

		DMibLock(m_Lock);
		m_Directories[_Directory] = bOfRoot;

		return bOfRoot;
	}

	CPerforceRootFiles::CPerforceRootFiles(CStr const &_Root, TCSet<CStr> const &_OtherRepositories, CPerforceIgnore::CSettings const &_Settings)
		: mp_pInternal(fg_Construct(_Root, _OtherRepositories, _Settings))
	{
	}

	CPerforceRootFiles::~CPerforceRootFiles() = default;

	auto CPerforceRootFiles::f_GetSettings() const -> CPerforceIgnore::CSettings const &
	{
		return mp_pInternal->m_Settings;
	}

	// Whether f_List would list the file, which need not exist. _Path is absolute and spelled like the root.
	bool CPerforceRootFiles::f_Contains(CStr const &_Path) const
	{
		auto &Internal = *mp_pInternal;
		if (!_Path.f_StartsWith(Internal.m_Root + "/"))
			return false;

		for (aint iSeparator = _Path.f_Find(aint(Internal.m_Root.f_GetLen() + 1), "/"); iSeparator >= 0; iSeparator = _Path.f_Find(iSeparator + 1, "/"))
		{
			if (!Internal.f_IsDirectoryOfRoot(_Path.f_Left(iSeparator)))
				return false;
		}

		return !CFile::fs_FileExists(_Path, EFileAttrib_Link) && !Internal.m_Ignore.f_IsIgnored(_Path, false);
	}

	// Without o_pExcluded an unlistable directory fails the walk: it reads as empty and would drop its files unnoticed. One that is
	// readable but not searchable gives names without attributes, which are dropped too, and fs_CheckFileRights checks only reading.
	void CPerforceRootFiles::CInternal::f_Walk(TCVector<CStr> *o_pFiles, TCVector<CStr> *o_pExcluded, TCSet<CStr> *o_pRuleFiles)
	{
		// The ignore files of bare names, looked for in every directory above a path, the P4CONFIG files of p4 run in the root and
		// the enviro file, whether they exist or not. A P4CONFIG with a path is looked for from each directory too, unless it is
		// absolute.
		TCSet<CStr> IgnoreNames;
		if (o_pRuleFiles)
		{
			// A name with the platform's separator is a path, as the matcher tells them.
#ifdef DPlatformFamily_Windows
			constexpr ch8 c_PathSeparator = '\\';
#else
			constexpr ch8 c_PathSeparator = '/';
#endif
			for (auto &Name : m_Settings.m_IgnoreNames.f_Split(";"))
			{
				if (Name.f_IsEmpty())
					continue;
				if (Name.f_FindChar(c_PathSeparator) < 0)
					IgnoreNames[Name];
				else
					(*o_pRuleFiles)[CFile::fs_GetMalterlibPath(Name)];
			}

			if (m_Settings.m_EnviroFile)
				(*o_pRuleFiles)[m_Settings.m_EnviroFile];

			CStr ConfigName = CFile::fs_GetMalterlibPath(m_Settings.m_ConfigName);
			bool bAbsoluteConfig = ConfigName && CFile::fs_IsPathAbsolute(ConfigName);
			if (bAbsoluteConfig)
				(*o_pRuleFiles)[ConfigName];

			for (CStr Directory = m_Root; Directory;)
			{
				if (ConfigName && !bAbsoluteConfig)
					(*o_pRuleFiles)[Directory / ConfigName];
				if (Directory != m_Root)
				{
					for (auto &Name : IgnoreNames)
						(*o_pRuleFiles)[Directory / Name];
				}

				CStr Parent = CFile::fs_GetPath(Directory);
				if (Parent == Directory)
					break;
				Directory = Parent;
			}
		}

		// A name with a wildcard character would match other files, and the wildcards have no escape. The root itself is every
		// file.
		auto fInsertExcluded = [&](CStr const &_Path, bool _bDirectory)
			{
				CStr Relative = CFile::fs_MakePathRelative(_Path, m_Root);
				if (Relative.f_FindChars("*?") >= 0)
					return;

				if (Relative.f_IsEmpty())
					o_pExcluded->f_Insert("*");
				else
					o_pExcluded->f_Insert(Relative + (_bDirectory ? "/*" : ""));
			}
		;

		TCVector<CStr> Directories = {m_Root};

		while (!Directories.f_IsEmpty())
		{
			CStr Directory = Directories.f_GetLast();
			Directories.f_Remove(Directories.f_GetLen() - 1);

			// A listing that fails part of the way fails as a whole.
			TCVector<CFile::CFoundFile> Entries;
#ifdef DPlatformFamily_Windows
			bool bListable = CFile::fs_CheckFileRights(Directory, EFileRight_Read) != ECheckFileRights_NoAccess;
#else
			bool bListable = access(Directory.f_GetStr(), R_OK | X_OK) == 0;
#endif
			if (bListable)
			{
				try
				{
					CFile::fs_FindFilesGeneral
						(
							Directory / "*"
							, [&](CStr const &_Path, EFileAttrib _Attribs, bool _bPreRecurse)
							{
								if (!_bPreRecurse)
								{
									auto &Entry = Entries.f_Insert();
									Entry.m_Path = _Path;
									Entry.m_Attribs = _Attribs;
								}

								return false;
							}
						)
					;
				}
				catch (CExceptionFile const &)
				{
					bListable = false;
				}
			}

			if (!bListable)
			{
				if (!o_pExcluded)
					DMibError("'{}' cannot be listed"_f << Directory);

				fInsertExcluded(Directory, true);
				continue;
			}

			// A directory of the root that becomes another repository's, as its .git is created, is no longer the root's. A .git
			// that is there is not followed, as git changes the time of its directory with every command.
			if (o_pRuleFiles)
			{
				(*o_pRuleFiles)[Directory / ".git"];
				for (auto &Name : IgnoreNames)
					(*o_pRuleFiles)[Directory / Name];
			}

			for (auto &Entry : Entries)
			{
				bool bDirectory = (Entry.m_Attribs & EFileAttrib_Directory) != 0;

				bool bOfRoot = bDirectory ? f_IsDirectoryOfRoot(Entry.m_Path) : !(Entry.m_Attribs & EFileAttrib_Link) && !m_Ignore.f_IsIgnored(Entry.m_Path, false);
				if (!bOfRoot)
				{
					if (o_pExcluded)
						fInsertExcluded(Entry.m_Path, bDirectory);
				}
				else if (bDirectory)
					Directories.f_Insert(Entry.m_Path);
				else if (o_pFiles)
					o_pFiles->f_Insert(CFile::fs_MakePathRelative(Entry.m_Path, m_Root));
			}
		}
	}

	// The root's files, relative to it and sorted. Fails when a directory of the root cannot be listed.
	TCVector<CStr> CPerforceRootFiles::f_List() const
	{
		TCVector<CStr> Files;
		mp_pInternal->f_Walk(&Files, nullptr, nullptr);
		Files.f_Sort();

		return Files;
	}

	// Wildcards relative to the root, sorted, for what f_List leaves out, for rules that know no P4IGNORE: "<Directory>/*" for a
	// directory, also one that cannot be listed, and the path of a file. A file excluded after the walk is not among them, nor a
	// path with a wildcard character. o_pRuleFiles gets the ignore and P4CONFIG files p4 reads for the root, also those that don't
	// exist, so that one created later can be noticed.
	TCVector<CStr> CPerforceRootFiles::f_ListExcluded(TCVector<CStr> *o_pRuleFiles) const
	{
		TCVector<CStr> Excluded;
		TCSet<CStr> RuleFiles;
		mp_pInternal->f_Walk(nullptr, &Excluded, o_pRuleFiles ? &RuleFiles : nullptr);
		Excluded.f_Sort();

		if (o_pRuleFiles)
		{
			for (auto &RuleFile : RuleFiles)
				o_pRuleFiles->f_Insert(RuleFile);
		}

		return Excluded;
	}
}
