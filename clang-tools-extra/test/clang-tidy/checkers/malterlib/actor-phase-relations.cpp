// RUN: %check_clang_tidy -std=c++20-or-later %s malterlib-actor-phase %t -- -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" -- -I %S/Inputs

#include "malterlib-actor-phase.h"

// Whether what a function of a phase is given is the own object of the actor,
// which is in that phase, or what it holds by value, is told by what the
// expression is relative to the object: the object itself, a member of it,
// also of a member, through a local alias, a cast, a conditional of two such
// or a comma, is; what a pointer member points to, which can be another
// actor, isn't, also through a local alias of it or in a conditional.
// This file is generated: each case gives a form to a function of the
// Destroy phase from a function of that phase.

namespace std {
class type_info;
}

namespace NPhaseRelations {
struct CParts;
// What gives back the pointer it is given by reference, and what gives back
// the object it is given or a global one.
inline CParts *fg_IdPointer(CParts *const &_pParts) { return _pParts; }
extern CParts g_Other;
inline CParts &fg_ChooseRef(bool _bOwn, CParts &_Own) {
  if (_bOwn)
    return _Own;
  return g_Other;
}
struct CState {
  DMibActorEntries(Destroy) TCMap<int, int> mp_Items;
};
DMibActorPhase(Destroy) void fg_ResetParts(CParts **_ppParts) {
  *_ppParts = nullptr;
}
DMibActorPhase(Destroy) void fg_TypeOf(std::type_info const *_pInfo);
DMibActorPhase(Destroy) void fg_ClearState(CState *_pThis) {
  _pThis->mp_Items.f_Clear();
}
DMibActorPhase(Destroy) void fg_ClearStateRef(CState *const &_pThis) {
  _pThis->mp_Items.f_Clear();
}
struct COuter {
  CState m_Inner;
};
struct CParts : CPhaseActor {
  CState m_State;
  COuter m_Outer;
  CState m_States[2];
  CParts *m_pOther = nullptr;
  CState *m_pState = nullptr;
  bool m_bChoose = false;
  // A wrapper that holds another actor and gives it.
  struct CRef {
    CParts *m_pActor = nullptr;
    CParts *get() const { return m_pActor; }
  };
  CRef m_Ref;
  // Another actor, which `->` of this one gives.
  CParts *operator->();
  DMibActorEntries(Destroy) TCMap<int, int> mp_Items;

  DMibActorPhase(Destroy) static void fs_Clear(CParts *_pThis) {
    _pThis->mp_Items.f_Clear();
  }

  DMibActorPhase(Destroy) void f_Own() {
    fs_Clear(this);
  }

  DMibActorPhase(Destroy) void f_OwnAlias() {
    auto *pThis = this;
    fs_Clear(pThis);
  }

  DMibActorPhase(Destroy) void f_OwnCast() {
    fs_Clear(static_cast<CParts *>(this));
  }

  DMibActorPhase(Destroy) void f_OwnConditional() {
    fs_Clear(m_bChoose ? this : this);
  }

  DMibActorPhase(Destroy) void f_OwnComma() {
    fs_Clear(((void)0, this));
  }

  DMibActorPhase(Destroy) void f_Other() {
    fs_Clear(m_pOther);
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: 'fs_Clear' runs only in the Destroy phase of its actor, but is given another object here, which may not be in that phase [malterlib-actor-phase]
  }

  DMibActorPhase(Destroy) void f_OtherAlias() {
    auto *pOther = m_pOther;
    fs_Clear(pOther);
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: 'fs_Clear' runs only in the Destroy phase of its actor, but is given another object here, which may not be in that phase [malterlib-actor-phase]
  }

  DMibActorPhase(Destroy) void f_OtherConditional() {
    fs_Clear(m_bChoose ? this : m_pOther);
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: 'fs_Clear' runs only in the Destroy phase of its actor, but is given another object here, which may not be in that phase [malterlib-actor-phase]
  }

  DMibActorPhase(Destroy) void f_OtherComma() {
    fs_Clear(((void)0, m_pOther));
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: 'fs_Clear' runs only in the Destroy phase of its actor, but is given another object here, which may not be in that phase [malterlib-actor-phase]
  }

  DMibActorPhase(Destroy) void f_ByValue() {
    fg_ClearState(&m_State);
  }

  DMibActorPhase(Destroy) void f_MemberOfMember() {
    fg_ClearState(&m_Outer.m_Inner);
  }

