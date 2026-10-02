// RUN: %check_clang_tidy -std=c++20-or-later %s malterlib-parameter-naming %t -- -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}"

struct CStream;

void fg_Parameters(int _Value, bool _bFlag, int const &_Reference, int &&_Moved, CStream &_Stream, CStream &o_Stream, int *_pValue, int *o_pValue, int const *_pConst);
void fg_Output(int &o_Value, bool &o_bFlag, int *&o_pPointer);

void fg_Bad(int Value, int &Output, int const &o_Input, int _value, int const *o_pConst);
// CHECK-MESSAGES: :[[@LINE-1]]:17: warning: parameter 'Value' should have the '_' prefix [malterlib-parameter-naming]
// CHECK-MESSAGES: :[[@LINE-2]]:29: warning: parameter 'Output' should have the 'o_' or '_' prefix [malterlib-parameter-naming]
// CHECK-MESSAGES: :[[@LINE-3]]:48: warning: parameter 'o_Input' has the 'o_' prefix of an output, but can't be written through, being a value or const; name it with the '_' prefix [malterlib-parameter-naming]
// CHECK-MESSAGES: :[[@LINE-4]]:61: warning: parameter '_value' should be upper camel case after its prefix [malterlib-parameter-naming]
// CHECK-MESSAGES: :[[@LINE-5]]:80: warning: parameter 'o_pConst' has the 'o_' prefix of an output, but can't be written through, being a value or const; name it with the '_' prefix [malterlib-parameter-naming]
void fg_Round345Concept(int _lCount);
// CHECK-MESSAGES: :[[@LINE-1]]:29: warning: parameter '_lCount' has the prefix 'l', which isn't a concept prefix; remove it or use 'b', 'i', 'n', 'p', 'f' or 'r' [malterlib-parameter-naming]
template <int tf_lCount>
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: function template parameter 'tf_lCount' has the prefix 'l', which isn't a concept prefix; remove it or use 'b', 'i', 'n', 'p', 'f' or 'r' [malterlib-parameter-naming]
void fg_Round345Template();

// Each parameter is named, also an unused one, but not the `int` of a postfix
// increment or decrement.
void fg_Unnamed(int, bool);
// CHECK-MESSAGES: :[[@LINE-1]]:20: warning: parameter should be named, also when it is unused, so that what it is can be told [malterlib-parameter-naming]
// CHECK-MESSAGES: :[[@LINE-2]]:26: warning: parameter should be named, also when it is unused, so that what it is can be told [malterlib-parameter-naming]
void fg_UnnamedInType(void (*_pCallback)(int));
// CHECK-MESSAGES: :[[@LINE-1]]:45: warning: parameter should be named, also when it is unused, so that what it is can be told [malterlib-parameter-naming]
auto g_UnnamedInLambda = [](int) {};
// CHECK-MESSAGES: :[[@LINE-1]]:32: warning: parameter should be named, also when it is unused, so that what it is can be told [malterlib-parameter-naming]
struct CPostfix
{
	CPostfix operator ++ (int);
	CPostfix operator -- (int);
};
CPostfix operator ++ (CPostfix &_Value, int);

template <typename ...tfp_CParams>
void fg_Pack(tfp_CParams ...p_Params);

template <typename ...tfp_CParams>
void fg_OutputPack(tfp_CParams &...po_Params);

template <typename ...tfp_CParams>
void fg_BadPack(tfp_CParams ..._Params);
// CHECK-MESSAGES: :[[@LINE-1]]:32: warning: parameter pack '_Params' should have the 'po_' or 'p_' prefix [malterlib-parameter-naming]

// A reference to a template parameter type can bind const objects.
template <typename tf_CType>
void fg_Forward(tf_CType &_Value, tf_CType &o_Value, tf_CType &&_Forward);

template <typename t_CType, int t_Value, bool t_bFlag, template <typename> class t_TCTemplate, typename ...tp_CTypes>
struct TCTemplate
{
	template <typename tf_CType, int tf_Value, typename ...tfp_CTypes>
	void f_Function();

	void f_Lambda()
	{
		auto fLambda = []<typename tf_CType>(int _Value)
			{
			}
		;
		auto fBad = []<typename t_CType2>()
// CHECK-MESSAGES: :[[@LINE-1]]:27: warning: function template parameter 't_CType2' should have the 'tf_' prefix [malterlib-parameter-naming]
			{
			}
		;
	}
};

template <typename t_CType, int t_Value, bool t_bFlag, template <typename> class t_TCTemplate, typename ...tp_CTypes>
template <typename tf_CType, int tf_Value, typename ...tfp_CTypes>
void TCTemplate<t_CType, t_Value, t_bFlag, t_TCTemplate, tp_CTypes...>::f_Function()
{
}

