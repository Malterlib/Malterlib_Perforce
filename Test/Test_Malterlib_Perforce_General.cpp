// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <Mib/Core/Core>
#include <Mib/File/File>
#include <Mib/Perforce/FileNames>
#include <Mib/Perforce/Functions>
#include <Mib/Test/Exception>

namespace
{
	using namespace NMib;
	using namespace NMib::NStr;
	using namespace NMib::NContainer;
	using namespace NMib::NPerforce;

	struct CGeneral_Tests : public NMib::NTest::CTest
	{
		void f_DoTests();
	};

	void CGeneral_Tests::f_DoTests()
	{
		DMibTestSuite("WorkspaceRoot")
		{
			auto fCheckRoot = [&]
				(
					CStr const &_TestPath
					, CStr const &_CurrentRoot
					, TCVector<CStr> const &_CurrentStreamPaths
					, CStr const &_ExpectedStrippedRoot
					, CStr const &_TargetStreamPath
					, CStr const &_ExpectedFullRoot
				)
				-> void
				{
					DMibTestPath(_TestPath);

					auto StrippedRoot = CPerforceFunctions::fs_StripRoot(_CurrentRoot, _CurrentStreamPaths, false);

					DMibExpect(StrippedRoot.m_Root, ==, _ExpectedStrippedRoot);
					DMibExpectTrue(StrippedRoot.m_bStripped);
					DMibExpect(CPerforceFunctions::fs_GetFullRoot("user_host_Hansoft", StrippedRoot, _TargetStreamPath), ==, _ExpectedFullRoot);
				}
			;

			// The current workspace was switched to a stream with a different name, so nothing can be stripped and initializing fails instead of guessing a root
			auto fCheckNotStripped = [&]
				(CStr const &_TestPath, CStr const &_CurrentRoot, TCVector<CStr> const &_CurrentStreamPaths, CStr const &_TargetStreamPath, CStr const &_ExpectedStreamPaths) -> void
				{
					DMibTestPath(_TestPath);

					auto StrippedRoot = CPerforceFunctions::fs_StripRoot(_CurrentRoot, _CurrentStreamPaths, false);

					DMibExpect(StrippedRoot.m_Root, ==, _CurrentRoot);
					DMibExpectFalse(StrippedRoot.m_bStripped);
					DMibExpectException
						(
							CPerforceFunctions::fs_GetFullRoot("user_host_Hansoft", StrippedRoot, _TargetStreamPath)
							, DMibErrorInstance
							(
								"The root '{}' of workspace 'user_host_Hansoft' does not contain its stream path ({}). Fix the root or switch the workspace back to its stream before "
								"initializing a stream from it"_f
								<< _CurrentRoot
								<< _ExpectedStreamPaths
							)
						)
					;
				}
			;

			fCheckRoot("RoundTrip", "/work/Hansoft/Dev", {"/Hansoft/Dev"}, "/work", "/Hansoft/Dev", "/work/Hansoft/Dev");
			fCheckRoot("OtherStream", "/work/Hansoft/Dev", {"/Hansoft/Dev"}, "/work", "/Hansoft/Main", "/work/Hansoft/Main");

			fCheckNotStripped("NotStripped", "/work/Hansoft/Main", {"/Hansoft/CVD"}, "/Hansoft/Main", "/Hansoft/CVD");
			fCheckNotStripped("NotStrippedOtherStream", "/work/Hansoft/Main", {"/Hansoft/CVD", "/Hansoft/Continuous"}, "/Hansoft/Dev", "/Hansoft/CVD, /Hansoft/Continuous");

			fCheckRoot("Case", "/work/hansoft/dev", {"/Hansoft/Dev"}, "/work", "/Hansoft/Main", "/work/Hansoft/Main");
			fCheckNotStripped("CaseNotStripped", "/work/HANSOFT/MAIN", {"/Hansoft/CVD"}, "/Hansoft/Main", "/Hansoft/CVD");

#ifdef DPlatformFamily_Windows
			fCheckRoot("Backslash", R"(X:\Development\Hansoft\Dev)", {"/Hansoft/Dev"}, "X:\\Development", "/Hansoft/Main", "X:\\Development/Hansoft/Main");
			fCheckNotStripped("BackslashNotStripped", R"(X:\Development\Hansoft\Main)", {"/Hansoft/CVD"}, "/Hansoft/Main", "/Hansoft/CVD");
#endif

			fCheckRoot("OldName", "/work/Hansoft/Development", {"/Hansoft/Dev", "/Hansoft/Development"}, "/work", "/Hansoft/Dev", "/work/Hansoft/Dev");

			// A stripped base that ends with the target stream path still gets it appended, so the new workspace does not overlap the current one
			fCheckRoot("StrippedBaseEndsWithTarget", "/work/depot/Main/depot/Dev", {"/depot/Dev"}, "/work/depot/Main", "/depot/Main", "/work/depot/Main/depot/Main");

			// An already doubled root is kept as it is
			fCheckRoot("AlreadyDoubled", "/work/Hansoft/Dev/Hansoft/Dev", {"/Hansoft/Dev"}, "/work/Hansoft/Dev", "/Hansoft/Dev", "/work/Hansoft/Dev/Hansoft/Dev");

			// Workspaces for remapped streams are created at the base, so a root without the stream path is used as it is
			{
				DMibTestPath("RemappedBase");

				auto StrippedRoot = CPerforceFunctions::fs_StripRoot("/work", {"/depot/Remapped"}, true);

				DMibExpect(StrippedRoot.m_Root, ==, "/work");
				DMibExpectTrue(StrippedRoot.m_bStripped);
				DMibExpect(CPerforceFunctions::fs_GetFullRoot("user_host_Hansoft", StrippedRoot, "/depot/Dev"), ==, "/work/depot/Dev");
			}

			{
				DMibTestPath("RemappedContainsStreamPath");

				auto StrippedRoot = CPerforceFunctions::fs_StripRoot("/work/depot/Remapped", {"/depot/Remapped"}, true);

				DMibExpect(StrippedRoot.m_Root, ==, "/work");
				DMibExpectTrue(StrippedRoot.m_bStripped);
				DMibExpect(CPerforceFunctions::fs_GetFullRoot("user_host_Hansoft", StrippedRoot, "/depot/Dev"), ==, "/work/depot/Dev");
			}
		};

		DMibTestSuite("StreamPaths")
		{
			DMibTestCategory("ParentStreamPath")
			{
				CPerforceClient::CStream Stream;
				Stream.m_Name = "Dev";
				Stream.m_Type = "development";
				Stream.m_Parent = "//Hansoft/Main";
				DMibExpectFalse(CPerforceFunctions::fs_UsesParentStreamPath(Stream));

				CPerforceClient::CStream TaskStream;
				TaskStream.m_Name = "Dev.2BC18132CD3C4E529E43F946AFA947B8";
				TaskStream.m_Type = "task";
				TaskStream.m_Parent = "//Hansoft/Dev";
				DMibExpectTrue(CPerforceFunctions::fs_UsesParentStreamPath(TaskStream));

				CPerforceClient::CStream UniqueNameStream;
				UniqueNameStream.m_Name = "Dev.2BC18132CD3C4E529E43F946AFA947B8";
				UniqueNameStream.m_Type = "development";
				UniqueNameStream.m_Parent = "//Hansoft/Dev";
				UniqueNameStream.m_Description = "UniqueName true";
				DMibExpectTrue(CPerforceFunctions::fs_UsesParentStreamPath(UniqueNameStream));
			};

			DMibTestCategory("OldNames")
			{
				CPerforceClient::CStream Stream;
				Stream.m_Name = "Dev";
				Stream.m_Type = "development";
				Stream.m_Description = "OldNames { Name Development Name Develop }";

				auto StreamPaths = CPerforceFunctions::fs_GetStreamPaths("Hansoft", Stream);
				DMibExpect(StreamPaths, ==, (TCVector<CStr>{"/Hansoft/Dev", "/Hansoft/Development", "/Hansoft/Develop"}));
			};

			DMibTestCategory("NoOldNames")
			{
				CPerforceClient::CStream Stream;
				Stream.m_Name = "Main";
				Stream.m_Type = "mainline";
				Stream.m_Description = "Created by builder.";

				auto StreamPaths = CPerforceFunctions::fs_GetStreamPaths("Hansoft", Stream);
				DMibExpect(StreamPaths, ==, (TCVector<CStr>{"/Hansoft/Main"}));
			};
		};

		// A project's paths as the workspace's root spells it, which p4 takes them as
		DMibTestSuite("RootSpelling")
		{
			CStr Root = NFile::CFile::fs_GetProgramDirectory() / "PerforceTestRootSpelling";
			if (NFile::CFile::fs_FileExists(Root))
				NFile::CFile::fs_DeleteDirectoryRecursive(Root);
			NFile::CFile::fs_CreateDirectory(Root / "Real/Workspace/Project");
			NFile::CFile::fs_CreateDirectory(Root / "Other");
			fg_TestAddCleanupPath(Root);

			CStr Workspace = Root / "Real/Workspace";
			{
				DMibTestPath("Below the root");
				auto Spelling = CPerforceRootSpelling::fs_Find(Workspace / "Project", Workspace);
				DMibExpect(Spelling.m_ServerRoot, ==, Workspace / "Project");
				DMibExpect(Spelling.f_ServerPath(Workspace / "Project/Pins@Dev.MRepo"), ==, Workspace / "Project/Pins@Dev.MRepo");
				DMibExpect(Spelling.f_ServerFileName(Workspace / "Project/Pins@Dev.MRepo"), ==, Workspace / "Project/Pins%40Dev.MRepo");

				// p4 can't escape '...', which would name the files it matches.
				DMibExpectException
					(
						Spelling.f_ServerFileName(Workspace / "Project/Sample...txt")
						, DMibErrorInstance("'{}' holds '...', which p4 reads as a wildcard in any file name"_f << (Workspace / "Project/Sample...txt"))
					)
				;
			}
			{
				DMibTestPath("Outside the workspace");
				auto Spelling = CPerforceRootSpelling::fs_Find(Root / "Other", Workspace);
				DMibExpect(Spelling.m_ServerRoot, ==, Root / "Other");
			}
#ifndef DPlatformFamily_Windows
			{
				DMibTestPath("Through a symbolic link");
				NFile::CFile::fs_CreateSymbolicLink(Root / "Real", Root / "Link", NFile::EFileAttrib_Directory, NFile::ESymbolicLinkFlag_Relative);

				CStr Project = Root / "Link/Workspace/Project";
				auto Spelling = CPerforceRootSpelling::fs_Find(Project, Workspace);
				DMibExpect(Spelling.m_ServerRoot, ==, Workspace / "Project");
				DMibExpect(Spelling.f_ServerPath(Project / "Pins@Dev.MRepo"), ==, Workspace / "Project/Pins@Dev.MRepo");
				DMibExpect(Spelling.f_ProjectPath(Workspace / "Project/Pins@Dev.MRepo"), ==, Project / "Pins@Dev.MRepo");
				DMibExpect(Spelling.f_ServerFileName(Project / "Data*.cmake"), ==, Workspace / "Project/Data%2A.cmake");
				DMibExpect(Spelling.f_ServerPath(Root / "Other/File.txt"), ==, Root / "Other/File.txt");
			}
#endif
		};
	}
}

DMibTestRegister(CGeneral_Tests, Malterlib::Perforce);
