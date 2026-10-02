//===-- TidyProviderTests.cpp - Clang tidy configuration provider tests ---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Config.h"
#include "Feature.h"
#include "TestFS.h"
#include "TidyProvider.h"
#include "support/Context.h"
#include "llvm/Testing/Support/SupportHelpers.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace clang {
namespace clangd {

namespace {

TEST(TidyProvider, NestedDirectories) {
  MockFS FS;
  FS.Files[testPath(".clang-tidy")] = R"yaml(
  Checks: 'llvm-*'
  CheckOptions:
    TestKey: 1
)yaml";
  FS.Files[testPath("sub1/.clang-tidy")] = R"yaml(
  Checks: 'misc-*'
  CheckOptions:
    TestKey: 2
)yaml";
  FS.Files[testPath("sub1/sub2/.clang-tidy")] = R"yaml(
  Checks: 'bugprone-*'
  CheckOptions:
    TestKey: 3
  InheritParentConfig: true
)yaml";

  TidyProvider Provider = provideClangTidyFiles(FS);

  auto BaseOptions = getTidyOptionsForFile(Provider, testPath("File.cpp"));
  ASSERT_TRUE(BaseOptions.Checks.has_value());
  EXPECT_EQ(*BaseOptions.Checks, "llvm-*");
  EXPECT_EQ(BaseOptions.CheckOptions.lookup("TestKey").Value, "1");

  auto Sub1Options = getTidyOptionsForFile(Provider, testPath("sub1/File.cpp"));
  ASSERT_TRUE(Sub1Options.Checks.has_value());
  EXPECT_EQ(*Sub1Options.Checks, "misc-*");
  EXPECT_EQ(Sub1Options.CheckOptions.lookup("TestKey").Value, "2");

  auto Sub2Options =
      getTidyOptionsForFile(Provider, testPath("sub1/sub2/File.cpp"));
  ASSERT_TRUE(Sub2Options.Checks.has_value());
  EXPECT_EQ(*Sub2Options.Checks, "misc-*,bugprone-*");
  EXPECT_EQ(Sub2Options.CheckOptions.lookup("TestKey").Value, "3");
}

// The options of the clangd configuration take the place of those of the
// .clang-tidy files, which they keep, with those of earlier fragments, for the
// options whose values edit those before.
TEST(TidyProvider, ClangdConfigKeepsReplacedOptions) {
  MockFS FS;
  FS.Files[testPath(".clang-tidy")] = R"yaml(
  CheckOptions:
    LowerCaseFreeFunctions: clear
)yaml";
  Config Cfg;
  Cfg.Diagnostics.ClangTidy.CheckOptions["LowerCaseFreeFunctions"] = "erase";
  Cfg.Diagnostics.ClangTidy.CheckOptionFragments["LowerCaseFreeFunctions"] = 2;
  Cfg.Diagnostics.ClangTidy.ReplacedCheckOptions["LowerCaseFreeFunctions"] = {
      {"remove", 1}};
  WithContextValue WithConfig(Config::Key, std::move(Cfg));
  std::vector<TidyProvider> Providers;
  Providers.push_back(provideClangTidyFiles(FS));
  Providers.push_back(provideClangdConfig());
  TidyProvider Provider = combine(std::move(Providers));

  auto Options = getTidyOptionsForFile(Provider, testPath("File.cpp"));
  const auto &Value = Options.CheckOptions.lookup("LowerCaseFreeFunctions");
  EXPECT_EQ(Value.Value, "erase");
  std::vector<std::string> Replaced;
  for (const auto &[Text, Priority] : Value.Replaced)
    Replaced.push_back(Text);
  EXPECT_THAT(Replaced, ::testing::ElementsAre("clear", "remove"));
}

// An option of a later fragment comes after one of an earlier fragment, also a
// plain one after one with the name of a check.
TEST(TidyProvider, ClangdConfigOrdersFragments) {
  MockFS FS;
  Config Cfg;
  auto &Tidy = Cfg.Diagnostics.ClangTidy;
  Tidy.CheckOptions["example-check.CheckPointers"] = "true";
  Tidy.CheckOptionFragments["example-check.CheckPointers"] = 1;
  Tidy.CheckOptions["CheckPointers"] = "false";
  Tidy.CheckOptionFragments["CheckPointers"] = 2;
  WithContextValue WithConfig(Config::Key, std::move(Cfg));
  std::vector<TidyProvider> Providers;
  Providers.push_back(provideClangTidyFiles(FS));
  Providers.push_back(provideClangdConfig());
  TidyProvider Provider = combine(std::move(Providers));

  auto Options = getTidyOptionsForFile(Provider, testPath("File.cpp"));
  EXPECT_LT(Options.CheckOptions.lookup("example-check.CheckPointers").Priority,
            Options.CheckOptions.lookup("CheckPointers").Priority);
}

TEST(TidyProvider, IsFastTidyCheck) {
  EXPECT_THAT(isFastTidyCheck("misc-const-correctness"), llvm::ValueIs(false));
  EXPECT_THAT(isFastTidyCheck("bugprone-suspicious-include"),
              llvm::ValueIs(true));
  // Linked in (ParsedASTTests.cpp) but not measured.
  EXPECT_EQ(isFastTidyCheck("replay-preamble-check"), std::nullopt);
}

#if CLANGD_TIDY_CHECKS
TEST(TidyProvider, IsValidCheck) {
  EXPECT_TRUE(isRegisteredTidyCheck("bugprone-argument-comment"));
  EXPECT_FALSE(isRegisteredTidyCheck("bugprone-argument-clinic"));
}
#endif

} // namespace
} // namespace clangd
} // namespace clang
