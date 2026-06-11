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
#include "llvm/BinaryFormat/MachO.h" // CPU_TYPE_*/CPU_SUBTYPE_*
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/raw_ostream.h"
#include "gtest/gtest.h"
#include <set>
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

// A file whose slices differ: arm64 carries extra symbols (one weak) that
// x86_64 does not, to exercise per-arch export filtering and the weak flag.
static const char TBDv4Slices[] =
    "--- !tapi-tbd\n"
    "tbd-version: 4\n"
    "targets:  [ x86_64-macos, arm64-macos ]\n"
    "install-name: /usr/lib/libbar.dylib\n"
    "current-version: 2.0\n"
    "compatibility-version: 1.0\n"
    "exports:\n"
    "  - targets: [ x86_64-macos, arm64-macos ]\n"
    "    symbols: [ _common ]\n"
    "  - targets: [ arm64-macos ]\n"
    "    symbols: [ _arm64_only ]\n"
    "    weak-symbols: [ _weak_arm64 ]\n"
    "...\n";

// ObjC symbols (ObjC2 ABI: arm64): a class, an ivar, and a plain global.
static const char TBDv4ObjC[] =
    "--- !tapi-tbd\n"
    "tbd-version: 4\n"
    "targets:  [ arm64-macos ]\n"
    "install-name: /usr/lib/libobjcish.dylib\n"
    "exports:\n"
    "  - targets: [ arm64-macos ]\n"
    "    symbols: [ _plain ]\n"
    "    objc-classes: [ Widget ]\n"
    "    objc-ivars: [ Widget._count ]\n"
    "...\n";

// An ObjC EH-type (arm64).
static const char TBDv4ObjCEH[] =
    "--- !tapi-tbd\n"
    "tbd-version: 4\n"
    "targets:  [ arm64-macos ]\n"
    "install-name: /usr/lib/libeh.dylib\n"
    "exports:\n"
    "  - targets: [ arm64-macos ]\n"
    "    objc-eh-types: [ Bumper ]\n"
    "...\n";

// An ObjC class on i386/macOS, which uses the legacy ObjC1 ABI mangling.
static const char TBDv4ObjCLegacy[] =
    "--- !tapi-tbd\n"
    "tbd-version: 4\n"
    "targets:  [ i386-macos ]\n"
    "install-name: /usr/lib/liblegacy.dylib\n"
    "exports:\n"
    "  - targets: [ i386-macos ]\n"
    "    objc-classes: [ Widget ]\n"
    "...\n";

// $ld$ linker-directive symbols alongside a real export (arm64).
static const char TBDv4Ld[] =
    "--- !tapi-tbd\n"
    "tbd-version: 4\n"
    "targets:  [ arm64-macos ]\n"
    "install-name: /usr/lib/libld.dylib\n"
    "exports:\n"
    "  - targets: [ arm64-macos ]\n"
    "    symbols: [ _real, \"$ld$add$os10.5$_added\", \"$ld$previous$abc\" ]\n"
    "...\n";

// A TBD v5 file with per-arch metadata (rpaths require v5). The arm64 slice
// carries umbrella/rpath/reexport/allowable-client/weak data that x86_64 lacks,
// plus a whole-file not_for_dyld_shared_cache flag.
static const char TBDv5Meta[] = R"({
"tapi_tbd_version": 5,
"main_library": {
  "target_info": [
    { "target": "x86_64-macos", "min_deployment": "13.0" },
    { "target": "arm64-macos", "min_deployment": "14.0" }
  ],
  "flags": [
    { "targets": [ "x86_64-macos", "arm64-macos" ],
      "attributes": [ "not_for_dyld_shared_cache" ] }
  ],
  "install_names": [ { "name": "/usr/lib/libmeta.dylib" } ],
  "rpaths": [
    { "targets": [ "arm64-macos" ], "paths": [ "@loader_path/A" ] }
  ],
  "parent_umbrellas": [
    { "targets": [ "arm64-macos" ], "umbrella": "TheUmbrella" }
  ],
  "allowable_clients": [
    { "targets": [ "arm64-macos" ], "clients": [ "ClientArm" ] }
  ],
  "reexported_libraries": [
    { "targets": [ "arm64-macos" ], "names": [ "/usr/lib/libre.dylib" ] }
  ],
  "exported_symbols": [
    { "targets": [ "arm64-macos" ],
      "data": { "global": [ "_g" ], "weak": [ "_weakArm" ] } }
  ]
}
})";

