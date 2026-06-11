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
#include "llvm/TextAPI/PackedVersion.h"
#include "llvm/TextAPI/Symbol.h"
#include "llvm/TextAPI/Target.h"
#include "llvm/TextAPI/TextAPIReader.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <iterator>
#include <memory>
#include <mutex>
#include <string>
#include <tuple>
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
/// to match tapi::LinkerInterfaceFile, e.g. "_OBJC_CLASS_$_Foo") and whether it
/// is a weak definition. Synthesized $ld$add exports have no source symbol, so
/// the weak flag is stored directly rather than referencing a Symbol.
struct TextAPIExportedSymbol {
  std::string Name;
  bool WeakDefined;
};

/// One architecture slice: the file's read-path data flattened for a single
/// selected architecture (mirroring tapi::LinkerInterfaceFile).
struct TextAPISlice {
  const InterfaceFile *File = nullptr; // source file (for inlined resolution)
  std::string InstallName;
  uint32_t CurrentVersion = 0;
  uint32_t CompatibilityVersion = 0;
  std::string ParentFrameworkName; // empty if none
  std::vector<std::pair<uint32_t, uint32_t>> PlatformsAndMinOS; // (platform, ver)
  std::vector<std::string> RPaths;
  std::vector<std::string> ReexportedLibraries;
  std::vector<std::string> AllowableClients;
  std::vector<std::string> InlinedFrameworkNames;
  bool HasWeakDefinedExports = false;
  bool IsNotForDyldSharedCache = false;
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

/// Parse a version string (e.g. "10.5") into a Mach-O packed version, matching
/// tapi's parseVersion32 (zero on an empty or malformed string).
static PackedVersion parseVersion32(StringRef Str) {
  PackedVersion Version;
  Version.parse32(Str);
  return Version;
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

// Build a slice for one architecture of \p File, mirroring
// tapi::LinkerInterfaceFile::init. Returns a heap slice the caller owns, or
// nullptr with *OutError set.
static TextAPISlice *buildSlice(const InterfaceFile *File, uint32_t CPUType,
                                uint32_t CPUSubType, uint32_t Flags,
                                uint32_t PackedMinOS, char **OutError) {
  if (OutError)
    *OutError = nullptr;

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

  // Per-arch metadata, mirroring tapi::LinkerInterfaceFile::init.
  Slice->IsNotForDyldSharedCache = File->isOSLibNotForSharedCache();
  Slice->InstallName = File->getInstallName().str();
  Slice->CurrentVersion = File->getCurrentVersion().rawValue();
  Slice->CompatibilityVersion = File->getCompatibilityVersion().rawValue();

  // $ld$ conditions match the deployment version with its patch level dropped.
  PackedVersion MinOS(PackedVersion(PackedMinOS).getMajor(),
                      PackedVersion(PackedMinOS).getMinor(), 0);
  bool DisallowWeakImports = Flags & LLVMTextAPIParsingFlagsDisallowWeakImports;
  std::vector<std::string> IgnoreExports;

  for (const std::pair<Target, std::string> &Umbrella : File->umbrellas())
    if (Umbrella.first.Arch == Arch) {
      Slice->ParentFrameworkName = Umbrella.second;
      break;
    }

  for (const Target &T : File->targets()) {
    if (T.Arch != Arch || T.Platform == PLATFORM_UNKNOWN)
      continue;
    Slice->PlatformsAndMinOS.emplace_back(
        static_cast<uint32_t>(T.Platform),
        PackedVersion(T.MinDeployment).rawValue());
  }

  for (const std::pair<Target, std::string> &RPath : File->rpaths())
    if (RPath.first.Arch == Arch)
      Slice->RPaths.push_back(RPath.second);

  for (const InterfaceFileRef &Lib : File->reexportedLibraries())
    for (const Target &T : Lib.targets())
      if (T.Arch == Arch)
        Slice->ReexportedLibraries.push_back(Lib.getInstallName().str());

  for (const InterfaceFileRef &Lib : File->allowableClients())
    for (const Target &T : Lib.targets())
      if (T.Arch == Arch)
        Slice->AllowableClients.push_back(Lib.getInstallName().str());

  // Pre-scan global export symbols for $ld$ linker directives, which can hide
  // or add exports and override the install name / compatibility version.
  auto processLd = [&](StringRef Name) {
    // $ld$ <action> $ <condition> $ <symbol-name>
    if (!Name.starts_with("$ld$"))
      return;
    StringRef Rest = Name.drop_front(4);
    StringRef Action, Condition, SymbolName;
    std::tie(Action, Rest) = Rest.split('$');
    std::tie(Condition, SymbolName) = Rest.split('$');
    if (Action.empty() || Condition.empty() || SymbolName.empty())
      return;
    if (!Condition.starts_with("os"))
      return;
    if (parseVersion32(Condition.drop_front(2)) != MinOS)
      return;

    if (Action == "hide") {
      IgnoreExports.push_back(SymbolName.str());
    } else if (Action == "add") {
      Slice->Exports.push_back({SymbolName.str(), /*WeakDefined=*/false});
    } else if (Action == "weak") {
      if (DisallowWeakImports)
        IgnoreExports.push_back(SymbolName.str());
    } else if (Action == "install_name") {
      Slice->InstallName = SymbolName.str();
      if (Slice->InstallName == "/System/Library/Frameworks/"
                                "ApplicationServices.framework/Versions/A/"
                                "ApplicationServices")
        Slice->CompatibilityVersion = PackedVersion(1, 0, 0).rawValue();
    } else if (Action == "compatibility_version") {
      Slice->CompatibilityVersion = parseVersion32(SymbolName).rawValue();
    }
  };
  for (const Symbol *Sym : File->exports())
    if (Sym->getKind() == EncodeKind::GlobalSymbol && Sym->hasArchitecture(Arch))
      processLd(Sym->getName());
  llvm::sort(IgnoreExports);
  IgnoreExports.erase(std::unique(IgnoreExports.begin(), IgnoreExports.end()),
                      IgnoreExports.end());

  // Add a flattened export unless a $ld$hide/$ld$weak directive ignored it.
  auto addExport = [&](std::string Name, bool WeakDefined) {
    if (!std::binary_search(IgnoreExports.begin(), IgnoreExports.end(), Name))
      Slice->Exports.push_back({std::move(Name), WeakDefined});
  };
  for (const Symbol *Sym : File->symbols()) {
    if (Sym->isUndefined() || !Sym->hasArchitecture(Arch))
      continue;
    bool Weak = Sym->isWeakDefined();

    switch (Sym->getKind()) {
    case EncodeKind::GlobalSymbol:
      // $ld$ symbols are linker directives rather than real exports, except
      // $ld$previous. This matches LinkerInterfaceFile.
      if (Sym->getName().starts_with("$ld$") &&
          !Sym->getName().starts_with("$ld$previous"))
        continue;
      addExport(Sym->getName().str(), Weak);
      break;
    case EncodeKind::ObjectiveCClass:
      if (UseObjC1ABI) {
        addExport(".objc_class_name_" + Sym->getName().str(), Weak);
      } else {
        addExport("_OBJC_CLASS_$_" + Sym->getName().str(), Weak);
        addExport("_OBJC_METACLASS_$_" + Sym->getName().str(), Weak);
      }
      break;
    case EncodeKind::ObjectiveCClassEHType:
      addExport("_OBJC_EHTYPE_$_" + Sym->getName().str(), Weak);
      break;
    case EncodeKind::ObjectiveCInstanceVariable:
      addExport("_OBJC_IVAR_$_" + Sym->getName().str(), Weak);
      break;
    }

    if (Weak)
      Slice->HasWeakDefinedExports = true;
  }

  // Inlined frameworks: all documents, not arch-filtered. Keep the source file
  // so LLVMTextAPISliceGetInlinedFramework can resolve a sub-document later.
  Slice->File = File;
  for (const std::shared_ptr<InterfaceFile> &Doc : File->documents())
    Slice->InlinedFrameworkNames.push_back(Doc->getInstallName().str());

  return Slice.release();
}

LLVMTextAPISliceRef LLVMTextAPIGetSlice(LLVMTextAPIRef FileRef, uint32_t CPUType,
                                        uint32_t CPUSubType, uint32_t Flags,
                                        uint32_t PackedMinOS, char **OutError) {
  return wrap(buildSlice(unwrap(FileRef), CPUType, CPUSubType, Flags,
                         PackedMinOS, OutError));
}

void LLVMTextAPISliceDispose(LLVMTextAPISliceRef Slice) {
  delete unwrap(Slice);
}

const char *LLVMTextAPISliceGetInstallName(LLVMTextAPISliceRef Slice) {
  return unwrap(Slice)->InstallName.c_str();
}

uint32_t LLVMTextAPISliceGetCurrentVersion(LLVMTextAPISliceRef Slice) {
  return unwrap(Slice)->CurrentVersion;
}

uint32_t LLVMTextAPISliceGetCompatibilityVersion(LLVMTextAPISliceRef Slice) {
  return unwrap(Slice)->CompatibilityVersion;
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
  return unwrap(Symbol)->WeakDefined;
}

const char *LLVMTextAPISliceGetParentFrameworkName(LLVMTextAPISliceRef Slice) {
  const std::string &Name = unwrap(Slice)->ParentFrameworkName;
  return Name.empty() ? nullptr : Name.c_str();
}

unsigned LLVMTextAPISliceGetPlatformCount(LLVMTextAPISliceRef Slice) {
  return static_cast<unsigned>(unwrap(Slice)->PlatformsAndMinOS.size());
}

void LLVMTextAPISliceGetPlatform(LLVMTextAPISliceRef Slice, unsigned Index,
                                 uint32_t *OutPlatform,
                                 uint32_t *OutPackedMinOS) {
  const std::vector<std::pair<uint32_t, uint32_t>> &V =
      unwrap(Slice)->PlatformsAndMinOS;
  if (Index >= V.size())
    return;
  if (OutPlatform)
    *OutPlatform = V[Index].first;
  if (OutPackedMinOS)
    *OutPackedMinOS = V[Index].second;
}

unsigned LLVMTextAPISliceGetRPathCount(LLVMTextAPISliceRef Slice) {
  return static_cast<unsigned>(unwrap(Slice)->RPaths.size());
}

const char *LLVMTextAPISliceGetRPath(LLVMTextAPISliceRef Slice, unsigned Index) {
  const std::vector<std::string> &V = unwrap(Slice)->RPaths;
  return Index < V.size() ? V[Index].c_str() : nullptr;
}

unsigned LLVMTextAPISliceGetReexportedLibraryCount(LLVMTextAPISliceRef Slice) {
  return static_cast<unsigned>(unwrap(Slice)->ReexportedLibraries.size());
}

const char *LLVMTextAPISliceGetReexportedLibrary(LLVMTextAPISliceRef Slice,
                                                 unsigned Index) {
  const std::vector<std::string> &V = unwrap(Slice)->ReexportedLibraries;
  return Index < V.size() ? V[Index].c_str() : nullptr;
}

unsigned LLVMTextAPISliceGetAllowableClientCount(LLVMTextAPISliceRef Slice) {
  return static_cast<unsigned>(unwrap(Slice)->AllowableClients.size());
}

const char *LLVMTextAPISliceGetAllowableClient(LLVMTextAPISliceRef Slice,
                                               unsigned Index) {
  const std::vector<std::string> &V = unwrap(Slice)->AllowableClients;
  return Index < V.size() ? V[Index].c_str() : nullptr;
}

LLVMBool LLVMTextAPISliceHasWeakDefinedExports(LLVMTextAPISliceRef Slice) {
  return unwrap(Slice)->HasWeakDefinedExports;
}

LLVMBool LLVMTextAPISliceIsNotForDyldSharedCache(LLVMTextAPISliceRef Slice) {
  return unwrap(Slice)->IsNotForDyldSharedCache;
}

unsigned LLVMTextAPISliceGetInlinedFrameworkCount(LLVMTextAPISliceRef Slice) {
  return static_cast<unsigned>(unwrap(Slice)->InlinedFrameworkNames.size());
}

const char *LLVMTextAPISliceGetInlinedFrameworkName(LLVMTextAPISliceRef Slice,
                                                    unsigned Index) {
  const std::vector<std::string> &V = unwrap(Slice)->InlinedFrameworkNames;
  return Index < V.size() ? V[Index].c_str() : nullptr;
}

LLVMTextAPISliceRef LLVMTextAPISliceGetInlinedFramework(
    LLVMTextAPISliceRef Slice, const char *InstallName, uint32_t CPUType,
    uint32_t CPUSubType, uint32_t Flags, uint32_t PackedMinOS,
    char **OutError) {
  if (OutError)
    *OutError = nullptr;

  const InterfaceFile *File = unwrap(Slice)->File;
  for (const std::shared_ptr<InterfaceFile> &Doc : File->documents())
    if (Doc->getInstallName() == InstallName)
      return wrap(buildSlice(Doc.get(), CPUType, CPUSubType, Flags, PackedMinOS,
                             OutError));

  if (OutError)
    *OutError = copyCString("no such inlined framework");
  return nullptr;
}
