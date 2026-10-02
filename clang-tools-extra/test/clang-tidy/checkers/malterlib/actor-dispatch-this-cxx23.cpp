// RUN: %check_clang_tidy -std=c++23-or-later %s malterlib-actor-dispatch-this %t -- -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" -- -I %S/Inputs

#include "malterlib-concurrency.h"

// An explicit object call operator is given the actor as its parameter after the object.
namespace NRound115ExplicitObject {
struct App;
struct Reader {
  int operator()(this Reader &, App *p);
};
struct App : CActor {
  int value;
  int f(App *other) {
    Reader r;
    return r(other);
// CHECK-MESSAGES: :[[@LINE-1]]:14: warning: 'operator()' is given the object of an actor, which it uses like 'this' on the actor the call runs on, but this may be of another actor; give it the object of the current actor, like 'this', or call it on the actor of the object [malterlib-actor-dispatch-this]
  }
};
int Reader::operator()(this Reader &, App *p) {
  return p->value;
}
}
