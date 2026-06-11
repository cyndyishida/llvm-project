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
 * @}
 */

LLVM_C_EXTERN_C_END

#endif /* LLVM_C_TEXTAPI_H */