  DMibActorPhase(Destroy) void f_ArrayElement() {
    fg_ClearState(&m_States[1]);
  }

  DMibActorPhase(Destroy) void f_ByValueAlias() {
    auto *pState = &m_State;
    fg_ClearState(pState);
  }

  DMibActorPhase(Destroy) void f_ByValueThisAlias() {
    auto *pThis = this;
    fg_ClearState(&pThis->m_State);
  }

  DMibActorPhase(Destroy) void f_ByValueCast() {
    fg_ClearState(static_cast<CState *>(&m_State));
  }

  DMibActorPhase(Destroy) void f_ByValueConditional() {
    fg_ClearState(m_bChoose ? &m_State : &m_Outer.m_Inner);
  }

  DMibActorPhase(Destroy) void f_ByValueComma() {
    fg_ClearState(((void)0, &m_State));
  }

  DMibActorPhase(Destroy) void f_ThroughPointer() {
    fg_ClearState(&m_pOther->m_State);
// CHECK-MESSAGES: :[[@LINE-1]]:19: warning: 'fg_ClearState' runs only in the Destroy phase of its actor, but is given another object here, which may not be in that phase [malterlib-actor-phase]
  }

  DMibActorPhase(Destroy) void f_ThroughPointerMemberOfMember() {
    fg_ClearState(&m_pOther->m_Outer.m_Inner);
// CHECK-MESSAGES: :[[@LINE-1]]:19: warning: 'fg_ClearState' runs only in the Destroy phase of its actor, but is given another object here, which may not be in that phase [malterlib-actor-phase]
  }

  DMibActorPhase(Destroy) void f_ThroughPointerAlias() {
    auto *pOther = m_pOther;
    fg_ClearState(&pOther->m_State);
// CHECK-MESSAGES: :[[@LINE-1]]:19: warning: 'fg_ClearState' runs only in the Destroy phase of its actor, but is given another object here, which may not be in that phase [malterlib-actor-phase]
  }

  DMibActorPhase(Destroy) void f_ThroughPointerCast() {
    fg_ClearState(static_cast<CState *>(&m_pOther->m_State));
// CHECK-MESSAGES: :[[@LINE-1]]:19: warning: 'fg_ClearState' runs only in the Destroy phase of its actor, but is given another object here, which may not be in that phase [malterlib-actor-phase]
  }

  DMibActorPhase(Destroy) void f_ThroughPointerConditional() {
    fg_ClearState(m_bChoose ? &m_State : &m_pOther->m_State);
// CHECK-MESSAGES: :[[@LINE-1]]:19: warning: 'fg_ClearState' runs only in the Destroy phase of its actor, but is given another object here, which may not be in that phase [malterlib-actor-phase]
  }

  DMibActorPhase(Destroy) void f_ThroughPointerComma() {
    fg_ClearState(((void)0, &m_pOther->m_State));
// CHECK-MESSAGES: :[[@LINE-1]]:19: warning: 'fg_ClearState' runs only in the Destroy phase of its actor, but is given another object here, which may not be in that phase [malterlib-actor-phase]
  }

  DMibActorPhase(Destroy) void f_ConstantCondition() {
    constexpr bool c_bOwn = true;
    fg_ClearState(c_bOwn ? &m_State : &m_pOther->m_State);
  }

  DMibActorPhase(Destroy) void f_WrapperGetter() {
    fg_ClearState(&m_Ref.get()->m_State);
// CHECK-MESSAGES: :[[@LINE-1]]:19: warning: 'fg_ClearState' runs only in the Destroy phase of its actor, but is given another object here, which may not be in that phase [malterlib-actor-phase]
  }

  DMibActorPhase(Destroy) void f_ConditionallyAssigned() {
    CState Local;
    CState *pState = &Local;
    if (m_bChoose) pState = &m_pOther->m_State;
    fg_ClearState(pState);
// CHECK-MESSAGES: :[[@LINE-1]]:19: warning: 'fg_ClearState' runs only in the Destroy phase of its actor, but is given another object here, which may not be in that phase [malterlib-actor-phase]
  }

  DMibActorPhase(Destroy) void f_AssignedInBranches() {
    CState *pState;
    if (m_bChoose) pState = m_pOther->m_pState;
    else pState = &m_pOther->m_State;
    fg_ClearState(pState);
// CHECK-MESSAGES: :[[@LINE-1]]:19: warning: 'fg_ClearState' runs only in the Destroy phase of its actor, but is given another object here, which may not be in that phase [malterlib-actor-phase]
  }

