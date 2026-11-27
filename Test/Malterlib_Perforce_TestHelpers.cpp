// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include "Malterlib_Perforce_TestHelpers.h"

#ifdef DPlatformFamily_Windows
#	include <Mib/Core/PlatformSpecific/WindowsString>
#endif

namespace NMib::NPerforce
{
	// Whether the active code page represents the text, as it must a path that p4 takes without a Unicode charset on Windows.
	// Elsewhere every text is represented.
	bool fg_CodePageRepresents(NStr::CStr const &_Text)
	{
#ifdef DPlatformFamily_Windows
		NStr::CAnsiStr Encoded;
		NStr::NPlatform::fg_SystemEncodeAnsiStr(_Text, Encoded, '?');
		NStr::CStr Decoded;
		NStr::NPlatform::fg_SystemDecodeAnsiStr(Encoded, Decoded);

		return Decoded == _Text;
#else
		return true;
#endif
	}

	// A non-ASCII character that the active code page represents, one of several scripts' as code pages represent different ones,
	// or empty when it represents none of them.
	NStr::CStr fg_CodePageCharacter()
	{
		for (NStr::CStr Character : {"é", "ж", "λ", "א", "ع", "ก", "あ", "中", "가"})
		{
			if (fg_CodePageRepresents(Character))
				return Character;
		}

		return {};
	}
}
