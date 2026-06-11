/*===-- llvm-c/TextAPI.h - TextAPI (.tbd) C Interface -------------*- C -*-===*\
|*                                                                            *|
|* Part of the LLVM Project, under the Apache License v2.0 with LLVM          *|
|* Exceptions.                                                                *|
|* See https://llvm.org/LICENSE.txt for license information.                  *|
|* SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception                    *|
|*                                                                            *|
|*===----------------------------------------------------------------------===*|
|*                                                                            *|
|* This header declares a stable C interface to llvm::MachO's text-based      *|
|* dynamic library stub (.tbd) reader.                                        *|
|*                                                                            *|
|* The API is intended to be both backward and forward compatible. New        *|
|* functions are added with a bump to LLVM_TEXTAPI_VERSION_MINOR; source- or  *|
|* ABI-breaking changes (which are not expected) would bump                   *|
|* LLVM_TEXTAPI_VERSION_MAJOR. A client linking or dlopen'ing libLLVM can      *|
|* discover the API generation it actually loaded at runtime via              *|
|* LLVMTextAPIGetAPIVersion() and gate feature use accordingly:               *|
|*                                                                            *|
|*   if (LLVMTextAPIGetAPIVersion() >= LLVM_TEXTAPI_VERSION_ENCODE(1, 1))      *|
|*     ... use a function added in 1.1 ...                                    *|
|*                                                                            *|
\*===----------------------------------------------------------------------===*/

#ifndef LLVM_C_TEXTAPI_H
#define LLVM_C_TEXTAPI_H

#include "llvm-c/ExternC.h"
#include "llvm-c/Types.h"
#include "llvm-c/Visibility.h"

LLVM_C_EXTERN_C_BEGIN

/**
 * @defgroup LLVMCTextAPI TextAPI
 * @ingroup LLVMC
 *
 * A stable C interface to the text-based API (.tbd) reader in llvm::MachO.
 *
 * @{
 */

/**
 * The major version of the TextAPI C API.
 *
 * This is bumped only for source- or ABI-breaking changes, which are not
 * expected. It is intended to remain stable.
 */
#define LLVM_TEXTAPI_VERSION_MAJOR 1

/**
 * The minor version of the TextAPI C API.
 *
 * This is bumped on every backward-compatible API addition.
 */
#define LLVM_TEXTAPI_VERSION_MINOR 0

/**
 * Encode a (major, minor) pair into a single comparable version number.
 */
#define LLVM_TEXTAPI_VERSION_ENCODE(major, minor) (((major) * 10000) + (minor))

/**
 * The TextAPI C API version this header describes.
 */
#define LLVM_TEXTAPI_VERSION                                                   \
  LLVM_TEXTAPI_VERSION_ENCODE(LLVM_TEXTAPI_VERSION_MAJOR,                       \
                              LLVM_TEXTAPI_VERSION_MINOR)

/**
 * Returns the TextAPI C API version of the loaded library.
 *
 * Because the implementation lives in libLLVM, a client built against one
 * version of this header may load a differently-versioned library; this
 * function reports the version the library was actually built with, encoded
 * with LLVM_TEXTAPI_VERSION_ENCODE.
 */
LLVM_C_ABI unsigned LLVMTextAPIGetAPIVersion(void);

/**
 * A context owns a cache of parsed .tbd files.
 *
 * Files parsed through a context (see LLVMTextAPIParse) are owned by it and
 * remain valid until the context is disposed. Re-parsing the same file through
 * the same context returns the previously parsed handle instead of parsing
 * again. This mirrors the lifetime model of clang's CXIndex.
 *
 * A context is not thread-safe for concurrent disposal, but concurrent
 * LLVMTextAPIParse calls on the same context are safe.
 */
typedef struct LLVMTextAPIOpaqueContext *LLVMTextAPIContextRef;

/**
 * A parsed .tbd file (all target slices). Owned by the context that parsed it;
 * do not dispose it directly.
 */
typedef struct LLVMTextAPIOpaqueFile *LLVMTextAPIRef;

/**
 * Create an empty context (parse cache). Pair with LLVMTextAPIContextDispose.
 */
LLVM_C_ABI LLVMTextAPIContextRef LLVMTextAPIContextCreate(void);

/**
 * Dispose a context, freeing every file parsed through it. All
 * LLVMTextAPIRef handles obtained from this context become invalid.
 */
LLVM_C_ABI void LLVMTextAPIContextDispose(LLVMTextAPIContextRef Ctx);

/**
 * Parse the .tbd file at \p Path, or return a cached result.
 *
 * The cache is keyed on the file's real (symlink-resolved) path together with
 * its modification time and size, so an unchanged file is parsed only once
 * (subsequent calls return the same handle), while a file edited in place is
 * re-parsed.
 *
 * On success returns a non-NULL handle owned by \p Ctx. On failure returns
 * NULL and, if \p OutError is non-NULL, stores a malloc'd diagnostic string in
 * *OutError that the caller must release with LLVMDisposeMessage. *OutError is
 * set to NULL on success.
 */
