.. title:: clang-tidy - malterlib-no-zero-bit-scan

malterlib-no-zero-bit-scan
==========================

Finds calls of bit scans that require a value other than zero, like
``fg_GetHighestBitSetNoZero``, whose argument is not checked for zero first.
Passing zero to them is undefined behavior; ``fg_GetHighestBitSet`` handles
zero.

This is a best effort check. The argument is taken as checked when it is a
constant other than zero, is not zero by construction (``Value | 1``,
``1 << Shift``, or of an integer class, ``Value | CInt(1)``, whose ``|``
gives that class, not a narrower type that can lose the bit, also converted
to a narrower type that keeps a bit that is set, ``uint8(Value | 1)``, also
of a signed integer class, ``uint256(_Bits | int512(1))``, also
of a ``const`` local made with it, ``uint8(Bits)`` of ``const unsigned Bits =
_Bits | 1``, also
through a ``const`` local made of it, ``const unsigned Bits = _Bits | 1``),
also bound to a ``const &`` parameter, a conditional each of whose values is
one of those or checked, also by its condition, ``_Value ? _Value : 1``, also
widened to an integer class, ``uint256(_Bits ? _Bits : 1)``, also the only
member of a wrapper, also of a signed integer class, ``int256(_Bits)`` of a
``zint256``, or when a variable it
reads is tested, in the direction the call runs in, also through its
``operator bool``, by:

- the condition of an ``if`` (not its initializer), ``while``, ``for`` or
  ``?:`` the call is in, also the condition of a ``for`` for its increment
  when the body doesn't write the variable,
- the left side of a ``&&`` or ``||`` the call is on the right side of,
- an earlier ``if`` in an enclosing block that returns, throws, breaks,
  continues, jumps past the call or calls what doesn't return, ``[[noreturn]]
  void fg_Fail()``, also in parentheses or cast to void, ``(fg_Fail());``, and
  whose other branch doesn't write the
  variable,
- an earlier assertion macro, like ``DMibCheck(_Value != 0)``, or
  ``[[assume]]``; an assertion that is compiled out, like ``DMibFastCheck``
  in a release build, tells nothing,
- an earlier fact for the analysis, ``DMibAnalyzerAssume(Size >= t_CParams::mc_MinSize)``,
  which expands to an unevaluated ``static_cast<void>(sizeof(!!(...)))``, so it
  changes neither the code nor what the optimizer assumes, and is checked with
  the types and values of the instantiation in every configuration: beside an
  assertion that a configuration compiles out, it tells what the assertion
  does where it isn't (not one that would change the variable, ``(_Bits =
  1)``, which isn't evaluated, but a comparison that only reads it, also of
  an integer class, ``_Bits != 0``), and an ``[[assume]]`` without side
  effects, which the compiler doesn't ignore,