// A TBD v5 file with one inlined library (the "libraries" array).
static const char TBDv5Inlined[] = R"({
"tapi_tbd_version": 5,
"main_library": {
  "target_info": [ { "target": "arm64-macos", "min_deployment": "14.0" } ],
  "install_names": [ { "name": "/usr/lib/libroot.dylib" } ],
  "exported_symbols": [
    { "targets": [ "arm64-macos" ], "data": { "global": [ "_root" ] } }
  ]
},
"libraries": [
  {
    "target_info": [ { "target": "arm64-macos", "min_deployment": "14.0" } ],
    "install_names": [ { "name": "/usr/lib/libinlined.dylib" } ],
    "exported_symbols": [
      { "targets": [ "arm64-macos" ], "data": { "global": [ "_inlinedSym" ] } }
    ]
  }
]
})";

// A TBD v4 file carrying $ld$ back-deployment directives (hide/weak/add/
// install_name/compatibility_version), all gated on os10.5.
static const char TBDv4Ld2[] =
    "--- !tapi-tbd\n"
    "tbd-version: 4\n"
    "targets:  [ x86_64-macos ]\n"
    "install-name: /usr/lib/libld2.dylib\n"
    "current-version: 3.0\n"
    "compatibility-version: 1.0\n"
    "exports:\n"
    "  - targets: [ x86_64-macos ]\n"
    "    symbols: [ _keep, _hideme, _weakme, \"$ld$hide$os10.5$_hideme\", "
    "\"$ld$weak$os10.5$_weakme\", \"$ld$add$os10.5$_addme\", "
    "\"$ld$install_name$os10.5$/new/name\", "
    "\"$ld$compatibility_version$os10.5$2.0\" ]\n"
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

// Collect the result of an (count, index->Copy) accessor pair into a set,
// disposing each returned string.
static std::set<std::string>
collectStrings(LLVMTextAPIRef File, unsigned (*Count)(LLVMTextAPIRef),
               char *(*Copy)(LLVMTextAPIRef, unsigned)) {
  std::set<std::string> Result;
  for (unsigned I = 0, N = Count(File); I < N; ++I) {
    char *S = Copy(File, I);
    EXPECT_NE(S, nullptr);
    if (S) {
      Result.insert(S);
      LLVMDisposeMessage(S);
    }
  }
  return Result;
}

TEST(TextAPICWholeFile, ArchitecturesAndTargets) {
  std::string Path = writeTempTBD(TBDv4MultiArch);
  LLVMTextAPIContextRef Ctx = LLVMTextAPIContextCreate();
  char *Err = nullptr;
  LLVMTextAPIRef File = LLVMTextAPIParse(Ctx, Path.c_str(), &Err);
  ASSERT_NE(File, nullptr) << (Err ? Err : "");

  // Two slices, two distinct architectures.
  EXPECT_EQ(LLVMTextAPIGetArchitectureCount(File), 2u);
  std::set<std::string> Archs = collectStrings(
      File, LLVMTextAPIGetArchitectureCount, LLVMTextAPICopyArchitectureName);
  EXPECT_EQ(Archs, (std::set<std::string>{"x86_64", "arm64"}));

  EXPECT_EQ(LLVMTextAPIGetTargetCount(File), 2u);
  std::set<std::string> Triples = collectStrings(
      File, LLVMTextAPIGetTargetCount, LLVMTextAPICopyTargetTriple);
  EXPECT_EQ(Triples,
            (std::set<std::string>{"x86_64-apple-macos", "arm64-apple-macos"}));

  LLVMTextAPIContextDispose(Ctx);
  sys::fs::remove(Path);
}

