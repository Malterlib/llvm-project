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

In a source, a class declares its members and their bodies follow it, as a
header and its ``.hpp`` do: a member function or a friend that is defined in
its class definition in one of :option:`OutOfClassFileExtensions` is reported,
also of a nested class or a class template, but not one that is defaulted or
deleted, nor those of a class local to a function, also an Objective-C method
or a block, nor lambdas. The source is the one the body is in, also one the
class includes its members from, and it is reported there unless its name is
in the same file. A body is in a source by where its braces are only: its
opening brace, or, when an include gives that, its closing brace, of a
function try block those of its block and of its last handler, not its
``try``, of the braces written where they are used, also in an argument of a
macro, not those a macro's definition gives, also when a macro gives the rest
of the body; not by statements written between them. A file whose extension is also one of
:option:`DeclarationFileExtensions` is left to that rule, which reports any
definition in it. What of such a body a declaration file has is reported too,
which defining it after the class would leave there.

Options
-------

.. option:: DeclarationFileExtensions

   Semicolon-separated list of the extensions of files that hold declarations
   only, without the dot. Default is `h`.

.. option:: OutOfClassFileExtensions

   Semicolon-separated list of the extensions of the sources whose classes
   define no member functions in their definition. Default is
   ``cpp;cc;cxx;mm``.