unless the variable is written between the test and the call, also later in a
loop around the call (but not one that runs its body once, like ``do { ... }
while (false)``, nor a ``do`` loop whose condition tests what it writes, ``do
{ ... } while ((_Bits >>= 1) != 0)``, also by a prefix decrement, also of an
integer class, ``while (--_Value)``, or one whose body ends with a ``break``, ``return`` or
``throw``, also of what has to be destroyed, ``throw CError{};``, which still runs its
initializer and condition before, ``for (_Value = 0; ;)``, nor a ``do`` loop
whose condition checks it, ``do { ... } while (_Value)``, also what it assigns,
``while ((_Value &= _Value - 1) != 0)``, when the argument reads only what it
assigns, which a later iteration runs after), through a local reference to it, also a structured
binding of a reference, ``auto &[Bits] = _Info``, also one bound to it
through a cast, ``static_cast<umint &>(_Value)``, or as either of a
conditional, ``bFirst ? _Value : _Other`` (whose test tells only of the
argument when both are it), or a local pointer to it, like ``*pValue = 0`` with ``pValue = &_Value``, also a static one, also through a cast to a
pointer, ``static_cast<umint *>(&_Value)``, also to its bytes, whose elements
overlap it, ``pBytes[1] = 0`` with ``pBytes = reinterpret_cast<uint8
*>(&_Value)``, also to an element or
a member of it, ``pAlias[1] = 0`` with ``pAlias = _pValues``, also moved by a
constant, ``*pAlias = 0`` with ``pAlias = _pValues + 1`` of ``_pValues[1]``,
or by what isn't, which can be zero, ``*pAlias = 0`` with ``pAlias = _pValues +
_Offset`` of ``_pValues[0]`` (an element at another distance isn't told),
also of a cast, ``static_cast<unsigned *>(_pValues) + 1`` (but not assigning the pointer
itself, ``pValue = nullptr``, nor a pointer that is only read through, ``Copy = *pValue``, before it is pointed elsewhere), also a pointer chosen by a conditional,
``bChoose ? &_Value : &Other``, a pointer a member function of it gives
into it, ``*pBits`` of ``auto *pBits = _Bits.f_FindEqual(_Key)``, which any
write of it changes, ``_Bits[_Key] = 0``, by a local array of pointers to it, ``unsigned *Pointers[] = {&_Value}``, by a
reference a helper gives back, ``unsigned &Bits = fg_Ref(_Value)``, by an
aggregate that refers or points to it (not one only read, also in an array
or another aggregate made with a list, ``CView Views[]{{_Value}}``, also extended by a
local reference, ``const CView &View = CView{_Value}``, also given by a
reference to const to a function whose body only reads it the same way,
``fg_Read(View)``), ``CClear Clear{_Value}`` or
``CClear Clear(_Value)`` (when its destructor writes, also through a member
function of it that it calls, ``~CClear() { f_Clear(); }``, a callback it
keeps, also through a local reference to it, ``auto &fCleanup = m_fCleanup;
fCleanup();``, or atomically,
``__atomic_store_n(&m_Bits, 0u, __ATOMIC_RELAXED)``, where it is destroyed),
also a temporary a local reference extends, ``auto const &Clear =
CClear{_Value}``, or made of a local lambda that writes it, ``TCGuard
Guard{fClear}``, also of a copy of one, ``auto fCopy = fClear;``,
also through its member later, ``Borrowed.m_Bits = 0``
of ``CBorrowed Borrowed{_Value}``, also through a base, ``CDerived
Borrowed{{_Value}}``, an aggregate member, an array member, ``unsigned
*m_pBits[1]``, the member of a union it is made with, or a copy of it, ``CBorrowed Copy =
Borrowed``, also one converted from another specialization of its template,
``TCSharedPointer<CBase> pBase = _pState``, also one shared as const,
``_pState.f_ShareAsConst()``, what a pointer element of a copy of a
container points to, ``*Copy[0]``, an iterator of a map at the element of
any key, also one only known where it runs, ``Masks[_Key]``, also a reverse
one, ``f_GetIteratorReverse()``, a pointer element of a map assigned it,
a shared pointer locked from a weak one it gives, ``_pState.f_Weak()
.f_Lock()``,
``Pointers[umint(0)] = &_Bits``, also one moved from a copy,
``auto pMoved = fg_Move(pCopy)``, through what the copy shares, (not
destroying a copy of a smart pointer made of one that keeps what it points
to, ``{ auto pCopy = _pState; }``, which only gives up its share, nor writing
or destroying a copy of its own of what can't be copied as it is,
``TCUniquePointer<CState> pCopy(fg_Copy(_pState))``, nor writing an element of
a copy at the index of the one checked, ``Copy[_Index] = 0``, nor
writing an element of an element of a copy of a container of containers,
``Copy[0][0]``, also of a map whose index operator is a template, which
copies them), a reference member or what a pointer member points to (not a member of
its own, ``Copy.m_Bits`` of ``CInfo Copy = _Info``) (a scan in the initializer of a ``for``
runs once, before the loop writes), by the destructor of what the
initializer of the check makes of it, ``if (CClear Clear{_Value}; !_Value)
return;``, also of a loop or a ``switch``, destroyed where it ends, ``for
(CClear Clear{_Value}; false;) {}``, also by subscript, ``m_pBits[0] = 0``, or
through what a pointer member points to, ``m_pInfo->m_Bits = 0`` (not where
it doesn't run, in a discarded branch of an ``if constexpr`` or in ``sizeof``
or ``noexcept``, nor in a branch of an ``if`` that leaves before the call,
``if (_bClear) { _Value = 0; return -1; }``), also through a function it gives it to, ``~CReset() {
fg_Clear(m_Bits); }`` (not one that can be seen not to write through what it keeps,
``~CObserve() {}``, also of a temporary), also a temporary of the condition, ``if ((CClear{_Value},
_Value))``, by a loop over it by reference, ``for (auto &Value :
_Values)``, also over pointers to it, ``for (auto pBits : Pointers) *pBits =
0;``, which can change it also when it is destroyed, or by a function it is
given to by a reference or a pointer that can change it, like
``fg_Clear(_Value)``, also what a smart pointer moved on to it points to,
``fg_Consume(fg_Move(pCopy))``, or by destroying a lambda that captures what
writes it where it is destroyed, ``[Cleanup = g_OnScopeExit / [&] { _Bits = 0;
}] {}``, or by a lambda that writes through a pointer it captures by
reference, as the pointer is where it is called, ``[&pBits] { *pBits = 0; }``
(not by moving what owns it, ``fg_Move(_pState)``, which
leaves what it points to), or by the constructor or destructor of a local made
between them, ``CReset Reset;``, or by a default argument of a call between
them, which runs where the call is, ``fg_Work()`` of ``void fg_Work(int
_Unused = (g_Bits = 0))``, or by what runs while a coroutine is suspended
between them, ``co_await fg_Work()``, which can write a global or a member
of the object, like other calls of an actor do, or what a pointer or a
reference refers to, ``*_pBits``, but not a local or a parameter it holds by
value. A test of a local or a parameter that nothing in
the function writes, also as an output of inline assembly, also of a parameter
by reference to ``const`` in a function that isn't a coroutine, ``uint256 const
&_Bits``, also checks a ``const`` local copied from it, ``const
unsigned Bits = _Bits``; of one that it writes, also only after the copy,
``_Bits = 0;``, it doesn't, which is reported where the copy is scanned. A test labeled like ``case 0: if (!_Value) return;``
checks what follows it, but a label between the test and the call, also one
in a statement there, ``{ Target:; }``, which a
jump can reach past the test, doesn't. A test of a value the
argument narrows, like ``static_cast<uint8>(_Value)``, also implicitly, a
``uint16`` given as a ``uint8`` and tested as an ``int``, doesn't check the
argument, a test of a call, like ``fg_NextBits() != 0``, doesn't check another
call of it, which can give another value (unless it is an operator or a
conversion that can't change its object or what it is given, also by a
reference that isn't to const that its body can be seen only to read, a key
forwarded, ``operator[](tf_CKey &&_Key) const``, also to a lambda, or an index
operator that inserts what it is asked for (whose element of one key is
apart from those of the others, ``_Masks[1] = 0`` of a check of ``_Masks[0]``,
also of keys written as converted or named constants, ``_Masks[umint(1)]``),
``_Masks[_Key]`` of a map that
isn't const, which gives the same element for the same key; a function, also
a ``constexpr`` one, can read or change other state), and neither does comparing with zero what can be zero when the
argument isn't, like ``_Value - 1 != 0``, unlike ``(_Value & Mask) != 0`` or
``_Value % 4 != 0``, nor comparing with another constant what zero can pass
too, like ``(_Value | 1) == 1``. A value less a constant, ``_Size - 1``, also of an integer class, is
zero only when the value is that constant (not narrowed, ``uint8(_Size -
1)``, which is zero at other values too), which comparing the value with a
constant, as it is converted, ``umint(-1)``, also a named one of an integer
class, also one made of its words, ``uint256(1, 0)``, as the class converts
it, ``uint256(-1)``, the largest, can rule out, ``_Size <= 1`` returning, and a test of a member of an
element, ``_Sizes[_Size].m_bEnabled``, tells nothing of its index. A
condition that changes the value after it reads it, like ``_Value-- != 0``
or ``_Value != 0 ? (_Value = 0, true) : false``, doesn't check what it is
after, while one that changes it first, like ``++_Value`` or ``(_Value =
_Other) != 0``, tests the new value.

A negation, ``if (-_Value)``, checks the value, but a complement, ``if
(~_Value)``, which is zero when the value is all ones, doesn't.

The complement of a value of the argument's type, like
``CEntryType(~Entry)``, is checked by comparing the value with all ones, like
``Entry != mcp_FullySet``, also by the operators of an integer class, also
with a constant of it, ``gc_Zero``, also one made of an integer,
``_Bits < CInt(10)``, which is that integer, unless what a constant one makes
is zero, ``CByte(256)``, also one made of several words,
``CInt(0, 0)``, which compares with zero like its signed word, of a signed
one, when that is negative, and else like its first word that isn't zero, and a conversion to ``bool`` tells of the
value only when it is zero when the argument is, not of ``_Value - CInt(1)``,
which
must not change what they compare, like the others, also through another name
of it, like a global it is, ``g_Value.m_Bits = 0``.

A template is checked as each of its instantiations makes it, where the
types and constants that tell are known: a call there is reported when an
instantiation doesn't check its argument, like
``fg_GetLowestBitSetNoZero<uint8>(_Value)`` of an ``unsigned``, or calls an
operator on it that can change it, like an ``operator!=`` that clears it, and a
template that isn't instantiated isn't checked, nor is a call in a branch of
an ``if constexpr`` that is discarded. A call in a template argument or an
array bound is evaluated and checked; one in ``sizeof``, ``decltype``,
``noexcept`` or ``__builtin_constant_p`` isn't. What the left side of an assignment changes,
``fg_Slot(_Bits) = fg_GetLowestBitSetNoZero(_Bits)``, runs after its right
side. What the right side of ``&&`` or ``||`` changes, like
``_Value && fg_Clear(_Value)``, undoes what the left side tested, and so does
giving on a pointer to it, like ``fg_Clear(_pValue)``, also read through
another, ``fg_Clear(*_ppValue)``, or to a member function through a pointer to
it, ``(_Worker.*fClear)(_Value)``, also by a const reference to what it writes
through a mutable member, also of a member function or a constructor,
``_Worker.f_Clear(_Info)``, also called through an unchanged local pointer to
it, ``auto fClear = &fg_Clear; fClear(_Info);``, or a local pointer to const whose const is cast
away, ``*const_cast<unsigned *>(pBits) = 0``, and a member function of the object that
isn't const, ``f_Clear()``, also through a pointer to one,
and a function that writes a global, also called through an unchanged local
pointer to it, ``auto fClear = &fg_Clear; fClear();``, also a copy of one,
``auto fCopy = fClear; fCopy();``, also given a lambda
that writes it, ``fInvoke(fClear)``,
``(_Worker.*fClear)()``, changes its members, and so does a const one that can
change what is mutable, ``mutable unsigned m_Bits``, also of a base or of a
member, or through a local reference to it, ``auto &Bits = m_Bits``, also
given on, ``fg_Clear(Bits)``, or atomically, ``__atomic_store_n(&m_Bits, 0,
...)``, what a
member pointer points to, ``*m_pBits`` or ``m_pBits[0]``, also through a local
copy of it, ``auto pBits = m_pBits``, or an element of an array of them,
``*m_pBits[0]``, or what a reference member refers to, also by giving it to
what takes a reference or pointer that isn't to const (not a value,
``fg_Observe(m_Bits)``, nor a pointer to a function whose body only gives it
back, also decoded, ``CLink::fs_GetPtr(m_pRoot)``), also through another const member function of the
object, or of a member of it, ``m_Inner.f_Clear()``, also through a local
reference to it, it calls, unless its body
can be seen not to (not what doesn't run, like ``sizeof(m_Bits = 0)``, a
discarded branch of an ``if constexpr`` or a lambda it only makes), and one
defined elsewhere can change either. One that can be seen to change only
mutable members, ``++m_nQueries``, changes only those, not another one that
was tested. A function given it by a reference or a pointer to const whose
body can be seen, also through the functions it gives it on to, changes what
it writes of its mutable members, ``_Info.m_Bits = 0`` (not
another one, ``++_Info.m_nQueries``), and
one given it by a reference to const or by value what it writes through its
reference or pointer members, ``_View.m_Bits = 0`` of ``unsigned &m_Bits``,
also by subscript, ``_View.m_pBits[0] = 0``, also a member function or a
constructor given it, and the destructor of a copy given by value, which runs
when the function returns, ``~CClear() { m_Bits = 0; }``, also through the
functions it gives the copy on to, ``fg_Clear(_View)``, but not in a branch of
an ``if constexpr`` that is discarded. A function whose body can be seen
changes a global or a static member it writes, also through the functions it
calls, ``void fg_Clear() { g_Bits = 0; }``, as the function it names, not an
override of a virtual one, which, like a function whose body can't be seen,
isn't looked into. A lambda given to a function
whose body can be seen to call it, ``fg_Invoke([&] { _Bits = 0; })``, also by
name, ``_fWork.operator()()``, also by giving it on to another function that
does, ``fg_Invoke(_fWork)``, writes what it writes, but not when the function
only makes a lambda that calls it, ``return [_fWork] { _fWork(); };``, unless
that one is called where it is made, nor where it isn't evaluated,
``noexcept(_fWork())``. A pointer through a local
reference to it, ``*pAlias`` of ``unsigned *&pAlias = pBits``, is that pointer,
and the elements of a braced list run in order, so later ones don't change
what an earlier one reads. A test is also told
through ``static_cast<bool>(_Value)``, a conversion to floating point,
``static_cast<double>(_Value)``, a conversion to an integer that isn't
narrower, ``static_cast<unsigned>(_Value == 0)``, a conditional,
``_Value ? false : true``, by what each value tells with the condition that
chose it, and the operators of an integer class,
also ``||`` and ``&&``, which tell of each side like the built-in ones,
unless either side changes the value, since both run in either order, and
so does a comparison, ``_Bits > fg_Clear(_Bits)``, but not a side that is
what it is changed to, ``--_Value != 0``,
like ``(_Value & _Mask) != 0``, also rewritten, like ``!=`` through
``operator==``, or a relational one through ``operator <=>``, which must be
const and take what they are given by value or by a reference to const: one
that can change it, like an ``operator bool`` that isn't const, or a const
one that changes what is mutable, or one that writes it through a global, also
rewritten, doesn't tell what it is after. Pointing
the pointer that a tested member or element is read through elsewhere,
``_pInfo = _pOther`` or ``++_pInfo``, also through a reference to the
pointer, ``pAlias = _pOther`` with ``CInfo *&pAlias = _pInfo``, also a
pointer member, ``_View.m_pInfo = _pOther``, undoes the
test of ``_pInfo->m_Bits`` or ``_pInfo[0]``, but not of a reference bound to it
before, ``umint const &Bits = _pInfo->m_Bits``, which a test of
``_pInfo->m_Bits`` then doesn't check either, and so does changing an index
it was bound through, ``++Index`` of ``unsigned const &Bits = Values[Index]``
(pointing it elsewhere before the
reference is bound, or after the call outside a loop around it, doesn't
matter). An argument that changes the value, ``--_Value``, isn't
what a test of the same expression tested, and neither is a member of a base
that is another object of the same class, ``_Info.CVirtual::m_Bits`` of a
virtual base beside ``_Info.CDirect::m_Bits`` of a base that isn't. A comparison with a constant that
isn't zero tells whether zero passes it, like ``_Value != 1`` or ``_Value <
10`` that it can, and ``_Value > 10`` that it can't, also a floating point
one, ``_Value < 0.5`` that it can, also through the
operators of an integer class. So does a comparison of unsigned values with
what isn't constant, ``_Value <= _Other`` that zero always passes and ``_Value
> _Other`` that it never does, also with what is never zero, ``_Value >=
(_Other | 1)``; one that can't tell, like ``_Value == _Other``, is taken as a
check.

