#include "llvm/ADT/StringRef.h"
#include "llvm/Support/JSON.h"
#include "llvm/TextAPI/InterfaceFile.h"

#ifndef LLVM_TEXTAPI_JSON_STUB_H
#define LLVM_TEXTAPI_JSON_STUB_H

namespace llvm {

namespace MachO {

Expected<std::unique_ptr<InterfaceFile>> parseToInterfaceFile(StringRef JSON);

}
} // namespace llvm
#endif
