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
``static``. A local variable or a structured binding, which has no storage
prefix, doesn't start with a type prefix followed by a capital or a digit,
``C``, ``IC``, ``TC``, ``TIC``, ``F``, ``TF`` or ``E``, so it doesn't read as
the name of a type, like ``TCPv4`` as a class template, ``EJsonUserBlock`` as
an enum or ``C1`` as a class; ``Count``, ``Format``, ``Entry`` and ``Chroma1``
are fine. The placeholder ``_`` of what is ignored, ``auto [Ticket, _, _] =
...``, which C++26 lets repeat, is no name and isn't checked. Groups that underscores separate, like ``m_Category_Value``, are
upper camel case too, or a built-in type of Malterlib, like ``fp32``, and a
name doesn't end with an underscore. A block scope ``extern`` declaration
declares a global, and is named like one.

The constant prefixes, ``c_``, ``gc_``, ``mc_`` and ``mcp_``, are for
``constexpr`` variables, also references, and only for them, also when only
another declaration is ``constexpr``, like the definition of ``extern const
int gc_Value;``, ``constexpr int gc_Value = 42;``, or of a static member
initialized in its class, ``constexpr int CValues::mc_Value;``. A ``const``
global declared before it is defined, ``extern const int gc_Value;``, also a
reference, ``extern int &gc_Reference;``, and a static member declared
``const`` in its class without its value, ``static CStr const mc_Prefix;``,
also a reference to ``const``, ``static CStr const &mc_Empty;``, whose
definition in one source, ``constexpr CStr CKey::mc_Prefix = ...;``,
makes it once instead of in every translation unit that includes the header,
take
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

.. example-before
   using umint = unsigned long;

.. code-block:: c++

  struct CCounter // fine
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

.. example-check

  int g_Value = 0; // fine
  int Value = 0; // warning: a global has the 'g_' prefix
  constexpr int gc_Value = 1; // fine
  const int gc_Count = 42; // warning: const, but not constexpr
  constexpr int g_Limit = 1; // warning: constexpr, so 'gc_'
  static int gs_Value = 0; // fine
  struct CMembers
  {
    int Value; // warning: a member has the 'm_' prefix
    static int ms_Count; // fine
    static constexpr int mc_Limit = 4; // fine
    static int mc_Wrong; // warning: not constexpr
    static int const mc_Defined; // fine: defined elsewhere, either prefix
  private:
    int m_Private; // warning: a private member has the 'mp_' prefix
  };
  void fg_Locals()
  {
    int m_Local = 0; // warning: a local has no prefix
    static int s_Count = 0; // fine
    constexpr int c_Size = 4; // fine
    try
    {
    }
    catch (int _Error) // fine: the variable of a catch has the '_' prefix
    {
    }
  }

Options
-------

.. option:: MinimumNameLength

   The least number of characters a name has after its prefixes, like
   `Value` of `m_pValue`. Default is `2`.
