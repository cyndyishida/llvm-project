#include "JSONStub.h"
#include "llvm/ADT/StringSwitch.h"

using namespace llvm;
using namespace llvm::json;
using namespace llvm::MachO;

class StubSerializer {};

class JSONStubError : public llvm::ErrorInfo<llvm::json::ParseError> {
public:
  JSONStubError(Twine errorMsg) : msg(errorMsg.str()) {}

  void log(llvm::raw_ostream &os) const override { os << msg << "\n"; }
  std::error_code convertToErrorCode() const override {
    return llvm::inconvertibleErrorCode();
  }

private:
  std::string msg;
};

template <typename JsonT, typename StubT = JsonT>
Expected<StubT> getRequiredValue(
    StringRef Key, const Object *Obj,
    std::function<Optional<JsonT>(const Object *, StringRef)> getValue,
    StringRef ErrorMsg,
    std::function<Optional<StubT>(JsonT)> validate = nullptr) {
  Optional<JsonT> Val = getValue(Obj, Key);
  if (!Val)
    return make_error<JSONStubError>(ErrorMsg);

  if (validate == nullptr)
    return static_cast<StubT>(*Val);

  Optional<StubT> Result = validate(*Val);
  if (!Result.has_value())
    return make_error<JSONStubError>(ErrorMsg);
  return Result.value();
}

template <typename JsonT, typename StubT = JsonT>
Expected<StubT> getOptionalValue(
    StringRef Key, const Object *Obj,
    std::function<Optional<JsonT>(const Object *, StringRef)> getValue,
    StubT DefaultValue, StringRef ErrorMsg) {
  Optional<JsonT> Val = getValue(Obj, Key);
  if (!Val)
    return DefaultValue;

  return static_cast<StubT>(*Val);
}

template <typename JsonT, typename StubT = JsonT>
Expected<StubT> getOptionalValue(
    StringRef Key, const Object *Obj,
    std::function<Optional<JsonT>(const Object *, StringRef)> getValue,
    StubT DefaultValue, StringRef ErrorMsg,
    std::function<Optional<StubT>(JsonT)> validate) {
  Optional<JsonT> Val = getValue(Obj, Key);
  if (!Val)
    return DefaultValue;

  Optional<StubT> Result;
  Result = validate(*Val);
  if (!Result.has_value())
    return make_error<JSONStubError>(ErrorMsg);
  return Result.value();
}

Error collectFromArray(StringRef Key, const Object *Obj, StringRef ErrorMsg,
                       std::function<void(StringRef)> append,
                       bool isRequired = false) {
  auto Values = Obj->getArray(Key);
  if (!Values) {
    if (isRequired)
      return make_error<JSONStubError>(ErrorMsg);
    return Error::success();
  }

  for (Value Val : *Values) {
    auto ValStr = Val.getAsString();
    if (!ValStr.has_value())
      return make_error<JSONStubError>(ErrorMsg);
    append(ValStr.value());
  }

  return Error::success();
}

