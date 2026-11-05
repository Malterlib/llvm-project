.. title:: clang-tidy - malterlib-parameter-naming

malterlib-parameter-naming
==========================

Checks that parameter names have the prefix of the Malterlib coding standard,
followed by upper camel case and optionally one concept prefix (``_bFlag``).
Each parameter is named, also one that is unused, and one of a function type,
like ``TCFunction<void (CStr _Message)>``, so that what it is can be told; only
the ``int`` that tells a postfix ``operator ++`` or ``--`` apart is left
unnamed. An explicit instantiation only names a specialization that exists,
``template TCValue<long>::TCValue(long &&);``, also with ``extern`` or a
calling convention, ``template void __cdecl fg_Run<int>(int);``, so the
parameters of the function it names aren't checked; those of a function type
among its template arguments are. What an instantiation declares is checked in the
template; what is specialized for an instantiated class is written as it is
and checked, also a requirement in the initializer of a static data member
specialized so, ``requires(int _Value) { ... }``, and a lambda in an
enumerator of a member enum specialized so,
``template <> enum TCBox<int>::EMode : int { EMode_Value = [](int _Value) {
... }(0) };``.

=========== ==============================================================
Prefix      Parameter
=========== ==============================================================
``_``       Function parameter
``p_``      Function parameter pack
``o_``      Output parameter
``po_``     Output parameter pack
``t_``      Template parameter of a class, alias, variable or concept
``tp_``     Template parameter pack
``tf_``     Template parameter of a function template or generic lambda
            (not of a deduction guide, which is named like its class
            template)
``tfp_``    Template parameter pack of a function template
=========== ==============================================================

Whether a parameter is an output is what its author means by it, a concept
of its name like the others, not what the function does with it: a parameter
that the function can write through can be named either way, ``_Value`` or
``o_Value``, also one it only reads or one it writes, also a pointer to an
Objective-C object, ``CObject *o_pObject``. Only a parameter that
can't be written through can't be named as an output (``o_`` or ``po_``):

- A value, like ``int _Value`` or ``CStr _Text``, also of a template
  parameter, ``tf_CType _Value``, which is deduced as a value (but a type a
  template is given that can be a reference, ``typename
  tf_CTraits::CReference o_Value``, can be an output, and so can an object of
  a class that refers to what it can be written through, one of whose const
  member functions, also const through an explicit object, ``int
  &f_Get(this CWrapper const &_Self)``, or a base's, gives what can be written through, as a
  parameter of its type can be: a reference, also an rvalue one, or a pointer
  to what can be written, also through const pointers, ``int *const &f_Get()
  const``, an Objective-C object pointer, or an object of such a class, like
  ``TCReference<CActorSubscription> o_Subscription`` with ``f_Get() const``,
  whatever its access, also of a private base, hidden, deleted or
  constrained, as ``o_`` is only rejected for what definitely can't be
  written; of a specialization the declaration doesn't need complete, which
  clang doesn't instantiate, ``TCReference<int const> o_Value``, or with a
  member function template, also one whose explicit object depends on its
parameters, ``int &f_Get(this tf_CSelf &&_Self)``, or a result that depends
on what isn't known, or
  one deduced only when it is used, ``decltype(auto) f_Get() const``, it
  can't be told from what is instantiated, so it can be named either way).
- A const reference to a scalar, like ``int const &_Value``, or to an array
  of them, ``int const (&_Values)[2]``.
- A pointer to const scalars, like ``int const *_pValue``, also through const
  pointers, ``int const *const *_pValue``, or to an array of them (but not of
  objects of a class, ``CPromise const (*o_pPromises)[2]``, which their
  mutable members can make outputs).
- A function, also by reference, ``void (&_fCallback)()`` or ``void
  (&&_fCallback)()``.

What a type that a template is given is, ``typename tf_CTraits::CReference
o_Value`` or ``t_CReference o_Value``, also what a reference or pointer to it
refers to, ``tf_CType const &o_Value``, which can be a const scalar, is told
by what clang instantiated:
it can be an output when an instantiation can be written through, and can't
when instantiations can't; one nothing instantiates can be, also a type
parameter of the function template itself taken by value, ``tf_CType
o_pValue``, which can be deduced as a pointer. One of a function type, ``using
FCallback = void (t_CType o_Value)``, has no instantiation of its own, only
the types it is used with, so it can be.

What a const object of a class can be written through, its const member
functions, like a promise that is given its result through
``TCPromise<tf_CType> const &o_Promise``, can make it an output, and so can
a const pointer to a non-const object, ``int *const &o_pValue``, also an array
of them, and a pointer that reaches what isn't const through const pointers,
``int *const *o_pValue``.

The name of a type template parameter continues with a type prefix: ``C``,
``IC``, ``F`` or ``E`` (``t_CType``, ``tf_FFunction``), not the prefix of a
template (``t_TCStrTraits``), which the name of a template template parameter
has (``t_TCTemplate``, ``t_TICInterface``, ``t_TFCallback``), not a bare ``T``
(``t_TVector``).

.. code-block:: c++

  template <typename tf_CType, typename ...tfp_CParams>
  bool fg_Parse(tf_CType const &_Value, umint &o_iPosition, tfp_CParams &&...p_Params);

The parameters of functions with C linkage are checked like any other: their
names are neither the function's symbol nor part of its ABI, and only the
sources of the repositories the checks are enabled for are checked, so the
declarations of system headers and libraries aren't.

Options
-------

.. option:: MinimumNameLength

   The least number of characters a name has after its prefixes, like
   `Value` of `_pValue`, also of template parameters after their type
   prefixes, like `Type` of `t_CType`. Default is `2`.
