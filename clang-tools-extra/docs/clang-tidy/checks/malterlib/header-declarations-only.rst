.. title:: clang-tidy - malterlib-header-declarations-only

malterlib-header-declarations-only
==================================

Finds function definitions in ``.h`` files. A ``.h`` file holds declarations
and type definitions only, which keeps the interface compact. Template, inline
and ``constexpr`` function definitions go in ``.hpp`` files, and other
definitions in ``.cpp`` files.

.. code-block:: c++

  // Example.h
  struct CExample
  {
    void f_Run() {}       // warning
    void f_Stop();
    CExample() = default; // defaulted and deleted functions have no body
  };

Lambdas, and function bodies written in the definitions of macros, are not
checked; a body written in the arguments of a macro is, and so is one whose
statements are written in the file, also in an argument of the macro, while
a macro gives only its braces, or nested in statements that the macro writes,
and so is a constructor whose initializers are written in a ``.h`` that
includes its body, also a fragment in the middle of one, ``m_Value(1 +
#include "Fragment.h" + 3)``, and one whose initializers are written in the file while a
macro gives its body, also a part of one written in an argument of the macro,
``DCtor(fg_Make())`` of ``m_Value(1 + d_Value)``, and a body opened in a ``.hpp`` whose statements, also
nested in other statements, like an ``#include`` in the block of an ``if``, or
in an expression, ``return 1 + #include "Fragment.h"``, also the closing brace
of such a block, or
closing brace are in a ``.h`` it includes, where they are, but not types a
``.h`` included in a body defines, ``struct CLocal { ... };``, which aren't
implementation, nor types the arguments of a macro name, like the base of
what it defines, ``DErrorClass(CError, CParent)``, also where they qualify
what its bodies call, ``d_CParent::f_Value()``: only what is written from its
first to its last token in an argument is.

Options
-------

.. option:: DeclarationFileExtensions

   Semicolon-separated list of the extensions of files that hold declarations
   only, without the dot. Default is `h`.
