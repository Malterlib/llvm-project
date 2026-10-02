// RUN: clang-tidy %s '-checks=-*,malterlib-struct-not-class' -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" -- -std=c++20 2>&1 | FileCheck %s --implicit-check-not=warning:

// A class named through a dependent base that names itself through the other
// template, each a base of the other, is looked for in each class once
// (nestedIn), and names none; a class elsewhere is still checked.

template <typename t_CType>
struct TCB;

template <typename t_CType>
struct TCA : TCB<t_CType>::CZ
{
};

template <typename t_CType>
struct TCB : TCA<t_CType>::CZ
{
};

template <typename t_CType>
void fg_Use(class TCA<t_CType>::CQ *_pValue);

// CHECK: :[[@LINE+1]]:1: warning: 'CPlain' should be declared with 'struct', like Malterlib types are [malterlib-struct-not-class]
class CPlain
{
public:
	int m_Value;
};
