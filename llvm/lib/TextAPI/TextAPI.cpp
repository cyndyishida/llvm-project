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

#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/Support/CBindingWrapping.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/TextAPI/Architecture.h"
#include "llvm/TextAPI/ArchitectureSet.h"
#include "llvm/TextAPI/InterfaceFile.h"
#include "llvm/TextAPI/Target.h"
#include "llvm/TextAPI/TextAPIReader.h"

#include <cstdlib>
#include <cstring>
#include <iterator>
#include <memory>
#include <mutex>
#include <string>

using namespace llvm;
using namespace llvm::MachO;

namespace {

/// A cache entry for one real path. The modification time and size form a cheap
/// validity stamp: if either changed since we parsed, the file is re-parsed.
struct CacheEntry {
  uint64_t MTime = 0;
  uint64_t Size = 0;
  std::unique_ptr<InterfaceFile> File;
};

/// The opaque context: a parse cache plus a lock guarding it, since the linker
/// parses inputs concurrently.
struct TextAPIContext {
  std::mutex Mutex;
  StringMap<CacheEntry> Cache;
};

} // namespace

DEFINE_SIMPLE_CONVERSION_FUNCTIONS(TextAPIContext, LLVMTextAPIContextRef)
DEFINE_SIMPLE_CONVERSION_FUNCTIONS(InterfaceFile, LLVMTextAPIRef)

/// Duplicate \p Message into a malloc'd C string the caller frees with
/// LLVMDisposeMessage (which calls free), matching the rest of llvm-c.
static char *copyCString(const Twine &Message) {
  SmallString<128> Storage;
  StringRef Ref = Message.toStringRef(Storage);
  return strdup(Ref.str().c_str());
}

unsigned LLVMTextAPIGetAPIVersion(void) { return LLVM_TEXTAPI_VERSION; }

LLVMTextAPIContextRef LLVMTextAPIContextCreate(void) {
  return wrap(new TextAPIContext());
}

void LLVMTextAPIContextDispose(LLVMTextAPIContextRef Ctx) {
  delete unwrap(Ctx);
}

LLVMTextAPIRef LLVMTextAPIParse(LLVMTextAPIContextRef CtxRef, const char *Path,
                                char **OutError) {
  if (OutError)
    *OutError = nullptr;

  TextAPIContext *Ctx = unwrap(CtxRef);

  // Resolve symlinks so two paths to the same file share one cache entry.
  SmallString<256> RealPath;
  if (std::error_code EC = sys::fs::real_path(Path, RealPath)) {
    if (OutError)
      *OutError = copyCString(Twine(Path) + ": " + EC.message());
    return nullptr;
  }

  sys::fs::file_status Status;
  if (std::error_code EC = sys::fs::status(RealPath, Status)) {
    if (OutError)
      *OutError = copyCString(RealPath + ": " + EC.message());
    return nullptr;
  }
  uint64_t MTime = static_cast<uint64_t>(
      Status.getLastModificationTime().time_since_epoch().count());
  uint64_t Size = Status.getSize();

  std::lock_guard<std::mutex> Lock(Ctx->Mutex);

  CacheEntry &Entry = Ctx->Cache[RealPath];
  if (Entry.File && Entry.MTime == MTime && Entry.Size == Size)
    return wrap(Entry.File.get());

  ErrorOr<std::unique_ptr<MemoryBuffer>> BufOrErr =
      MemoryBuffer::getFile(RealPath);
  if (!BufOrErr) {
    if (OutError)
      *OutError = copyCString(RealPath + ": " + BufOrErr.getError().message());
    return nullptr;
  }

  Expected<std::unique_ptr<InterfaceFile>> FileOrErr =
      TextAPIReader::get((*BufOrErr)->getMemBufferRef());
  if (!FileOrErr) {
    if (OutError)
      *OutError =
          copyCString(RealPath + ": " + toString(FileOrErr.takeError()));
    return nullptr;
  }

  Entry.MTime = MTime;
  Entry.Size = Size;
  Entry.File = std::move(*FileOrErr);
  return wrap(Entry.File.get());
}

unsigned LLVMTextAPIGetArchitectureCount(LLVMTextAPIRef File) {
  return static_cast<unsigned>(unwrap(File)->getArchitectures().count());
}

char *LLVMTextAPICopyArchitectureName(LLVMTextAPIRef File, unsigned Index) {
  unsigned I = 0;
  for (Architecture Arch : unwrap(File)->getArchitectures()) {
    if (I++ == Index)
      return copyCString(getArchitectureName(Arch));
  }
  return nullptr;
}

unsigned LLVMTextAPIGetTargetCount(LLVMTextAPIRef File) {
  auto Targets = unwrap(File)->targets();
  return static_cast<unsigned>(std::distance(Targets.begin(), Targets.end()));
}

char *LLVMTextAPICopyTargetTriple(LLVMTextAPIRef File, unsigned Index) {
  unsigned I = 0;
  for (const Target &T : unwrap(File)->targets()) {
    if (I++ == Index)
      return copyCString(getTargetTripleName(T));
  }
  return nullptr;
}

char *LLVMTextAPICopyInstallName(LLVMTextAPIRef File) {
  return copyCString(unwrap(File)->getInstallName());
}

uint32_t LLVMTextAPIGetCurrentVersion(LLVMTextAPIRef File) {
  return unwrap(File)->getCurrentVersion().rawValue();
}

uint32_t LLVMTextAPIGetCompatibilityVersion(LLVMTextAPIRef File) {
  return unwrap(File)->getCompatibilityVersion().rawValue();
}