LLVM_C_ABI LLVMTextAPIRef LLVMTextAPIParse(LLVMTextAPIContextRef Ctx,
                                           const char *Path, char **OutError);

/**
 * The number of distinct architectures the file supports, across all of its
 * target slices (e.g. {x86_64, arm64, arm64e} counts as 3).
 */
LLVM_C_ABI unsigned LLVMTextAPIGetArchitectureCount(LLVMTextAPIRef File);

/**
 * Copy the name of the architecture at \p Index (e.g. "arm64e"), where Index is
 * in [0, LLVMTextAPIGetArchitectureCount). Returns a malloc'd string the caller
 * releases with LLVMDisposeMessage, or NULL if \p Index is out of range.
 */
LLVM_C_ABI char *LLVMTextAPICopyArchitectureName(LLVMTextAPIRef File,
                                                 unsigned Index);

/**
 * The number of target slices in the file. A target is an
 * (architecture, platform) pair, so this is the finer-grained companion to
 * LLVMTextAPIGetArchitectureCount (e.g. arm64-macos and arm64-ios are two
 * targets but one architecture).
 */
LLVM_C_ABI unsigned LLVMTextAPIGetTargetCount(LLVMTextAPIRef File);

/**
 * Copy the LLVM target-triple string for the slice at \p Index (e.g.
 * "arm64-apple-ios17.0"), where Index is in [0, LLVMTextAPIGetTargetCount).
 * Returns a malloc'd string the caller releases with LLVMDisposeMessage, or
 * NULL if \p Index is out of range.
 */
LLVM_C_ABI char *LLVMTextAPICopyTargetTriple(LLVMTextAPIRef File,
                                             unsigned Index);

/**
 * Copy the install name of the file (e.g. "/usr/lib/libSystem.B.dylib").
 * Returns a malloc'd string the caller releases with LLVMDisposeMessage.
 */
LLVM_C_ABI char *LLVMTextAPICopyInstallName(LLVMTextAPIRef File);

/**
 * The current version, as a Mach-O packed 32-bit version
 * (major << 16 | minor << 8 | patch).
 */
LLVM_C_ABI uint32_t LLVMTextAPIGetCurrentVersion(LLVMTextAPIRef File);

/**
 * The compatibility version, as a Mach-O packed 32-bit version
 * (major << 16 | minor << 8 | patch).
 */
LLVM_C_ABI uint32_t LLVMTextAPIGetCompatibilityVersion(LLVMTextAPIRef File);

/**
 * A single architecture slice of a file, as the linker consumes it. Created
 * with LLVMTextAPIGetSlice and released with LLVMTextAPISliceDispose. The
 * underlying file (and thus the slice's symbols) must outlive the slice.
 */
typedef struct LLVMTextAPIOpaqueSlice *LLVMTextAPISliceRef;

/**
 * A symbol within a slice. Borrowed from the slice; valid while the slice
 * lives. Do not dispose it directly.
 */
typedef struct LLVMTextAPIOpaqueSymbol *LLVMTextAPISymbolRef;

/**
 * Flags controlling slice selection (mirrors tapi::ParsingFlags).
 */
typedef enum {
  /** Default: if the exact CPU subtype is absent, fall back to an
   *  ABI-compatible slice of the same CPU type. */
  LLVMTextAPIParsingFlagsNone = 0,
  /** Require an exact CPU-subtype match; do not fall back. */
  LLVMTextAPIParsingFlagsExactCPUSubType = 1u << 0,
} LLVMTextAPIParsingFlags;

/**
 * Select the slice of \p File matching the Mach-O (\p CPUType, \p CPUSubType)
 * pair (the subtype distinguishes e.g. arm64 from arm64e).
 *
 * If the exact subtype is not present, the closest ABI-compatible slice of the
 * same CPU type is chosen, unless LLVMTextAPIParsingFlagsExactCPUSubType is set
 * in \p Flags. This matches tapi::LinkerInterfaceFile's arch selection.
 *
 * On success returns a non-NULL slice the caller releases with
 * LLVMTextAPISliceDispose. If no compatible architecture is found, returns NULL
 * and, if \p OutError is non-NULL, stores a malloc'd diagnostic in *OutError
 * that the caller releases with LLVMDisposeMessage.
 */
LLVM_C_ABI LLVMTextAPISliceRef LLVMTextAPIGetSlice(LLVMTextAPIRef File,
                                                   uint32_t CPUType,
                                                   uint32_t CPUSubType,
                                                   uint32_t Flags,
                                                   char **OutError);

/**
 * Release a slice obtained from LLVMTextAPIGetSlice.
 */
LLVM_C_ABI void LLVMTextAPISliceDispose(LLVMTextAPISliceRef Slice);

/**
 * The number of exported (defined) symbols in the slice.
 */