template <typename tf_CType, typename t_CType, typename tf_Bad, int tf_lower, template <typename> class tf_CTemplate, typename ...tf_CPack>
// CHECK-MESSAGES: :[[@LINE-1]]:39: warning: function template parameter 't_CType' should have the 'tf_' prefix [malterlib-parameter-naming]
// CHECK-MESSAGES: :[[@LINE-2]]:57: warning: type function template parameter 'tf_Bad' should have a type prefix after 'tf_', like 'tf_C' or 'tf_F' [malterlib-parameter-naming]
// CHECK-MESSAGES: :[[@LINE-3]]:69: warning: function template parameter 'tf_lower' should be upper camel case after its prefix [malterlib-parameter-naming]
// CHECK-MESSAGES: :[[@LINE-4]]:105: warning: template function template parameter 'tf_CTemplate' should have a template type prefix after 'tf_', like 'tf_TC' [malterlib-parameter-naming]
// CHECK-MESSAGES: :[[@LINE-5]]:131: warning: function template parameter 'tf_CPack' should have the 'tfp_' prefix [malterlib-parameter-naming]
void fg_TemplateBad();

template <template <typename t_CInner, typename Inner> class t_TCTemplate>
// CHECK-MESSAGES: :[[@LINE-1]]:49: warning: template parameter 'Inner' should have the 't_' prefix [malterlib-parameter-naming]
struct TCNested;

template <typename tf_CType>
struct TCBadClass;
// CHECK-MESSAGES: :[[@LINE-2]]:20: warning: template parameter 'tf_CType' should have the 't_' prefix [malterlib-parameter-naming]

template <typename t_CType>
struct TCPartial;

template <typename t_CType>
struct TCPartial<t_CType *>;

template <typename t_CType>
concept cConcept = true;

template <typename t_CType>
using TCAlias = t_CType;

template <typename t_CType>
constexpr bool gc_bVariable = true;

extern "C" void c_function(int value);

void fg_NoLint(int Value); // NOLINT(malterlib-parameter-naming)

// Whether a parameter that can be written through is an output is what its
// author means by it, also of one the function writes or only reads.
unsigned fg_ReadOnlyReference(unsigned &o_LongestLeft)
{
	return o_LongestLeft;
}

void fg_WrittenReference(unsigned &_Value)
{
	_Value = 1;
}

extern "C"
{
	struct CExternCParameters
	{
		void f_Function(int Value);
// CHECK-MESSAGES: :[[@LINE-1]]:23: warning: parameter 'Value' should have the '_' prefix [malterlib-parameter-naming]
	};
}

// A name has at least two characters after its prefixes.
void fg_ShortParameters(int _X, int *_pY, int _Xy);
// CHECK-MESSAGES: :[[@LINE-1]]:29: warning: parameter '_X' should have at least 2 characters after its prefixes [malterlib-parameter-naming]
// CHECK-MESSAGES: :[[@LINE-2]]:38: warning: parameter '_pY' should have at least 2 characters after its prefixes [malterlib-parameter-naming]

// A forwarding reference can be an output, and template parameters have a
// minimum length too.
template <typename tf_CValue>
void fg_DecodeForwarded(tf_CValue &&o_Value)
{
	o_Value = 42;
}
template <int t_X>
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: template parameter 't_X' should have at least 2 characters after its prefixes [malterlib-parameter-naming]
struct TCShortParameter
{
};
template <int tf_X>
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: function template parameter 'tf_X' should have at least 2 characters after its prefixes [malterlib-parameter-naming]
void fg_ShortParameter()
{
}

// A const pointer to a non-const object and an rvalue reference can be
// outputs, and type template parameters have a minimum length too.
void fg_WriteThroughConstPointer(int *const &o_pValue);
void fg_WriteMoved(int &&o_Value);
template <typename t_CX>
// CHECK-MESSAGES: :[[@LINE-1]]:20: warning: template parameter 't_CX' should have at least 2 characters after its prefixes [malterlib-parameter-naming]
struct TCShortType
{
};
template <template <typename> class t_TCX>
// CHECK-MESSAGES: :[[@LINE-1]]:37: warning: template parameter 't_TCX' should have at least 2 characters after its prefixes [malterlib-parameter-naming]
struct TCShortTemplate
{
};

// A deduction guide is named for its class template.
template <typename t_CValue>
struct TCGuided
{
	TCGuided(t_CValue _Value);
};
template <typename t_CValue>
TCGuided(t_CValue) -> TCGuided<t_CValue>;
// CHECK-MESSAGES: :[[@LINE-1]]:18: warning: parameter should be named, also when it is unused, so that what it is can be told [malterlib-parameter-naming]

// A function referred to can't be an output.
void fg_CallReference(void (&o_fCallback)());
// CHECK-MESSAGES: :[[@LINE-1]]:30: warning: parameter 'o_fCallback' has the 'o_' prefix of an output, but can't be written through, being a value or const; name it with the '_' prefix [malterlib-parameter-naming]

// A const reference to a class can be an output, like a promise that is given
// its result through its const member functions; a const reference to a
// scalar can't.
struct CRound60Promise
{
	mutable int m_Value = 0;
	void f_SetValue(int _Value) const
	{
		m_Value = _Value;
	}
};
void fg_Round60Complete(CRound60Promise const &o_Promise, CRound60Promise const &_Other, int const &o_Scalar)
{
	o_Promise.f_SetValue(42);
}
// CHECK-MESSAGES: :[[@LINE-4]]:{{[0-9]+}}: warning: parameter 'o_Scalar' has the 'o_' prefix of an output, but can't be written through, being a value or const; name it with the '_' prefix [malterlib-parameter-naming]