namespace StubParser {

enum TBDFlags : unsigned {
  None = 0U,
  FlatNamespace = 1U << 0,
  NotApplicationExtensionSafe = 1U << 1,
  LLVM_MARK_AS_BITMASK_ENUM(/*LargestValue=*/NotApplicationExtensionSafe),
};

struct PODSymbol {
  SymbolKind Kind;
  std::string Name;
  SymbolFlags Flags;
  bool Data;
};

Expected<FileType> getVersion(const Object *File) {
  auto VersionOrErr = getRequiredValue<int64_t, FileType>(
      "tapi-tbd-version", File, &Object::getInteger, "invalid tbd version",
      [](int64_t Val) -> Optional<FileType> {
        unsigned Result = Val;
        if (Result != 5)
          return llvm::None;
        return FileType::TBD_V5;
      });

  if (!VersionOrErr)
    return VersionOrErr.takeError();
  return *VersionOrErr;
}

Expected<TargetList> getTargets(const Object *Section) {
  auto Targets = Section->getArray("targets");
  if (!Targets)
    return make_error<JSONStubError>("invalid value for target");

  TargetList IFTargets;
  for (Value JSONTarget : *Targets) {
    auto TargetStr = JSONTarget.getAsString();
    if (!TargetStr.has_value())
      return make_error<JSONStubError>("invalid value for target");
    auto TargetOrErr = Target::create(TargetStr.value());
    if (!TargetOrErr)
      return make_error<JSONStubError>("invalid value for target");
    IFTargets.push_back(*TargetOrErr);
  }
  return IFTargets;
}

using TargetsToSymbols =
    SmallVector<std::pair<TargetList, std::vector<PODSymbol>>>;
Error collectSymbolsFromSegment(const Object *Segment, TargetsToSymbols &Result,
                                bool isData, SymbolFlags SectionFlag) {
  auto Err = collectFromArray("symbols", Segment, "invalid globals section",
                              [&Result, isData, &SectionFlag](StringRef Name) {
                                PODSymbol Sym = {SymbolKind::GlobalSymbol,
                                                 Name.str(), SectionFlag,
                                                 /*Data*/ isData};
                                Result.back().second.emplace_back(Sym);
                              });
  if (Err)
    return Err;

  Err = collectFromArray(
      "objc-classes", Segment, "invalid objc-classes section",
      [&Result, isData, &SectionFlag](StringRef Name) {
        PODSymbol Sym = {SymbolKind::ObjectiveCClass, Name.str(), SectionFlag,
                         /*Data*/ isData};
        Result.back().second.emplace_back(Sym);
      });
  if (Err)
    return Err;

  Err = collectFromArray("objc-eh-types", Segment,
                         "invalid objc-eh-types section",
                         [&Result, isData, &SectionFlag](StringRef Name) {
                           PODSymbol Sym = {SymbolKind::ObjectiveCClassEHType,
                                            Name.str(), SectionFlag,
                                            /*Data*/ isData};
                           Result.back().second.emplace_back(Sym);
                         });
  if (Err)
    return Err;

  Err = collectFromArray("objc-ivars", Segment, "invalid objc-ivars section",
                         [&Result, isData, &SectionFlag](StringRef Name) {
                           PODSymbol Sym = {
                               SymbolKind::ObjectiveCInstanceVariable,
                               Name.str(), SectionFlag,
                               /*Data*/ isData};
                           Result.back().second.emplace_back(Sym);
                         });
  if (Err)
    return Err;

  SymbolFlags WeakFlag = SectionFlag | (SectionFlag == SymbolFlags::Undefined
                                            ? SymbolFlags::WeakReferenced
                                            : SymbolFlags::WeakDefined);
  Err = collectFromArray("weak", Segment, "invalid weak section",
                         [&Result, isData, WeakFlag](StringRef Name) {
                           PODSymbol Sym = {SymbolKind::GlobalSymbol,
                                            Name.str(), WeakFlag,
                                            /*Data*/ isData};
                           Result.back().second.emplace_back(Sym);
                         });
  if (Err)
    return Err;

  Err = collectFromArray(
      "thread-local", Segment, "invalid thread local section",
      [&Result, isData, SectionFlag](StringRef Name) {
        PODSymbol Sym = {SymbolKind::GlobalSymbol, Name.str(),
                         SymbolFlags::ThreadLocalValue | SectionFlag,
                         /*Data*/ isData};
        Result.back().second.emplace_back(Sym);
      });
  if (Err)
    return Err;

  return Error::success();
}

Expected<TargetsToSymbols> getSymbolSection(const Object *File, StringRef Key) {

  auto Section = File->getArray(Key);
  if (!Section)
    return TargetsToSymbols();

  SymbolFlags SectionFlag = StringSwitch<SymbolFlags>(Key)
                                .Case("reexports", SymbolFlags::Rexported)
                                .Case("undefineds", SymbolFlags::Undefined)
                                .Default(SymbolFlags::None);

  TargetsToSymbols Result;
  for (auto Val : *Section) {
    auto Obj = Val.getAsObject();
    if (!Obj)
      continue;

    auto TargetsOrErr = getTargets(Obj);
    if (!TargetsOrErr)
      return TargetsOrErr.takeError();
    auto Targets = *TargetsOrErr;
    Result.emplace_back(std::make_pair(Targets, std::vector<PODSymbol>()));

    auto DataSection = Obj->getObject("data");
    auto TextSection = Obj->getObject("text");
    // There should be at least one valid section.
    if (!DataSection && !TextSection)
      return make_error<JSONStubError>("invalid " + Key + " section");

    if (DataSection) {
      auto Err = collectSymbolsFromSegment(DataSection, Result, /*isData=*/true,
                                           SectionFlag);
      if (Err)
        return Err;
    }
    if (TextSection) {
      auto Err = collectSymbolsFromSegment(TextSection, Result,
                                           /*isData=*/false, SectionFlag);
      if (Err)
        return Err;
    }
  }

  return Result;
}

using LibsToTargets = std::map<std::string, TargetList>;
Expected<LibsToTargets> getLibSection(const Object *File, StringRef Key,
                                      StringRef SubKey, StringRef ErrorMsg) {
  auto Section = File->getArray(Key);
  if (!Section)
    return LibsToTargets();

  LibsToTargets Result;
  for (auto Val : *Section) {
    auto Obj = Val.getAsObject();
    if (!Obj)
      continue;

    auto TargetsOrErr = getTargets(Obj);
    if (!TargetsOrErr)
      return TargetsOrErr.takeError();
    auto Targets = *TargetsOrErr;

    auto Err = collectFromArray(
        SubKey, Obj, ErrorMsg,
        [&Result, &Targets](StringRef Key) { Result[Key.str()] = Targets; });
    if (Err)
      return Err;
  }

  return Result;
}

Expected<LibsToTargets> getUmbrella(const Object *File) {
  auto Clients = File->getArray("parent-umbrella");
  if (!Clients)
    return LibsToTargets();

  LibsToTargets Result;
  for (auto Val : *Clients) {
    auto Obj = Val.getAsObject();
    if (!Obj)
      continue;

    // Get Targets section.
    auto TargetsOrErr = getTargets(Obj);
    if (!TargetsOrErr)
      return TargetsOrErr.takeError();
    auto Targets = *TargetsOrErr;

    auto UmbrellaOrErr =
        getRequiredValue<StringRef>("umbrella", Obj, &Object::getString,
                                    "invalid value for parent umbrella");
    if (!UmbrellaOrErr)
      return UmbrellaOrErr.takeError();
    Result[UmbrellaOrErr->str()] = Targets;
  }
  return Result;
}

using IFPtr = std::unique_ptr<InterfaceFile>;
Expected<IFPtr> parseToIF(const Object *File) {
  if (!File)
    return make_error<JSONStubError>("invalid values for \"files\"");

  auto NameOrErr = getRequiredValue<StringRef>(
      "install-name", File, &Object::getString, "invalid install name");
  if (!NameOrErr)
    return NameOrErr.takeError();
  StringRef Name = *NameOrErr;

  auto validatePVValue =
      [](StringRef Version) -> llvm::Optional<PackedVersion> {
    PackedVersion PV;
    auto [success, truncated] = PV.parse64(Version);
    if (!success || truncated)
      return llvm::None;
    return PV;
  };

  auto CurrVersionOrErr = getOptionalValue<StringRef, PackedVersion>(
      "current-version", File, &Object::getString, PackedVersion(1, 0, 0),
      "invalid current version", validatePVValue);
  if (!CurrVersionOrErr)
    return CurrVersionOrErr.takeError();
  PackedVersion CurrVersion = *CurrVersionOrErr;

  auto CompVersionOrErr = getOptionalValue<StringRef, PackedVersion>(
      "compatibility-version", File, &Object::getString, PackedVersion(1, 0, 0),
      "invalid compatibility version", validatePVValue);
  if (!CompVersionOrErr)
    return CompVersionOrErr.takeError();
  PackedVersion CompVersion = *CompVersionOrErr;

  auto SwiftABIOrErr = getOptionalValue<int64_t, uint8_t>(
      "swift-abi-version", File, &Object::getInteger, 0,
      "invalid swift abi version");
  if (!SwiftABIOrErr)
    return SwiftABIOrErr.takeError();
  uint8_t SwiftABI = *SwiftABIOrErr;

  TBDFlags Flags = TBDFlags::None;
  auto FlagsErr = collectFromArray(
      "flags", File, "invalid flags", [&Flags](StringRef Flag) {
        TBDFlags TBDFlag = StringSwitch<TBDFlags>(Flag)
                               .Case("flat_namespace", TBDFlags::FlatNamespace)
                               .Case("not_app_extension_safe",
                                     TBDFlags::NotApplicationExtensionSafe)
                               .Default(TBDFlags::None);
        Flags |= TBDFlag;
      });
  if (FlagsErr)
    return FlagsErr;

  auto UmbrellasOrErr = getUmbrella(File);
  if (!UmbrellasOrErr)
    return UmbrellasOrErr.takeError();
  LibsToTargets Umbrellas = *UmbrellasOrErr;

  auto ClientsOrErr = getLibSection(File, "allowable-clients", "clients",
                                    "invalid allowable client");
  if (!ClientsOrErr)
    return ClientsOrErr.takeError();
  LibsToTargets Clients = *ClientsOrErr;

  auto RLOrErr = getLibSection(File, "reexported-libraries", "libraries",
                               "invalid reexported libraries");
  if (!RLOrErr)
    return RLOrErr.takeError();
  LibsToTargets ReexportLibs = std::move(*RLOrErr);

  auto ExportsOrErr = getSymbolSection(File, "exports");
  if (!ExportsOrErr)
    return ExportsOrErr.takeError();
  TargetsToSymbols Exports = std::move(*ExportsOrErr);

  auto ReexportsOrErr = getSymbolSection(File, "reexports");
  if (!ReexportsOrErr)
    return ReexportsOrErr.takeError();
  TargetsToSymbols Reexports = std::move(*ReexportsOrErr);

  auto UndefinedsOrErr = getSymbolSection(File, "undefineds");
  if (!UndefinedsOrErr)
    return UndefinedsOrErr.takeError();
  TargetsToSymbols Undefineds = std::move(*UndefinedsOrErr);

  IFPtr F(new InterfaceFile);
  F->setInstallName(Name);
  F->setCurrentVersion(CurrVersion);
  F->setCompatibilityVersion(CompVersion);
  F->setSwiftABIVersion(SwiftABI);
  F->setTwoLevelNamespace(!(Flags & TBDFlags::FlatNamespace));
  F->setApplicationExtensionSafe(
      !(Flags & TBDFlags::NotApplicationExtensionSafe));
  for (auto [Lib, Targets] : Clients)
    for (auto Target : Targets)
      F->addAllowableClient(Lib, Target);
  for (auto [Lib, Targets] : ReexportLibs)
    for (auto Target : Targets)
      F->addReexportedLibrary(Lib, Target);
  for (auto [Lib, Targets] : Umbrellas)
    for (auto Target : Targets)
      F->addParentUmbrella(Target, Lib);
  for (auto &[Targets, Symbols] : Exports)
    for (auto &Sym : Symbols)
      F->addSymbol(Sym.Kind, Sym.Name, Targets, Sym.Flags);
  for (auto &[Targets, Symbols] : Reexports)
    for (auto &Sym : Symbols)
      F->addSymbol(Sym.Kind, Sym.Name, Targets, Sym.Flags);
  for (auto &[Targets, Symbols] : Undefineds)
    for (auto &Sym : Symbols)
      F->addSymbol(Sym.Kind, Sym.Name, Targets, Sym.Flags);

  return F;
}

Expected<std::vector<IFPtr>> getFiles(const Object *File, FileType Type) {
  const Array *Libraries = File->getArray("files");
  if (!Libraries)
    return make_error<JSONStubError>("invalid values for \"files\"");

  std::vector<IFPtr> IFs;
  for (auto Lib : *Libraries) {
    auto IFOrErr = parseToIF(Lib.getAsObject());
    if (!IFOrErr)
      return IFOrErr.takeError();
    auto IF = std::move(*IFOrErr);
    IF->setFileType(Type);
    IFs.emplace_back(std::move(IF));
  }
  return IFs;
}

} // namespace StubParser

Expected<std::unique_ptr<InterfaceFile>>
MachO::parseToInterfaceFile(StringRef JSON) {
  auto ValOrErr = parse(JSON);
  if (!ValOrErr)
    return ValOrErr.takeError();

  auto *Root = ValOrErr->getAsObject();
  auto VersionOrErr = StubParser::getVersion(Root);
  if (!VersionOrErr)
    return VersionOrErr.takeError();
  FileType Version = *VersionOrErr;

  auto IFsOrErr = StubParser::getFiles(Root, Version);
  if (!IFsOrErr)
    return IFsOrErr.takeError();
  auto IFs = std::move(*IFsOrErr);
  assert(IFs.size() >= 1 && "expected at least one file");
  std::unique_ptr<InterfaceFile> IF(std::move(IFs.front()));

  for (auto &File : llvm::drop_begin(IFs))
    IF->addDocument(std::shared_ptr<InterfaceFile>(std::move(File)));

  return std::move(IF);
}
