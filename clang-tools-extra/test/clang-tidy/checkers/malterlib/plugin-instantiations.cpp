// The instantiations of a template declared first in a header that isn't
// reported are what tells the checks what its definition here is used with.
// REQUIRES: malterlib-plugin
// RUN: not %clang -fplugin-arg-malterlib-options-file=%S/Inputs/ClangTidyOptions.yaml -std=c++20 -fsyntax-only -Werror -fplugin-arg-malterlib-checks='-*,malterlib-parameter-naming' '-fplugin-arg-malterlib-header-filter=' -I %S/Inputs %s 2>&1 | FileCheck --check-prefix=CHECK-MEMBER --implicit-check-not=warning: --implicit-check-not=error: %s

#include "plugin-instantiations.hpp"

template <typename tf_CType>
void fg_Write(tf_CType o_Value)
{
	o_Value = 1;
}

void fg_Run()
{
	int Value = 0;
	fg_Write<int &>(Value);
}

// Also the instantiations of a member of a class template, declared in a
// header that isn't reported, and defined here.
template <typename t_CType>
void TCPluginBox<t_CType>::f_Run(t_CType const &o_Value)
// CHECK-MEMBER: plugin-instantiations.cpp:[[@LINE-1]]:49: error: parameter 'o_Value' has the 'o_' prefix of an output, but can't be written through, being a value or const; name it with the '_' prefix [malterlib-parameter-naming]
{
}

void fg_RunBox()
{
	TCPluginBox<int> Box;
	Box.f_Run(1);
}

// And of a member of a partial specialization.
template <typename t_CType>
void TCPluginPartial<t_CType *>::f_Run(t_CType const &o_Value)
// CHECK-MEMBER: plugin-instantiations.cpp:[[@LINE-1]]:55: error: parameter 'o_Value' has the 'o_' prefix of an output, but can't be written through, being a value or const; name it with the '_' prefix [malterlib-parameter-naming]
{
}

void fg_RunPartial()
{
	TCPluginPartial<int *> Partial;
	Partial.f_Run(1);
}
