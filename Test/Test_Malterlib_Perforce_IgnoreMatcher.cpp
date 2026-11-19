// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <Mib/Core/Core>
#include <Mib/Cryptography/UUID>
#include <Mib/File/File>
#include <Mib/Test/Test>

#ifdef DCompiler_MSVC
#	pragma warning(push,2)
#endif
#include "clientapi.h"
#include "ignore.h"
#include "strarray.h"
#ifdef DCompiler_MSVC
#	pragma warning(pop)
#endif

namespace
{
	using namespace NMib;
	using namespace NMib::NStr;
	using namespace NMib::NFile;

	int fg_ReadNoRuleFile(char const *_pPath)
	{
		return 0;
	}

	struct CIgnoreMatcher_Tests : public NMib::NTest::CTest
	{
		void f_DoTests();
	};

	void CIgnoreMatcher_Tests::f_DoTests()
	{
		// The matcher of p4 keeps the rules and the files it has read for the next path, which the ignore files and the filter
		// of the files it reads that Malterlib sets on it decide
		DMibTestSuite("IgnoreMatcher")
		{
			CStr Root = CFile::fs_GetTemporaryDirectory() / "MalterlibPerforceIgnoreMatcher" / NCryptography::fg_GetRandomUuidString();
			CFile::fs_CreateDirectory(Root);
			NTest::fg_TestAddCleanupPath(Root);

			CFile::fs_WriteStringToFile(Root / "A.p4ignore", "*.a\n", false);
			CFile::fs_WriteStringToFile(Root / "B.p4ignore", "*.b\n", false);

			auto fNativePath = [](CStr const &_Path)
				{
#ifdef DPlatformFamily_Windows
					return _Path.f_Replace("/", "\\");
#else
					return _Path;
#endif
				}
			;
			auto fSetIgnoreFile = [&](Ignore &_Matcher, CStr const &_File)
				{
					StrArray Files;
					Files.Put()->Set(StrRef(fNativePath(Root / _File).f_GetStr()));
					_Matcher.SetIgnoreFiles(StrRef("Rules"), Files);
				}
			;
			// With a config name the matcher keeps its rules from one path to the next in the same directory
			auto fRejected = [&](Ignore &_Matcher, CStr const &_File)
				{
					return _Matcher.Reject(StrRef(fNativePath(Root / _File).f_GetStr()), StrRef("Rules"), ".p4config") != 0;
				}
			;

			DMibTestCategory("IgnoreFiles")
			{
				Ignore Matcher;
				fSetIgnoreFile(Matcher, "A.p4ignore");
				DMibExpectTrue(fRejected(Matcher, "File.a"));

				fSetIgnoreFile(Matcher, "B.p4ignore");
				DMibExpectFalse(fRejected(Matcher, "File.a"));
				DMibExpectTrue(fRejected(Matcher, "File.b"));
			};

			DMibTestCategory("FileFilter")
			{
				Ignore Matcher;
				fSetIgnoreFile(Matcher, "A.p4ignore");
				DMibExpectTrue(fRejected(Matcher, "File.a"));

				Matcher.SetFileFilter(fg_ReadNoRuleFile);
				DMibExpectFalse(fRejected(Matcher, "File.a"));
			};
		};
	}

	DMibTestRegister(CIgnoreMatcher_Tests, Malterlib::Perforce);
}
