//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "../ClangTidy.h"
#include "../ClangTidyDiagnosticConsumer.h"
#include "../ClangTidyModule.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Frontend/FrontendPluginRegistry.h"
#include "clang/Frontend/MultiplexConsumer.h"
#include "clang/Lex/PPCallbacks.h"
#include "clang/Lex/Preprocessor.h"

namespace clang::tidy {
namespace {

/// Ends the source file of the diagnostic consumer of the checks, which
/// forwards the last diagnostic it holds back. Ends it once, both when the main
/// file ends and when the AST consumer is destroyed, which clang skips with
/// -disable-free.
class SourceFileEnd {
public:
  explicit SourceFileEnd(DiagnosticConsumer &Consumer) : Consumer(Consumer) {}

  void end() {
    if (Ended)
      return;
    Ended = true;
    Consumer.EndSourceFile();
  }

private:
  DiagnosticConsumer &Consumer;
  bool Ended = false;
};

/// Ends the source file of the checks when the main file ends. Registered
/// before the callbacks of the checks, it runs after them, so it also forwards
/// what they report at the end of the main file.
class EndOfMainFileCallback : public PPCallbacks {
public:
  explicit EndOfMainFileCallback(std::shared_ptr<SourceFileEnd> End)
      : End(std::move(End)) {}

  void EndOfMainFile() override { End->end(); }

private:
  std::shared_ptr<SourceFileEnd> End;
};

/// The core clang tidy plugin action. This just provides the AST consumer and
/// command line flag parsing for using clang-tidy as a clang plugin.
class ClangTidyPluginAction : public PluginASTAction {
  /// Wrapper to grant the context and diagnostics engine the same lifetime as
  /// the action.
  /// We use MultiplexConsumer to avoid writing out all the forwarding methods.
  class WrapConsumer : public MultiplexConsumer {
    std::unique_ptr<ClangTidyContext> Context;
    std::unique_ptr<DiagnosticsEngine> DiagEngine;
    std::shared_ptr<SourceFileEnd> End;

  public:
    WrapConsumer(std::unique_ptr<ClangTidyContext> Context,
                 std::unique_ptr<DiagnosticsEngine> DiagEngine,
                 std::shared_ptr<SourceFileEnd> End,
                 std::vector<std::unique_ptr<ASTConsumer>> Consumer)
        : MultiplexConsumer(std::move(Consumer)), Context(std::move(Context)),
          DiagEngine(std::move(DiagEngine)), End(std::move(End)) {}

    ~WrapConsumer() override { End->end(); }
  };

public:
  std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance &Compiler,
                                                 StringRef File) override {
    // Create and set diagnostics engine
    auto *DiagConsumer =
        new ClangTidyDiagnosticConsumer(*Context, &Compiler.getDiagnostics());
    auto DiagOpts = std::make_unique<DiagnosticOptions>();
    auto DiagEngine = std::make_unique<DiagnosticsEngine>(
        DiagnosticIDs::create(), *DiagOpts, DiagConsumer);
    Context->setDiagnosticsEngine(std::move(DiagOpts), DiagEngine.get());
    // Clang only tells its own diagnostic client about the source file.
    DiagConsumer->BeginSourceFile(Compiler.getLangOpts(),
                                  &Compiler.getPreprocessor());
    auto End = std::make_shared<SourceFileEnd>(*DiagConsumer);
    Compiler.getPreprocessor().addPPCallbacks(
        std::make_unique<EndOfMainFileCallback>(End));

    // Create the AST consumer.
    ClangTidyASTConsumerFactory Factory(*Context);
    std::vector<std::unique_ptr<ASTConsumer>> Vec;
    Vec.push_back(Factory.createASTConsumer(Compiler, File));

    return std::make_unique<WrapConsumer>(std::move(Context),
                                          std::move(DiagEngine), std::move(End),
                                          std::move(Vec));
  }

  bool ParseArgs(const CompilerInstance &,
                 const std::vector<std::string> &Args) override {
    const ClangTidyGlobalOptions GlobalOptions;
    const ClangTidyOptions DefaultOptions;
    ClangTidyOptions OverrideOptions;

    // Parse the extra command line args.
    // FIXME: This is very limited at the moment.
    for (const StringRef Arg : Args)
      if (Arg.starts_with("-checks="))
        OverrideOptions.Checks = std::string(Arg.substr(strlen("-checks=")));

    auto Options = std::make_unique<FileOptionsProvider>(
        GlobalOptions, DefaultOptions, OverrideOptions);
    Context = std::make_unique<ClangTidyContext>(std::move(Options));
    return true;
  }

private:
  std::unique_ptr<ClangTidyContext> Context;
};

} // namespace
} // namespace clang::tidy

// This anchor is used to force the linker to link in the generated object file
// and thus register the clang-tidy plugin.
// NOLINTNEXTLINE(misc-use-internal-linkage)
volatile int ClangTidyPluginAnchorSource = 0;

static clang::FrontendPluginRegistry::Add<clang::tidy::ClangTidyPluginAction>
    X("clang-tidy", "clang-tidy");
