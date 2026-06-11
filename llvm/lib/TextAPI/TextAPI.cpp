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
#include "llvm/ADT/Twine.h"
#include "llvm/BinaryFormat/MachO.h"
#include "llvm/Support/CBindingWrapping.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/TextAPI/Architecture.h"
#include "llvm/TextAPI/ArchitectureSet.h"
#include "llvm/TextAPI/InterfaceFile.h"
#include "llvm/TextAPI/Symbol.h"
#include "llvm/TextAPI/Target.h"
#include "llvm/TextAPI/TextAPIReader.h"

#include <cstdlib>
#include <cstring>
#include <iterator>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

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

/// One exported symbol of a slice: the Mach-O linker symbol name (synthesized
/// to match tapi::LinkerInterfaceFile, e.g. "_OBJC_CLASS_$_Foo") together with
/// the originating llvm::MachO::Symbol it derives from (owned by the file),
/// which carries the flags (weak, etc.).
struct TextAPIExportedSymbol {
  std::string Name;
  const Symbol *Source;
};

/// One architecture slice: the file's exported symbols flattened to the names
/// the static linker consumes, captured at selection time for O(1) indexed
/// access. The source Symbols remain owned by the file (and its context).
struct TextAPISlice {
  std::vector<TextAPIExportedSymbol> Exports;
};

} // namespace

DEFINE_SIMPLE_CONVERSION_FUNCTIONS(TextAPIContext, LLVMTextAPIContextRef)
DEFINE_SIMPLE_CONVERSION_FUNCTIONS(InterfaceFile, LLVMTextAPIRef)
DEFINE_SIMPLE_CONVERSION_FUNCTIONS(TextAPISlice, LLVMTextAPISliceRef)
DEFINE_SIMPLE_CONVERSION_FUNCTIONS(TextAPIExportedSymbol, LLVMTextAPISymbolRef)

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
  Entry.File->setPath(RealPath);
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

// Reproduce ArchitectureSet::getABICompatibleSlice (an Apple addition not yet
// in open-source TextAPI): the first arch in the set sharing the requested
// arch's CPU type, ignoring the subtype.
static Architecture getABICompatibleSlice(ArchitectureSet Archs,
                                          Architecture Arch) {
  uint32_t CPUType = getCPUTypeFromArchitecture(Arch).first;
  for (Architecture Candidate : Archs)
    if (getCPUTypeFromArchitecture(Candidate).first == CPUType)
      return Candidate;
  return AK_unknown;
}

LLVMTextAPISliceRef LLVMTextAPIGetSlice(LLVMTextAPIRef FileRef, uint32_t CPUType,
                                        uint32_t CPUSubType, uint32_t Flags,
                                        char **OutError) {
  if (OutError)
    *OutError = nullptr;

  InterfaceFile *File = unwrap(FileRef);
  ArchitectureSet Archs = File->getArchitectures();
  Architecture Exact = getArchitectureFromCpuType(CPUType, CPUSubType);

  // Select the architecture, matching tapi::LinkerInterfaceFile::getArchForCPU:
  // an exact match if present; otherwise an ABI-compatible slice of the same
  // CPU type, unless an exact subtype was demanded.
  Architecture Arch;
  if (Archs.has(Exact))
    Arch = Exact;
  else if (Flags & LLVMTextAPIParsingFlagsExactCPUSubType)
    Arch = AK_unknown;
  else
    Arch = getABICompatibleSlice(Archs, Exact);

  if (Arch == AK_unknown) {
    if (OutError) {
      std::string Msg = (Twine("missing required architecture ") +
                         getArchitectureName(Exact) + " in file " +
                         File->getPath())
                            .str();
      size_t Count = Archs.count();
      if (Count > 1)
        Msg += " (" + std::to_string(Count) + " slices)";
      *OutError = copyCString(Msg);
    }
    return nullptr;
  }

  // Flatten exported symbols to the names the static linker consumes, matching
  // tapi::LinkerInterfaceFile::create: ObjC classes expand to two symbols, ObjC
  // EH-types/ivars are mangled, and the legacy ObjC1 ABI class mangling is used
  // only for i386/macOS.
  bool UseObjC1ABI =
      File->getPlatforms().count(PLATFORM_MACOS) && Arch == AK_i386;

  auto Slice = std::make_unique<TextAPISlice>();
  auto addExport = [&](std::string Name, const Symbol *Source) {
    Slice->Exports.push_back({std::move(Name), Source});
  };
  for (const Symbol *Sym : File->symbols()) {
    if (Sym->isUndefined() || !Sym->hasArchitecture(Arch))
      continue;

    switch (Sym->getKind()) {
    case EncodeKind::GlobalSymbol:
      // $ld$ symbols are linker directives rather than real exports, except
      // $ld$previous. This matches LinkerInterfaceFile.
      if (Sym->getName().starts_with("$ld$") &&
          !Sym->getName().starts_with("$ld$previous"))
        continue;
      addExport(Sym->getName().str(), Sym);
      break;
    case EncodeKind::ObjectiveCClass:
      if (UseObjC1ABI) {
        addExport(".objc_class_name_" + Sym->getName().str(), Sym);
      } else {
        addExport("_OBJC_CLASS_$_" + Sym->getName().str(), Sym);
        addExport("_OBJC_METACLASS_$_" + Sym->getName().str(), Sym);
      }
      break;
    case EncodeKind::ObjectiveCClassEHType:
      addExport("_OBJC_EHTYPE_$_" + Sym->getName().str(), Sym);
      break;
    case EncodeKind::ObjectiveCInstanceVariable:
      addExport("_OBJC_IVAR_$_" + Sym->getName().str(), Sym);
      break;
    }
  }
  return wrap(Slice.release());
}

void LLVMTextAPISliceDispose(LLVMTextAPISliceRef Slice) {
  delete unwrap(Slice);
}

unsigned LLVMTextAPISliceGetExportedSymbolCount(LLVMTextAPISliceRef Slice) {
  return static_cast<unsigned>(unwrap(Slice)->Exports.size());
}

LLVMTextAPISymbolRef
LLVMTextAPISliceGetExportedSymbol(LLVMTextAPISliceRef Slice, unsigned Index) {
  const std::vector<TextAPIExportedSymbol> &Exports = unwrap(Slice)->Exports;
  if (Index >= Exports.size())
    return nullptr;
  return wrap(&Exports[Index]);
}

char *LLVMTextAPISymbolCopyName(LLVMTextAPISymbolRef Symbol) {
  return copyCString(unwrap(Symbol)->Name);
}

LLVMBool LLVMTextAPISymbolIsWeakDefined(LLVMTextAPISymbolRef Symbol) {
  return unwrap(Symbol)->Source->isWeakDefined();
}
