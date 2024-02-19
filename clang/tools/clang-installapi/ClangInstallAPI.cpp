#include "llvm/ADT/ArrayRef.h"
#include "llvm/Support/ManagedStatic.h"
#include "clang/Driver/DriverDiagnostic.h"
#include "llvm/Support/PrettyStackTrace.h"
#include "llvm/Support/Process.h"
#include "llvm/Support/Signals.h"
#include "clang/Driver/Compilation.h"
#include "llvm/Support/CommandLine.h"
#include "clang/Frontend/FrontendActions.h"
#include "clang/Basic/DiagnosticIDs.h"
#include "llvm/Option/Option.h"
#include "llvm/Support/LLVMDriver.h"
#include "llvm/TargetParser/Host.h"
#include "clang/Frontend/FrontendDiagnostic.h"
#include "llvm/Support/VirtualFileSystem.h"
#include "clang/Driver/Driver.h"
#include "clang/Frontend/CompilerInvocation.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Driver/Tool.h"
#include "clang/Frontend/TextDiagnosticBuffer.h"
#include "clang/Frontend/TextDiagnosticPrinter.h"
#include "clang/InstallAPI/Context.h"
#include "llvm/TextAPI/TextAPIWriter.h"
#include "Options.h"

using namespace clang;
using namespace clang::installapi;
using namespace llvm::opt;
using namespace llvm::MachO;
using namespace clang::driver::options;

static const ArgStringList *
getCC1Arguments(clang::DiagnosticsEngine& Diags,
                clang::driver::Compilation *Compilation) {
  const auto &Jobs = Compilation->getJobs();
  if (Jobs.size() != 1 || !isa<clang::driver::Command>(*Jobs.begin())) {
    SmallString<256> error_msg;
    llvm::raw_svector_ostream error_stream(error_msg);
    Jobs.Print(error_stream, "; ", true);
    Diags.Report(clang::diag::err_fe_expected_compiler_job)
        << error_stream.str();
    return nullptr;
  }

  // The one job we find should be to invoke clang again.
  const auto &Cmd  = cast<clang::driver::Command>(*Jobs.begin());
  if (StringRef(Cmd.getCreator().getName()) != "clang") {
    Diags.Report(clang::diag::err_fe_expected_clang_command);
    return nullptr;
  }

  return &Cmd.getArguments();
}

static CompilerInvocation *createInvocation(clang::DiagnosticsEngine &Diags,
                                  const ArgStringList &cc1Args) {
  assert(!cc1Args.empty() && "Must at least contain the program name!");
  CompilerInvocation *Invocation = new CompilerInvocation;
  CompilerInvocation::CreateFromArgs(*Invocation, cc1Args, Diags);
  Invocation->getFrontendOpts().DisableFree = false;
  Invocation->getCodeGenOpts().DisableFree = false;
  return Invocation;
}