// A reference to an array of const scalars can't be an output.
void fg_Round63Read(const int (&o_Values)[2]);
// CHECK-MESSAGES: :[[@LINE-1]]:33: warning: parameter 'o_Values' has the 'o_' prefix of an output, but can't be written through, being a value or const; name it with the '_' prefix [malterlib-parameter-naming]

// An array of const pointers can write what they point to.
void fg_Round68Pointers(int *const (&o_Values)[2])
{
	*o_Values[0] = 42;
}

// A type template parameter of a function type has the 'F' prefix, not the
// 'TF' prefix of a template template parameter of an alias template.
template <typename t_CType>
using TFRound71Callback = void (t_CType _Value);
template <typename t_TFCallback>
// CHECK-MESSAGES: :[[@LINE-1]]:20: warning: type template parameter 't_TFCallback' has the prefix of a template template parameter; use 't_C', 't_IC', 't_F' or 't_E' [malterlib-parameter-naming]
struct TCRound71Handler
{
	t_TFCallback *m_fCallback;
};

// What a pointer reaches through a const pointer can be written.
void fg_Round79Write(int *const *o_pValue)
{
	**o_pValue = 42;
}

// An rvalue reference to a function can't be written.
void fg_Round83Callback(void (&&o_fCallback)());
// CHECK-MESSAGES: :[[@LINE-1]]:33: warning: parameter 'o_fCallback' has the 'o_' prefix of an output, but can't be written through, being a value or const; name it with the '_' prefix [malterlib-parameter-naming]

// A pointer to an array of const objects of a class can be written through
// their mutable members.
struct CRound92Promise
{
	mutable int m_Value;
};
void fg_Round92Fill(CRound92Promise const (*o_pPromises)[2]);

// Writable through a const reference to a pointer to what isn't const, or
// to a mutable member, but not through a pointer to a const scalar.
void fg_Round93Write(int *const &&o_pValue);
void fg_Round93Fill(CRound92Promise const *const &o_pPromise);
void fg_Round93Const(int const *const &o_pValue);
// CHECK-MESSAGES: :[[@LINE-1]]:40: warning: parameter 'o_pValue' has the 'o_' prefix of an output, but can't be written through, being a value or const; name it with the '_' prefix [malterlib-parameter-naming]

// What a template is given can be a reference, which can be an output.
struct CRound94Traits
{
	using CReference = int &;
};

template <typename tf_CTraits>
void fg_Round94Set(typename tf_CTraits::CReference o_Value)
{
	o_Value = 42;
}

void fg_Round94Run()
{
	int Value = 0;
	fg_Round94Set<CRound94Traits>(Value);
}

// What a template is given tells whether a parameter of its type can be an
// output: instantiated with a reference it can, with a const one it can't,
// and one deduced as a value can't when nothing instantiates it otherwise.
template <typename t_CReference>
struct TCRound95Writer
{
	void f_Write(t_CReference o_Value)
	{
		o_Value = 42;
	}
};

struct CRound95ConstTraits
{
	using CReference = int const &;
};

template <typename tf_CTraits>
void fg_Round95Read(typename tf_CTraits::CReference o_Value)
// CHECK-MESSAGES: :[[@LINE-1]]:53: warning: parameter 'o_Value' has the 'o_' prefix of an output, but can't be written through, being a value or const; name it with the '_' prefix [malterlib-parameter-naming]
{
}

// Nothing instantiates it, and it can be deduced as a pointer.
template <typename tf_CType>
void fg_Round95Deduced(tf_CType o_pValue);

template <typename tf_CType>
void fg_Round95Explicit(tf_CType o_Value)
{
}

void fg_Round95Use()
{
	int Value = 0;
	TCRound95Writer<int &>{}.f_Write(Value);
	fg_Round95Read<CRound95ConstTraits>(42);
	fg_Round95Explicit<int &>(Value);
}

// One to what a template is given is told by its instantiations.
template <typename tf_CType>
void fg_Round96Read(tf_CType const &o_Value)
// CHECK-MESSAGES: :[[@LINE-1]]:37: warning: parameter 'o_Value' has the 'o_' prefix of an output, but can't be written through, being a value or const; name it with the '_' prefix [malterlib-parameter-naming]
{
}

void fg_Round96Use()
{
	fg_Round96Read(42);
}

// Packs of an instantiation are told also when there are several.
template <typename ...tfp_CFirst, typename ...tfp_CSecond>
void fg_Round97Set(tfp_CFirst ...po_First, tfp_CSecond ...p_Second)
{
}

void fg_Round97UseSet()
{
	int Value = 0;
	fg_Round97Set<int &>(Value);
}

// An empty pack tells nothing about the output parameters.
template <typename ...tfp_CTypes>
void fg_Round98Clear(tfp_CTypes &...po_Values)
{
}

void fg_Round98UseClear()
{
	fg_Round98Clear();
}

