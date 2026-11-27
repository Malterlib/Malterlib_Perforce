// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#pragma once

#include <Mib/Core/Core>

namespace NMib::NPerforce
{
	bool fg_CodePageRepresents(NStr::CStr const &_Text);
	NStr::CStr fg_CodePageCharacter();
}
