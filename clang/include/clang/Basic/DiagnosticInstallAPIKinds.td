//==--- DiagnosticInstallAPIKinds.td - installapi diagnostics -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

let Component = "InstallAPI" in {

def err_iapi_no_such_header_file : Error<"no such %select{public|private|project}1 header file: '%0'">;
} // let Component = "InstallAPI" in