// A pack inside another type is told by the instantiation too.
template <typename t_CType>
struct TCRound99Box
{
};

template <typename ...tfp_CFirst, typename ...tfp_CSecond>
void fg_Round99Read(TCRound99Box<tfp_CFirst> ...p_Boxes, tfp_CSecond ...po_Values)
// CHECK-MESSAGES: :[[@LINE-1]]:73: warning: parameter pack 'po_Values' has the 'po_' prefix of an output, but can't be written through, being a value or const; name it with the 'p_' prefix [malterlib-parameter-naming]
{
}

void fg_Round99UseRead()
{
	fg_Round99Read<int>(TCRound99Box<int>{}, 1);
}

// A const vector can't be written.
using CRound99Values = int __attribute__((ext_vector_type(4)));

void fg_Round99ReadVector(CRound99Values const &o_Values)
// CHECK-MESSAGES: :[[@LINE-1]]:49: warning: parameter 'o_Values' has the 'o_' prefix of an output, but can't be written through, being a value or const; name it with the '_' prefix [malterlib-parameter-naming]
{
}

void fg_Round99ReadVectorPointer(CRound99Values const *o_pValues)
// CHECK-MESSAGES: :[[@LINE-1]]:56: warning: parameter 'o_pValues' has the 'o_' prefix of an output, but can't be written through, being a value or const; name it with the '_' prefix [malterlib-parameter-naming]
{
}

// A pack of values before another pack, told by the instantiation.
template <int ...tfp_Indices, typename ...tfp_CTypes>
void fg_Round100Run(decltype(tfp_Indices) ...p_Indices, tfp_CTypes ...po_Values)
// CHECK-MESSAGES: :[[@LINE-1]]:71: warning: parameter pack 'po_Values' has the 'po_' prefix of an output, but can't be written through, being a value or const; name it with the 'p_' prefix [malterlib-parameter-naming]
{
}

void fg_Round100UseRun()
{
	fg_Round100Run<0>(1, 2);
}

// Const `void` can't be written.
void fg_Round101Read(void const *o_pData);
// CHECK-MESSAGES: :[[@LINE-1]]:34: warning: parameter 'o_pData' has the 'o_' prefix of an output, but can't be written through, being a value or const; name it with the '_' prefix [malterlib-parameter-naming]
void fg_Round101ReadIndirect(void const *const *o_pData);
// CHECK-MESSAGES: :[[@LINE-1]]:49: warning: parameter 'o_pData' has the 'o_' prefix of an output, but can't be written through, being a value or const; name it with the '_' prefix [malterlib-parameter-naming]

// A pack of the class template a member function template is of.
template <typename... tp_CTypes>
struct TCRound107Box
{
	template <typename... tfp_COthers>
	void f_Run(tp_CTypes... po_Values, tfp_COthers... p_Others)
// CHECK-MESSAGES: :[[@LINE-1]]:26: warning: parameter pack 'po_Values' has the 'po_' prefix of an output, but can't be written through, being a value or const; name it with the 'p_' prefix [malterlib-parameter-naming]
	{
	}
};

TCRound107Box<int> g_Round107Box;

void fg_Round107Use()
{
	g_Round107Box.f_Run(1);
}

// A generic lambda in a function template is checked as it is instantiated
// in the instantiations of the function.
template <typename tf_COuter>
void fg_Round112NestedLambda()
{
	auto fLambda = []<typename tf_CInner>(tf_CInner const &o_Value)
// CHECK-MESSAGES: :[[@LINE-1]]:57: warning: parameter 'o_Value' has the 'o_' prefix of an output, but can't be written through, being a value or const; name it with the '_' prefix [malterlib-parameter-naming]
		{
		};
	fLambda(1);
}
void fg_Round112UseNestedLambda()
{
	fg_Round112NestedLambda<int>();
}

// A parameter of a function type has no instantiation of its own: what the
// uses of an alias template make it tells whether it can be an output.
namespace NRound116FunctionTypeOutput {
template <typename t_CType>
using FCallback = void (t_CType o_Value);
// CHECK-MESSAGES: :[[@LINE-1]]:33: warning: parameter 'o_Value' has the 'o_' prefix of an output, but can't be written through, being a value or const; name it with the '_' prefix [malterlib-parameter-naming]
FCallback<int> *g_fCallback;
template <typename t_CType>
using FOutput = void (t_CType o_Value);
FOutput<int &> *g_fOutput;
}

// A const atomic can't be written through.
namespace NRound117ConstAtomic {
void fg_Run(_Atomic(int) const &o_Value);
// CHECK-MESSAGES: :[[@LINE-1]]:33: warning: parameter 'o_Value' has the 'o_' prefix of an output, but can't be written through, being a value or const; name it with the '_' prefix [malterlib-parameter-naming]
void fg_Other(_Atomic(int) const *o_pValue);
// CHECK-MESSAGES: :[[@LINE-1]]:35: warning: parameter 'o_pValue' has the 'o_' prefix of an output, but can't be written through, being a value or const; name it with the '_' prefix [malterlib-parameter-naming]
}