TEST(TextAPICWholeFile, InstallNameAndVersions) {
  std::string Path = writeTempTBD(TBDv4MultiArch);
  LLVMTextAPIContextRef Ctx = LLVMTextAPIContextCreate();
  LLVMTextAPIRef File = LLVMTextAPIParse(Ctx, Path.c_str(), nullptr);
  ASSERT_NE(File, nullptr);

  char *Name = LLVMTextAPICopyInstallName(File);
  ASSERT_NE(Name, nullptr);
  EXPECT_STREQ(Name, "/usr/lib/libfoo.dylib");
  LLVMDisposeMessage(Name);

  // current-version: 1.2.3  compatibility-version: 1.0
  EXPECT_EQ(LLVMTextAPIGetCurrentVersion(File), (1u << 16) | (2u << 8) | 3u);
  EXPECT_EQ(LLVMTextAPIGetCompatibilityVersion(File), (1u << 16));

  LLVMTextAPIContextDispose(Ctx);
  sys::fs::remove(Path);
}

TEST(TextAPICWholeFile, OutOfRangeIndexReturnsNull) {
  std::string Path = writeTempTBD(TBDv4MultiArch);
  LLVMTextAPIContextRef Ctx = LLVMTextAPIContextCreate();
  LLVMTextAPIRef File = LLVMTextAPIParse(Ctx, Path.c_str(), nullptr);
  ASSERT_NE(File, nullptr);
  EXPECT_EQ(LLVMTextAPICopyArchitectureName(File, 999), nullptr);
  EXPECT_EQ(LLVMTextAPICopyTargetTriple(File, 999), nullptr);
  LLVMTextAPIContextDispose(Ctx);
  sys::fs::remove(Path);
}

// Gather the exported symbol names of a slice into a set, disposing each.
static std::set<std::string> sliceExportNames(LLVMTextAPISliceRef Slice) {
  std::set<std::string> Names;
  for (unsigned I = 0, N = LLVMTextAPISliceGetExportedSymbolCount(Slice); I < N;
       ++I) {
    LLVMTextAPISymbolRef Sym = LLVMTextAPISliceGetExportedSymbol(Slice, I);
    EXPECT_NE(Sym, nullptr);
    char *Name = LLVMTextAPISymbolCopyName(Sym);
    EXPECT_NE(Name, nullptr);
    if (Name) {
      Names.insert(Name);
      LLVMDisposeMessage(Name);
    }
  }
  return Names;
}

TEST(TextAPICSlice, ExportsFilteredByArch) {
  std::string Path = writeTempTBD(TBDv4Slices);
  LLVMTextAPIContextRef Ctx = LLVMTextAPIContextCreate();
  LLVMTextAPIRef File = LLVMTextAPIParse(Ctx, Path.c_str(), nullptr);
  ASSERT_NE(File, nullptr);

  // x86_64 sees only the common symbol.
  char *Err = nullptr;
  LLVMTextAPISliceRef X86 = LLVMTextAPIGetSlice(
      File, MachO::CPU_TYPE_X86_64, MachO::CPU_SUBTYPE_X86_64_ALL,
      LLVMTextAPIParsingFlagsNone, /*minOS=*/0, &Err);
  ASSERT_NE(X86, nullptr) << (Err ? Err : "");
  EXPECT_EQ(sliceExportNames(X86), (std::set<std::string>{"_common"}));
  EXPECT_EQ(LLVMTextAPISliceGetExportedSymbol(X86, 999), nullptr);
  LLVMTextAPISliceDispose(X86);

  // arm64 sees the common symbol plus its two arm64-only symbols.
  LLVMTextAPISliceRef Arm = LLVMTextAPIGetSlice(
      File, MachO::CPU_TYPE_ARM64, MachO::CPU_SUBTYPE_ARM64_ALL,
      LLVMTextAPIParsingFlagsNone, /*minOS=*/0, nullptr);
  ASSERT_NE(Arm, nullptr);
  EXPECT_EQ(sliceExportNames(Arm),
            (std::set<std::string>{"_common", "_arm64_only", "_weak_arm64"}));
  LLVMTextAPISliceDispose(Arm);

  LLVMTextAPIContextDispose(Ctx);
  sys::fs::remove(Path);
}

