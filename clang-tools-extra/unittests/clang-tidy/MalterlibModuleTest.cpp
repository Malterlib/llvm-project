//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "malterlib/NamingUtils.h"
#include "clang/Frontend/ASTUnit.h"
#include "clang/Tooling/Tooling.h"
#include "gtest/gtest.h"
#include <thread>

namespace clang::tidy::malterlib {
namespace {

struct CFacts {
  int Value = 0;
};

// The facts a thread keeps of an AST are of that AST, also when another
// thread destroys it, like a clangd worker that evicts the AST of another, and
// the next AST is at its address.
TEST(MalterlibAstCacheTest, ForgetsAnAstAnotherThreadDestroys) {
  std::unique_ptr<ASTUnit> First = tooling::buildASTFromCode("int a;");
  const uint64_t FirstIdentity = astIdentity(First->getASTContext());
  EXPECT_EQ(astIdentity(First->getASTContext()), FirstIdentity);
  astCache<CFacts>(First->getASTContext()).Value = 1;
  std::thread([&First] { First.reset(); }).join();
  // Each AST is made until one is at the address of the one that is gone, or
  // a number of them were.
  std::vector<std::unique_ptr<ASTUnit>> Next;
  for (int I = 0; I < 16; ++I) {
    Next.push_back(tooling::buildASTFromCode("int b;"));
    ASTContext &Context = Next.back()->getASTContext();
    EXPECT_NE(astIdentity(Context), FirstIdentity);
    EXPECT_EQ(astCache<CFacts>(Context).Value, 0);
  }
}

} // namespace
} // namespace clang::tidy::malterlib
