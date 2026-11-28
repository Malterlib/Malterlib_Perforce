// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

// Every public header of the module, in a test target built with DMibPNoShortCuts, so that a header that needs the shortcuts of
// Malterlib's namespaces fails the build.

#include <Mib/Perforce/FileNames>
#include <Mib/Perforce/Functions>
#include <Mib/Perforce/Ignore>
#include <Mib/Perforce/TestServer>
#include <Mib/Perforce/Wrapper>