TEST(TextAPICSlice, WeakDefinedFlag) {
  std::string Path = writeTempTBD(TBDv4Slices);
  LLVMTextAPIContextRef Ctx = LLVMTextAPIContextCreate();
  LLVMTextAPIRef File = LLVMTextAPIParse(Ctx, Path.c_str(), nullptr);
  ASSERT_NE(File, nullptr);

  LLVMTextAPISliceRef Arm = LLVMTextAPIGetSlice(
      File, MachO::CPU_TYPE_ARM64, MachO::CPU_SUBTYPE_ARM64_ALL,
      LLVMTextAPIParsingFlagsNone, /*minOS=*/0, nullptr);
  ASSERT_NE(Arm, nullptr);

  bool SawWeak = false, SawStrong = false;
  for (unsigned I = 0, N = LLVMTextAPISliceGetExportedSymbolCount(Arm); I < N;
       ++I) {
    LLVMTextAPISymbolRef Sym = LLVMTextAPISliceGetExportedSymbol(Arm, I);
    char *Name = LLVMTextAPISymbolCopyName(Sym);
    if (std::string(Name) == "_weak_arm64") {
      EXPECT_TRUE(LLVMTextAPISymbolIsWeakDefined(Sym));
      SawWeak = true;
    } else {
      EXPECT_FALSE(LLVMTextAPISymbolIsWeakDefined(Sym));
      SawStrong = true;
    }
    LLVMDisposeMessage(Name);
  }
  EXPECT_TRUE(SawWeak);
  EXPECT_TRUE(SawStrong);

  LLVMTextAPISliceDispose(Arm);
  LLVMTextAPIContextDispose(Ctx);
  sys::fs::remove(Path);
}

TEST(TextAPICSlice, MissingArchitectureReportsError) {
  std::string Path = writeTempTBD(TBDv4Slices); // x86_64 + arm64 only
  LLVMTextAPIContextRef Ctx = LLVMTextAPIContextCreate();
  LLVMTextAPIRef File = LLVMTextAPIParse(Ctx, Path.c_str(), nullptr);
  ASSERT_NE(File, nullptr);

  char *Err = nullptr;
  LLVMTextAPISliceRef Slice = LLVMTextAPIGetSlice(
      File, MachO::CPU_TYPE_ARM, MachO::CPU_SUBTYPE_ARM_V7,
      LLVMTextAPIParsingFlagsNone, /*minOS=*/0, &Err);
  EXPECT_EQ(Slice, nullptr);
  ASSERT_NE(Err, nullptr);
  EXPECT_NE(std::string(Err).find("missing required architecture"),
            std::string::npos);
  LLVMDisposeMessage(Err);

  LLVMTextAPIContextDispose(Ctx);
  sys::fs::remove(Path);
}

