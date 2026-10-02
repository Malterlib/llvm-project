.. title:: clang-tidy - malterlib-macro-naming

malterlib-macro-naming
======================

Checks that macro names have the ``D`` prefix followed by upper camel case,
and that the parameters of function-like macros have the ``d_`` prefix
followed by upper camel case and optionally concept or type prefixes
(``d_bFlag``, ``d_CType``).

.. code-block:: c++

  #define DMibCheck(d_Condition) ...

Macros without upper case letters emulate language features
(``inline_always``), so their names are not checked, but their parameters are.
Macros without lower case letters configure external code (``NOMINMAX``,
``_GNU_SOURCE``), so they are not checked. A parameter is at least
`MinimumNameLength` characters long after its prefixes, ``d_Value`` but not
``d_X``.

Options
-------

.. option:: MinimumNameLength

   The least number of characters a macro parameter has after its prefixes,
   like `Value` of `d_Value`. Default is `2`.
