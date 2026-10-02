.. title:: clang-tidy - malterlib-variable-naming

malterlib-variable-naming
=========================

Checks that variable names have the storage prefix of the Malterlib coding
standard, followed by upper camel case and optionally one concept prefix
(``m_bFlag``, ``pValue``).

=========== ==============================================================
Prefix      Variable
=========== ==============================================================
            Local variable, structured binding and init capture
``c_``      ``constexpr`` local variable
``s_``      Static local variable
``g_``      Global variable, also in an anonymous namespace
``gc_``     ``constexpr`` global variable
``gs_``     Global variable declared ``static``
``m_``      Member variable
``mc_``     ``constexpr`` static member variable
``ms_``     Static member variable
``mp_``     Private or protected member variable
``mcp_``    Private or protected ``constexpr`` static member variable
``msp_``    Private or protected static member variable
=========== ==============================================================

A member of an anonymous union outside of a class is named as a variable of
the scope it is in: a local in a function, ``gs_`` at namespace scope when it
is ``static``, ``static union { int gs_Value; };``, and ``g_`` in an anonymous
namespace, ``namespace { union { int g_Value; }; }``. A
structured binding is named for the storage of what it decomposes: like a
local variable, as a constant only when it is ``constexpr``, or
with ``g_``,
``gs_`` or ``gc_`` at namespace scope and ``s_`` or ``c_`` when it is
``static``. Groups that underscores separate, like ``m_Category_Value``, are
upper camel case too, or a built-in type of Malterlib, like ``fp32``, and a
name doesn't end with an underscore. A block scope ``extern`` declaration
declares a global, and is named like one.

The constant prefixes, ``c_``, ``gc_``, ``mc_`` and ``mcp_``, are for
``constexpr`` variables, also references, and only for them, also when only
another declaration is ``constexpr``, like the definition of ``extern const
int gc_Value;``, ``constexpr int gc_Value = 42;``, or of a static member
initialized in its class, ``constexpr int CValues::mc_Value;``. A ``const``
global declared before it is defined, ``extern const int gc_Value;``, also a
reference, ``extern int &gc_Reference;``, takes
either prefix there, and is checked where the definition is, also when the
declaration is in a header that isn't checked. A
``const`` variable that isn't, also one
made by a constant expression, ``const int gc_Count = 42``, as was written
before ``constexpr`` was, is reported for its constant prefix: declare it
``constexpr``, or name it with the plain prefix when it can't be one. An
explicit or partial specialization of a variable template is checked by
itself, ``template <> inline int gc_Value<void> = 42;`` is reported also when
the template is ``constexpr``.

The variable of a ``catch`` has the ``_`` prefix of a parameter, and an init
capture of a pack is named like any other local, not with the ``p_`` prefix of
a parameter pack. The identifier of
a locale facet, ``static std::locale::id id``, which the standard library looks
up by that name, isn't checked.

.. code-block:: c++

  struct CCounter
  {
    static constexpr umint mc_MaxCount = 10;
    umint m_nCount = 0;

  private:
    bool mp_bStopped = false;
  };

  void fg_Count()
  {
    static CCounter s_Counter;
    umint nItems = 0;
  }

Members of specializations of templates from system headers, like
``std::tuple_size<CType>::value``, and variables with C linkage in C++ are
not checked.

Options
-------

.. option:: MinimumNameLength

   The least number of characters a name has after its prefixes, like
   `Value` of `m_pValue`. Default is `2`.
