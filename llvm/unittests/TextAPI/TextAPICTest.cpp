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
#include "llvm-c/Core.h" // LLVMDisposeMessage
#include "llvm/ADT/SmallString.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/raw_ostream.h"
#include "gtest/gtest.h"
#include <string>

using namespace llvm;

namespace {

static const char TBDv4MultiArch[] =
    "--- !tapi-tbd\n"
    "tbd-version: 4\n"
    "targets:  [ x86_64-macos, arm64-macos ]\n"
    "install-name: /usr/lib/libfoo.dylib\n"
    "current-version: 1.2.3\n"
    "compatibility-version: 1.0\n"
    "exports:\n"
    "  - targets: [ x86_64-macos, arm64-macos ]\n"
    "    symbols: [ _foo ]\n"
    "...\n";

// Write `Contents` to a fresh temp .tbd file and return its path.
static std::string writeTempTBD(StringRef Contents) {
  SmallString<128> Path;
  int FD = -1;
  std::error_code EC =
      sys::fs::createTemporaryFile("llvm-c-textapi", "tbd", FD, Path);
  EXPECT_FALSE(EC) << EC.message();
  raw_fd_ostream OS(FD, /*shouldClose=*/true);
  OS << Contents;
  OS.flush();
  return std::string(Path);
}

// The library reports the version it was built with, encoded as the documented
// major*10000+minor.
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

TEST(TextAPICParse, ParsesValidFile) {
  std::string Path = writeTempTBD(TBDv4MultiArch);

  LLVMTextAPIContextRef Ctx = LLVMTextAPIContextCreate();
  ASSERT_NE(Ctx, nullptr);

  char *Err = nullptr;
  LLVMTextAPIRef File = LLVMTextAPIParse(Ctx, Path.c_str(), &Err);
  EXPECT_EQ(Err, nullptr);
  EXPECT_NE(File, nullptr);

  LLVMTextAPIContextDispose(Ctx);
  sys::fs::remove(Path);
}

// Re-parsing the same unchanged path must reuse the cached handle rather than
// parse the file again.
TEST(TextAPICParse, ReusesCachedHandle) {
  std::string Path = writeTempTBD(TBDv4MultiArch);

  LLVMTextAPIContextRef Ctx = LLVMTextAPIContextCreate();
  char *Err = nullptr;
  LLVMTextAPIRef First = LLVMTextAPIParse(Ctx, Path.c_str(), &Err);
  ASSERT_NE(First, nullptr);
  EXPECT_EQ(Err, nullptr);

  char *Err2 = nullptr;
  LLVMTextAPIRef Second = LLVMTextAPIParse(Ctx, Path.c_str(), &Err2);
  EXPECT_EQ(Err2, nullptr);
  EXPECT_EQ(First, Second); // same handle => no reparse

  LLVMTextAPIContextDispose(Ctx);
  sys::fs::remove(Path);
}

// Distinct contexts maintain independent caches.
TEST(TextAPICParse, DistinctContextsParseIndependently) {
  std::string Path = writeTempTBD(TBDv4MultiArch);

  LLVMTextAPIContextRef Ctx1 = LLVMTextAPIContextCreate();
  LLVMTextAPIContextRef Ctx2 = LLVMTextAPIContextCreate();
  LLVMTextAPIRef File1 = LLVMTextAPIParse(Ctx1, Path.c_str(), nullptr);
  LLVMTextAPIRef File2 = LLVMTextAPIParse(Ctx2, Path.c_str(), nullptr);
  ASSERT_NE(File1, nullptr);
  ASSERT_NE(File2, nullptr);
  EXPECT_NE(File1, File2); // separate caches => separate parses

  LLVMTextAPIContextDispose(Ctx1);
  LLVMTextAPIContextDispose(Ctx2);
  sys::fs::remove(Path);
}

TEST(TextAPICParse, ReportsErrorForMissingFile) {
  LLVMTextAPIContextRef Ctx = LLVMTextAPIContextCreate();
  char *Err = nullptr;
  LLVMTextAPIRef File =
      LLVMTextAPIParse(Ctx, "/definitely/not/here/libnope.tbd", &Err);
  EXPECT_EQ(File, nullptr);
  ASSERT_NE(Err, nullptr);
  LLVMDisposeMessage(Err);
  LLVMTextAPIContextDispose(Ctx);
}

TEST(TextAPICParse, ReportsErrorForMalformedFile) {
  std::string Path = writeTempTBD("this is not a valid tbd file\n");
  LLVMTextAPIContextRef Ctx = LLVMTextAPIContextCreate();
  char *Err = nullptr;
  LLVMTextAPIRef File = LLVMTextAPIParse(Ctx, Path.c_str(), &Err);
  EXPECT_EQ(File, nullptr);
  EXPECT_NE(Err, nullptr);
  if (Err)
    LLVMDisposeMessage(Err);
  LLVMTextAPIContextDispose(Ctx);
  sys::fs::remove(Path);
}

// A NULL OutError must be tolerated on the error path.
TEST(TextAPICParse, NullOutErrorIsTolerated) {
  LLVMTextAPIContextRef Ctx = LLVMTextAPIContextCreate();
  LLVMTextAPIRef File =
      LLVMTextAPIParse(Ctx, "/definitely/not/here/libnope.tbd", nullptr);
  EXPECT_EQ(File, nullptr);
  LLVMTextAPIContextDispose(Ctx);
}

} // namespace
