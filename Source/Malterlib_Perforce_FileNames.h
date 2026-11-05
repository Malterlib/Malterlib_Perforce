// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#pragma once

#include <Mib/Core/Core>

namespace NMib::NPerforce
{
#ifdef DPlatformFamily_Windows
	// Makes the Perforce API name files in UTF-8, as Malterlib's paths are, on this thread only while it lives.
	struct CUTF8FileNames
	{
		CUTF8FileNames();
		~CUTF8FileNames();
	};
#endif
}