// arm64e falls back to the arm64 slice (same CPU type) unless an exact subtype
// is demanded, matching LinkerInterfaceFile's getArchForCPU.
TEST(TextAPICSlice, ArchSubtypeFallback) {
  std::string Path = writeTempTBD(TBDv4Slices); // x86_64 + arm64, no arm64e
  LLVMTextAPIContextRef Ctx = LLVMTextAPIContextCreate();
  LLVMTextAPIRef File = LLVMTextAPIParse(Ctx, Path.c_str(), nullptr);
  ASSERT_NE(File, nullptr);

  // Without ExactCPUSubType: arm64e resolves to the arm64 slice.
  char *Err = nullptr;
  LLVMTextAPISliceRef Fallback = LLVMTextAPIGetSlice(
      File, MachO::CPU_TYPE_ARM64, MachO::CPU_SUBTYPE_ARM64E,
      LLVMTextAPIParsingFlagsNone, /*minOS=*/0, &Err);
  ASSERT_NE(Fallback, nullptr) << (Err ? Err : "");
  EXPECT_EQ(sliceExportNames(Fallback),
            (std::set<std::string>{"_common", "_arm64_only", "_weak_arm64"}));
  LLVMTextAPISliceDispose(Fallback);

  // With ExactCPUSubType: there is no arm64e slice, so this fails.
  char *Err2 = nullptr;
  LLVMTextAPISliceRef Exact = LLVMTextAPIGetSlice(
      File, MachO::CPU_TYPE_ARM64, MachO::CPU_SUBTYPE_ARM64E,
      LLVMTextAPIParsingFlagsExactCPUSubType, /*minOS=*/0, &Err2);
  EXPECT_EQ(Exact, nullptr);
  ASSERT_NE(Err2, nullptr);
  LLVMDisposeMessage(Err2);

  LLVMTextAPIContextDispose(Ctx);
  sys::fs::remove(Path);
}

// ObjC class -> _OBJC_CLASS_$_ + _OBJC_METACLASS_$_, ivar -> _OBJC_IVAR_$_,
// plain global unchanged. Matches tapi::LinkerInterfaceFile (ObjC2 ABI).
TEST(TextAPICSlice, ObjCSymbolManglingObjC2ABI) {
  std::string Path = writeTempTBD(TBDv4ObjC);
  LLVMTextAPIContextRef Ctx = LLVMTextAPIContextCreate();
  LLVMTextAPIRef File = LLVMTextAPIParse(Ctx, Path.c_str(), nullptr);
  ASSERT_NE(File, nullptr);

  LLVMTextAPISliceRef Arm = LLVMTextAPIGetSlice(
      File, MachO::CPU_TYPE_ARM64, MachO::CPU_SUBTYPE_ARM64_ALL,
      LLVMTextAPIParsingFlagsNone, /*minOS=*/0, nullptr);
  ASSERT_NE(Arm, nullptr);
  EXPECT_EQ(sliceExportNames(Arm),
            (std::set<std::string>{"_plain", "_OBJC_CLASS_$_Widget",
                                   "_OBJC_METACLASS_$_Widget",
                                   "_OBJC_IVAR_$_Widget._count"}));
  LLVMTextAPISliceDispose(Arm);
  LLVMTextAPIContextDispose(Ctx);
  sys::fs::remove(Path);
}

// An ObjC EH-type yields the _OBJC_EHTYPE_$_ symbol (the reader also synthesizes
// the backing class, so this checks membership rather than the exact set).
TEST(TextAPICSlice, ObjCEHTypeMangling) {
  std::string Path = writeTempTBD(TBDv4ObjCEH);
  LLVMTextAPIContextRef Ctx = LLVMTextAPIContextCreate();
  LLVMTextAPIRef File = LLVMTextAPIParse(Ctx, Path.c_str(), nullptr);
  ASSERT_NE(File, nullptr);

  LLVMTextAPISliceRef Arm = LLVMTextAPIGetSlice(
      File, MachO::CPU_TYPE_ARM64, MachO::CPU_SUBTYPE_ARM64_ALL,
      LLVMTextAPIParsingFlagsNone, /*minOS=*/0, nullptr);
  ASSERT_NE(Arm, nullptr);
  EXPECT_EQ(sliceExportNames(Arm).count("_OBJC_EHTYPE_$_Bumper"), 1u);
  LLVMTextAPISliceDispose(Arm);
  LLVMTextAPIContextDispose(Ctx);
  sys::fs::remove(Path);
}