  DMibActorPhase(Destroy) void f_IdentityOfOther() {
    fs_Clear(fg_IdPointer(m_pOther));
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: 'fs_Clear' runs only in the Destroy phase of its actor, but is given another object here, which may not be in that phase [malterlib-actor-phase]
  }

  DMibActorPhase(Destroy) void f_IdentityOfOwn() {
    CParts *pThis = this;
    fs_Clear(fg_IdPointer(pThis));
  }

  DMibActorPhase(Destroy) void f_ChoiceWithGlobal() {
    fs_Clear(&fg_ChooseRef(m_bChoose, *this));
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: 'fs_Clear' runs only in the Destroy phase of its actor, but is given another object here, which may not be in that phase [malterlib-actor-phase]
  }

  DMibActorPhase(Destroy) void f_ConstantConditionOfPointers() {
    auto *pLocal = this;
    fs_Clear(true ? pLocal : m_pOther);
  }

  DMibActorPhase(Destroy) void f_ConstantConditionOfPointersOther() {
    auto *pLocal = this;
    fs_Clear(false ? pLocal : m_pOther);
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: 'fs_Clear' runs only in the Destroy phase of its actor, but is given another object here, which may not be in that phase [malterlib-actor-phase]
  }

  DMibActorPhase(Destroy) void f_ConditionalPointeesByReference() {
    CState *pOther = &m_pOther->m_State;
    fg_ClearStateRef(m_bChoose ? pOther : m_pState);
// CHECK-MESSAGES: :[[@LINE-1]]:22: warning: 'fg_ClearStateRef' runs only in the Destroy phase of its actor, but is given another object here, which may not be in that phase [malterlib-actor-phase]
  }

  DMibActorPhase(Destroy) void f_OtherActorThroughArrow() {
    fg_ClearState(&(*this)->m_State);
// CHECK-MESSAGES: :[[@LINE-1]]:19: warning: 'fg_ClearState' runs only in the Destroy phase of its actor, but is given another object here, which may not be in that phase [malterlib-actor-phase]
  }

  DMibActorPhase(Destroy) void f_LambdaGivesPointerMember() {
    fs_Clear([](CParts *const &_pParts) { return _pParts; }(m_pOther));
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: 'fs_Clear' runs only in the Destroy phase of its actor, but is given another object here, which may not be in that phase [malterlib-actor-phase]
  }

