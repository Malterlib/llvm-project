.. title:: clang-tidy - malterlib-coawait-precedence

malterlib-coawait-precedence
============================

Finds ``co_await X % "Message"``. ``co_await`` binds tighter than ``%``, so the
message is applied to the result of the awaiting instead of to the future
whose errors it describes.

.. code-block:: c++

  CStr Data = co_await fg_Read() % "Error reading";    // warning
  CStr Data = co_await (fg_Read() % "Error reading");  // fixed

The fix parenthesizes the operand of ``co_await``. A ``co_await`` that is
itself parenthesized, ``(co_await X) % Y``, says what it means and is not
reported, nor is the remainder of a number that is a class by one of its type,
``co_await fg_Read() % Divisor`` with a ``TCInt Divisor``.

``co_await`` in a ``catch`` handler is not checked, since the compiler already
rejects it; use ``g_CaptureExceptions`` instead.

A template is checked as each of its instantiations makes it, and one that
isn't instantiated isn't checked.
