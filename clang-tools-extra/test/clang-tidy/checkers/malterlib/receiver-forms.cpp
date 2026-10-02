// UNSUPPORTED: system-windows
// RUN: %python %S/Inputs/receiver-forms.py implicit > %t-implicit.cpp
// RUN: clang-tidy %t-implicit.cpp '-checks=-*,malterlib-no-zero-bit-scan,malterlib-coroutine-reference-parameter,malterlib-coroutine-reference-capture,malterlib-coroutine-unsafe-this,malterlib-runs-to-completion,malterlib-coroutine-actor-reference' -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" -- -std=c++23 -I %S/Inputs 2>&1 | sed -n -E 's/^.*[.]cpp:([0-9]+:[0-9]+): warning: .*[[](malterlib-[a-z-]+)[]]$/\1 \2/p' > %t-implicit.txt
// RUN: %python %S/Inputs/receiver-forms.py explicit > %t-explicit.cpp
// RUN: clang-tidy %t-explicit.cpp '-checks=-*,malterlib-no-zero-bit-scan,malterlib-coroutine-reference-parameter,malterlib-coroutine-reference-capture,malterlib-coroutine-unsafe-this,malterlib-runs-to-completion,malterlib-coroutine-actor-reference' -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" -- -std=c++23 -I %S/Inputs 2>&1 | sed -n -E 's/^.*[.]cpp:([0-9]+:[0-9]+): warning: .*[[](malterlib-[a-z-]+)[]]$/\1 \2/p' > %t-explicit.txt
// RUN: %python %S/Inputs/receiver-forms.py forwarding > %t-forwarding.cpp
// RUN: clang-tidy %t-forwarding.cpp '-checks=-*,malterlib-no-zero-bit-scan,malterlib-coroutine-reference-parameter,malterlib-coroutine-reference-capture,malterlib-coroutine-unsafe-this,malterlib-runs-to-completion,malterlib-coroutine-actor-reference' -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" -- -std=c++23 -I %S/Inputs 2>&1 | sed -n -E 's/^.*[.]cpp:([0-9]+:[0-9]+): warning: .*[[](malterlib-[a-z-]+)[]]$/\1 \2/p' > %t-forwarding.txt
// RUN: diff %t-implicit.txt %t-explicit.txt
// RUN: diff %t-implicit.txt %t-forwarding.txt
// RUN: FileCheck %s --input-file=%t-implicit.txt --implicit-check-not=malterlib-

// Each check that recognizes a getter, or what acts on the object of a
// member function, reports the same of each form of a receiver (Inputs/
// receiver-forms.py): an implicit object, `f_Get() const`, an explicit one
// of the class, `f_Get(this CType const &_This)`, and a deduced one,
// `f_Get(this tf_CThis &&_This)`, line for line, so that the forms can't
// tell apart again. Of each check one that is reported and one that isn't;
// of a setter a constructor calls, getters by value and by reference, a
// factory, fluent setters and a read-only call, all clean by type; of a copy
// of a member of the object captured, the use of its pointer member, given
// the object, and of its own value, clean by type.

// CHECK: 23:10 malterlib-no-zero-bit-scan
// CHECK-NEXT: 38:38 malterlib-coroutine-reference-parameter
// CHECK-NEXT: 48:23 malterlib-coroutine-reference-capture
// CHECK-NEXT: 60:25 malterlib-coroutine-unsafe-this
// CHECK-NEXT: 81:11 malterlib-runs-to-completion
// CHECK-NEXT: 90:13 malterlib-runs-to-completion
// CHECK-NEXT: 110:15 malterlib-coroutine-actor-reference
// CHECK-NEXT: 150:24 malterlib-coroutine-reference-capture
// CHECK-NEXT: 177:24 malterlib-coroutine-reference-capture
// CHECK-NEXT: 183:24 malterlib-coroutine-reference-capture
// CHECK-NEXT: 198:25 malterlib-coroutine-unsafe-this
// Reported: 183 (a write through a fluent result to a member of a local
// record captured by copy); 81 (a getter that returns what a pointer member
// of a reference parameter points to), which can't be told to live until
// the join unless the member states that only its class points it
// (DMibSetByOwnClass), as the last one does; 198 (the use of a copy's
// pointer member, given the object), a pointer member of a record that
// points into the object. 150 (a setter that resets) and 177 (a fluent
// setter that writes the owner) are owners released or reassigned between
// the borrow and the take.