.. title:: clang-tidy - malterlib-macro-naming

malterlib-macro-naming
======================

Checks that macro names have the ``D`` prefix followed by upper camel case,
and that the parameters of function-like macros have the ``d_`` prefix
followed by upper camel case and optionally concept or type prefixes
(``d_bFlag``, ``d_CType``).

.. code-block:: c++

  #define DMibCheck(d_Condition) ... // fine

.. example-check

  #define DCheckValue(d_Value) d_Value // fine
  #define CheckValue(d_Value) d_Value // warning: no 'D' prefix
  #define DESKTOP_ALL 1 // warning: capitals aren't upper camel case
  #define DCheckType(d_TYPE) d_TYPE // warning: a parameter in capitals
  #define DCheckID(d_ID) d_ID // fine: an acronym of two capitals
  #define DCheckShort(d_X) d_X // warning: shorter than MinimumNameLength
  #define inline_always inline // fine: emulates a language feature
  #define DCheckPrefix(Value) Value // warning: a parameter without 'd_'

Macros without upper case letters emulate language features
(``inline_always``), so their names are not checked, but their parameters are.
A name in capitals, also after the ``D`` prefix, like ``AVLKEY`` or
``DESKTOP_ALL``, is not upper camel case and is reported, and so is a parameter
in capitals after its prefixes, ``d_TYPE``; acronyms of up to two capitals,
``d_ID``, are upper camel case. One that configures external code, like
``NOMINMAX`` or ``_GNU_SOURCE``, has a ``NOLINT(malterlib-macro-naming)``. A
parameter is at least
`MinimumNameLength` characters long after its prefixes, ``d_Value`` but not
``d_X``.

Options
-------

.. option:: MinimumNameLength

   The least number of characters a macro parameter has after its prefixes,
   like `Value` of `d_Value`. Default is `2`.
