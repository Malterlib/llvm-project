// UNSUPPORTED: system-windows
// RUN: %python %S/Inputs/ownership-forms.py direct > %t-direct.cpp
// RUN: clang-tidy %t-direct.cpp '-checks=-*,malterlib-coroutine-reference-capture,malterlib-coroutine-unsafe-this' -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" -- -std=c++23 -I %S/Inputs 2>&1 | sed -n -E 's/^.*[.]cpp:([0-9]+:[0-9]+): warning: .*[[](malterlib-[a-z-]+)[]]$/\1 \2/p' > %t-direct.txt
// RUN: %python %S/Inputs/ownership-forms.py alias > %t-alias.cpp
// RUN: clang-tidy %t-alias.cpp '-checks=-*,malterlib-coroutine-reference-capture,malterlib-coroutine-unsafe-this' -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" -- -std=c++23 -I %S/Inputs 2>&1 | sed -n -E 's/^.*[.]cpp:([0-9]+:[0-9]+): warning: .*[[](malterlib-[a-z-]+)[]]$/\1 \2/p' > %t-alias.txt
// RUN: %python %S/Inputs/ownership-forms.py copy > %t-copy.cpp
// RUN: clang-tidy %t-copy.cpp '-checks=-*,malterlib-coroutine-reference-capture,malterlib-coroutine-unsafe-this' -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" -- -std=c++23 -I %S/Inputs 2>&1 | sed -n -E 's/^.*[.]cpp:([0-9]+:[0-9]+): warning: .*[[](malterlib-[a-z-]+)[]]$/\1 \2/p' > %t-copy.txt
// RUN: %python %S/Inputs/ownership-forms.py forward > %t-forward.cpp
// RUN: clang-tidy %t-forward.cpp '-checks=-*,malterlib-coroutine-reference-capture,malterlib-coroutine-unsafe-this' -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" -- -std=c++23 -I %S/Inputs 2>&1 | sed -n -E 's/^.*[.]cpp:([0-9]+:[0-9]+): warning: .*[[](malterlib-[a-z-]+)[]]$/\1 \2/p' > %t-forward.txt
// RUN: %python %S/Inputs/ownership-forms.py convert > %t-convert.cpp
// RUN: clang-tidy %t-convert.cpp '-checks=-*,malterlib-coroutine-reference-capture,malterlib-coroutine-unsafe-this' -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" -- -std=c++23 -I %S/Inputs 2>&1 | sed -n -E 's/^.*[.]cpp:([0-9]+:[0-9]+): warning: .*[[](malterlib-[a-z-]+)[]]$/\1 \2/p' > %t-convert.txt
// RUN: %python %S/Inputs/ownership-forms.py value > %t-value.cpp
// RUN: clang-tidy %t-value.cpp '-checks=-*,malterlib-coroutine-reference-capture,malterlib-coroutine-unsafe-this' -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" -- -std=c++23 -I %S/Inputs 2>&1 | sed -n -E 's/^.*[.]cpp:([0-9]+:[0-9]+): warning: .*[[](malterlib-[a-z-]+)[]]$/\1 \2/p' > %t-value.txt
// RUN: %python %S/Inputs/ownership-forms.py rvalue > %t-rvalue.cpp
// RUN: clang-tidy %t-rvalue.cpp '-checks=-*,malterlib-coroutine-reference-capture,malterlib-coroutine-unsafe-this' -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" -- -std=c++23 -I %S/Inputs 2>&1 | sed -n -E 's/^.*[.]cpp:([0-9]+:[0-9]+): warning: .*[[](malterlib-[a-z-]+)[]]$/\1 \2/p' > %t-rvalue.txt
// RUN: diff %t-direct.txt %t-alias.txt
// RUN: diff %t-direct.txt %t-copy.txt
// RUN: diff %t-direct.txt %t-forward.txt
// RUN: diff %t-direct.txt %t-convert.txt
// RUN: diff %t-direct.txt %t-value.txt
// RUN: diff %t-direct.txt %t-rvalue.txt
// RUN: FileCheck %s --input-file=%t-direct.txt --implicit-check-not=malterlib-

