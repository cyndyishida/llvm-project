//===- FileList.cpp ---------------------------------------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "clang/InstallAPI/FileList.h"
#include "clang/Basic/DiagnosticInstallAPI.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/Support/Error.h"
#include "llvm/Support/JSON.h"
#include "llvm/TextAPI/TextAPIError.h"
#include <optional>

// clang-format off
/*
 
 InstallAPI JSON Input Format specification.

--- 
{
  "headers" : [                              # Required: Key must exist.
    {                                        # Optional: May contain 0 or more header inputs.
      "path" : "/usr/include/mach-o/dlfn.h", # Required: Path should point to destination location where applicable.
      "type" : "public"                      # Required: Maps to HeaderType for header
    }
  ],
  "version" : "3"                            # Required: Version 3 supports language mode & project header input.
}
*/
// clang-format on

using namespace llvm;
using namespace llvm::json;
using namespace llvm::MachO;
using namespace clang::installapi;

class FileListReader::Implementation {
private:
  Expected<StringRef> parseString(const Object *Obj, StringRef Key,
                                  StringRef Error);
  Expected<StringRef> parsePath(const Object *Obj);
  Expected<HeaderType> parseType(const Object *Obj);
  std::optional<clang::Language> parseLanguage(const Object *Obj);
  Error parseHeaders(Array &Headers);

public:
  std::unique_ptr<MemoryBuffer> InputBuffer;
  unsigned Version;
  std::vector<HeaderInfo> HeaderList;

  Error parse(StringRef Input);
};

Expected<StringRef>
FileListReader::Implementation::parseString(const Object *Obj, StringRef Key,
                                            StringRef Error) {
  auto Str = Obj->getString(Key);
  if (!Str)
    return make_error<StringError>(Error, inconvertibleErrorCode());
  return *Str;
}

Expected<HeaderType>
FileListReader::Implementation::parseType(const Object *Obj) {
  auto TypeStr =
      parseString(Obj, "type", "required field 'type' not specified");
  if (!TypeStr)
    return TypeStr.takeError();

  if (*TypeStr == "public")
    return HeaderType::Public;
  else if (*TypeStr == "private")
    return HeaderType::Private;
  else if (*TypeStr == "project" && Version >= 2)
    return HeaderType::Project;

  return make_error<TextAPIError>(TextAPIErrorCode::InvalidInputFormat,
                                  "unsupported header type");
}

Expected<StringRef>
FileListReader::Implementation::parsePath(const Object *Obj) {
  auto Path = parseString(Obj, "path", "required field 'path' not specified");
  if (!Path)
    return Path.takeError();

  return *Path;
}

std::optional<clang::Language>
FileListReader::Implementation::parseLanguage(const Object *Obj) {
  auto Language = Obj->getString("language");
  if (!Language)
    return std::nullopt;

  return StringSwitch<clang::Language>(*Language)
      .Case("c", clang::Language::C)
      .Case("c++", clang::Language::CXX)
      .Case("objective-c", clang::Language::ObjC)
      .Case("objective-c++", clang::Language::ObjCXX)
      .Default(clang::Language::Unknown);
}

Error FileListReader::Implementation::parseHeaders(Array &Headers) {
  for (const auto &H : Headers) {
    auto *Obj = H.getAsObject();
    if (!Obj)
      return make_error<StringError>("expect a JSON object",
                                     inconvertibleErrorCode());
    auto Type = parseType(Obj);
    if (!Type)
      return Type.takeError();
    auto Path = parsePath(Obj);
    if (!Path)
      return Path.takeError();
    auto Language = parseLanguage(Obj);

    HeaderList.emplace_back(HeaderInfo{*Type, std::string(*Path), Language});
  }

  return Error::success();
}

Error FileListReader::Implementation::parse(StringRef Input) {
  auto Val = json::parse(Input);
  if (!Val)
    return Val.takeError();

  auto *Root = Val->getAsObject();
  if (!Root)
    return make_error<StringError>("not a JSON object",
                                   inconvertibleErrorCode());

  auto VersionStr = Root->getString("version");
  if (!VersionStr)
    return make_error<TextAPIError>(TextAPIErrorCode::InvalidInputFormat,
                                    "required field 'version' not specified");
  if (VersionStr->getAsInteger(10, Version))
    return make_error<TextAPIError>(TextAPIErrorCode::InvalidInputFormat,
                                    "invalid version number");

  if (Version < 1 || Version > 3)
    return make_error<TextAPIError>(TextAPIErrorCode::InvalidInputFormat,
                                    "unsupported version");

  // Not specifying any header files should be atypical, but valid.
  auto Headers = Root->getArray("headers");
  if (!Headers)
    return Error::success();

  Error Err = parseHeaders(*Headers);
  if (Err)
    return Err;

  return Error::success();
}

FileListReader::FileListReader(std::unique_ptr<MemoryBuffer> InputBuffer,
                               Error &Error)
    : Impl(*new FileListReader::Implementation()) {
  ErrorAsOutParameter ErrorAsOutParam(&Error);
  Impl.InputBuffer = std::move(InputBuffer);

  Error = Impl.parse(Impl.InputBuffer->getBuffer());
}

Expected<std::unique_ptr<FileListReader>>
FileListReader::get(std::unique_ptr<MemoryBuffer> InputBuffer) {
  Error Error = Error::success();
  std::unique_ptr<FileListReader> Reader(
      new FileListReader(std::move(InputBuffer), Error));
  if (Error)
    return std::move(Error);

  return Reader;
}

FileListReader::~FileListReader() { delete &Impl; }

int FileListReader::getVersion() const { return Impl.Version; }

void FileListReader::visit(Visitor &Visitor) {
  for (auto &File : Impl.HeaderList)
    Visitor.visitHeaderFile(File);
}

FileListReader::Visitor::~Visitor() {}

void FileListReader::Visitor::visitHeaderFile(HeaderInfo &Header) {}

void FileListVisitor::visitHeaderFile(FileListReader::HeaderInfo &Header) {
  llvm::vfs::Status Result;
  if (FM.getNoncachedStatValue(Header.Path, Result) || !Result.exists()) {
    Diag.Report(diag::err_iapi_no_such_header_file)
        << Header.Path << (unsigned)Header.Type;
    return;
  }

  // Track full paths for project headers, as they are looked up via
  // quote includes.
  if (Header.Type == HeaderType::Project) {
    HeaderFiles.emplace_back(Header.Path, Header.Type,
                             /*IncludeName*/ "", Header.Language);
    return;
  }

  auto IncludeName = createIncludeHeaderName(Header.Path);
  HeaderFiles.emplace_back(Header.Path, Header.Type,
                           IncludeName.has_value() ? IncludeName.value() : "",
                           Header.Language);
}
