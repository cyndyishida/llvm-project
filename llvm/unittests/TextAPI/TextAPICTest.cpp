//===-- TextAPICTest.cpp - TextAPI C API tests ----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Tests for the stable C bindings declared in llvm-c/TextAPI.h.
//
//===----------------------------------------------------------------------===//

#include "llvm-c/TextAPI.h"
#include "gtest/gtest.h"

namespace {

// The version the library reports at runtime must match the header it was
// built against, and the encoding must be the documented major*10000+minor.
TEST(TextAPICAPIVersion, MatchesHeader) {
  EXPECT_EQ(LLVMTextAPIGetAPIVersion(),
            static_cast<unsigned>(LLVM_TEXTAPI_VERSION));
}

TEST(TextAPICAPIVersion, Encoding) {
  EXPECT_EQ(LLVM_TEXTAPI_VERSION_ENCODE(1, 0), 10000);
  EXPECT_EQ(LLVM_TEXTAPI_VERSION_ENCODE(1, 7), 10007);
  EXPECT_EQ(LLVM_TEXTAPI_VERSION_ENCODE(2, 0), 20000);
  // Minor bumps stay below the next major; majors dominate the ordering.
  EXPECT_LT(LLVM_TEXTAPI_VERSION_ENCODE(1, 99),
            LLVM_TEXTAPI_VERSION_ENCODE(2, 0));
}

TEST(TextAPICAPIVersion, AtLeastInitial) {
  EXPECT_GE(LLVMTextAPIGetAPIVersion(),
            static_cast<unsigned>(LLVM_TEXTAPI_VERSION_ENCODE(1, 0)));
}

} // namespace
