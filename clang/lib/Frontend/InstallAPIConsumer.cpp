//===--- InstallAPIConsumer.cpp -------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/InstallAPI/Context.h"

using namespace clang;
using namespace clang::installapi;

std::unique_ptr<ASTConsumer>
InstallAPIAction::CreateASTConsumer(CompilerInstance &CI, StringRef InFile) {
  InstallAPIContext Ctx;
  return std::make_unique<InstallAPIConsumer>(std::move(Ctx));
}
