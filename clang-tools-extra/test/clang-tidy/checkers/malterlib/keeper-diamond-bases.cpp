// UNSUPPORTED: system-windows
// RUN: %python %S/Inputs/keeper-diamond-bases.py 30 > %t.cpp
// RUN: sh -c "ulimit -t 20 && clang-tidy %t.cpp '-checks=-*,malterlib-coroutine-reference-parameter' -config=\"{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}\" -- -std=c++20 -I %S/Inputs" 2>&1 | FileCheck %s

// Thirty layers of two classes, each derived from both classes of the layer
// before, not virtually: whether the keeper of the last layer fits the
// unrelated class of the reference is told by a walk over the classes of the
// bases, each once (isBaseOf), not once per path, which doubles with each
// layer. Nothing keeps what the reference refers to, so it is reported.

// CHECK: warning: TCFuture coroutine 'fg_Use' uses '_Other' after a suspension