// i386/macOS uses the legacy .objc_class_name_ mangling and emits no metaclass.
TEST(TextAPICSlice, ObjCClassLegacyABIi386) {
  std::string Path = writeTempTBD(TBDv4ObjCLegacy);
  LLVMTextAPIContextRef Ctx = LLVMTextAPIContextCreate();
  LLVMTextAPIRef File = LLVMTextAPIParse(Ctx, Path.c_str(), nullptr);
  ASSERT_NE(File, nullptr);

  LLVMTextAPISliceRef X86 = LLVMTextAPIGetSlice(
      File, MachO::CPU_TYPE_I386, MachO::CPU_SUBTYPE_I386_ALL,
      LLVMTextAPIParsingFlagsNone, /*minOS=*/0, nullptr);
  ASSERT_NE(X86, nullptr);
  std::set<std::string> Syms = sliceExportNames(X86);
  EXPECT_EQ(Syms.count(".objc_class_name_Widget"), 1u);
  EXPECT_EQ(Syms.count("_OBJC_CLASS_$_Widget"), 0u);
  EXPECT_EQ(Syms.count("_OBJC_METACLASS_$_Widget"), 0u);
  LLVMTextAPISliceDispose(X86);
  LLVMTextAPIContextDispose(Ctx);
  sys::fs::remove(Path);
}

// $ld$ directives are dropped, except $ld$previous. Matches LinkerInterfaceFile.
TEST(TextAPICSlice, FiltersLdSymbols) {
  std::string Path = writeTempTBD(TBDv4Ld);
  LLVMTextAPIContextRef Ctx = LLVMTextAPIContextCreate();
  LLVMTextAPIRef File = LLVMTextAPIParse(Ctx, Path.c_str(), nullptr);
  ASSERT_NE(File, nullptr);

  LLVMTextAPISliceRef Arm = LLVMTextAPIGetSlice(
      File, MachO::CPU_TYPE_ARM64, MachO::CPU_SUBTYPE_ARM64_ALL,
      LLVMTextAPIParsingFlagsNone, /*minOS=*/0, nullptr);
  ASSERT_NE(Arm, nullptr);
  EXPECT_EQ(sliceExportNames(Arm),
            (std::set<std::string>{"_real", "$ld$previous$abc"}));
  LLVMTextAPISliceDispose(Arm);
  LLVMTextAPIContextDispose(Ctx);
  sys::fs::remove(Path);
}