// Round 125: the call operator of a lambda in a variable template is
// instantiated where it is written, in each specialization.
namespace NRound125
{
template <typename t_CType>
inline auto g_fSet = []<typename tf_CValue>(tf_CValue o_Value) { o_Value = 1; };

void fg_Run() {
  int Value = 0;
  g_fSet<int>.template operator()<int &>(Value);
}

template <typename t_CType>
inline auto g_fRead = []<typename tf_CValue>(tf_CValue o_Value) { (void)o_Value; };
// CHECK-MESSAGES: :[[@LINE-1]]:56: warning: parameter 'o_Value' has the 'o_' prefix of an output, but can't be written through, being a value or const; name it with the '_' prefix [malterlib-parameter-naming]

void fg_Read() {
  g_fRead<int>.template operator()<int>(0);
}
}

// Round 134: an Objective-C object is written through its pointer.
// RUN: printf '@interface CObject { @public int m_Value; }\n@end\nvoid fg_Set(CObject *o_pObject) { o_pObject->m_Value = 1; }\n' > %t.objc.mm
// RUN: clang-tidy --config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" --checks='-*,malterlib-parameter-naming' %t.objc.mm -- -x objective-c++ 2>&1 | FileCheck --check-prefix=CHECK-OBJC --allow-empty %s
// CHECK-OBJC-NOT: warning:
// Round 150: also through a const reference to its pointer.
// RUN: printf '@interface CObject { @public int m_Value; }\n@end\nvoid fg_Set(CObject *const &o_pObject) { o_pObject->m_Value = 1; }\n' > %t.objcref.mm
// RUN: clang-tidy --config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" --checks='-*,malterlib-parameter-naming' %t.objcref.mm -- -x objective-c++ 2>&1 | FileCheck --check-prefix=CHECK-OBJC-REF --allow-empty %s
// CHECK-OBJC-REF-NOT: warning:

namespace NRound150 {
// A const atomic pointer to what can be written.
void fg_SetAtomic(_Atomic(int *) const &o_pValue)
{
	*o_pValue = 1;
}
}

namespace NRound151 {
// A pack of a member template of a class template in a class template.
template <typename t_COuter>
struct TCOuter
{
	template <typename... tp_CFirst>
	struct TCInner
	{
		template <typename... tfp_CSecond>
		void f_Run(tp_CFirst const &...po_First, tfp_CSecond... p_Second) {}
// CHECK-MESSAGES: :[[@LINE-1]]:34: warning: parameter pack 'po_First' has the 'po_' prefix of an output, but can't be written through, being a value or const; name it with the 'p_' prefix [malterlib-parameter-naming]
	};
};

void fg_RunInner()
{
	TCOuter<int>::TCInner<int> Inner;
	Inner.f_Run(42);
}
}

namespace NRound153 {
template <typename t_CType> struct TCVector {};
template <template <typename> class t_TVector> struct TCWrapper {};
// CHECK-MESSAGES: :[[@LINE-1]]:37: warning: template template parameter 't_TVector' should have a template type prefix after 't_', like 't_TC' [malterlib-parameter-naming]
TCWrapper<TCVector> g_Wrapper;
}

namespace NRound155 {
// Nothing instantiates it, and it can be deduced as a pointer.
template <typename tf_CType>
void fg_Set(tf_CType o_pValue) {
  *o_pValue = 1;
}
}

namespace NRound165 {
// Each of several packs, also of templates.
template <typename t_CType> struct TCBox {};
template <template <typename> class... tfp_TCBoxes, typename... tfp_CTypes>
void fg_Run(tfp_TCBoxes<int>... po_Boxes, tfp_CTypes const &...po_Values) {}
// CHECK-MESSAGES: :[[@LINE-1]]:33: warning: parameter pack 'po_Boxes' has the 'po_' prefix of an output, but can't be written through, being a value or const; name it with the 'p_' prefix [malterlib-parameter-naming]
// CHECK-MESSAGES: :[[@LINE-2]]:64: warning: parameter pack 'po_Values' has the 'po_' prefix of an output, but can't be written through, being a value or const; name it with the 'p_' prefix [malterlib-parameter-naming]
void fg_Use() { fg_Run<TCBox>(TCBox<int>{}, 42); }
}

namespace NRound187 {
// An output prefix of a parameter of a function type in the type of a member,
// which its instantiations make a value.
template <typename t_FSignature> struct TCFunction {};
struct CTraits {
  using CBool = bool;
  using CBoolRef = bool &;
};
template <typename t_CTraits>
struct TCListener {
  TCFunction<void (typename t_CTraits::CBool o_bReady)> m_fCallback;
// CHECK-MESSAGES: :[[@LINE-1]]:46: warning: parameter 'o_bReady' has the 'o_' prefix of an output, but can't be written through, being a value or const; name it with the '_' prefix [malterlib-parameter-naming]
  TCFunction<void (typename t_CTraits::CBoolRef o_bWritten)> m_fWrite;
};
TCListener<CTraits> g_Listener;
}