static bool run(ArrayRef<const char *> CommandArgs) {
  // InstallAPI only needs to parse AST, so always force on certain options.
  std::vector<const char *> Args;
  Args.reserve(CommandArgs.size() + 1);
  llvm::copy(CommandArgs, std::back_inserter(Args));
  Args.push_back("-fsyntax-only");

  // Setup Diagnostics engine.
  IntrusiveRefCntPtr<DiagnosticOptions> DiagOpts = new DiagnosticOptions();
  const llvm::opt::OptTable &ClangOpts= clang::driver::getDriverOptTable();
  unsigned MissingArgIndex, MissingArgCount;
  llvm::opt::InputArgList ParsedArgs = ClangOpts.ParseArgs(
      ArrayRef(Args).slice(1), MissingArgIndex, MissingArgCount);
  ParseDiagnosticArgs(*DiagOpts, ParsedArgs);
  
  IntrusiveRefCntPtr<DiagnosticsEngine> Diag = new clang::DiagnosticsEngine(
      new clang::DiagnosticIDs(), DiagOpts.get(),
      new clang::TextDiagnosticPrinter(llvm::errs(), DiagOpts.get()));
  
  // Create file manager for all file operations.
  IntrusiveRefCntPtr<clang::FileManager> FM(new FileManager(clang::FileSystemOptions()));

  // Pickup installed directory. 
  void *MainAddr = (void*) (intptr_t)run;
  std::string ExecutablePath = llvm::sys::path::stem(Args.front()).str();
  if (!FM->exists(ExecutablePath)) {
    if (llvm::ErrorOr<std::string> P =
            llvm::sys::findProgramByName(ExecutablePath)) 
      ExecutablePath = *P;
    else 
      ExecutablePath = llvm::sys::fs::getMainExecutable(Args.front(), MainAddr);
  }

  // Set up driver to parse input arguments.
  auto DriverArgs = llvm::ArrayRef(Args).slice(1);
  clang::driver::Driver Driver(ExecutablePath, llvm::sys::getDefaultTargetTriple(), *Diag, "clang installapi tool");
  Driver.setInstalledDir(llvm::sys::path::parent_path(ExecutablePath));
  bool HasError = false;
  llvm::opt::InputArgList ArgList =
      Driver.ParseArgStrings(DriverArgs, /*UseDriverMode=*/true, HasError);
  // TODO(diagnose) 
  if (HasError) 
    return EXIT_FAILURE; 
  Driver.setCheckInputsExist(false);
  std::unique_ptr<CompilerInstance> CI(new CompilerInstance());

  // Capture InstallAPI specific options and diagnose any option errors.
  Options Opts(*Diag, FM.get(), ArgList);
  if (Diag->hasErrorOccurred())
    return EXIT_FAILURE;

  InstallAPIContext Ctx;
  Ctx.OutputLoc = Opts.DriverOptions.OutputPath;

  // Create compilation and build jobs. 
  // TODO: do this per target triple x proj/priv/proj
  const std::unique_ptr<clang::driver::Compilation> Compilation(
      Driver.BuildCompilation(llvm::ArrayRef(DriverArgs)));
  if (!Compilation)
    return EXIT_FAILURE;
  const llvm::opt::ArgStringList *const cc1Args =
      getCC1Arguments(*Diag, Compilation.get());
  if (!cc1Args)
    return EXIT_FAILURE; 

  std::unique_ptr<clang::CompilerInvocation> Invocation(
      createInvocation(*Diag, *cc1Args));

  CI->setInvocation(std::move(Invocation));
  CI->setFileManager(FM.get());
  //auto Action = std::make_unique<InstallAPIAction>(context);
  CI->createDiagnostics();
  if (!CI->hasDiagnostics())
    return false;
  CI->createSourceManager(*FM);

  llvm::errs() << "clang Invocation:\n";
  Compilation->getJobs().Print(llvm::errs(), "\n", true);
  llvm::errs() << "\n";
  auto Out = CI->createOutputFile(Ctx.OutputLoc, /*Binary=*/false,
                                 /*RemoveFileOnSignal=*/false, 
                                 /*UseTemporary=*/false, /*CreateMissingDirectories=*/false);

  if (!Out) 
    return EXIT_FAILURE;

  InterfaceFile IF;
  IF.addTarget(Target(AK_x86_64, PLATFORM_MACOS, VersionTuple(10, 14)));
  IF.setInstallName("tmp");

  // Write output file and perform CI cleanup.
  if (auto Err = TextAPIWriter::writeToStream(*Out, IF, Ctx.FT)) {
    CI->getDiagnostics().Report(diag::err_cannot_open_file) << Ctx.OutputLoc;
    CI->clearOutputFiles(/*EraseFiles=*/true);
    return EXIT_FAILURE;
  } 

  CI->clearOutputFiles(/*EraseFiles=*/false);
  return EXIT_SUCCESS;
}

int main(int argc, const char **argv) {
  // Standard set up, so program fails gracefully.
  llvm::sys::PrintStackTraceOnErrorSignal(argv[0]);
  llvm::PrettyStackTraceProgram StackPrinter(argc, argv);
  llvm::llvm_shutdown_obj Shutdown;

  if (llvm::sys::Process::FixupStandardFileDescriptors())
    return 1;

  return run(llvm::ArrayRef(argv, argc));
}