// Each check that tells whether what an owner owns is alive where a closure
// uses it reports the same of each way the closure takes the owner
// (Inputs/ownership-forms.py): directly, through an unchanged local
// reference, by a copy function, a forward, a converting copy, and from a
// parameter by value or by `&&`, line for line, so that the ways can't tell
// apart again. What a shared owner, a moved unique one and a moved buffer one
// keep is clean, also through a copy of the owner taken before or after the
// reference, the same allocation, a view of what it owns captured by value,
// an object holding a pointer into it, and a member of the object a shared
// owner of it is captured with, also of the explicit object; and so is the
// object of a class of CAllowUnsafeThis, through `this`, a reference and a
// pointer parameter, what an actor's helper given its own object refers to,
// and a structured binding of an actor's member. The owner is a local, a
// member of `this`, of a local, of a member or of the explicit object, or a
// structured binding; what it holds is what reaches the reference and the
// capture: assigned before the reference, a buffer changed before the
// pointer into it, a copy the reference is made through released after it,
// a copy moved into the closure, the owner released after the closure takes
// it, and `f_ShareAsConst()`, are clean. Reported: what an owner the closure
// doesn't take owns, an owner captured by reference, what a reference
// parameter refers to, a release or another allocation between the reference
// and the capture, a member of `this` a member function can change between
// them, a pointer into the allocation of an earlier pass of a loop, and an
// object holding a pointer into a local. Round 163: the slot only needs to
// be unchanged between the reference and the capture, whatever reaches it
// (assigned on both or one branch before the reference); an owner in a
// record captured whole, a capture of the closure around, a tuple-like
// structured binding, an element of a container, what a getter or a forward
// gives back are slots too; reported: a change or a release on one path
// between them, a change of the container, and a suspension between them of
// a member of `this`. Round 165: a loop body, each pass its own owner; owner
// paths through a local pointer to a record, to `this`, and one a function
// gives; copies of a record holding the owner; a sibling member written and
// a member function that doesn't change the owner, also a final override;
// an identity comparison of `this`; and a closure made in a closure, are
// clean; reported: a call that can run an override, a release in the same
// pass, a write through the pointer to the record, and the owner of a copy
// reassigned before the capture. Round 166: an exchange, a const alias, a
// record moved by a cast, an extended temporary, a borrow in the closure's
// own captures, and a record holding a shared owner of `this` whose other
// member the closure writes, are clean; what is borrowed after the value is
// moved out is reported. Round 166, owners values hold: a container of
// shared owners copied or moved whole, an array copied, an array member, an
// element of an array, a reference bound to the element at an index or of a
// key, the variable of a loop over a range, a copy and a reference, and a
// record made in place with the owner, captured and assigned, are clean, and
// so is a record holding a shared owner of `this` made in place; an element
// of the container changed between, the container cleared, an element at
// another index assigned where the reference is bound at an index, the
// member of the record reassigned, and what is borrowed from a container
// after it is moved out are reported. Round 167: a holder whose constructor
// copies the owner, a base holding it, a cast to a const reference, an
// element an accessor gives and one a lookup gives bound to a reference, a
// getter's owner with its holder captured, an owner swapped into the
// captured slot, and containers of records holding each other, told in
// either order, are clean; a constructor that doesn't keep the owner, a base
// member reset between, a swap that moves the owner away from the captured
// slot, and the element erased between are reported. Round 168: holders
// whose constructor assigns the owner in its body, keeps it by a converting
// copy, takes it from a member of a parameter, through a constructor it
// delegates to or a base's constructor, also captured from a local, a
// factory giving a holder made of it, records swapped and exchanged whole,
// an array made of owners, lookups through a pointer, an identity wrapper
// and a local pointer, the last element of a container moved whole, and a
// sharing accessor of an explicit object, are clean; a constructor that
// assigns it on one branch only, a base constructor that doesn't keep it, a
// record swapped into the captured one whose owner is then reset, and an
// element a local pointer points to whose container changes between, are
// reported. Round 169: a constructor storing a local copy of the owner,
// also moved, an inherited constructor, one that throws on another path, a
// factory returning a named local, and a helper giving the owner back, are
// clean; a constructor that throws and otherwise doesn't store, a local copy
// reset before it is stored, and an inherited constructor of a base that
// doesn't keep it are reported. Round 169, rest: a constructor keeping the
// owner in each element of an array member, sharing through a pointer to the
// owner, a getter given its holder by a reference that isn't const that
// doesn't change it, and a copy of a string whose copies share its buffer,
// are clean; a getter that resets the owner between, and the string changed
// between, are reported. Round 170: a holder's address given to a function
// that only reads through it, a getter that checks before it gives the
// owner back, and a member function that changes another member than the
// array holding the owner, are clean; a function that clears the owner
// through the address and a checked getter that resets it, between, are
// reported. Round 170, rest: functions giving the owner a member of what they
// are given is, by reference through a pointer, also a local one, and by
// value through a reference or a pointer, a setter a constructor calls, and
// a tuple whose elements are in bases copied whole, are clean; a function
// that clears the owner through the pointer before it gives it back, a
// setter that keeps it on one branch only, and the tuple's element reset
// between, are reported.

