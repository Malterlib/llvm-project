// RUN: %clang_cc1 -std=c++20 -fsyntax-only -verify %s
// expected-no-diagnostics

// The declarations the default member initializers of a constructor use are
// marked as referenced where the constructor is, each constructor's once
// while those of one are marked: also of one that a default member
// initializer of its own class uses, without end, and of a chain of classes
// whose member is initialized with two of the one before, in time linear in
// the length of the chain, not exponential.

extern bool b;

struct SSelf {
  SSelf() {}
  int n = b ? SSelf{}.n : 0;
};
void self() { SSelf Value; }

int leaf();
struct SChain0 {
  int x = leaf();
  SChain0() {}
};
struct SChain1 {
  SChain0 m = b ? SChain0{} : SChain0{};
  SChain1() {}
};
struct SChain2 {
  SChain1 m = b ? SChain1{} : SChain1{};
  SChain2() {}
};
struct SChain3 {
  SChain2 m = b ? SChain2{} : SChain2{};
  SChain3() {}
};
struct SChain4 {
  SChain3 m = b ? SChain3{} : SChain3{};
  SChain4() {}
};
struct SChain5 {
  SChain4 m = b ? SChain4{} : SChain4{};
  SChain5() {}
};
struct SChain6 {
  SChain5 m = b ? SChain5{} : SChain5{};
  SChain6() {}
};
struct SChain7 {
  SChain6 m = b ? SChain6{} : SChain6{};
  SChain7() {}
};
struct SChain8 {
  SChain7 m = b ? SChain7{} : SChain7{};
  SChain8() {}
};
struct SChain9 {
  SChain8 m = b ? SChain8{} : SChain8{};
  SChain9() {}
};
struct SChain10 {
  SChain9 m = b ? SChain9{} : SChain9{};
  SChain10() {}
};
struct SChain11 {
  SChain10 m = b ? SChain10{} : SChain10{};
  SChain11() {}
};
struct SChain12 {
  SChain11 m = b ? SChain11{} : SChain11{};
  SChain12() {}
};
struct SChain13 {
  SChain12 m = b ? SChain12{} : SChain12{};
  SChain13() {}
};
struct SChain14 {
  SChain13 m = b ? SChain13{} : SChain13{};
  SChain14() {}
};
struct SChain15 {
  SChain14 m = b ? SChain14{} : SChain14{};
  SChain15() {}
};
struct SChain16 {
  SChain15 m = b ? SChain15{} : SChain15{};
  SChain16() {}
};
struct SChain17 {
  SChain16 m = b ? SChain16{} : SChain16{};
  SChain17() {}
};
struct SChain18 {
  SChain17 m = b ? SChain17{} : SChain17{};
  SChain18() {}
};
struct SChain19 {
  SChain18 m = b ? SChain18{} : SChain18{};
  SChain19() {}
};
struct SChain20 {
  SChain19 m = b ? SChain19{} : SChain19{};
  SChain20() {}
};
struct SChain21 {
  SChain20 m = b ? SChain20{} : SChain20{};
  SChain21() {}
};
struct SChain22 {
  SChain21 m = b ? SChain21{} : SChain21{};
  SChain22() {}
};
struct SChain23 {
  SChain22 m = b ? SChain22{} : SChain22{};
  SChain23() {}
};
struct SChain24 {
  SChain23 m = b ? SChain23{} : SChain23{};
  SChain24() {}
};
struct SChain25 {
  SChain24 m = b ? SChain24{} : SChain24{};
  SChain25() {}
};
struct SChain26 {
  SChain25 m = b ? SChain25{} : SChain25{};
  SChain26() {}
};
struct SChain27 {
  SChain26 m = b ? SChain26{} : SChain26{};
  SChain27() {}
};
struct SChain28 {
  SChain27 m = b ? SChain27{} : SChain27{};
  SChain28() {}
};
struct SChain29 {
  SChain28 m = b ? SChain28{} : SChain28{};
  SChain29() {}
};
struct SChain30 {
  SChain29 m = b ? SChain29{} : SChain29{};
  SChain30() {}
};
struct SChain31 {
  SChain30 m = b ? SChain30{} : SChain30{};
  SChain31() {}
};
struct SChain32 {
  SChain31 m = b ? SChain31{} : SChain31{};
  SChain32() {}
};
struct SChain33 {
  SChain32 m = b ? SChain32{} : SChain32{};
  SChain33() {}
};
struct SChain34 {
  SChain33 m = b ? SChain33{} : SChain33{};
  SChain34() {}
};
struct SChain35 {
  SChain34 m = b ? SChain34{} : SChain34{};
  SChain35() {}
};
struct SChain36 {
  SChain35 m = b ? SChain35{} : SChain35{};
  SChain36() {}
};
struct SChain37 {
  SChain36 m = b ? SChain36{} : SChain36{};
  SChain37() {}
};
struct SChain38 {
  SChain37 m = b ? SChain37{} : SChain37{};
  SChain38() {}
};
struct SChain39 {
  SChain38 m = b ? SChain38{} : SChain38{};
  SChain39() {}
};
struct SChain40 {
  SChain39 m = b ? SChain39{} : SChain39{};
  SChain40() {}
};
void use(SChain40);
void chain() { use(SChain40{}); }
