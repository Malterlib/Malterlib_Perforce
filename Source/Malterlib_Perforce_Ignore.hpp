// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#pragma once

namespace NMib::NPerforce
{
	template <typename tf_CStr>
	void CPerforceIgnore::CSettingValue::f_Format(tf_CStr &o_Str) const
	{
		o_Str += f_GetDescription();
	}
}
