// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "Malterlib_Perforce_FileNames.h"

#ifdef DPlatformFamily_Windows
#ifdef DCompiler_MSVC
#	pragma warning(push,2)
#endif
#include "i18napi.h"
#include "charset.h"
#ifdef DCompiler_MSVC
#	pragma warning(pop)
#endif

namespace NMib::NPerforce
{
	CUTF8FileNames::CUTF8FileNames()
	{
		GlobalCharSet::UseAlt(true);
		GlobalCharSet::Set(CharSetApi::UTF_8);
	}

	CUTF8FileNames::~CUTF8FileNames()
	{
		GlobalCharSet::UseAlt(false);
	}
}
#endif