// Per-arch read-path metadata matches LinkerInterfaceFile: the arm64 slice gets
// the arm64-targeted umbrella/rpath/reexport/client/weak data; x86_64 does not.
// The not_for_dyld_shared_cache flag is whole-file, so every slice reports it.
TEST(TextAPICSlice, Metadata) {
  std::string Path = writeTempTBD(TBDv5Meta);
  LLVMTextAPIContextRef Ctx = LLVMTextAPIContextCreate();
  char *Err = nullptr;
  LLVMTextAPIRef File = LLVMTextAPIParse(Ctx, Path.c_str(), &Err);
  ASSERT_NE(File, nullptr) << (Err ? Err : "");

  LLVMTextAPISliceRef Arm = LLVMTextAPIGetSlice(
      File, MachO::CPU_TYPE_ARM64, MachO::CPU_SUBTYPE_ARM64_ALL,
      LLVMTextAPIParsingFlagsNone, /*minOS=*/0, nullptr);
  ASSERT_NE(Arm, nullptr);

  EXPECT_STREQ(LLVMTextAPISliceGetParentFrameworkName(Arm), "TheUmbrella");

  ASSERT_EQ(LLVMTextAPISliceGetPlatformCount(Arm), 1u);
  uint32_t Platform = 0, MinOS = 0;
  LLVMTextAPISliceGetPlatform(Arm, 0, &Platform, &MinOS);
  EXPECT_EQ(Platform, static_cast<uint32_t>(MachO::PLATFORM_MACOS));
  EXPECT_EQ(MinOS, 14u << 16); // 14.0.0 packed

  ASSERT_EQ(LLVMTextAPISliceGetRPathCount(Arm), 1u);
  EXPECT_STREQ(LLVMTextAPISliceGetRPath(Arm, 0), "@loader_path/A");

  ASSERT_EQ(LLVMTextAPISliceGetReexportedLibraryCount(Arm), 1u);
  EXPECT_STREQ(LLVMTextAPISliceGetReexportedLibrary(Arm, 0),
               "/usr/lib/libre.dylib");

  ASSERT_EQ(LLVMTextAPISliceGetAllowableClientCount(Arm), 1u);
  EXPECT_STREQ(LLVMTextAPISliceGetAllowableClient(Arm, 0), "ClientArm");

  EXPECT_TRUE(LLVMTextAPISliceHasWeakDefinedExports(Arm));
  EXPECT_TRUE(LLVMTextAPISliceIsNotForDyldSharedCache(Arm));
  LLVMTextAPISliceDispose(Arm);

  LLVMTextAPISliceRef X86 = LLVMTextAPIGetSlice(
      File, MachO::CPU_TYPE_X86_64, MachO::CPU_SUBTYPE_X86_64_ALL,
      LLVMTextAPIParsingFlagsNone, /*minOS=*/0, nullptr);
  ASSERT_NE(X86, nullptr);
  // NOTE: the v5 reader applies parent_umbrellas to every file target (it
  // ignores the per-umbrella "targets" key), so both slices report it. The
  // rpath/reexport/client lists below ARE per-target, so x86_64 has none.
  EXPECT_STREQ(LLVMTextAPISliceGetParentFrameworkName(X86), "TheUmbrella");
  EXPECT_EQ(LLVMTextAPISliceGetRPathCount(X86), 0u);
  EXPECT_EQ(LLVMTextAPISliceGetReexportedLibraryCount(X86), 0u);
  EXPECT_EQ(LLVMTextAPISliceGetAllowableClientCount(X86), 0u);
  EXPECT_FALSE(LLVMTextAPISliceHasWeakDefinedExports(X86));
  EXPECT_TRUE(LLVMTextAPISliceIsNotForDyldSharedCache(X86));
  LLVMTextAPISliceDispose(X86);

  LLVMTextAPIContextDispose(Ctx);
  sys::fs::remove(Path);
}

// Inlined frameworks are enumerated by name and each resolves to its own slice
// (re-running slice selection on the sub-document), matching
// LinkerInterfaceFile::inlinedFrameworkNames/getInlinedFramework.
TEST(TextAPICSlice, InlinedFrameworks) {
  std::string Path = writeTempTBD(TBDv5Inlined);
  LLVMTextAPIContextRef Ctx = LLVMTextAPIContextCreate();
  char *Err = nullptr;
  LLVMTextAPIRef File = LLVMTextAPIParse(Ctx, Path.c_str(), &Err);
  ASSERT_NE(File, nullptr) << (Err ? Err : "");

  LLVMTextAPISliceRef Root = LLVMTextAPIGetSlice(
      File, MachO::CPU_TYPE_ARM64, MachO::CPU_SUBTYPE_ARM64_ALL,
      LLVMTextAPIParsingFlagsNone, /*minOS=*/0, nullptr);
  ASSERT_NE(Root, nullptr);

  ASSERT_EQ(LLVMTextAPISliceGetInlinedFrameworkCount(Root), 1u);
  EXPECT_STREQ(LLVMTextAPISliceGetInlinedFrameworkName(Root, 0),
               "/usr/lib/libinlined.dylib");

  // Resolve the inlined framework and read its own exports.
  char *Err2 = nullptr;
  LLVMTextAPISliceRef Inlined = LLVMTextAPISliceGetInlinedFramework(
      Root, "/usr/lib/libinlined.dylib", MachO::CPU_TYPE_ARM64,
      MachO::CPU_SUBTYPE_ARM64_ALL, LLVMTextAPIParsingFlagsNone, /*minOS=*/0,
      &Err2);
  ASSERT_NE(Inlined, nullptr) << (Err2 ? Err2 : "");
  EXPECT_EQ(sliceExportNames(Inlined), (std::set<std::string>{"_inlinedSym"}));
  LLVMTextAPISliceDispose(Inlined);

  // An unknown install name is an error.
  char *Err3 = nullptr;
  LLVMTextAPISliceRef Missing = LLVMTextAPISliceGetInlinedFramework(
      Root, "/usr/lib/libnope.dylib", MachO::CPU_TYPE_ARM64,
      MachO::CPU_SUBTYPE_ARM64_ALL, LLVMTextAPIParsingFlagsNone, /*minOS=*/0,
      &Err3);
  EXPECT_EQ(Missing, nullptr);
  ASSERT_NE(Err3, nullptr);
  LLVMDisposeMessage(Err3);

  LLVMTextAPISliceDispose(Root);
  LLVMTextAPIContextDispose(Ctx);
  sys::fs::remove(Path);
}

