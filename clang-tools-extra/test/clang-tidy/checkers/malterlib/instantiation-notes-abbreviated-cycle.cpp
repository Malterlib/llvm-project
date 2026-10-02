// RUN: %check_clang_tidy -std=c++20-or-later %s malterlib-parameter-naming,malterlib-conceptual-prefix %t -- -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" -- -I %S/Inputs

// A conversion to a class local to it, in a template whose arguments are too
// large to print, is printed as `operator ...`, in an instantiation note and
// in a type: the type it converts to names the conversion again.
template <typename tf_FFunction>
void fg_TakeFunction(tf_FFunction *_fObject)
{
}
void fg_Function();
void fg_UseFunction()
{
	fg_TakeFunction(&fg_Function);
}

template <typename t_CType, typename t_CDeep>
struct TCBox
{
	operator auto()
	{
		struct CLocal
		{
			void f_Run(t_CType o_Value)
			{
				fg_TakeFunction(this);
			}
		};
		return CLocal{};
	}
};

using CDeep = int ***************************************************************;
using CResult = decltype(TCBox<int, CDeep>{}.operator auto());

// CHECK-NOTES: :7:36: warning: '_fObject' has the 'f' concept prefix of a function object, but this instantiation makes it a pointer to 'TCBox<...>::operator ...(...)::CLocal', which is neither a function nor a function object; the 'p' concept prefix fits every instantiation [malterlib-conceptual-prefix]
// CHECK-NOTES: :25:5: note: in instantiation of function template specialization 'fg_TakeFunction<...>' requested here
// CHECK-NOTES: :21:10: note: in instantiation of member function 'TCBox<...>::operator ...(...)::CLocal::f_Run' requested here
// CHECK-NOTES: :23:23: warning: parameter 'o_Value' has the 'o_' prefix of an output, but can't be written through, being a value or const; name it with the '_' prefix [malterlib-parameter-naming]
// CHECK-NOTES: :21:10: note: in instantiation of member function 'TCBox<...>::operator ...(...)::CLocal::f_Run' requested here