namespace NRound191 {
// An output prefix of a parameter of a requires expression in a function
// template, which its instantiations make const.
struct CTraits {
  using CValue = int const;
  using CWritable = int;
};
template <typename tf_CTraits>
void fg_Require() {
  static_assert(requires(typename tf_CTraits::CValue &o_Value) { o_Value + 1; });
// CHECK-MESSAGES: :[[@LINE-1]]:55: warning: parameter 'o_Value' has the 'o_' prefix of an output, but can't be written through, being a value or const; name it with the '_' prefix [malterlib-parameter-naming]
  static_assert(requires(typename tf_CTraits::CWritable &o_Value) { o_Value = 1; });
}
void fg_Use() { fg_Require<CTraits>(); }
}

namespace NRound192 {
// A function type in the return type of a function template, of its
// instantiations.
template <typename t_FSignature>
struct TCFunction {};
struct CTraits {
  using CBool = bool;
};
template <typename tf_CTraits>
TCFunction<void(typename tf_CTraits::CBool o_Ready)> fg_Make() { return {}; }
// CHECK-MESSAGES: :[[@LINE-1]]:44: warning: parameter 'o_Ready' has the 'o_' prefix of an output, but can't be written through, being a value or const; name it with the '_' prefix [malterlib-parameter-naming]
auto g_fCallback = fg_Make<CTraits>();
}

namespace NRound193 {
// Of the member a function type is in, not of another overload of it.
template <typename t_FSignature>
struct TCFunction {};
struct CTraits {
  using CBool = bool;
};
template <typename t_CTraits>
struct TCListener {
  TCFunction<void(typename t_CTraits::CBool o_Ready)> f_Make(int _Tag);
// CHECK-MESSAGES: :[[@LINE-1]]:45: warning: parameter 'o_Ready' has the 'o_' prefix of an output, but can't be written through, being a value or const; name it with the '_' prefix [malterlib-parameter-naming]
  TCFunction<void(int &o_Value)> f_Make();
};
TCListener<CTraits> g_Listener;
}

namespace NRound198 {
// A function type expanded from a pack is each of the function types it
// expands to.
template <typename t_FSignature, typename... tp_FSignatures>
struct TCFunction {};
struct CFlagTraits {
  using CValue = bool;
};
struct CCountTraits {
  using CValue = int;
};
template <typename... tp_CTraits>
struct TCListeners {
  TCFunction<void(typename tp_CTraits::CValue o_Value)...> m_fCallback;
// CHECK-MESSAGES: :[[@LINE-1]]:47: warning: parameter 'o_Value' has the 'o_' prefix of an output, but can't be written through, being a value or const; name it with the '_' prefix [malterlib-parameter-naming]
};
TCListeners<CFlagTraits, CCountTraits> g_Listeners;
}

namespace NRound199 {
// A function type in a base of a class template, of its instantiations.
template <typename t_FSignature>
struct TCFunction {};
struct CTraits {
  using CBool = bool;
};
template <typename t_CTraits>
struct TCHandler : TCFunction<void(typename t_CTraits::CBool _Ready, typename t_CTraits::CBool o_bDone)> {};
// CHECK-MESSAGES: :[[@LINE-1]]:96: warning: parameter 'o_bDone' has the 'o_' prefix of an output, but can't be written through, being a value or const; name it with the '_' prefix [malterlib-parameter-naming]
TCHandler<CTraits> g_fHandler;
}

// An output pack in an alias template's function type that expands to values.
namespace NRound204 {
struct CTraits {
  using CValue = int;
};
template <typename... tp_CTraits>
using TFCallback = void(typename tp_CTraits::CValue... po_Values);
// CHECK-MESSAGES: :[[@LINE-1]]:56: warning: parameter pack 'po_Values' has the 'po_' prefix of an output, but can't be written through, being a value or const; name it with the 'p_' prefix [malterlib-parameter-naming]
using FCallback = TFCallback<CTraits>;
}

namespace NRound221Alias
{
	// The parameters of a function type an alias gives through another
	// alias.
	struct CTraits
	{
		using CType = bool;
	};
	template <typename t_CTraits>
	using TFCallback = void (typename t_CTraits::CType o_Value);
// CHECK-MESSAGES: :[[@LINE-1]]:53: warning: parameter 'o_Value' has the 'o_' prefix of an output, but can't be written through, being a value or const; name it with the '_' prefix [malterlib-parameter-naming]
	template <typename t_CTraits>
	using TFAlias = TFCallback<t_CTraits>;
	TFAlias<CTraits> *g_fCallback;
}

namespace NRound222
{
	// The parameters of a function type in a cast, as instantiated.
	template <typename t_FSignature>
	struct TCFunctionNoAlloc
	{
	};
	struct CTraits
	{
		using CBool = bool;
	};
	template <typename tf_CTraits>
	void fg_Run(void *_pContext)
	{
		(void)reinterpret_cast<TCFunctionNoAlloc<void (typename tf_CTraits::CBool o_bReady)> *>(_pContext);
// CHECK-MESSAGES: :[[@LINE-1]]:77: warning: parameter 'o_bReady' has the 'o_' prefix of an output, but can't be written through, being a value or const; name it with the '_' prefix [malterlib-parameter-naming]
	}
	void fg_Use()
	{
		fg_Run<CTraits>(nullptr);
	}
}

