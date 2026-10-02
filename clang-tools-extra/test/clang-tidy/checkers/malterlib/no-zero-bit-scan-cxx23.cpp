// RUN: %check_clang_tidy -std=c++23-or-later %s malterlib-no-zero-bit-scan %t -- -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}"

using umint = decltype(sizeof(0));

template <typename t_CType>
int fg_GetLowestBitSetNoZero(t_CType const &_Value);

// The explicit object parameter of a member function is the object it is
// called on, as `this` is: a getter or an operator that only gives access
// to what is in it changes it no more than one with an implicit object,
// called from a getter, `(*this)[0]`, `**this` or `this->f_GetLast()`, or
// directly, `_Masks.f_GetLast()` or `*_Masks`.
template <typename t_CData>
struct TCExplicitVector {
  t_CData *m_pData;
  template <typename tf_CThis>
  auto &operator[](this tf_CThis &&_This, umint _iIndex) {
    return _This.m_pData[_iIndex];
  }
  template <typename tf_CThis>
  auto &operator*(this tf_CThis &&_This) {
    return _This.m_pData[0];
  }
  template <typename tf_CThis>
  auto &f_GetLast(this tf_CThis &&_This) {
    return _This.m_pData[0];
  }
  t_CData &f_GetFirst() { return (*this)[0]; }
  t_CData &f_GetDereferenced() { return **this; }
  t_CData &f_GetLastOfThis() { return this->f_GetLast(); }
};
void fg_ExplicitObjects(TCExplicitVector<umint> &_Masks) {
  if (_Masks.f_GetFirst())
    fg_GetLowestBitSetNoZero(_Masks.f_GetFirst());
  if (_Masks.f_GetDereferenced())
    fg_GetLowestBitSetNoZero(_Masks.f_GetDereferenced());
  if (_Masks.f_GetLastOfThis())
    fg_GetLowestBitSetNoZero(_Masks.f_GetLastOfThis());
  if (_Masks[0])
    fg_GetLowestBitSetNoZero(_Masks[0]);
  if (*_Masks)
    fg_GetLowestBitSetNoZero(*_Masks);
  if (_Masks.f_GetLast())
    fg_GetLowestBitSetNoZero(_Masks.f_GetLast());
  TCExplicitVector<umint> *pMasks = &_Masks;
  if (pMasks->f_GetLast())
    fg_GetLowestBitSetNoZero(pMasks->f_GetLast());
}
// Not one that changes the object in between, also with an explicit object.
struct CExplicitClear {
  umint m_Bits;
  template <typename tf_CThis>
  umint &f_Get(this tf_CThis &&_This) {
    return _This.m_Bits;
  }
  void f_Clear(this CExplicitClear &_This) { _This.m_Bits = 0; }
};
int fg_ExplicitCleared(CExplicitClear &_Bits) {
  if (!_Bits.f_Get())
    return -1;
  _Bits.f_Clear();
  return fg_GetLowestBitSetNoZero(_Bits.f_Get());
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
}
// The same without the clear, which nothing changes in between, also with
// an implicit object.
int fg_ExplicitNotCleared(CExplicitClear &_Bits) {
  if (!_Bits.f_Get())
    return -1;
  return fg_GetLowestBitSetNoZero(_Bits.f_Get());
}
struct CImplicitGet {
  umint m_Bits;
  umint &f_Get() { return m_Bits; }
};
int fg_ImplicitNotCleared(CImplicitGet &_Bits) {
  if (!_Bits.f_Get())
    return -1;
  return fg_GetLowestBitSetNoZero(_Bits.f_Get());
}
// A const explicit object, of a conversion to bool and of a getter, reads
// the object, as a const member function does, also of an integer class
// tested by its conversion.
struct CConstBits {
  umint m_Bits;
  explicit operator bool(this CConstBits const &_This) { return _This.m_Bits != 0; }
  umint f_Get(this CConstBits const &_This) { return _This.m_Bits; }
};
int fg_ConstExplicit(CConstBits const &_Bits) {
  if (_Bits)
    return fg_GetLowestBitSetNoZero(_Bits);
  return -1;
}
int fg_ConstExplicitGetter(CConstBits &_Bits) {
  if (!_Bits.m_Bits)
    return -1;
  _Bits.f_Get();
  return fg_GetLowestBitSetNoZero(_Bits.m_Bits);
}


// Round 160: an explicit object taken by value is a copy (isObjectCopied):
// what the call writes of its own members, also mutable ones, is the copy's,
// not the object it is called on; one taken by reference writes the object.
namespace NReview2Round160 {
struct CMask {
  mutable umint m_CachedBits;
  CMask(CMask const &_Other) : m_CachedBits(_Other.m_CachedBits) {}
  CMask f_WithoutCache(this CMask _This) { _This.m_CachedBits = 0; return _This; }
  void f_Clear(this CMask &_This) { _This.m_CachedBits = 0; }
};
int fg_Copied(CMask &_Mask) {
  if (_Mask.m_CachedBits) {
    auto Copy = _Mask.f_WithoutCache();
    return fg_GetLowestBitSetNoZero(_Mask.m_CachedBits);
  }
  return -1;
}
int fg_Cleared(CMask &_Mask) {
  if (_Mask.m_CachedBits) {
    _Mask.f_Clear();
    return fg_GetLowestBitSetNoZero(_Mask.m_CachedBits);
// CHECK-MESSAGES: :[[@LINE-1]]:12: warning: the argument of 'fg_GetLowestBitSetNoZero' is not checked for zero first; check it, or call 'fg_GetLowestBitSet', which handles zero [malterlib-no-zero-bit-scan]
  }
  return -1;
}
}
