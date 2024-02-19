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
  // Handle inputs.
  for (const auto &Path : Args.getAllArgValues(OPT_INPUT)) {
    if (!FM->exists(Path)) {
      Diags->Report(clang::diag::err_drv_no_such_file) << Path;
      return false;
    }
    DriverOptions.FileLists.push_back(Path);
  }

  // Handle output.
  SmallString<PATH_MAX> OutputPath;
  if (auto *Arg = Args.getLastArg(OPT_o)) {
    OutputPath = Arg->getValue();
    if (OutputPath != "-")
      FM->makeAbsolutePath(OutputPath);
    DriverOptions.OutputPath = std::string(OutputPath);
  }

  // Do basic error checking first for mixing -target and -arch options.
  auto *ArgArch = Args.getLastArgNoClaim(OPT_arch);
  auto *ArgTarget = Args.getLastArgNoClaim(OPT_target);
  auto *ArgTargetVariant =
      Args.getLastArgNoClaim(OPT_darwin_target_variant_triple);
  if (ArgArch && (ArgTarget || ArgTargetVariant)) {
    Diags->Report(clang::diag::err_drv_argument_not_allowed_with)
        << ArgArch->getAsString(Args)
        << (ArgTarget ? ArgTarget : ArgTargetVariant)->getAsString(Args);
    return false;
  }

  auto *ArgMinTargetOS = Args.getLastArgNoClaim(OPT_mtargetos_EQ);
  if ((ArgTarget || ArgTargetVariant) && ArgMinTargetOS) {
    Diags->Report(clang::diag::err_drv_cannot_mix_options)
        << ArgTarget->getAsString(Args) << ArgMinTargetOS->getAsString(Args);
    return false;
  }

  // Capture target triples first.
  if (ArgTarget) {
    for (auto *Arg : Args.filtered(OPT_target)) {
      llvm::Expected<Target> TAPITarget = Target::create(Arg->getValue());
      llvm::Triple TargetTriple(Arg->getValue());
      if (auto Err = TAPITarget.takeError()) {
        Diags->Report(clang::diag::err_drv_installapi_unsupported)
            << TargetTriple.str();
        return false;
      }
      DriverOptions.Targets[*TAPITarget] = TargetTriple;
    }
  }

  return true;
}
bool Options::processLinkerOptions(InputArgList &Args) {
  // TODO: add error handling.

  // Required arguments.
  if (const Arg *A = Args.getLastArg(options::OPT_install__name))
    LinkerOptions.InstallName = A->getValue();

  // Defaulted or optional arguments.
  if (auto *Arg = Args.getLastArg(OPT__current_version))
    auto ParseResult = LinkerOptions.CurrentVersion.parse64(Arg->getValue());

  LinkerOptions.IsApplicationExtensionSafe =
      Args.hasFlag(OPT_fapplication_extension, OPT_fno_application_extension,
                   /*Default=*/LinkerOptions.IsApplicationExtensionSafe);

  if (::getenv("LD_NO_ENCRYPT") != nullptr)
    LinkerOptions.IsApplicationExtensionSafe = true;

  if (::getenv("LD_APPLICATION_EXTENSION_SAFE") != nullptr)
    LinkerOptions.IsApplicationExtensionSafe = true;
  return true;
}

Options::Options(DiagnosticsEngine &Diag, FileManager *FM,
                 InputArgList &ArgList)
    : Diags(&Diag), FM(FM) {
  if (!processDriverOptions(ArgList)) 
    return;

  if (!processLinkerOptions(ArgList))
    return;
}
}
}
