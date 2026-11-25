// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <Mib/Core/Core>
#include <Mib/Concurrency/AsyncDestroy>
#include <Mib/File/File>
#include <Mib/Test/Test>

using namespace NMib;
using namespace NMib::NStr;
using namespace NMib::NContainer;
using namespace NMib::NStorage;
using namespace NMib::NFunction;
using namespace NMib::NConcurrency;
using namespace NMib::NEncoding;

#include <Mib/Perforce/TestServer>
#include <Mib/Perforce/Wrapper>

#ifndef DPlatformFamily_Windows
#	include <fcntl.h>
#endif

using namespace NMib::NPerforce;

namespace
{
	// The ranges of the differences as a unified diff without context heads its hunks, '-<first>,<count> +<first>,<count>;' each.
	CStr fg_Ranges(CPerforceClient::CFileDiff const &_Diff)
	{
		CStr Ranges;
		for (auto const &Range : _Diff.m_Ranges)
			Ranges += "-{},{} +{},{};"_f << Range.m_iOldFirst << Range.m_nOld << Range.m_iNewFirst << Range.m_nNew;

		return Ranges;
	}

	struct CDiff_Tests : public NMib::NTest::CTest
	{
		void f_DoTests();
	};

	void CDiff_Tests::f_DoTests()
	{
		// The opened files' differences from what p4 diff compares them with, from the API's own diff whatever P4DIFF names: a
		// moved file's from its source, a file a sync has scheduled a resolve for from the revision given, and a binary file's
		// none.
		DMibTestSuite("Opened") -> TCFuture<void>
		{
			if (CPerforceTestServer::fs_FindServerExecutable().f_IsEmpty())
			{
				DMibConOut("{}\n", "Skipping the Perforce test of opened files' differences: p4d was not found in PATH");
				co_return {};
			}

			CStr Root = NFile::CFile::fs_GetProgramDirectory() / "TestPerforceDiffOpened";
			if (NFile::CFile::fs_FileExists(Root, NFile::EFileAttrib_Directory))
				NFile::CFile::fs_DeleteDirectoryRecursive(Root, true);
			NFile::CFile::fs_CreateDirectory(Root);
			NTest::fg_TestAddCleanupPath(Root);

			TCActor<CPerforceTestServer> Server = fg_Construct<CPerforceTestServer>();
			co_await Server(&CPerforceTestServer::f_Start, Root / "Server", CStr("tester"));
			auto ServerCleanup = co_await fg_AsyncDestroy(Server);
			co_await Server(&CPerforceTestServer::f_CreateStreamDepot, CStr("Streams"));
			co_await Server(&CPerforceTestServer::f_CreateMainlineStream, CStr("//Streams/Main"));
			co_await Server
				(
					&CPerforceTestServer::f_SubmitFiles
					, CStr("//Streams/Main")
					, TCVector<CPerforceTestServer::CFile>
					{
						{"Edited.txt", "a\nb\n"}
						, {"Moved.txt", "x\n"}
						, {"Synced.txt", "r1\n"}
						, {"Lines.txt", "a\rb\r"}
						, {"CrToLf.txt", "a\rb\r"}
						, {"Shortened.txt", "a\nb\n"}
						, {"Deleted.txt", "a\nb\nc\n"}
						, {"Changed.txt", "a\nb\nc\n"}
						, {"Unterminated.txt", "a\nb\n"}
					}
					, CStr("Add the files")
				)
			;

			CStr WorkRoot = Root / "Work";
			co_await Server(&CPerforceTestServer::f_CreateClient, CStr("TestWork"), CStr("//Streams/Main"), WorkRoot);
			auto Connection = co_await Server(&CPerforceTestServer::f_GetConnectionInfo, CStr("TestWork"));

			co_await
				(
					g_Dispatch(fg_BlockingActor())
					/ [Connection, WorkRoot]
					{
						CPerforceClientThrow Client(Connection);
						Client.f_Login(CStr());
						Client.f_Sync(CPerforceClient::fs_EscapeFileName(WorkRoot) / "...");

						auto fWriteText = [](CStr const &_Path, CStr const &_Text)
							{
								NFile::CFile::fs_WriteStringToFile(_Path, CPerforceTestServer::fs_WorkspaceText(_Text), false);
							}
						;

						CByteVector Binary;
						for (uint8 Byte : {uint8(0), uint8(1), uint8(2)})
							Binary.f_Insert(Byte);

						NFile::CFile::fs_WriteFile(WorkRoot / "Binary.bin", Binary);
						Client.f_Add(WorkRoot / "Binary.bin");
						Client.f_Submit(WorkRoot / "Binary.bin", "Add a binary file");

						Client.f_OpenForEdit(WorkRoot / "Edited.txt");
						fWriteText(WorkRoot / "Edited.txt", "a\nb\nc\n");
						Client.f_OpenForEdit(WorkRoot / "Moved.txt");
						Client.f_Move(WorkRoot / "Moved.txt", WorkRoot / "MovedTo.txt");
						fWriteText(WorkRoot / "MovedTo.txt", "x\ny\n");
						Client.f_OpenForEdit(WorkRoot / "Synced.txt");
						fWriteText(WorkRoot / "Synced.txt", "r1\nmine\n");
						Client.f_OpenForEdit(WorkRoot / "Lines.txt");
						NFile::CFile::fs_WriteStringToFile(WorkRoot / "Lines.txt", "a\rb\rc\r", false);
						Client.f_OpenForEdit(WorkRoot / "CrToLf.txt");
						NFile::CFile::fs_WriteStringToFile(WorkRoot / "CrToLf.txt", "a\nb\n", false);
						Client.f_OpenForEdit(WorkRoot / "Shortened.txt");
						fWriteText(WorkRoot / "Shortened.txt", "a\n");
						Client.f_OpenForEdit(WorkRoot / "Deleted.txt");
						fWriteText(WorkRoot / "Deleted.txt", "a\nc\n");
						Client.f_OpenForEdit(WorkRoot / "Changed.txt");
						fWriteText(WorkRoot / "Changed.txt", "a\nB\nc\n");
						Client.f_OpenForEdit(WorkRoot / "Unterminated.txt");
						fWriteText(WorkRoot / "Unterminated.txt", "a\nb");
						Client.f_OpenForEdit(WorkRoot / "Binary.bin");
						Binary.f_Insert(uint8(3));
						NFile::CFile::fs_WriteFile(WorkRoot / "Binary.bin", Binary);
					}
					% "Failed to open the files"
				)
			;

			// A sync after this schedules a resolve of the opened file.
			co_await Server(&CPerforceTestServer::f_SubmitFiles, CStr("//Streams/Main"), TCVector<CPerforceTestServer::CFile>{{"Synced.txt", "r2\n"}}, CStr("Change a file"));

			// The API's own diff is used, not the one P4DIFF names.
			auto DiffConnection = Connection;
			DiffConnection.m_Environment = TCMap<CStr, CStr>{{"P4DIFF", "false"}};
			umint nLeakedDescriptors = 0;
			auto Diffs = co_await
				(
					g_Dispatch(fg_BlockingActor()) / [DiffConnection, WorkRoot, &nLeakedDescriptors]() -> TCMap<CStr, CPerforceClient::CFileDiff>
					{
						CPerforceClientThrow Client(DiffConnection);
						Client.f_Login(CStr());
						Client.f_Sync(CPerforceClient::fs_EscapeFileName(WorkRoot) / "...");

						TCMap<CStr, CPerforceClient::CFileDiff> Diffs;
						TCVector<CStr> Files =
							{
								WorkRoot / "Edited.txt"
								, WorkRoot / "MovedTo.txt"
								, CPerforceClient::fs_EscapeFileName(WorkRoot / "Synced.txt") + "#1"
								, WorkRoot / "Binary.bin"
								, WorkRoot / "Lines.txt"
								, WorkRoot / "CrToLf.txt"
								, WorkRoot / "Shortened.txt"
								, WorkRoot / "Deleted.txt"
								, WorkRoot / "Changed.txt"
								, WorkRoot / "Unterminated.txt"
							}
						;
						for (auto &Diff : Client.f_DiffOpenedFiles(Files))
							Diffs[NFile::CFile::fs_GetFile(Diff.m_File)] = Diff;

						// A diff closes every file it opens: diffing again leaves as many descriptors open as before. Descriptors are
						// counted outside Windows only.
						auto fOpenDescriptors = []
							{
								umint nOpen = 0;
#ifndef DPlatformFamily_Windows
								for (int iDescriptor = 0; iDescriptor < 4096; ++iDescriptor)
								{
									if (fcntl(iDescriptor, F_GETFD) != -1)
										++nOpen;
								}
#endif
								return nOpen;
							}
						;
						umint nBefore = fOpenDescriptors();
						Client.f_DiffOpenedFiles(Files);
						umint nAfter = fOpenDescriptors();
						nLeakedDescriptors = nAfter > nBefore ? nAfter - nBefore : 0;

						return Diffs;
					}
					% "Failed to diff the files"
				)
			;

			auto fDiff = [&](CStr const &_Name) -> CPerforceClient::CFileDiff
				{
					if (auto *pDiff = Diffs.f_FindEqual(_Name))
						return *pDiff;

					return {};
				}
			;

			DMibExpect(Diffs.f_GetLen(), ==, 10u);
			DMibExpect(nLeakedDescriptors, ==, 0u);
			DMibExpect(fg_Ranges(fDiff("Edited.txt")), ==, "-3,0 +3,1;");
			DMibExpect(fg_Ranges(fDiff("MovedTo.txt")), ==, "-2,0 +2,1;");
			DMibExpect(fg_Ranges(fDiff("Synced.txt")), ==, "-2,0 +2,1;");
			DMibExpect(fDiff("Binary.bin").m_bBinary, ==, true);

			// The bytes are compared as they are, where only an LF ends a line, so a change from a lone CR to an LF is one.
			DMibExpect(fg_Ranges(fDiff("Lines.txt")), ==, "-1,1 +1,1;");
			DMibExpect(fg_Ranges(fDiff("CrToLf.txt")), ==, "-1,1 +1,2;");

			// A deleted line, a changed line, and a last line whose final newline the change takes away, which changes it.
			DMibExpect(fg_Ranges(fDiff("Deleted.txt")), ==, "-2,1 +2,0;");
			DMibExpect(fg_Ranges(fDiff("Changed.txt")), ==, "-2,1 +2,1;");
			DMibExpect(fg_Ranges(fDiff("Unterminated.txt")), ==, "-2,1 +2,1;");

			DMibExpectTrue(fDiff("Edited.txt").m_ComparedLineEnds == CPerforceClient::EComparedLineEnds::mc_None);
			DMibExpectFalse(fDiff("Edited.txt").m_bConverted);
			DMibExpect(fg_Ranges(fDiff("Shortened.txt")), ==, "-2,1 +2,0;");

			co_return {};
		};

		// A unicode file in the charset the output is in compares by its bytes as they are, as p4's own diff compares it, so a change
		// from a lone CR to an LF is one, which LineEnd mac's reading of the files would turn into the same LF and leave only the line
		// added.
		DMibTestSuite("OpenedInOutputCharset") -> TCFuture<void>
		{
			if (CPerforceTestServer::fs_FindServerExecutable().f_IsEmpty())
			{
				DMibConOut("{}\n", "Skipping the Perforce test of the differences of a file in the output's charset: p4d was not found in PATH");
				co_return {};
			}

			CStr Root = NFile::CFile::fs_GetProgramDirectory() / "TestPerforceDiffOutputCharset";
			if (NFile::CFile::fs_FileExists(Root, NFile::EFileAttrib_Directory))
				NFile::CFile::fs_DeleteDirectoryRecursive(Root, true);
			NFile::CFile::fs_CreateDirectory(Root);
			NTest::fg_TestAddCleanupPath(Root);

			TCActor<CPerforceTestServer> Server = fg_Construct<CPerforceTestServer>();
			co_await Server(&CPerforceTestServer::f_StartUnicode, Root / "Server", CStr("tester"));
			auto ServerCleanup = co_await fg_AsyncDestroy(Server);
			co_await Server(&CPerforceTestServer::f_CreateStreamDepot, CStr("Streams"));
			co_await Server(&CPerforceTestServer::f_CreateMainlineStream, CStr("//Streams/Main"));

			CStr WorkRoot = Root / "Work";
			auto AdminConnection = co_await Server(&CPerforceTestServer::f_GetConnectionInfo, CStr());
			co_await
				(
					g_Dispatch(fg_BlockingActor())
					/ [AdminConnection, WorkRoot]
					{
						CPerforceClientThrow Client(AdminConnection);
						Client.f_Login(CStr());
						Client.f_SetTypeMap({"unicode //Streams/Main/Bom.txt"});

						CPerforceClient::CClient Spec;
						Spec.m_Root = WorkRoot;
						Spec.m_Description = "A workspace whose lines end in CR.";
						Spec.m_LineEndings = "mac";
						Spec.m_Stream = "//Streams/Main";
						Client.f_SetClient("TestWork", Spec);
					}
					% "Failed to set up the server"
				)
			;
			co_await Server(&CPerforceTestServer::f_SubmitFiles, CStr("//Streams/Main"), TCVector<CPerforceTestServer::CFile>{{"Bom.txt", "a\nb\n"}}, CStr("Add the file"));

			auto Connection = co_await Server(&CPerforceTestServer::f_GetConnectionInfo, CStr("TestWork"));
			Connection.m_Charset = "utf8-bom";
			auto Diffs = co_await
				(
					g_Dispatch(fg_BlockingActor()) / [Connection, WorkRoot]() -> TCVector<CPerforceClient::CFileDiff>
					{
						CPerforceClientThrow Client(Connection);
						Client.f_Login(CStr());
						Client.f_Sync(CPerforceClient::fs_EscapeFileName(WorkRoot) / "...");
						Client.f_OpenForEdit(WorkRoot / "Bom.txt");
						NFile::CFile::fs_WriteStringToFile(WorkRoot / "Bom.txt", "a\nb\rc\r", true);

						return Client.f_DiffOpenedFiles({WorkRoot / "Bom.txt"});
					}
					% "Failed to diff the file"
				)
			;

			DMibAssert(Diffs.f_GetLen(), ==, 1u);
			DMibExpect(fg_Ranges(Diffs[0]), !=, "-2,0 +3,1;");
			DMibExpectTrue(Diffs[0].m_ComparedLineEnds == CPerforceClient::EComparedLineEnds::mc_None);
			DMibExpectFalse(Diffs[0].m_bConverted);

			co_return {};
		};

		// A workspace whose LineEnd is win, as local is on Windows, has the revision compared with written with its lines ending in CRLF,
		// as the workspace's file has them: a line that only ends in CRLF in the workspace is unchanged.
		DMibTestSuite("OpenedWithWindowsLineEnds") -> TCFuture<void>
		{
			if (CPerforceTestServer::fs_FindServerExecutable().f_IsEmpty())
			{
				DMibConOut("{}\n", "Skipping the Perforce test of the differences in a workspace with Windows line ends: p4d was not found in PATH");
				co_return {};
			}

			CStr Root = NFile::CFile::fs_GetProgramDirectory() / "TestPerforceDiffWindowsLineEnds";
			if (NFile::CFile::fs_FileExists(Root, NFile::EFileAttrib_Directory))
				NFile::CFile::fs_DeleteDirectoryRecursive(Root, true);
			NFile::CFile::fs_CreateDirectory(Root);
			NTest::fg_TestAddCleanupPath(Root);

			TCActor<CPerforceTestServer> Server = fg_Construct<CPerforceTestServer>();
			co_await Server(&CPerforceTestServer::f_Start, Root / "Server", CStr("tester"));
			auto ServerCleanup = co_await fg_AsyncDestroy(Server);
			co_await Server(&CPerforceTestServer::f_CreateStreamDepot, CStr("Streams"));
			co_await Server(&CPerforceTestServer::f_CreateMainlineStream, CStr("//Streams/Main"));

			CStr WorkRoot = Root / "Work";
			auto AdminConnection = co_await Server(&CPerforceTestServer::f_GetConnectionInfo, CStr());
			co_await
				(
					g_Dispatch(fg_BlockingActor())
					/ [AdminConnection, WorkRoot]
					{
						CPerforceClientThrow Client(AdminConnection);
						Client.f_Login(CStr());

						CPerforceClient::CClient Spec;
						Spec.m_Root = WorkRoot;
						Spec.m_Description = "A workspace whose lines end in CRLF.";
						Spec.m_LineEndings = "win";
						Spec.m_Stream = "//Streams/Main";
						Client.f_SetClient("TestWork", Spec);
					}
					% "Failed to set up the server"
				)
			;
			co_await Server(&CPerforceTestServer::f_SubmitFiles, CStr("//Streams/Main"), TCVector<CPerforceTestServer::CFile>{{"Crlf.txt", "a\n"}}, CStr("Add the file"));

			auto Connection = co_await Server(&CPerforceTestServer::f_GetConnectionInfo, CStr("TestWork"));
			auto Diffs = co_await
				(
					g_Dispatch(fg_BlockingActor()) / [Connection, WorkRoot]() -> TCVector<CPerforceClient::CFileDiff>
					{
						CPerforceClientThrow Client(Connection);
						Client.f_Login(CStr());
						Client.f_Sync(CPerforceClient::fs_EscapeFileName(WorkRoot) / "...");
						Client.f_OpenForEdit(WorkRoot / "Crlf.txt");
						NFile::CFile::fs_WriteStringToFile(WorkRoot / "Crlf.txt", "a\r\nb\r\n", false);

						return Client.f_DiffOpenedFiles({WorkRoot / "Crlf.txt"});
					}
					% "Failed to diff the file"
				)
			;

			DMibAssert(Diffs.f_GetLen(), ==, 1u);
			DMibExpect(fg_Ranges(Diffs[0]), ==, "-2,0 +2,1;");
			DMibExpectTrue(Diffs[0].m_ComparedLineEnds == CPerforceClient::EComparedLineEnds::mc_None);
			DMibExpectFalse(Diffs[0].m_bConverted);

			co_return {};
		};

		// The differences between two depot revisions, with their types, and a revision that isn't there without one.
		DMibTestSuite("Revisions") -> TCFuture<void>
		{
			if (CPerforceTestServer::fs_FindServerExecutable().f_IsEmpty())
			{
				DMibConOut("{}\n", "Skipping the Perforce test of revisions' differences: p4d was not found in PATH");
				co_return {};
			}

			CStr Root = NFile::CFile::fs_GetProgramDirectory() / "TestPerforceDiffRevisions";
			if (NFile::CFile::fs_FileExists(Root, NFile::EFileAttrib_Directory))
				NFile::CFile::fs_DeleteDirectoryRecursive(Root, true);
			NFile::CFile::fs_CreateDirectory(Root);
			NTest::fg_TestAddCleanupPath(Root);

			TCActor<CPerforceTestServer> Server = fg_Construct<CPerforceTestServer>();
			co_await Server(&CPerforceTestServer::f_Start, Root / "Server", CStr("tester"));
			auto ServerCleanup = co_await fg_AsyncDestroy(Server);
			co_await Server(&CPerforceTestServer::f_CreateStreamDepot, CStr("Streams"));
			co_await Server(&CPerforceTestServer::f_CreateMainlineStream, CStr("//Streams/Main"));
			int64 First = co_await Server
				(
					&CPerforceTestServer::f_SubmitFiles
					, CStr("//Streams/Main")
					, TCVector<CPerforceTestServer::CFile>{{"Changed.txt", "a\n"}, {"Odd) - Name.txt", "a\n"}}
					, CStr("Add a file")
				)
			;

			co_await Server
				(
					&CPerforceTestServer::f_SubmitFiles
					, CStr("//Streams/Main")
					, TCVector<CPerforceTestServer::CFile>{{"Changed.txt", "a\nb\n"}, {"Added.txt", "c\n"}, {"Odd) - Name.txt", "a\nb\n"}}
					, CStr("Change a file and add another")
				)
			;

			auto Connection = co_await Server(&CPerforceTestServer::f_GetConnectionInfo, CStr());
			co_await
				(
					g_Dispatch(fg_BlockingActor())
					/ [Connection]()
					{
						CPerforceClientThrow Client(Connection);
						Client.f_Login(CStr());
						Client.f_SetTypeMap({"utf8 //Streams/Main/Retyped.txt"});
					}
					% "Failed to set the type map"
				)
			;
			co_await Server
				(
					&CPerforceTestServer::f_SubmitFiles
					, CStr("//Streams/Main")
					, TCVector<CPerforceTestServer::CFile>{{"Retyped.txt", "a\nc\n"}}
					, CStr("Add a file of another text type")
				)
			;

			auto Diffs = co_await
				(
					g_Dispatch(fg_BlockingActor()) / [Connection, First]() -> TCVector<CPerforceClient::CFileDiff>
					{
						CPerforceClientThrow Client(Connection);
						Client.f_Login(CStr());

						return
							{
								Client.f_DiffRevisions("//Streams/Main/Changed.txt@{}"_f << First, "//Streams/Main/Changed.txt#2")
								, Client.f_DiffRevisions("//Streams/Main/Changed.txt#2", "//Streams/Main/Changed.txt#2")
								, Client.f_DiffRevisions("//Streams/Main/Added.txt@{}"_f << First, "//Streams/Main/Added.txt#1")
								, Client.f_DiffRevisions("//Streams/Main/Odd) - Name.txt#1", "//Streams/Main/Odd) - Name.txt#2")
								, Client.f_DiffRevisions("//Streams/Main/Changed.txt#2", "//Streams/Main/Retyped.txt#1")
								, Client.f_DiffRevisions("//Streams/Main/Changed.txt#2", "//Streams/Main/Retyped.txt#1", true)
							}
						;
					}
					% "Failed to diff the revisions"
				)
			;

			DMibAssert(Diffs.f_GetLen(), ==, 6u);
			{
				DMibTestPath("Changed");
				DMibExpect(Diffs[0].m_Status, ==, "content");
				DMibExpect(Diffs[0].m_File, ==, "//Streams/Main/Changed.txt#2");
				DMibExpect(Diffs[0].m_Type1, ==, "text");
				DMibExpect(Diffs[0].m_Type2, ==, "text");
				DMibExpect(fg_Ranges(Diffs[0]), ==, "-2,0 +2,1;");
			}
			{
				DMibTestPath("Identical");
				DMibExpect(Diffs[1].m_Status, ==, "identical");
				DMibExpect(fg_Ranges(Diffs[1]), ==, "");
			}
			{
				DMibTestPath("Added");
				DMibExpect(Diffs[2].m_Type1, ==, "");
				DMibExpect(Diffs[2].m_File, ==, "//Streams/Main/Added.txt#1");
			}
			{
				// A name holding what separates the two in the header.
				DMibTestPath("Name");
				DMibExpect(Diffs[3].m_Type1, ==, "text");
				DMibExpect(Diffs[3].m_File, ==, "//Streams/Main/Odd) - Name.txt#2");
			}
			{
				// Two text types p4 diff2 only reports as different, unless asked to compare them as text.
				DMibTestPath("Types");
				DMibExpect(Diffs[4].m_Type1, ==, "text");
				DMibExpect(Diffs[4].m_Type2, ==, "utf8");
				DMibExpect(fg_Ranges(Diffs[4]), ==, "");
				DMibExpect(Diffs[5].m_Type1, ==, "text");
				DMibExpect(Diffs[5].m_Type2, ==, "utf8");
				DMibExpect(fg_Ranges(Diffs[5]), ==, "-2,1 +2,1;");
			}

			{
				// The untagged session the differences are read in is the session's: in its working directory, whose .p4config chooses
				// the workspace that specifications in the workspace's syntax are of.
				DMibTestPath("SessionWorkspace");
				CStr WorkRoot = Root / "Work";
				CStr OtherRoot = Root / "OtherWork";
				co_await Server(&CPerforceTestServer::f_CreateClient, CStr("TestWork"), CStr("//Streams/Main"), WorkRoot);
				co_await Server(&CPerforceTestServer::f_CreateClient, CStr("TestOtherWork"), CStr("//Streams/Main"), OtherRoot);
				NFile::CFile::fs_CreateDirectory(WorkRoot);
				NFile::CFile::fs_CreateDirectory(OtherRoot);
				NFile::CFile::fs_WriteStringToFile(WorkRoot / ".p4config", "P4CLIENT=TestWork\n", false);
				NFile::CFile::fs_WriteStringToFile(OtherRoot / ".p4config", "P4CLIENT=TestOtherWork\n", false);
				auto ConfigConnection = Connection;
				ConfigConnection.m_ConfigName = CStr(".p4config");
				ConfigConnection.m_Client = CStr();

				// A login in another directory moves the untagged session to that directory's workspace too.
				auto Diffs = co_await
					(
						g_Dispatch(fg_BlockingActor()) / [ConfigConnection, WorkRoot, OtherRoot]() -> TCVector<CPerforceClient::CFileDiff>
						{
							CPerforceClientThrow Client(ConfigConnection);
							Client.f_Login(CStr(), WorkRoot);
							TCVector<CPerforceClient::CFileDiff> Diffs;
							Diffs.f_Insert(Client.f_DiffRevisions("//TestWork/Changed.txt#1", "//TestWork/Changed.txt#2"));
							Client.f_Login(CStr(), OtherRoot);
							Diffs.f_Insert(Client.f_DiffRevisions("//TestOtherWork/Changed.txt#1", "//TestOtherWork/Changed.txt#2"));
							return Diffs;
						}
						% "Failed to diff the revisions in the session's workspace"
					)
				;
				DMibAssert(Diffs.f_GetLen(), ==, 2u);
				DMibExpect((TCVector<CStr>{Diffs[0].m_Status, Diffs[1].m_Status}), ==, (TCVector<CStr>{"content", "content"}));
				DMibExpect((TCVector<CStr>{fg_Ranges(Diffs[0]), fg_Ranges(Diffs[1])}), ==, (TCVector<CStr>{"-2,0 +2,1;", "-2,0 +2,1;"}));
			}

			co_return {};
		};
	}
}

DMibTestRegister(CDiff_Tests, Malterlib::Perforce);