A test saved in a local boolean that isn't changed, ``const bool bNonZero =
_Value != 0; if (bNonZero)``, is that test, when nothing between them changes
the value and no loop around the use runs it again; what isn't evaluated,
``noexcept(_Value != 0)``, tests nothing. A test that tells the value is zero
where the call runs, like ``if (_Value ==
0)``, also with a floating point zero, ``_Value == 0.0``, ``DMibCheck(_Value == 0)`` or the other side of ``if (_Value)``, doesn't
check it, and neither does arithmetic tested as a boolean that can be other
than zero when the argument is, like ``if (_Value | 1u)`` or ``if (_Value -
1)``. A call in a lambda called where it is made, ``[&] { return
fg_GetLowestBitSetNoZero(_Bits); }()``, also given arguments that change
nothing, ``(0)``, runs where the lambda is, so the tests
around the lambda check it, but not one in a lambda held and called later,
which a test around where it is made doesn't. A lambda that is called writes what it writes, also through its call operator
called by name, ``fClear.operator()()``, also one held in a local, what it
holds where it is called, also one assigned to it, ``fWork = [&] { _Bits = 0;
};``, also in a function wrapper, one a function made and gave back, ``auto
fClear = fg_MakeClear(_Bits)``, when its body writes what it captures, the
call operator of a lambda a class takes as its own, ``fg_MergeFunctors([&](int)
{ _Bits = 0; }, ...)``, an element of a local container given one, ``Work[0]()``
or through a range loop, what a local optional was given, ``(*fClear)()``,
also through a getter, ``fClear.f_Get()()``, a member of a local made with
one, ``State.m_fClear()`` of ``CState State{[&] { _Bits = 0; }}``, but not a const member function bound to an object,
``_pState->*&CState::f_Read``, also by a function that binds it,
``fg_MemberFunctionFunctor(&CState::f_Read, &_State)``, that can't change it,
a copy of one or through a pointer to one, ``(*pClear)()``, and a local
pointer pointed elsewhere before a write, ``pValue = &Other; *pValue = 0;``,
doesn't write the argument, unless a loop around it points it there again
after. A pointer that a lambda captures, ``[pValue = &_Value]``, also a copy
of a smart pointer to it, ``[pState = _pState]``, can write it,
but a lambda that captures the value by copy, ``[_Value]() mutable { _Value =
0; }``, also through a local reference to it, ``[Value]`` of ``umint &Value =
_Value``, also one called where it is made, changes only its own copy (but
not what a reference or pointer member of the copy shares, ``[_View] {
_View.m_Bits = 0; }`` of ``umint &m_Bits``),
and a member of a union shares its storage with the others, which then can
change it, though a test of one doesn't check another. A call in a macro used
in templates is told for each place the macro is used, and a call in a macro
is reported where the macro is used, ``DScan(_Bits)``, whose argument and
checks around it tell, not in the macro. An argument that
reads what is volatile, ``unsigned volatile &_Bits``, reads it again, which no
test of an earlier read checks. An element is told by how it is written: a
test of ``_pBits[1]`` doesn't check ``*(_pBits + 1)``, nor ``*pBits`` of
``pBits = _pBits + 1``. A test it can't
tell the direction of, like a call, is taken as a
check.