// $ld$ directives apply only when their os<version> condition equals the
// requested minOS, and then hide/add exports and override install
// name/compatibility version — matching LinkerInterfaceFile. $ld$weak only
// hides under DisallowWeakImports.
TEST(TextAPICSlice, LdDirectives) {
  std::string Path = writeTempTBD(TBDv4Ld2);
  LLVMTextAPIContextRef Ctx = LLVMTextAPIContextCreate();
  LLVMTextAPIRef File = LLVMTextAPIParse(Ctx, Path.c_str(), nullptr);
  ASSERT_NE(File, nullptr);

  const uint32_t MinOS105 = (10u << 16) | (5u << 8); // 10.5
  const uint32_t MinOS110 = 11u << 16;               // 11.0

  // Matching minOS: hide removes _hideme, add introduces _addme, $ld$weak is a
  // no-op without DisallowWeakImports, and install-name/compat are overridden.
  LLVMTextAPISliceRef S = LLVMTextAPIGetSlice(
      File, MachO::CPU_TYPE_X86_64, MachO::CPU_SUBTYPE_X86_64_ALL,
      LLVMTextAPIParsingFlagsNone, MinOS105, nullptr);
  ASSERT_NE(S, nullptr);
  EXPECT_EQ(sliceExportNames(S),
            (std::set<std::string>{"_keep", "_weakme", "_addme"}));
  EXPECT_STREQ(LLVMTextAPISliceGetInstallName(S), "/new/name");
  EXPECT_EQ(LLVMTextAPISliceGetCurrentVersion(S), 3u << 16);       // unchanged
  EXPECT_EQ(LLVMTextAPISliceGetCompatibilityVersion(S), 2u << 16); // overridden
  LLVMTextAPISliceDispose(S);

  // Matching minOS + DisallowWeakImports: $ld$weak now also hides _weakme.
  LLVMTextAPISliceRef SW = LLVMTextAPIGetSlice(
      File, MachO::CPU_TYPE_X86_64, MachO::CPU_SUBTYPE_X86_64_ALL,
      LLVMTextAPIParsingFlagsDisallowWeakImports, MinOS105, nullptr);
  ASSERT_NE(SW, nullptr);
  EXPECT_EQ(sliceExportNames(SW), (std::set<std::string>{"_keep", "_addme"}));
  LLVMTextAPISliceDispose(SW);

  // Non-matching minOS: every directive is ignored.
  LLVMTextAPISliceRef S2 = LLVMTextAPIGetSlice(
      File, MachO::CPU_TYPE_X86_64, MachO::CPU_SUBTYPE_X86_64_ALL,
      LLVMTextAPIParsingFlagsNone, MinOS110, nullptr);
  ASSERT_NE(S2, nullptr);
  EXPECT_EQ(sliceExportNames(S2),
            (std::set<std::string>{"_keep", "_hideme", "_weakme"}));
  EXPECT_STREQ(LLVMTextAPISliceGetInstallName(S2), "/usr/lib/libld2.dylib");
  EXPECT_EQ(LLVMTextAPISliceGetCompatibilityVersion(S2), 1u << 16);
  LLVMTextAPISliceDispose(S2);

  LLVMTextAPIContextDispose(Ctx);
  sys::fs::remove(Path);
}

} // namespace