  DMibActorPhase(Destroy) void f_LongAliasChain() {
    auto &pAlias0 = m_pOther;
    auto &pAlias1 = pAlias0;
    auto &pAlias2 = pAlias1;
    auto &pAlias3 = pAlias2;
    auto &pAlias4 = pAlias3;
    auto &pAlias5 = pAlias4;
    auto &pAlias6 = pAlias5;
    auto &pAlias7 = pAlias6;
    auto &pAlias8 = pAlias7;
    auto &pAlias9 = pAlias8;
    auto &pAlias10 = pAlias9;
    auto &pAlias11 = pAlias10;
    auto &pAlias12 = pAlias11;
    auto &pAlias13 = pAlias12;
    auto &pAlias14 = pAlias13;
    auto &pAlias15 = pAlias14;
    auto &pAlias16 = pAlias15;
    auto &pAlias17 = pAlias16;
    auto &pAlias18 = pAlias17;
    auto &pAlias19 = pAlias18;
    auto &pAlias20 = pAlias19;
    auto &pAlias21 = pAlias20;
    auto &pAlias22 = pAlias21;
    auto &pAlias23 = pAlias22;
    auto &pAlias24 = pAlias23;
    auto &pAlias25 = pAlias24;
    auto &pAlias26 = pAlias25;
    auto &pAlias27 = pAlias26;
    auto &pAlias28 = pAlias27;
    auto &pAlias29 = pAlias28;
    auto &pAlias30 = pAlias29;
    auto &pAlias31 = pAlias30;
    auto &pAlias32 = pAlias31;
    auto &pAlias33 = pAlias32;
    auto &pAlias34 = pAlias33;
    auto &pAlias35 = pAlias34;
    auto &pAlias36 = pAlias35;
    auto &pAlias37 = pAlias36;
    auto &pAlias38 = pAlias37;
    auto &pAlias39 = pAlias38;
    auto &pAlias40 = pAlias39;
    auto &pAlias41 = pAlias40;
    auto &pAlias42 = pAlias41;
    auto &pAlias43 = pAlias42;
    auto &pAlias44 = pAlias43;
    auto &pAlias45 = pAlias44;
    auto &pAlias46 = pAlias45;
    auto &pAlias47 = pAlias46;
    auto &pAlias48 = pAlias47;
    auto &pAlias49 = pAlias48;
    auto &pAlias50 = pAlias49;
    auto &pAlias51 = pAlias50;
    auto &pAlias52 = pAlias51;
    auto &pAlias53 = pAlias52;
    auto &pAlias54 = pAlias53;
    auto &pAlias55 = pAlias54;
    auto &pAlias56 = pAlias55;
    auto &pAlias57 = pAlias56;
    auto &pAlias58 = pAlias57;
    auto &pAlias59 = pAlias58;
    auto &pAlias60 = pAlias59;
    auto &pAlias61 = pAlias60;
    auto &pAlias62 = pAlias61;
    auto &pAlias63 = pAlias62;
    auto &pAlias64 = pAlias63;
    auto &pAlias65 = pAlias64;
    auto &pAlias66 = pAlias65;
    auto &pAlias67 = pAlias66;
    auto &pAlias68 = pAlias67;
    auto &pAlias69 = pAlias68;
    auto &pAlias70 = pAlias69;
    auto &pAlias71 = pAlias70;
    auto &pAlias72 = pAlias71;
    auto &pAlias73 = pAlias72;
    auto &pAlias74 = pAlias73;
    auto &pAlias75 = pAlias74;
    auto &pAlias76 = pAlias75;
    auto &pAlias77 = pAlias76;
    auto &pAlias78 = pAlias77;
    auto &pAlias79 = pAlias78;
    auto &pAlias80 = pAlias79;
    auto &pAlias81 = pAlias80;
    auto &pAlias82 = pAlias81;
    auto &pAlias83 = pAlias82;
    auto &pAlias84 = pAlias83;
    auto &pAlias85 = pAlias84;
    auto &pAlias86 = pAlias85;
    auto &pAlias87 = pAlias86;
    auto &pAlias88 = pAlias87;
    auto &pAlias89 = pAlias88;
    auto &pAlias90 = pAlias89;
    auto &pAlias91 = pAlias90;
    auto &pAlias92 = pAlias91;
    auto &pAlias93 = pAlias92;
    auto &pAlias94 = pAlias93;
    auto &pAlias95 = pAlias94;
    auto &pAlias96 = pAlias95;
    auto &pAlias97 = pAlias96;
    auto &pAlias98 = pAlias97;
    auto &pAlias99 = pAlias98;
    auto &pAlias100 = pAlias99;
    auto &pAlias101 = pAlias100;
    auto &pAlias102 = pAlias101;
    auto &pAlias103 = pAlias102;
    auto &pAlias104 = pAlias103;
    auto &pAlias105 = pAlias104;
    auto &pAlias106 = pAlias105;
    auto &pAlias107 = pAlias106;
    auto &pAlias108 = pAlias107;
    auto &pAlias109 = pAlias108;
    auto &pAlias110 = pAlias109;
    auto &pAlias111 = pAlias110;
    auto &pAlias112 = pAlias111;
    auto &pAlias113 = pAlias112;
    auto &pAlias114 = pAlias113;
    auto &pAlias115 = pAlias114;
    auto &pAlias116 = pAlias115;
    auto &pAlias117 = pAlias116;
    auto &pAlias118 = pAlias117;
    auto &pAlias119 = pAlias118;
    auto &pAlias120 = pAlias119;
    auto &pAlias121 = pAlias120;
    auto &pAlias122 = pAlias121;
    auto &pAlias123 = pAlias122;
    auto &pAlias124 = pAlias123;
    auto &pAlias125 = pAlias124;
    auto &pAlias126 = pAlias125;
    auto &pAlias127 = pAlias126;
    auto &pAlias128 = pAlias127;
    auto &pAlias129 = pAlias128;
    auto &pAlias130 = pAlias129;
    auto &pAlias131 = pAlias130;
    auto &pAlias132 = pAlias131;
    auto &pAlias133 = pAlias132;
    auto &pAlias134 = pAlias133;
    auto &pAlias135 = pAlias134;
    auto &pAlias136 = pAlias135;
    auto &pAlias137 = pAlias136;
    auto &pAlias138 = pAlias137;
    auto &pAlias139 = pAlias138;
    auto &pAlias140 = pAlias139;
    auto &pAlias141 = pAlias140;
    auto &pAlias142 = pAlias141;
    auto &pAlias143 = pAlias142;
    auto &pAlias144 = pAlias143;
    auto &pAlias145 = pAlias144;
    auto &pAlias146 = pAlias145;
    auto &pAlias147 = pAlias146;
    auto &pAlias148 = pAlias147;
    auto &pAlias149 = pAlias148;
    auto &pAlias150 = pAlias149;
    auto &pAlias151 = pAlias150;
    auto &pAlias152 = pAlias151;
    auto &pAlias153 = pAlias152;
    auto &pAlias154 = pAlias153;
    auto &pAlias155 = pAlias154;
    auto &pAlias156 = pAlias155;
    auto &pAlias157 = pAlias156;
    auto &pAlias158 = pAlias157;
    auto &pAlias159 = pAlias158;
    auto &pAlias160 = pAlias159;
    auto &pAlias161 = pAlias160;
    auto &pAlias162 = pAlias161;
    auto &pAlias163 = pAlias162;
    auto &pAlias164 = pAlias163;
    auto &pAlias165 = pAlias164;
    auto &pAlias166 = pAlias165;
    auto &pAlias167 = pAlias166;
    auto &pAlias168 = pAlias167;
    auto &pAlias169 = pAlias168;
    auto &pAlias170 = pAlias169;
    auto &pAlias171 = pAlias170;
    auto &pAlias172 = pAlias171;
    auto &pAlias173 = pAlias172;
    auto &pAlias174 = pAlias173;
    auto &pAlias175 = pAlias174;
    auto &pAlias176 = pAlias175;
    auto &pAlias177 = pAlias176;
    auto &pAlias178 = pAlias177;
    auto &pAlias179 = pAlias178;
    auto &pAlias180 = pAlias179;
    auto &pAlias181 = pAlias180;
    auto &pAlias182 = pAlias181;
    auto &pAlias183 = pAlias182;
    auto &pAlias184 = pAlias183;
    auto &pAlias185 = pAlias184;
    auto &pAlias186 = pAlias185;
    auto &pAlias187 = pAlias186;
    auto &pAlias188 = pAlias187;
    auto &pAlias189 = pAlias188;
    auto &pAlias190 = pAlias189;
    auto &pAlias191 = pAlias190;
    auto &pAlias192 = pAlias191;
    auto &pAlias193 = pAlias192;
    auto &pAlias194 = pAlias193;
    auto &pAlias195 = pAlias194;
    auto &pAlias196 = pAlias195;
    auto &pAlias197 = pAlias196;
    auto &pAlias198 = pAlias197;
    auto &pAlias199 = pAlias198;
    auto &pAlias200 = pAlias199;
    auto &pAlias201 = pAlias200;
    auto &pAlias202 = pAlias201;
    auto &pAlias203 = pAlias202;
    auto &pAlias204 = pAlias203;
    auto &pAlias205 = pAlias204;
    auto &pAlias206 = pAlias205;
    auto &pAlias207 = pAlias206;
    auto &pAlias208 = pAlias207;
    auto &pAlias209 = pAlias208;
    auto &pAlias210 = pAlias209;
    auto &pAlias211 = pAlias210;
    auto &pAlias212 = pAlias211;
    auto &pAlias213 = pAlias212;
    auto &pAlias214 = pAlias213;
    auto &pAlias215 = pAlias214;
    auto &pAlias216 = pAlias215;
    auto &pAlias217 = pAlias216;
    auto &pAlias218 = pAlias217;
    auto &pAlias219 = pAlias218;
    auto &pAlias220 = pAlias219;
    auto &pAlias221 = pAlias220;
    auto &pAlias222 = pAlias221;
    auto &pAlias223 = pAlias222;
    auto &pAlias224 = pAlias223;
    auto &pAlias225 = pAlias224;
    auto &pAlias226 = pAlias225;
    auto &pAlias227 = pAlias226;
    auto &pAlias228 = pAlias227;
    auto &pAlias229 = pAlias228;
    auto &pAlias230 = pAlias229;
    auto &pAlias231 = pAlias230;
    auto &pAlias232 = pAlias231;
    auto &pAlias233 = pAlias232;
    auto &pAlias234 = pAlias233;
    auto &pAlias235 = pAlias234;
    auto &pAlias236 = pAlias235;
    auto &pAlias237 = pAlias236;
    auto &pAlias238 = pAlias237;
    auto &pAlias239 = pAlias238;
    auto &pAlias240 = pAlias239;
    auto &pAlias241 = pAlias240;
    auto &pAlias242 = pAlias241;
    auto &pAlias243 = pAlias242;
    auto &pAlias244 = pAlias243;
    auto &pAlias245 = pAlias244;
    auto &pAlias246 = pAlias245;
    auto &pAlias247 = pAlias246;
    auto &pAlias248 = pAlias247;
    auto &pAlias249 = pAlias248;
    auto &pAlias250 = pAlias249;
    auto &pAlias251 = pAlias250;
    auto &pAlias252 = pAlias251;
    auto &pAlias253 = pAlias252;
    auto &pAlias254 = pAlias253;
    auto &pAlias255 = pAlias254;
    auto &pAlias256 = pAlias255;
    auto &pAlias257 = pAlias256;
    auto &pAlias258 = pAlias257;
    auto &pAlias259 = pAlias258;
    fg_ClearState(&pAlias259->m_State);
// CHECK-MESSAGES: :[[@LINE-1]]:19: warning: 'fg_ClearState' runs only in the Destroy phase of its actor, but is given another object here, which may not be in that phase [malterlib-actor-phase]
  }

