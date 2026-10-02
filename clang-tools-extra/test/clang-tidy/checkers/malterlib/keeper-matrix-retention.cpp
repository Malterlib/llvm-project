// UNSUPPORTED: system-windows
// RUN: %python %S/Inputs/keeper-matrix.py retention > %t.cpp
// RUN: %python %S/Inputs/keeper-matrix.py retention-expect > %t-expect.txt
// RUN: clang-tidy %t.cpp '-checks=-*,malterlib-coroutine-reference-parameter' -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" -- -std=c++23 -I %S/Inputs 2>&1 | sed -n -E 's/^.*[.]cpp:([0-9]+):[0-9]+: warning: .*[[](malterlib-[a-z-]+)[]]$/\1 \2/p' > %t-actual.txt
// RUN: diff %t-expect.txt %t-actual.txt

// Rule R, what keeps a reference parameter by type
// (Inputs/keeper-matrix.py): a TCFuture coroutine's reference parameter of
// an actor handle, a `shared`, `sharedbuffer`, weak, `unique` or `buffer`
// owner, an object or an int, given before the first suspension to an
// unseen function, one that returns a holder of a borrow, stored or
// discarded, one that returns an int or a future, a constructor of a class
// that can or can't hold a borrow, a receiver that can or can't, a function
// with another argument by non-const reference that can or can't, and a
// visible body that stores it in a static or only reads it; and a coroutine
// without a suspension that returns such a holder or class, also by the
// conversion `co_return` makes into one of another type. Each form that keeps
// an lvalue given it also as an xvalue and a prvalue, which are taken, never
// referred to. And an object
// given to a callee without a body of each form, decided by its type: a
// function pointer, a member function pointer, a function object with a
// const or non-const call operator, a const member function, and another
// argument by const or non-const reference or pointer. Reported exactly
// where it is kept by reference: by what can hold a borrow by type, the
// result stored or returned, the class, the receiver of a non-const member
// function, the other argument by reference or pointer to non-const, the
// body that stores it; never of what a callee copies, the handle and the
// `shared`, `sharedbuffer` and weak owners, never in the receiver of a const
// member function or call operator nor in another argument to const, and
// never by an unseen function that returns nothing, a known miss. Kept in a
// record the frame holds by value, a result, a receiver or another argument,
// it is used where the record is used after the suspension: one trivially
// destroyed and not touched after it is clean, the same one read after it,
// its kept member dereferenced or a member function called on it, is
// reported.
