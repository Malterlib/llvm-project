//===- unittest/AST/ASTContextParentMapTest.cpp - AST parent map test -----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Tests for the getParents(...) methods of ASTContext.
//
//===----------------------------------------------------------------------===//

#include "clang/AST/ASTContext.h"
#include "MatchVerifier.h"
#include "clang/ASTMatchers/ASTMatchFinder.h"
#include "clang/ASTMatchers/ASTMatchers.h"
#include "clang/Tooling/Tooling.h"
#include "gtest/gtest.h"
#include "gmock/gmock.h"

using testing::ElementsAre;

namespace clang {
namespace ast_matchers {

TEST(GetParents, ReturnsParentForDecl) {
  MatchVerifier<Decl> Verifier;
  EXPECT_TRUE(
      Verifier.match("class C { void f(); };",
                     cxxMethodDecl(hasParent(recordDecl(hasName("C"))))));
}

TEST(GetParents, ReturnsParentForStmt) {
  MatchVerifier<Stmt> Verifier;
  EXPECT_TRUE(Verifier.match("class C { void f() { if (true) {} } };",
                             ifStmt(hasParent(compoundStmt()))));
}

TEST(GetParents, ReturnsParentForTypeLoc) {
  MatchVerifier<TypeLoc> Verifier;
  EXPECT_TRUE(
      Verifier.match("namespace a { class b {}; } void f(a::b) {}",
                     typeLoc(hasParent(typeLoc(hasParent(functionDecl()))))));
}

TEST(GetParents, ReturnsParentForNestedNameSpecifierLoc) {
  MatchVerifier<NestedNameSpecifierLoc> Verifier;
  EXPECT_TRUE(Verifier.match("namespace a { class b {}; } void f(a::b) {}",
                             nestedNameSpecifierLoc(hasParent(typeLoc()))));
}

TEST(GetParents, ReturnsParentInsideTemplateInstantiations) {
  MatchVerifier<Decl> DeclVerifier;
  EXPECT_TRUE(DeclVerifier.match(
      "template<typename T> struct C { void f() {} };"
      "void g() { C<int> c; c.f(); }",
      cxxMethodDecl(hasName("f"),
                 hasParent(cxxRecordDecl(isTemplateInstantiation())))));
  EXPECT_TRUE(DeclVerifier.match(
      "template<typename T> struct C { void f() {} };"
      "void g() { C<int> c; c.f(); }",
      cxxMethodDecl(hasName("f"),
                 hasParent(cxxRecordDecl(unless(isTemplateInstantiation()))))));
  EXPECT_FALSE(DeclVerifier.match(
      "template<typename T> struct C { void f() {} };"
      "void g() { C<int> c; c.f(); }",
      cxxMethodDecl(
          hasName("f"),
          allOf(hasParent(cxxRecordDecl(unless(isTemplateInstantiation()))),
                hasParent(cxxRecordDecl(isTemplateInstantiation()))))));
}

TEST(GetParents, ReturnsMultipleParentsInTemplateInstantiations) {
  MatchVerifier<Stmt> TemplateVerifier;
  EXPECT_TRUE(TemplateVerifier.match(
      "template<typename T> struct C { void f() {} };"
      "void g() { C<int> c; c.f(); }",
      compoundStmt(allOf(
          hasAncestor(cxxRecordDecl(isTemplateInstantiation())),
          hasAncestor(cxxRecordDecl(unless(isTemplateInstantiation())))))));
}

TEST(GetParents, RespectsTraversalScope) {
  auto AST = tooling::buildASTFromCode(
      "struct foo { int bar; }; struct baz{};", "foo.cpp",
      std::make_shared<PCHContainerOperations>());
  auto &Ctx = AST->getASTContext();
  auto &TU = *Ctx.getTranslationUnitDecl();
  auto &Foo = *TU.lookup(&Ctx.Idents.get("foo")).front();
  auto &Bar = *cast<DeclContext>(Foo).lookup(&Ctx.Idents.get("bar")).front();
  auto &Baz = *TU.lookup(&Ctx.Idents.get("baz")).front();

  // Initially, scope is the whole TU.
  EXPECT_THAT(Ctx.getParents(Bar), ElementsAre(DynTypedNode::create(Foo)));
  EXPECT_THAT(Ctx.getParents(Foo), ElementsAre(DynTypedNode::create(TU)));
  EXPECT_THAT(Ctx.getParents(Baz), ElementsAre(DynTypedNode::create(TU)));

  // Restrict the scope, now some parents are gone.
  Ctx.setTraversalScope({&Foo});
  EXPECT_THAT(Ctx.getParents(Bar), ElementsAre(DynTypedNode::create(Foo)));
  EXPECT_THAT(Ctx.getParents(Foo), ElementsAre(DynTypedNode::create(TU)));
  EXPECT_THAT(Ctx.getParents(Baz), ElementsAre());

  // Reset the scope, we get back the original results.
  Ctx.setTraversalScope({&TU});
  EXPECT_THAT(Ctx.getParents(Bar), ElementsAre(DynTypedNode::create(Foo)));
  EXPECT_THAT(Ctx.getParents(Foo), ElementsAre(DynTypedNode::create(TU)));
  EXPECT_THAT(Ctx.getParents(Baz), ElementsAre(DynTypedNode::create(TU)));
}

TEST(GetParents, ImplicitLambdaNodes) {
  MatchVerifier<Decl> LambdaVerifier;
  EXPECT_TRUE(LambdaVerifier.match(
      "auto x = []{int y;};",
      varDecl(hasName("y"), hasAncestor(functionDecl(
                                hasOverloadedOperatorName("()"),
                                hasParent(cxxRecordDecl(
                                    isImplicit(), hasParent(lambdaExpr())))))),
      Lang_CXX11));
}

TEST(GetParents, FriendTypeLoc) {
  auto AST = tooling::buildASTFromCode("struct A { friend struct Fr; };"
                                       "struct B { friend struct Fr; };"
                                       "struct Fr;");
  auto &Ctx = AST->getASTContext();
  auto &TU = *Ctx.getTranslationUnitDecl();
  auto &A = *TU.lookup(&Ctx.Idents.get("A")).front();
  auto &B = *TU.lookup(&Ctx.Idents.get("B")).front();
  auto &FrA = *cast<FriendDecl>(*++(cast<CXXRecordDecl>(A).decls_begin()));
  auto &FrB = *cast<FriendDecl>(*++(cast<CXXRecordDecl>(B).decls_begin()));
  TypeLoc FrALoc = FrA.getFriendType()->getTypeLoc();
  TypeLoc FrBLoc = FrB.getFriendType()->getTypeLoc();
  bool FrAOwnsTag = FrALoc.getTypePtr()->getAs<TagType>()->isTagOwned();
  TagDecl *FrATagDecl = FrALoc.getTypePtr()->getAs<TagType>()->getDecl();
  bool FrBOwnsTag = FrBLoc.getTypePtr()->getAs<TagType>()->isTagOwned();

  EXPECT_THAT(Ctx.getParents(A), ElementsAre(DynTypedNode::create(TU)));
  EXPECT_THAT(Ctx.getParents(B), ElementsAre(DynTypedNode::create(TU)));
  EXPECT_THAT(Ctx.getParents(FrA), ElementsAre(DynTypedNode::create(A)));
  EXPECT_THAT(Ctx.getParents(FrB), ElementsAre(DynTypedNode::create(B)));
  EXPECT_THAT(Ctx.getParents(FrALoc), ElementsAre(DynTypedNode::create(FrA)));
  EXPECT_THAT(Ctx.getParents(FrBLoc), ElementsAre(DynTypedNode::create(FrB)));
  EXPECT_TRUE(FrAOwnsTag);
  EXPECT_FALSE(FrBOwnsTag);
  EXPECT_THAT(Ctx.getParents(*FrATagDecl),
              ElementsAre(DynTypedNode::create(FrA)));
}

TEST(GetParents, UserDefinedTupleLikeTypes) {
  MatchVerifier<VarDecl> Verifier;
  EXPECT_TRUE(Verifier.match(
      R"(
namespace std {

using size_t = __typeof(sizeof(int));

template <typename T>
struct tuple_size;

template <typename T>
struct tuple_size<T&> : tuple_size<T>{};

template <typename T>
requires requires { tuple_size<T>::value; }
struct tuple_size<const T> : tuple_size<T>{};


template<size_t i, typename T>
struct tuple_element;


}  // namespace std

struct Decomposable {};

template<> struct std::tuple_size<Decomposable> {
  static constexpr size_t value = 2;
};

template<std::size_t i> struct std::tuple_element<i, Decomposable> {
  using type = int;
};

template<std::size_t i> struct std::tuple_element<i, const Decomposable> {
  using type = const int;
};

template<std::size_t i>
const int& get(const Decomposable& d);

void F(const Decomposable& d) {
    const auto& [x, y] = d;
}
)",
      varDecl(hasName("x"), hasAncestor(decompositionDecl())), Lang_CXX20));
}

// The operand of a co_return is also below the call of return_value: it has
// both parents, also when coroutine lambdas that return nest so deep that
// traversing it again for each would never end.
TEST(GetParents, CoreturnOperand) {
  std::string Code = R"cpp(
namespace std {
template <class Ret, class... Args> struct coroutine_traits {
  using promise_type = typename Ret::promise_type;
};
template <class Promise = void> struct coroutine_handle {
  static coroutine_handle from_address(void *) noexcept;
};
} // namespace std
struct Awaitable {
  bool await_ready() noexcept;
  template <class Handle> void await_suspend(Handle) noexcept;
  int await_resume() noexcept;
};
struct Task {
  struct promise_type {
    Task get_return_object();
    Awaitable initial_suspend();
    Awaitable final_suspend() noexcept;
    void return_value(int);
    void unhandled_exception();
  };
};
int h(Task);
int g();
)cpp";
  std::string Returns = "g()";
  for (int Depth = 0; Depth < 40; ++Depth)
    Returns = "h([]() -> Task { co_return " + Returns + "; }())";
  Code += "int nested() { return " + Returns + "; }";
  auto AST = tooling::buildASTFromCodeWithArgs(Code, {"-std=c++20"});
  auto &Ctx = AST->getASTContext();
  const auto *Inner = selectFirst<CallExpr>(
      "g", match(callExpr(callee(functionDecl(hasName("g")))).bind("g"), Ctx));
  ASSERT_TRUE(Inner);
  const auto *Return = selectFirst<CoreturnStmt>(
      "return",
      match(coreturnStmt(has(equalsNode(Inner))).bind("return"), Ctx));
  ASSERT_TRUE(Return);
  EXPECT_EQ(Ctx.getParents(*Inner).size(), 2u);
  EXPECT_THAT(Ctx.getParents(*Return->getPromiseCall()),
              ElementsAre(DynTypedNode::create(*Return)));
}