  DMibActorPhase(Destroy) void f_SteppedPointerMember() {
    fs_Clear(++m_pOther);
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: 'fs_Clear' runs only in the Destroy phase of its actor, but is given another object here, which may not be in that phase [malterlib-actor-phase]
  }

  DMibActorPhase(Destroy) void f_LocalPointerStorage() {
    CParts *pLocal = m_pOther;
    fg_ResetParts(&pLocal);
  }

  DMibActorPhase(Destroy) void f_OwnOrLocal() {
    CState Local;
    fg_ClearState(m_bChoose ? &m_State : &Local);
  }

  DMibActorPhase(Destroy) void f_OtherOrLocal() {
    CState Local;
    fg_ClearState(m_bChoose ? &m_pOther->m_State : &Local);
// CHECK-MESSAGES: :[[@LINE-1]]:19: warning: 'fg_ClearState' runs only in the Destroy phase of its actor, but is given another object here, which may not be in that phase [malterlib-actor-phase]
  }

  DMibActorPhase(Destroy) void f_OwnOrLocalCast() {
    CState Local;
    fg_ClearState(static_cast<CState *>(m_bChoose ? &m_State : &Local));
  }

  DMibActorPhase(Destroy) void f_OwnOrLocalComma() {
    CState Local;
    fg_ClearState(((void)0, m_bChoose ? &m_State : &Local));
  }

