#include "Options.h"
#include "clang/Frontend/FrontendDiagnostic.h"
#include "clang/Driver/Driver.h"
#include "llvm/Support/Program.h"
#include "llvm/TargetParser/Host.h"

using namespace clang::driver;
using namespace clang::driver::options;
using namespace llvm::opt;

using namespace llvm::MachO;
namespace clang {
namespace installapi {

bool Options::processDriverOptions(InputArgList& Args) {
  if (Args.hasArg(OPT_help))
    DriverOptions.PrintHelp = true;

  SmallString<PATH_MAX> OutputPath;
  if (auto *Arg = Args.getLastArg(OPT_o)) {
    OutputPath = Arg->getValue();
    if (OutputPath != "-")
      FM->makeAbsolutePath(OutputPath);
    DriverOptions.OutputPath = std::string(OutputPath);
  }
 
  // Capture InstallAPI supported targets. 
  const Arg *ArgTarget = Args.getLastArgNoClaim(OPT_target);
  if (ArgTarget) {
    for (auto *Arg : Args.filtered(OPT_target)) {
      llvm::Triple Target(Arg->getValue());
      DriverOptions.Targets.push_back(Target);
    }
  }
  return true;
}

Options::Options(DiagnosticsEngine &Diag, FileManager* FM, InputArgList& ArgList) : Diags(&Diag), FM(FM) {

  if (!processDriverOptions(ArgList)) 
    return;
}

}
}