// The operand of a co_await is also below the expressions that make and call
// its awaiter: it has both parents, and what is below it its own, also when
// awaits nest so deep that traversing it again for each would never end.
TEST(GetParents, CoawaitOperand) {
  std::string Code = R"cpp(
namespace std {
template <class Ret, class... Args> struct coroutine_traits {
  using promise_type = typename Ret::promise_type;
};
template <class Promise = void> struct coroutine_handle {
  static coroutine_handle from_address(void *) noexcept;
};
} // namespace std
struct Awaitable {
  bool await_ready() noexcept;
  template <class Handle> void await_suspend(Handle) noexcept;
  int await_resume() noexcept;
};
struct Task {
  struct promise_type {
    Task get_return_object();
    Awaitable initial_suspend();
    Awaitable final_suspend() noexcept;
    void return_void();
    void unhandled_exception();
  };
};
Awaitable f(int);
int g();
)cpp";
  std::string Awaits = "g()";
  for (int Depth = 0; Depth < 40; ++Depth)
    Awaits = "co_await f(" + Awaits + ")";
  Code += "Task nested() { " + Awaits + "; }";
  auto AST = tooling::buildASTFromCodeWithArgs(Code, {"-std=c++20"});
  auto &Ctx = AST->getASTContext();
  const auto *Inner = selectFirst<CallExpr>(
      "g", match(callExpr(callee(functionDecl(hasName("g")))).bind("g"), Ctx));
  ASSERT_TRUE(Inner);
  EXPECT_THAT(Ctx.getParents(*Inner),
              ElementsAre(DynTypedNode::create(
                  *selectFirst<CallExpr>("f", match(callExpr(hasArgument(
                                                         0, equalsNode(Inner)))
                                                         .bind("f"),
                                                     Ctx)))));
  const auto *Operand = selectFirst<CallExpr>(
      "f", match(callExpr(hasArgument(0, equalsNode(Inner))).bind("f"), Ctx));
  ASSERT_TRUE(Operand);
  const auto *Await = selectFirst<CoawaitExpr>(
      "await", match(coawaitExpr(has(equalsNode(Operand))).bind("await"), Ctx));
  ASSERT_TRUE(Await);
  EXPECT_EQ(Ctx.getParents(*Operand).size(), 2u);
  EXPECT_TRUE(llvm::is_contained(Ctx.getParents(*Operand),
                                 DynTypedNode::create(*Await)));
}

