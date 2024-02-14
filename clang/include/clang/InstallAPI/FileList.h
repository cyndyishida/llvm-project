//===- InstallAPI/FileList.h ------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
/// The JSON file list parser is used to communicate input to InstallAPI.
///
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_INSTALLAPI_FILELIST_H
#define LLVM_CLANG_INSTALLAPI_FILELIST_H

#include "clang/Basic/Diagnostic.h"
#include "clang/Basic/FileManager.h"
#include "clang/InstallAPI/HeaderFile.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/MemoryBuffer.h"

namespace clang {
namespace installapi {

/// Abstract Interface for reading FileList JSON Input.
class FileListReader {
  class Implementation;

  Implementation &Impl;

  FileListReader(std::unique_ptr<llvm::MemoryBuffer> InputBuffer,
                 llvm::Error &Err);

public:
  static llvm::Expected<std::unique_ptr<FileListReader>>
  get(std::unique_ptr<llvm::MemoryBuffer> InputBuffer);

  ~FileListReader();

  FileListReader(const FileListReader &) = delete;
  FileListReader &operator=(const FileListReader &) = delete;

  int getVersion() const;

  struct HeaderInfo {
    HeaderType Type;
    std::string Path;
    std::optional<clang::Language> Language;
  };

  /// Visitor used when walking the contents of the file list.
  class Visitor {
  public:
    virtual ~Visitor();

    virtual void visitHeaderFile(HeaderInfo &header) = 0;
  };

  /// Visit the contents of the header list file, passing each entity to the
  /// given visitor. It visits in the same order as they appear in the json
  /// file.
  void visit(Visitor &visitor);
};

class FileListVisitor final : public FileListReader::Visitor {
  FileManager &FM;
  DiagnosticsEngine &Diag;
  HeaderSeq &HeaderFiles;

public:
  FileListVisitor(FileManager &FM, DiagnosticsEngine &Diag,
                  HeaderSeq &HeaderFiles)
      : FM(FM), Diag(Diag), HeaderFiles(HeaderFiles) {}

  void visitHeaderFile(FileListReader::HeaderInfo &Header) override;
};
} // namespace installapi
} // namespace clang
#endif // LLVM_CLANG_INSTALLAPI_FILELIST_H
