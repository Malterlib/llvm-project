#include "malterlib-actor-phase.h"

TCFuture<int> fg_Suspend();

// A virtual function that a class beside the one that calls it overrides,
// sharing the base that declares it as a virtual base, which a class derived
// from both takes as the final override.
#define DBase(d_Name)                                                          \
  struct d_Name : CPhaseActor {                                                \
    DMibActorEntries(Destroy) TCMap<int, int> mp_Items;                        \
    virtual void f_Tick() {}                                                   \
    TCFuture<int> fp_Destroy() override {                                      \
      mp_Items.f_Clear();                                                      \
      co_return 0;                                                             \
    }                                                                          \
  };

// Overridden in the precompiled header.
DBase(CHeaderBase)
struct CHeaderSide : virtual CHeaderBase {
  void f_Tick() override { (void)fp_DestroyInternal(); }
};

// Overridden in the source.
DBase(CSourceBase)

// Overridden by a template of the precompiled header that only the source
// instantiates.
DBase(CTemplateBase)
template <typename t_CType> struct TCTemplateSide : virtual CTemplateBase {
  t_CType m_Value;
  void f_Tick() override { (void)fp_DestroyInternal(); }
};

// Overridden by a function the source defines outside of its class.
DBase(COutOfLineBase)
struct COutOfLineSide : virtual COutOfLineBase {
  void f_Tick() override;
};

// Overridden by a function that doesn't destroy the actor.
DBase(CPlainBase)
struct CPlainSide : virtual CPlainBase {
  void f_Tick() override {}
};

// Classes the source doesn't use, whose lexical declaration contexts a walk of
// the whole translation unit reads.
#define DUnrelated(d_Name)                                                     \
  namespace d_Name {                                                           \
  struct CUnrelated : virtual CPlainBase {                                     \
    void f_Tick() override {}                                                  \
    void f_First() {}                                                          \
    void f_Second() {}                                                         \
  };                                                                           \
  }
DUnrelated(NUnrelated0) DUnrelated(NUnrelated1) DUnrelated(NUnrelated2)
DUnrelated(NUnrelated3) DUnrelated(NUnrelated4) DUnrelated(NUnrelated5)
DUnrelated(NUnrelated6) DUnrelated(NUnrelated7) DUnrelated(NUnrelated8)
DUnrelated(NUnrelated9) DUnrelated(NUnrelated10) DUnrelated(NUnrelated11)
DUnrelated(NUnrelated12) DUnrelated(NUnrelated13) DUnrelated(NUnrelated14)
DUnrelated(NUnrelated15) DUnrelated(NUnrelated16) DUnrelated(NUnrelated17)
DUnrelated(NUnrelated18) DUnrelated(NUnrelated19) DUnrelated(NUnrelated20)
DUnrelated(NUnrelated21) DUnrelated(NUnrelated22) DUnrelated(NUnrelated23)
DUnrelated(NUnrelated24) DUnrelated(NUnrelated25) DUnrelated(NUnrelated26)
DUnrelated(NUnrelated27) DUnrelated(NUnrelated28) DUnrelated(NUnrelated29)
