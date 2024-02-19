//===--- clang-installapi/Options.h - Options -------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_TOOLS_CLANG_INSTALLAPI_OPTIONS_H
#define LLVM_CLANG_TOOLS_CLANG_INSTALLAPI_OPTIONS_H

#include "llvm/Support/Program.h"
#include "clang/Basic/FileManager.h"
#include "clang/Frontend/FrontendOptions.h"
#include "clang/Basic/Diagnostic.h"
#include "llvm/Option/ArgList.h"
#include "llvm/Option/Option.h"
#include "llvm/TargetParser/Triple.h"
#include "llvm/TextAPI/Architecture.h"
#include "llvm/TextAPI/InterfaceFile.h"
#include "llvm/TextAPI/PackedVersion.h"
#include "llvm/TextAPI/Platform.h"
#include "llvm/TextAPI/Utils.h"
#include <set>
#include <string>
#include <vector>

namespace clang {
namespace installapi {
using Macro = std::pair<std::string, bool /*isUndef*/>;

struct DriverOptions {
  /// \brief Path to file lists (JSON).
  llvm::MachO::PathSeq FileLists;
  
  /// \brief Targets to build for.
  std::vector<llvm::Triple> Targets;

  /// \brief Output path.
  std::string OutputPath;

  /// \brief File encoding to print. 
  llvm::MachO::FileType OutFT = llvm::MachO::FileType::TBD_V5;

  /// \brief Infer the include paths based on the provided/found header files.
  bool InferIncludePaths = true;
  
  /// \brief Print version informtion.
  bool PrintVersion = false;

  /// \brief Print help.
  bool PrintHelp = false;
  
  /// \brief Verbose, show scan content and options.
  bool Verbose = false;
};

struct LinkerOptions {
  /// \brief The install name to use for the dynamic library.
  std::string InstallName;

  /// \brief The current version to use for the dynamic library.
  llvm::MachO::PackedVersion CurrentVersion;

  /// \brief The compatibility version to use for the dynamic library.
  llvm::MachO::PackedVersion CompatibilityVersion;

  /// \brief List of allowable clients to use for the dynamic library.
  std::vector<std::pair<std::string, llvm::MachO::ArchitectureSet>> AllowableClients;

  /// \brief List of reexported libraries to use for the dynamic library.
  std::vector<std::pair<std::string, llvm::MachO::ArchitectureSet>> ReexportedLibraries;

  /// \brief List of reexported libraries to use for the dynamic library.
  std::vector<std::pair<std::string, llvm::MachO::ArchitectureSet>> ReexportedLibraryPaths;

  /// \brief List of reexported frameworks to use for the dynamic library.
  std::vector<std::pair<std::string, llvm::MachO::ArchitectureSet>> ReexportedFrameworks;
  
  /// \brief List of run search paths.
  std::vector<std::pair<std::string, llvm::MachO::ArchitectureSet>> Rpaths;

  /// \brief Is application extension safe.
  bool IsApplicationExtensionSafe = false;

  /// \brief Is OS library that is not for shared cache.
  bool IsOSLibNotForSharedCache = false;
  
  /// \brief Set if we should scan for a dynamic library and not a framework.
  bool IsDylib = false;
  
};

struct FrontendOptions {

  /// \brief Additonal target variants to build for.
  std::vector<llvm::Triple> TargetVariants;

  /// \brief Specify the language to use for parsing.
  clang::Language Language = clang::Language::Unknown;

  /// \brief Language standard to use for parsing.
  std::string LangStd;

  /// \brief The sysroot to search for SDK headers.
  std::string Sysroot;

  /// \brief Additional SYSTEM framework search paths.
  llvm::MachO::PathSeq systemFrameworkPaths;

  /// \brief Additional framework search paths.
  llvm::MachO::PathSeq frameworkPaths;

  /// \brief Additional library search paths.
  llvm::MachO::PathSeq libraryPaths;

  /// \brief Additional SYSTEM include paths.
  llvm::MachO::PathSeq systemIncludePaths;

  /// \brief Additional AFTER include paths.
  llvm::MachO::PathSeq afterIncludePaths;

  /// \brief Additional include paths.
  llvm::MachO::PathSeq includePaths;

  /// \brief Additional include local paths.
  llvm::MachO::PathSeq quotedIncludePaths;

  /// \brief Macros to use for for parsing.
  std::vector<Macro> Macros;

  /// \brief overwrite to use RTTI.
  bool useRTTI = false;

  /// \brief overwrite to use no-RTTI.
  bool useNoRTTI = false;

  /// \brief Set the visibility.
  std::string Visibility;

  /// \brief Additional clang flags to be passed to the parser.
  std::vector<std::string> ClangExtraArgs;

  /// \brief Clang resource path.
  std::string ClangResourcePath;

  /// \brief Use Objective-C ARC (-fobjc-arc).
  bool UseObjectiveCARC = false;

  /// \brief Use Objective-C weak ARC (-fobjc-weak).
  bool UseObjectiveCWeakARC = false;
};

class Options {
private:
  bool processDriverOptions(llvm::opt::InputArgList &Args);

  bool processLinkerOptions(llvm::opt::InputArgList &Args);

  bool processFrontendOptions(llvm::opt::InputArgList &Args);

public:

  /// The various options grouped together.
  DriverOptions DriverOptions;
  LinkerOptions LinkerOptions;
  FrontendOptions FrontendOptions;

  Options() = delete;

  /// \brief Constructor for options.
  Options(clang::DiagnosticsEngine &Diag, FileManager* FM, llvm::opt::InputArgList& Args);

  /// \brief Print the help depending on the recognized coomand.
  void PrintHelp() const;

private:
  DiagnosticsEngine* Diags;
  FileManager* FM;
};

}
}
#endif 
