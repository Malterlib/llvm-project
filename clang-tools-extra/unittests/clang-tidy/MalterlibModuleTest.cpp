//===----------------------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "malterlib/NamingUtils.h"
#include "clang/AST/ASTContext.h"
#include "clang/Basic/Builtins.h"
#include "clang/Basic/FileManager.h"
#include "clang/Basic/SourceManager.h"
#include "gtest/gtest.h"
#include <new>
#include <thread>

namespace clang::tidy::malterlib {
namespace {

struct CFacts {
  int Value = 0;
};

// What an ASTContext is made of, which the contexts the test makes in the same
// storage share.
struct CContextParts {
  FileSystemOptions FileSystem;
  FileManager Files{FileSystem};
  DiagnosticOptions Options;
  DiagnosticsEngine Diagnostics{DiagnosticIDs::create(), Options,
                                new IgnoringDiagConsumer()};
  SourceManager Sources{Diagnostics, Files};
  LangOptions Language;
  IdentifierTable Identifiers{Language, nullptr};
  SelectorTable Selectors;
  Builtin::Context Builtins;

  ASTContext *make(void *Storage) {
    return new (Storage) ASTContext(Language, Sources, Identifiers, Selectors,
                                    Builtins, TU_Complete);
  }
};

// The facts a thread keeps of an AST are of that AST, also when another
// thread destroys it, like a clangd worker that evicts the AST of another, and
// the next AST is at its address. The next context is made in the storage of
// the one that is gone, so the thread's next lookup is at the address of its
// last one.
TEST(MalterlibAstCacheTest, ForgetsAnAstAnotherThreadDestroys) {
  CContextParts Parts;
  alignas(ASTContext) unsigned char Storage[sizeof(ASTContext)];
  ASTContext *First = Parts.make(Storage);
  const uint64_t FirstIdentity = astIdentity(*First);
  EXPECT_EQ(astIdentity(*First), FirstIdentity);
  astCache<CFacts>(*First).Value = 1;
  EXPECT_EQ(astCache<CFacts>(*First).Value, 1);
  std::thread([First] { First->~ASTContext(); }).join();
  ASTContext *Next = Parts.make(Storage);
  ASSERT_EQ(static_cast<void *>(Next), static_cast<void *>(First));
  EXPECT_NE(astIdentity(*Next), FirstIdentity);
  EXPECT_EQ(astCache<CFacts>(*Next).Value, 0);
  Next->~ASTContext();
}

} // namespace
} // namespace clang::tidy::malterlib