// Liveness by type: what owners own is alive where the closure holds a
// keeper of it by value, an owner whose type fits. Reported: what an owner
// captured by reference owns and what a reference parameter refers to.
// Reported too: an owner released, reassigned, swapped away or changed in a
// local slot, a member of the object or a member of a record copied into
// the closure between the borrow and the take, a record whose constructor
// doesn't store the owner on each way, a keeper of a new object, a record
// captured by value that holds a pointer into an allocation the closure's
// keeper doesn't own (288) or into a local (800), an element reset through a
// free getter that gives a part of the tuple it is given (1247) or through
// the `operator[]` of an array record (1372). Reported: a constructor whose
// body does nothing but initialize, which doesn't store the owner (772).
// Clean: a constructor given a constant `true` stores the owner, so the
// keeper keeps it (1239). Round 200: an owner reached only through a
// unique owner the closure takes keeps (1469); reported: the same through a
// shared owner (1475), and the nested owner reset by the closure (1481) or
// between the reference and the capture (1488).
// CHECK: 109:23 malterlib-coroutine-reference-capture
// CHECK-NEXT: 116:23 malterlib-coroutine-reference-capture
// CHECK-NEXT: 136:24 malterlib-coroutine-reference-capture
// CHECK-NEXT: 256:24 malterlib-coroutine-reference-capture
// CHECK-NEXT: 265:24 malterlib-coroutine-reference-capture
// CHECK-NEXT: 275:26 malterlib-coroutine-reference-capture
// CHECK-NEXT: 288:23 malterlib-coroutine-reference-capture
// CHECK-NEXT: 362:24 malterlib-coroutine-reference-capture
// CHECK-NEXT: 369:24 malterlib-coroutine-reference-capture
// CHECK-NEXT: 375:24 malterlib-coroutine-reference-capture
// CHECK-NEXT: 383:38 malterlib-coroutine-reference-capture
// CHECK-NEXT: 443:26 malterlib-coroutine-reference-capture
// CHECK-NEXT: 492:19 malterlib-coroutine-reference-capture
// CHECK-NEXT: 501:24 malterlib-coroutine-reference-capture
// CHECK-NEXT: 509:24 malterlib-coroutine-reference-capture
// CHECK-NEXT: 560:24 malterlib-coroutine-reference-capture
// CHECK-NEXT: 660:24 malterlib-coroutine-reference-capture
// CHECK-NEXT: 667:24 malterlib-coroutine-reference-capture
// CHECK-NEXT: 674:24 malterlib-coroutine-reference-capture
// CHECK-NEXT: 682:24 malterlib-coroutine-reference-capture
// CHECK-NEXT: 689:24 malterlib-coroutine-reference-capture
// CHECK-NEXT: 772:24 malterlib-coroutine-reference-capture
// CHECK-NEXT: 779:24 malterlib-coroutine-reference-capture
// CHECK-NEXT: 787:24 malterlib-coroutine-reference-capture
// CHECK-NEXT: 794:24 malterlib-coroutine-reference-capture
// CHECK-NEXT: 800:23 malterlib-coroutine-reference-capture
// CHECK-NEXT: 942:24 malterlib-coroutine-reference-capture
// CHECK-NEXT: 948:24 malterlib-coroutine-reference-capture
// CHECK-NEXT: 958:24 malterlib-coroutine-reference-capture
// CHECK-NEXT: 966:24 malterlib-coroutine-reference-capture
// CHECK-NEXT: 1025:24 malterlib-coroutine-reference-capture
// CHECK-NEXT: 1031:24 malterlib-coroutine-reference-capture
// CHECK-NEXT: 1037:24 malterlib-coroutine-reference-capture
// CHECK-NEXT: 1094:24 malterlib-coroutine-reference-capture
// CHECK-NEXT: 1101:24 malterlib-coroutine-reference-capture
// CHECK-NEXT: 1146:24 malterlib-coroutine-reference-capture
// CHECK-NEXT: 1154:24 malterlib-coroutine-reference-capture
// CHECK-NEXT: 1233:24 malterlib-coroutine-reference-capture
// CHECK-NEXT: 1247:24 malterlib-coroutine-reference-capture
// CHECK-NEXT: 1350:24 malterlib-coroutine-reference-capture
// CHECK-NEXT: 1356:24 malterlib-coroutine-reference-capture
// CHECK-NEXT: 1364:24 malterlib-coroutine-reference-capture
// CHECK-NEXT: 1372:24 malterlib-coroutine-reference-capture
// CHECK-NEXT: 1435:24 malterlib-coroutine-reference-capture
// CHECK-NEXT: 1443:24 malterlib-coroutine-reference-capture
// CHECK-NEXT: 1453:24 malterlib-coroutine-reference-capture
// CHECK-NEXT: 1475:24 malterlib-coroutine-reference-capture
// CHECK-NEXT: 1481:24 malterlib-coroutine-reference-capture
// CHECK-NEXT: 1488:24 malterlib-coroutine-reference-capture