namespace NRound223Default
{
	// The parameters of a function type in the default argument of a template
	// parameter, as each specialization has it.
	template <typename t_FSignature>
	struct TCFunction
	{
	};
	struct CTraits
	{
		using CBool = bool;
	};
	template <typename tf_CTraits, typename tf_FCallback = TCFunction<void (typename tf_CTraits::CBool _Ready, typename tf_CTraits::CBool o_bResult)>>
// CHECK-MESSAGES: :[[@LINE-1]]:136: warning: parameter 'o_bResult' has the 'o_' prefix of an output, but can't be written through, being a value or const; name it with the '_' prefix [malterlib-parameter-naming]
	void fg_Run()
	{
	}
	void fg_Use()
	{
		fg_Run<CTraits>();
	}
}

namespace NRound225
{
	// A default that a call replaces says nothing of what it is.
	template <typename t_FSignature>
	struct TCFunction
	{
	};
	struct CTraits
	{
		using CType = int;
	};
	template <typename tf_CTraits, typename tf_FOnResult = TCFunction<void (typename tf_CTraits::CType &o_Value)>>
	void fg_CombineResults(tf_FOnResult &&_fOnResult)
	{
	}
	void fg_Use()
	{
		fg_CombineResults<CTraits>(TCFunction<void (bool _bReady)>{});
	}
}

namespace NRound226Default
{
	// A default the call takes by leaving out the parameter it is the type of
	// is used, but not one a class is given another in place of.
	template <typename t_FSignature>
	struct TCFunction
	{
	};
	struct CTraits
	{
		using CType = bool;
	};
	template <typename tf_CTraits, typename tf_FCallback = TCFunction<void (typename tf_CTraits::CType _bReady, typename tf_CTraits::CType o_bResult)>>
// CHECK-MESSAGES: :[[@LINE-1]]:137: warning: parameter 'o_bResult' has the 'o_' prefix of an output, but can't be written through, being a value or const; name it with the '_' prefix [malterlib-parameter-naming]
	void fg_Run(tf_FCallback _fCallback = {})
	{
	}
	template <typename t_CTraits, typename t_FCallback = TCFunction<void (typename t_CTraits::CType o_bResult)>>
	struct TCBox
	{
	};
	TCBox<CTraits, TCFunction<void (int _Result)>> g_Box;
	void fg_Use()
	{
		fg_Run<CTraits>();
	}
}

namespace NRound227MemberDefault
{
	// The default of a member template of a class template, as each
	// specialization of the member template instantiated of it has it, also
	// with the parameters of the class.
	template <typename t_FSignature>
	struct TCFunction
	{
	};
	struct CTraits
	{
		using CBool = bool;
	};
	template <typename t_CTraits>
	struct TCBox
	{
		template <typename tf_FCallback = TCFunction<void (typename t_CTraits::CBool _Ready, typename t_CTraits::CBool o_bResult)>>
// CHECK-MESSAGES: :[[@LINE-1]]:114: warning: parameter 'o_bResult' has the 'o_' prefix of an output, but can't be written through, being a value or const; name it with the '_' prefix [malterlib-parameter-naming]
		void f_Run()
		{
		}
		template <typename tf_FCallback = TCFunction<void (typename t_CTraits::CBool _Given, typename t_CTraits::CBool o_bGiven)>>
		void f_RunGiven()
		{
		}
	};
	void fg_Test()
	{
		TCBox<CTraits>{}.f_Run();
		TCBox<CTraits>{}.f_RunGiven<TCFunction<void (int _Value, int &o_Value)>>();
	}
}

namespace NRound230
{
	// The default made with a reference, which collapses with the one it is
	// written with, is used.
	template <typename t_FSignature>
	struct TCFunction
	{
	};
	template <typename tf_CValue, typename tf_FCallback = TCFunction<void (tf_CValue const &o_Value)>>
// CHECK-MESSAGES: :[[@LINE-1]]:90: warning: parameter 'o_Value' has the 'o_' prefix of an output, but can't be written through, being a value or const; name it with the '_' prefix [malterlib-parameter-naming]
	void fg_Run(tf_FCallback _fCallback = {})
	{
	}
	void fg_Test()
	{
		fg_Run<int const &>();
	}
}

namespace NRound232
{
	// A callback signature in a template argument of a call is checked as it
	// is instantiated.
	template <typename t_FSignature>
	struct TCFunction
	{
	};
	struct CTraits
	{
		using CBool = bool;
	};
	template <typename tf_CType>
	tf_CType fg_Construct()
	{
		return {};
	}
	template <typename tf_CTraits>
	auto fg_Make()
	{
		return fg_Construct<TCFunction<void (typename tf_CTraits::CBool o_bReady)>>();
// CHECK-MESSAGES: :[[@LINE-1]]:67: warning: parameter 'o_bReady' has the 'o_' prefix of an output, but can't be written through, being a value or const; name it with the '_' prefix [malterlib-parameter-naming]
	}
	void fg_Test()
	{
		auto fCallback = fg_Make<CTraits>();
		(void)fCallback;
	}
}

