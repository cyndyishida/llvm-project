// Differences in -fmessage-length should not change the strict context hash.

// RUN: rm -rf %t
// RUN: split-file %s %t

// RUN: %clang_cc1 -fsyntax-only -fmodules -fimplicit-module-maps \
// RUN:   -fmodules-strict-context-hash -fmodules-cache-path=%t/cache -I %t \
// RUN:   -fmessage-length=1000 %t/tu.c -Rmodule-build 2>&1 \
// RUN:   | FileCheck %s --check-prefix=BUILD
// RUN: %clang_cc1 -fsyntax-only -fmodules -fimplicit-module-maps \
// RUN:   -fmodules-strict-context-hash -fmodules-cache-path=%t/cache -I %t \
// RUN:   -fmessage-length=2000 %t/tu.c -Rmodule-build 2>&1 \
// RUN:   | FileCheck %s --check-prefix=REUSE --allow-empty

// BUILD: building module 'Mod'
// REUSE-NOT: building module 'Mod'

//--- module.modulemap
module Mod { header "Mod.h" }

//--- Mod.h

//--- tu.c
#include "Mod.h"