// The initializers of nested initializer lists, which their syntactic and
// semantic forms share, are traversed once, also however deep they nest, and
// get the parents in both forms.
TEST(GetParents, NestedInitListInitializers) {
  std::string Code = "struct S { int x; };\n";
  std::string Initialized = "7";
  for (int Depth = 0; Depth < 40; ++Depth)
    Initialized = "S{.x = (" + Initialized + ")}.x";
  Code += "int nested() { return " + Initialized + "; }";
  auto AST = tooling::buildASTFromCodeWithArgs(Code, {"-std=c++20"});
  auto &Ctx = AST->getASTContext();
  const auto *Literal = selectFirst<IntegerLiteral>(
      "literal", match(integerLiteral(equals(7)).bind("literal"), Ctx));
  ASSERT_TRUE(Literal);
  // The parenthesized initializer, which the designator has as it is and the
  // semantic form converts.
  const DynTypedNodeList Parents = Ctx.getParents(*Literal);
  ASSERT_EQ(Parents.size(), 1u);
  const auto *Paren = Parents[0].get<ParenExpr>();
  ASSERT_TRUE(Paren);
  EXPECT_EQ(Ctx.getParents(*Paren).size(), 2u);
}

// A parameter of a lambda's call operator, which the types of the lambda's
// conversion to a pointer to a function and of its static invoker have too,
// is traversed once, also however deep lambdas nest in default arguments.
TEST(GetParents, NestedLambdaDefaultArguments) {
  std::string Nested = "7";
  for (int Depth = 0; Depth < 40; ++Depth)
    Nested = "[](int Value = " + Nested + ") { return Value; }(0)";
  std::string Code = "int nested() { return " + Nested + "; }";
  auto AST = tooling::buildASTFromCodeWithArgs(Code, {"-std=c++20"});
  auto &Ctx = AST->getASTContext();
  const auto *Literal = selectFirst<IntegerLiteral>(
      "literal", match(integerLiteral(equals(7)).bind("literal"), Ctx));
  ASSERT_TRUE(Literal);
  const DynTypedNodeList Parents = Ctx.getParents(*Literal);
  ASSERT_EQ(Parents.size(), 1u);
  const auto *Parameter = Parents[0].get<ParmVarDecl>();
  ASSERT_TRUE(Parameter);
  EXPECT_FALSE(Ctx.getParents(*Parameter).empty());
}

// So are the expressions in the return type and the exception specification
// of a lambda's call operator, which those types have too.
TEST(GetParents, NestedLambdaReturnTypesAndExceptionSpecifications) {
  for (bool Noexcept : {false, true}) {
    std::string Nested = "7";
    for (int Depth = 0; Depth < 40; ++Depth)
      Nested = Noexcept
                   ? "[]() noexcept(noexcept(" + Nested + ")) { return 0; }()"
                   : "[]() -> decltype(" + Nested + ") { return 0; }()";
    std::string Code = "int nested() { return " + Nested + "; }";
    auto AST = tooling::buildASTFromCodeWithArgs(Code, {"-std=c++20"});
    auto &Ctx = AST->getASTContext();
    const auto *Literal = selectFirst<IntegerLiteral>(
        "literal", match(integerLiteral(equals(7)).bind("literal"), Ctx));
    ASSERT_TRUE(Literal);
    EXPECT_FALSE(Ctx.getParents(*Literal).empty());
  }
}

} // end namespace ast_matchers
} // end namespace clang