LLVM_C_ABI unsigned
LLVMTextAPISliceGetExportedSymbolCount(LLVMTextAPISliceRef Slice);

/**
 * The exported symbol at \p Index, in [0, LLVMTextAPISliceGetExportedSymbolCount).
 * Returns a handle borrowed from the slice, or NULL if \p Index is out of range.
 */
LLVM_C_ABI LLVMTextAPISymbolRef
LLVMTextAPISliceGetExportedSymbol(LLVMTextAPISliceRef Slice, unsigned Index);

/**
 * Copy a symbol's name. Returns a malloc'd string the caller releases with
 * LLVMDisposeMessage.
 */
LLVM_C_ABI char *LLVMTextAPISymbolCopyName(LLVMTextAPISymbolRef Symbol);

/**
 * Whether the symbol is a weak definition.
 */
LLVM_C_ABI LLVMBool LLVMTextAPISymbolIsWeakDefined(LLVMTextAPISymbolRef Symbol);

/**
 * The slice's parent/umbrella framework name, or NULL if it has none.
 * Borrowed; valid while the slice lives.
 */
LLVM_C_ABI const char *
LLVMTextAPISliceGetParentFrameworkName(LLVMTextAPISliceRef Slice);

/**
 * The number of (platform, min-deployment) entries the slice targets.
 */
LLVM_C_ABI unsigned LLVMTextAPISliceGetPlatformCount(LLVMTextAPISliceRef Slice);

/**
 * Read the entry at \p Index in [0, LLVMTextAPISliceGetPlatformCount): the
 * Mach-O platform (PlatformType) into *OutPlatform and the packed
 * min-deployment version into *OutPackedMinOS. Either out-param may be NULL.
 * If \p Index is out of range, nothing is written.
 */
LLVM_C_ABI void LLVMTextAPISliceGetPlatform(LLVMTextAPISliceRef Slice,
                                            unsigned Index,
                                            uint32_t *OutPlatform,
                                            uint32_t *OutPackedMinOS);

/**
 * The runtime search paths (LC_RPATH) for the slice (count + borrowed indexed
 * accessor; the accessor returns NULL if \p Index is out of range).
 */
LLVM_C_ABI unsigned LLVMTextAPISliceGetRPathCount(LLVMTextAPISliceRef Slice);
LLVM_C_ABI const char *LLVMTextAPISliceGetRPath(LLVMTextAPISliceRef Slice,
                                                unsigned Index);

/**
 * The install names of libraries reexported by the slice.
 */
LLVM_C_ABI unsigned
LLVMTextAPISliceGetReexportedLibraryCount(LLVMTextAPISliceRef Slice);
LLVM_C_ABI const char *
LLVMTextAPISliceGetReexportedLibrary(LLVMTextAPISliceRef Slice, unsigned Index);

/**
 * The allowable client names of the slice.
 */
LLVM_C_ABI unsigned
LLVMTextAPISliceGetAllowableClientCount(LLVMTextAPISliceRef Slice);
LLVM_C_ABI const char *
LLVMTextAPISliceGetAllowableClient(LLVMTextAPISliceRef Slice, unsigned Index);

/**
 * Whether the slice has any weak-defined exported symbol.
 */
LLVM_C_ABI LLVMBool
LLVMTextAPISliceHasWeakDefinedExports(LLVMTextAPISliceRef Slice);

/**
 * Whether the library is an OS library that is not eligible for the dyld
 * shared cache.
 */
LLVM_C_ABI LLVMBool
LLVMTextAPISliceIsNotForDyldSharedCache(LLVMTextAPISliceRef Slice);

/**
 * The install names of the frameworks inlined into the file (count + borrowed
 * indexed accessor). This list is not architecture-filtered; the accessor
 * returns NULL if \p Index is out of range.
 */
LLVM_C_ABI unsigned
LLVMTextAPISliceGetInlinedFrameworkCount(LLVMTextAPISliceRef Slice);
LLVM_C_ABI const char *
LLVMTextAPISliceGetInlinedFrameworkName(LLVMTextAPISliceRef Slice,
                                        unsigned Index);

/**
 * Resolve the inlined framework whose install name is \p InstallName into its
 * own slice for the given architecture (selected as in LLVMTextAPIGetSlice).
 *
 * Returns a new slice the caller releases with LLVMTextAPISliceDispose, or NULL
 * and, if \p OutError is non-NULL, a malloc'd diagnostic in *OutError (released
 * with LLVMDisposeMessage) when there is no such framework or no compatible
 * architecture.
 */
LLVM_C_ABI LLVMTextAPISliceRef LLVMTextAPISliceGetInlinedFramework(
    LLVMTextAPISliceRef Slice, const char *InstallName, uint32_t CPUType,
    uint32_t CPUSubType, uint32_t Flags, char **OutError);

/**
 * @}
 */

LLVM_C_EXTERN_C_END

#endif /* LLVM_C_TEXTAPI_H */
