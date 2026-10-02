.. title:: clang-tidy - malterlib-unused-parameter-cast

malterlib-unused-parameter-cast
===============================

Finds parameters silenced with a cast to void, like ``(void)_Param;``, also
more than once, ``(void)(void)_Param;``. An
unused parameter keeps its name, which tells what it is, and is left alone:
the builds do not warn about unused parameters, only about unused variables.

.. code-block:: c++

  void fg_Function(int _Value)
  {
    (void)_Value;
  }

  // Fixed
  void fg_Function(int _Value)
  {
  }

A cast in the initializer of an ``if``, a ``switch`` or a loop,
``switch ((void)_Value; _Bits)``, is reported too, without a fix.

The fix removes the cast. A cast that is the body of an ``if`` or a loop,
or follows a label, is reported without a fix, since removing it would leave
them without a statement, and so is one that a ``return`` of a void function
returns, like ``return (void)_Value;``, also ``co_return (void)_Value;``, or
that is the last statement of a statement expression, ``({ 42; (void)_Value;
})``, whose type it gives, or that is part of a comma
expression, like ``(void)_Value, (void)_Other;``. A cast of a parameter of a
class or enumeration on a side of a comma whose other side isn't cast too,
``((void)_Value, 42)`` or ``fg_Work(), (void)_Value``, also of a scalar whose
other side is a class or enumeration, ``(void)_Value, _Other``, or of the
pattern of a fold over a comma with a value that isn't cast too,
``((void)_Values, ..., 42)``, picks the built-in comma over an overloaded one
and isn't reported. A cast whose text spans files, like one whose operand
is in an included file, is reported without a fix.

A cast in a lambda or block of a parameter of a function around it isn't
reported: naming the parameter makes a default capture copy it, like
``(void)_Subscription;`` in a ``[=]`` lambda that keeps a subscription alive
in its closure.
