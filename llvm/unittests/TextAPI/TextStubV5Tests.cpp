//===-- TextStubV4Tests.cpp - TBD V4 File Test ----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===-----------------------------------------------------------------------===/

#include "TextStubHelpers.h"
#include "llvm/TextAPI/InterfaceFile.h"
#include "llvm/TextAPI/TextAPIReader.h"
#include "llvm/TextAPI/TextAPIWriter.h"
#include "gtest/gtest.h"
#include <string>
#include <vector>

using namespace llvm;
using namespace llvm::MachO;

namespace TBDv5 {

TEST(TBDv5, ReadFile) {
  std::string Path = "/tmp/Cleaned.json";
  auto errorOr = MemoryBuffer::getFile(Path, /*IsText=*/true,
                                       /*RequiresNullTerminator=*/true,
                                       /*IsVolatile=*/false);

  EXPECT_TRUE(std::error_code() == errorOr.getError());
  auto json = std::move(errorOr.get());
  Expected<TBDFile> Result = TextAPIReader::get(json->getMemBufferRef());
  EXPECT_TRUE(!!Result);
  TBDFile File = std::move(Result.get());
  EXPECT_EQ(FileType::TBD_V5, File->getFileType());
  EXPECT_EQ(std::string("/S/L/F/Foo.framework/Foo"), File->getInstallName());
  EXPECT_EQ(PackedVersion(1, 2, 1), File->getCurrentVersion());
  EXPECT_EQ(PackedVersion(1, 1, 0), File->getCompatibilityVersion());
  EXPECT_TRUE(File->isApplicationExtensionSafe());
  EXPECT_FALSE(File->isTwoLevelNamespace());

  TargetList Targets = {
      Target(AK_x86_64, PLATFORM_MACOS),
      Target(AK_x86_64, PLATFORM_MACCATALYST),
      Target(AK_arm64, PLATFORM_MACOS),
      Target(AK_arm64, PLATFORM_MACCATALYST),
  };

  InterfaceFileRef clientA("ClientA", Targets);
  InterfaceFileRef clientB("ClientB", Targets);
  EXPECT_EQ(2U, File->allowableClients().size());
  EXPECT_EQ(clientA, File->allowableClients().at(0));
  EXPECT_EQ(clientB, File->allowableClients().at(1));

  InterfaceFileRef reexportA("/u/l/l/libbar.dylib", Targets);
  InterfaceFileRef reexportB("/u/l/l/libfoo.dylib", Targets);
  EXPECT_EQ(2U, File->reexportedLibraries().size());
  EXPECT_EQ(reexportA, File->reexportedLibraries().at(0));
  EXPECT_EQ(reexportB, File->reexportedLibraries().at(1));

  std::vector<std::pair<Target, std::string>> Umbrellas = {
      {Target(AK_x86_64, PLATFORM_MACOS), "System"},
      {Target(AK_arm64, PLATFORM_MACOS), "System"}};
  EXPECT_EQ(Umbrellas, File->umbrellas());

  ExportedSymbolSeq Exports, Reexports, Undefineds;
  for (const auto *Sym : File->symbols()) {
    ExportedSymbol temp =
        ExportedSymbol{Sym->getKind(), std::string(Sym->getName()),
                       Sym->isWeakDefined(), Sym->isThreadLocalValue()};
    if (Sym->isUndefined())
      Undefineds.emplace_back(std::move(temp));
    else
      Sym->isReexported() ? Reexports.emplace_back(std::move(temp))
                          : Exports.emplace_back(std::move(temp));
  }
  llvm::sort(Exports);
  llvm::sort(Reexports);
  llvm::sort(Undefineds);

  static ExportedSymbol ExpectedExportedSymbols[] = {
      {SymbolKind::GlobalSymbol, "_func", false, false},
      {SymbolKind::GlobalSymbol, "_global", false, false},
      {SymbolKind::GlobalSymbol, "_symT", false, true},
      {SymbolKind::ObjectiveCClass, "ClassA", false, false},
  };

  // static ExportedSymbol ExpectedReexportedSymbols[] = {
  //     {SymbolKind::GlobalSymbol, "_globalRe", false, false},
  // };

  // static ExportedSymbol ExpectedUndefinedSymbols[] = {
  //     {SymbolKind::GlobalSymbol, "_symD", false, false},
  // };

  EXPECT_EQ(sizeof(ExpectedExportedSymbols) / sizeof(ExportedSymbol),
            Exports.size());
  // EXPECT_EQ(sizeof(ExpectedReexportedSymbols) / sizeof(ExportedSymbol),
  //           Reexports.size());
  // EXPECT_EQ(sizeof(ExpectedUndefinedSymbols) / sizeof(ExportedSymbol),
  //           Undefineds.size());
  EXPECT_TRUE(std::equal(Exports.begin(), Exports.end(),
                         std::begin(ExpectedExportedSymbols)));
  // EXPECT_TRUE(std::equal(Reexports.begin(), Reexports.end(),
  //                        std::begin(ExpectedReexportedSymbols)));
  // EXPECT_TRUE(std::equal(Undefineds.begin(), Undefineds.end(),
  //                        std::begin(ExpectedUndefinedSymbols)));
}

} // end namespace TBDv5