.. code-block:: c++

  umint fg_Bits(umint _Value)
  {
    if (_Value == 0)
      return 0;
    return fg_GetHighestBitSetNoZero(_Value) + 1; // checked
  }

Options
-------

.. option:: Functions

   Semicolon-separated list of the functions to check. None are built in; the Malterlib options file (:doc:`/clang-tidy/MalterlibOptionsFile`) adds Malterlib's, `fg_GetHighestBitSetNoZero;fg_GetLowestBitSetNoZero;fg_MostSignificantBitNoZero;fg_LeastSignificantBitNoZero`.
   The platform scans that implement them, like
   ``NPlatformHelpers::fg_GetHighestBitSet32NoZero``, are not in it: they are
   reached only through the scans in it, whose callers are checked.

.. option:: CheckMacros

   Semicolon-separated list of assertion macros that check the argument.
   Built in is `assert`, and the Malterlib options file (:doc:`/clang-tidy/MalterlibOptionsFile`) adds `DMibCheck;DMibFastCheck;DMibSafeCheck;DMibRequire`.

.. option:: AssumeMacros

   Semicolon-separated list of macros that state a fact for the analysis, and
   expand to ``static_cast<void>(sizeof(!!(Condition)))``. None are built in; the Malterlib options file (:doc:`/clang-tidy/MalterlibOptionsFile`) adds `DMibAnalyzerAssume`.