namespace NRound234
{
	template <typename t_FSignature>
	struct TCFunction
	{
	};
	struct CTraits
	{
		using CBool = bool;
	};
	// The parameters of the type of a temporary, as instantiated.
	template <typename tf_CTraits>
	void fg_Temporary()
	{
		auto fCallback = TCFunction<void (typename tf_CTraits::CBool o_bReady)>{};
// CHECK-MESSAGES: :[[@LINE-1]]:64: warning: parameter 'o_bReady' has the 'o_' prefix of an output, but can't be written through, being a value or const; name it with the '_' prefix [malterlib-parameter-naming]
	}
	// Around a pack it expands.
	template <typename tf_CTraits, typename ...tfp_CArgs>
	void fg_Pack(void *_pContext)
	{
		(void)reinterpret_cast<TCFunction<void (typename tf_CTraits::CBool o_bReady, tfp_CArgs ...p_Args)> *>(_pContext);
// CHECK-MESSAGES: :[[@LINE-1]]:70: warning: parameter 'o_bReady' has the 'o_' prefix of an output, but can't be written through, being a value or const; name it with the '_' prefix [malterlib-parameter-naming]
	}
	void fg_Use()
	{
		fg_Temporary<CTraits>();
		fg_Pack<CTraits, int, int>(nullptr);
	}
}

namespace NRound235
{
	template <typename t_FSignature>
	struct TCFunction
	{
	};
	struct CTraits
	{
		using CBool = bool;
	};
	// Of a member template of a class template.
	template <typename t_CTraits>
	struct TCBox
	{
		template <typename tf_CType>
		void f_Run()
		{
			(void)TCFunction<void (typename t_CTraits::CBool o_bResult)>{};
// CHECK-MESSAGES: :[[@LINE-1]]:53: warning: parameter 'o_bResult' has the 'o_' prefix of an output, but can't be written through, being a value or const; name it with the '_' prefix [malterlib-parameter-naming]
		}
	};
	// In what a constructor initializes.
	template <typename t_CTraits>
	struct TCHolder
	{
		TCFunction<void (bool _bReady)> m_fCallback;
		TCHolder()
			: m_fCallback(TCFunction<void (typename t_CTraits::CBool o_bResult)>{})
// CHECK-MESSAGES: :[[@LINE-1]]:61: warning: parameter 'o_bResult' has the 'o_' prefix of an output, but can't be written through, being a value or const; name it with the '_' prefix [malterlib-parameter-naming]
		{
		}
	};
	void fg_Use()
	{
		TCBox<CTraits>{}.f_Run<int>();
		TCHolder<CTraits> Holder;
		(void)Holder;
	}
}

namespace NRound239
{
	struct CTraits
	{
		using CType = int;
	};
	template <typename t_FSignature>
	struct TCFunction
	{
	};
	// The parameters of what a `new` makes, as instantiated.
	template <typename tf_CTraits>
	void fg_Make()
	{
		delete new TCFunction<void (typename tf_CTraits::CType o_Result)>;
// CHECK-MESSAGES: :[[@LINE-1]]:58: warning: parameter 'o_Result' has the 'o_' prefix of an output, but can't be written through, being a value or const; name it with the '_' prefix [malterlib-parameter-naming]
	}
	void fg_Use()
	{
		fg_Make<CTraits>();
	}
}

// Round 332: a type template parameter is a type, also an interface, a
// function type or an enum, not a template, which a template template
// parameter is.
template <typename t_CType, typename t_ICInterface, typename t_FCallback, typename t_EMode, typename t_TCStrTraits, typename t_TICOther>
// CHECK-MESSAGES: :[[@LINE-1]]:102: warning: type template parameter 't_TCStrTraits' has the prefix of a template template parameter; use 't_C', 't_IC', 't_F' or 't_E' [malterlib-parameter-naming]
// CHECK-MESSAGES: :[[@LINE-2]]:126: warning: type template parameter 't_TICOther' has the prefix of a template template parameter; use 't_C', 't_IC', 't_F' or 't_E' [malterlib-parameter-naming]
struct TCRound332Box
{
};

namespace NRound334 {
// Const outside an array alias makes its elements const.
using CData = int[4];
void fg_Read(CData const &o_Data);
// CHECK-MESSAGES: :[[@LINE-1]]:27: warning: parameter 'o_Data' has the 'o_' prefix of an output, but can't be written through, being a value or const; name it with the '_' prefix [malterlib-parameter-naming]
void fg_ReadPointer(CData const *o_pData);
// CHECK-MESSAGES: :[[@LINE-1]]:34: warning: parameter 'o_pData' has the 'o_' prefix of an output, but can't be written through, being a value or const; name it with the '_' prefix [malterlib-parameter-naming]
void fg_Control(int const (&o_Data)[4]);
// CHECK-MESSAGES: :[[@LINE-1]]:29: warning: parameter 'o_Data' has the 'o_' prefix of an output, but can't be written through, being a value or const; name it with the '_' prefix [malterlib-parameter-naming]
}
