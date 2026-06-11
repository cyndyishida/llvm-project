//===- TextAPI.cpp - TextAPI C API ----------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements the stable C bindings to the TextAPI library
// (llvm::MachO) declared in llvm-c/TextAPI.h.
//
//===----------------------------------------------------------------------===//

#include "llvm-c/TextAPI.h"

unsigned LLVMTextAPIGetAPIVersion(void) { return LLVM_TEXTAPI_VERSION; }
