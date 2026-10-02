//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
///
///  \file Implements classes required to build clang-tidy modules.
///
//===----------------------------------------------------------------------===//

#include "ClangTidyModule.h"
#include "ClangTidyCheck.h"

namespace clang::tidy {

void ClangTidyCheckFactories::registerCheckFactory(StringRef Name,
                                                   CheckFactory Factory) {
  Factories.insert_or_assign(Name, std::move(Factory));
}

std::vector<std::unique_ptr<ClangTidyCheck>>
ClangTidyCheckFactories::createChecks(ClangTidyContext *Context) const {
  std::vector<std::unique_ptr<ClangTidyCheck>> Checks;
  for (const auto &[CheckName, Factory] : Factories)
    if (Context->isCheckEnabled(CheckName))
      Checks.emplace_back(Factory(CheckName, Context));
  return Checks;
}

std::vector<std::unique_ptr<ClangTidyCheck>>
ClangTidyCheckFactories::createChecksForLanguage(
    ClangTidyContext *Context) const {
  std::vector<std::unique_ptr<ClangTidyCheck>> Checks;
  // Those of another language version too until all are made, so checks that
  // share what they report of their configuration while any of them exists
  // report it once.
  std::vector<std::unique_ptr<ClangTidyCheck>> Unsupported;
  const LangOptions &LO = Context->getLangOpts();
  for (const auto &[CheckName, Factory] : Factories) {
    if (!Context->isCheckEnabled(CheckName))
      continue;
    std::unique_ptr<ClangTidyCheck> Check = Factory(CheckName, Context);
    if (Check->isLanguageVersionSupported(LO))
      Checks.push_back(std::move(Check));
    else
      Unsupported.push_back(std::move(Check));
  }
  return Checks;
}

ClangTidyOptions ClangTidyModule::getModuleOptions() { return {}; }

} // namespace clang::tidy
