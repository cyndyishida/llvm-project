//===- llvm/TextAPI/Utils.h - TAPI Utils -----------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Helper functionality used for Darwin specific operations.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_TEXTAPI_UTILS_H
#define LLVM_TEXTAPI_UTILS_H

#include "llvm/ADT/Twine.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"

#if !defined(PATH_MAX)
#define PATH_MAX 1024
#endif

#define MACCATALYST_PREFIX_PATH "/System/iOSSupport"
#define DRIVERKIT_PREFIX_PATH "/System/DriverKit"

namespace llvm::MachO {

using PathSeq = std::vector<std::string>;

struct SymLink {
  std::string SrcPath;
  std::string LinkContent;

  SymLink(std::string Path, std::string Link)
      : SrcPath(std::move(Path)), LinkContent(std::move(Link)) {}

  SymLink(StringRef Path, StringRef Link)
      : SrcPath(std::string(Path)), LinkContent(std::string(Link)) {}
};

/// Replace extension considering frameworks.
///
/// \param Path Location of file.
/// \param Extension File extension to update with.
void replace_extension(SmallVectorImpl<char> &Path, const Twine &Extension);

///
///
/// \param Path
/// \param Result
std::error_code shouldSkipSymlink(const Twine &Path, bool &Result);

std::error_code read_link(const Twine &Path, SmallVectorImpl<char> &LinkPath);

std::error_code make_relative(StringRef From, StringRef To,
                              SmallVectorImpl<char> &RelativePath);

std::error_code realpath(SmallVectorImpl<char> &Path);
} // namespace llvm::MachO
#endif // LLVM_TEXTAPI_UTILS_H
