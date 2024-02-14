//===--- InstallAPIConsumer.cpp -------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "clang/Basic/DiagnosticFrontend.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/InstallAPI/Context.h"
#include "clang/InstallAPI/FileList.h"

using namespace clang;
using namespace clang::installapi;

std::unique_ptr<ASTConsumer>
InstallAPIAction::CreateASTConsumer(CompilerInstance &CI, StringRef InFile) {
  const InstallAPIOptions &Opts = CI.getInstallAPIOpts();
  InstallAPIContext Ctx;
  Ctx.BA.InstallName = Opts.InstallName;
  Ctx.BA.AppExtensionSafe = CI.getLangOpts().AppExt;
  Ctx.BA.CurrentVersion = Opts.CurrentVersion;
  // InstallAPI requires two level namespacing.
  Ctx.BA.TwoLevelNamespace = true;
  Ctx.TargetTriple = CI.getTarget().getTriple();

  Ctx.Diags = &CI.getDiagnostics();
  Ctx.OutputLoc = CI.getFrontendOpts().OutputFile;
  Ctx.OS = CreateOutputFile(CI, InFile);
  if (!Ctx.OS)
    return nullptr;
  return std::make_unique<InstallAPIConsumer>(std::move(Ctx));
}

std::unique_ptr<llvm::raw_pwrite_stream>
InstallAPIAction::CreateOutputFile(CompilerInstance &CI, StringRef InFile) {
  std::unique_ptr<raw_pwrite_stream> OS =
      CI.createDefaultOutputFile(/*Binary=*/false, InFile, /*Extension=*/"tbd",
                                 /*RemoveFileOnSignal=*/false);
  if (!OS)
    return nullptr;
  return OS;
}

bool InstallAPIAction::PrepareToExecuteAction(CompilerInstance &CI) {
  const FrontendInputFile &IF = getCurrentInput();
  DiagnosticsEngine &Diags = CI.getDiagnostics();
  if (!IF.isFile() || !IF.getFile().ends_with(".json"))
    return false;

  FileManager &FM = CI.getFileManager();
  auto FileOrErr = FM.getBufferForFile(IF.getFile());
  if (auto Err = FileOrErr.getError()) {
    Diags.Report(diag::err_fe_error_reading) << FileName << Err.message();
    return false;
  }

  HeaderSeq HeaderInputs;
  llvm::Expected<std::unique_ptr<FileListReader>> Reader =
      FileListReader::get(std::move(FileOrErr.get()));
  FileListVisitor Visitor(FM, Diags, HeaderInputs);
  Reader.get()->visit(Visitor);
  if (Diags.hasErrorOccurred())
    return false;

  return true;
}