  DMibActorPhase(Destroy) void f_OwnOrLocalAlias() {
    CState Local;
    CState *pState = m_bChoose ? &m_State : &Local;
    fg_ClearState(pState);
  }

  DMibActorPhase(Destroy) void f_OtherOrLocalAlias() {
    CState Local;
    CState *pState = m_bChoose ? &m_pOther->m_State : &Local;
    fg_ClearState(pState);
// CHECK-MESSAGES: :[[@LINE-1]]:19: warning: 'fg_ClearState' runs only in the Destroy phase of its actor, but is given another object here, which may not be in that phase [malterlib-actor-phase]
  }

  DMibActorPhase(Destroy) void f_OtherOrLocalCast() {
    CState Local;
    fg_ClearState(static_cast<CState *>(m_bChoose ? &m_pOther->m_State : &Local));
// CHECK-MESSAGES: :[[@LINE-1]]:19: warning: 'fg_ClearState' runs only in the Destroy phase of its actor, but is given another object here, which may not be in that phase [malterlib-actor-phase]
  }

  DMibActorPhase(Destroy) void f_LocalPointerStorageOfOwn() {
    CParts *pLocal = this;
    fg_ResetParts(&pLocal);
  }

  DMibActorPhase(Destroy) void f_TypeInformationOfOther() {
    fg_TypeOf(&typeid(*m_pOther));
  }

  DMibActorPhase(Destroy) void f_SteppedPointerMemberCompound() {
    fs_Clear(m_pOther += 1);
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: 'fs_Clear' runs only in the Destroy phase of its actor, but is given another object here, which may not be in that phase [malterlib-actor-phase]
  }
};
}
