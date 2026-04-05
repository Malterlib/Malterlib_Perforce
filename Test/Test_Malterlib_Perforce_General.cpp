// Copyright © Unbroken AB
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <Mib/Core/Core>

class CGeneral_Tests : public NMib::NTest::CTest
{
public:
	void f_DoTests()
	{
	}
};

DMibTestRegister(CGeneral_Tests, Malterlib::Perforce);
