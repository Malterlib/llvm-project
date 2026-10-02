.. title:: clang-tidy - malterlib-parameter-naming

malterlib-parameter-naming
==========================

Checks that parameter names have the prefix of the Malterlib coding standard,
followed by upper camel case and optionally one concept prefix (``_bFlag``).
Each parameter is named, also one that is unused, and one of a function type,
like ``TCFunction<void (CStr _Message)>``, so that what it is can be told; only
the ``int`` that tells a postfix ``operator ++`` or ``--`` apart is left
unnamed.

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
  tf_CTraits::CReference o_Value``, can be an output).
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

The parameters of functions with C linkage in C++ are not checked.

Options
-------

.. option:: MinimumNameLength

   The least number of characters a name has after its prefixes, like
   `Value` of `_pValue`, also of template parameters after their type
   prefixes, like `Type` of `t_CType`. Default is `2`.
