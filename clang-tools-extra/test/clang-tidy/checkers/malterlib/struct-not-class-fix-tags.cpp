// RUN: %check_clang_tidy -std=c++20-or-later %s malterlib-struct-not-class %t -- -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml', malterlib.FixIts: true}}"
// RUN: %clang -fsyntax-only -std=c++20 -Werror=mismatched-tags %t.cpp

// Clang compares the tag of a primary template with those of its explicit and
// partial specializations, and with those of the template-ids and explicit
// instantiations of its instances; tags equal before the fixes stay equal,
// so a fix that would make them differ is withheld, and the warnings stay.

// An explicit instantiation of an instance of a partial specialization
// declared with `struct`, of a primary template declared with `class`.
template <typename t_CType>
class TCBox1
// CHECK-MESSAGES: :[[@LINE-1]]:1: warning: 'TCBox1' should be declared with 'struct', like Malterlib types are [malterlib-struct-not-class]
// CHECK-FIXES: class TCBox1
{
public:
	t_CType m_Value;
};
template <typename t_CType>
struct TCBox1<t_CType *>
{
	t_CType *m_pValue;
};
template class TCBox1<int *>;
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: 'TCBox1<t_CType *>' is a struct, but is named with 'class' here; name it with 'struct' [malterlib-struct-not-class]
// CHECK-FIXES: template class TCBox1<int *>;

// The primary template can't be fixed, its explicit specialization can.
template <typename t_CType>
class TCBox2
// CHECK-MESSAGES: :[[@LINE-1]]:1: warning: 'TCBox2' should be declared with 'struct', like Malterlib types are [malterlib-struct-not-class]
// CHECK-FIXES: class TCBox2
{
	t_CType m_Value;
public:
	t_CType f_Get() const;
};
template <>
class TCBox2<int>
// CHECK-MESSAGES: :[[@LINE-1]]:1: warning: 'TCBox2<int>' should be declared with 'struct', like Malterlib types are [malterlib-struct-not-class]
// CHECK-FIXES: class TCBox2<int>
{
public:
	int m_Value;
};

// The same with a partial specialization.
template <typename t_CType>
class TCBox3
// CHECK-MESSAGES: :[[@LINE-1]]:1: warning: 'TCBox3' should be declared with 'struct', like Malterlib types are [malterlib-struct-not-class]
// CHECK-FIXES: class TCBox3
{
	t_CType m_Value;
public:
	t_CType f_Get() const;
};
template <typename t_CType>
class TCBox3<t_CType *>
// CHECK-MESSAGES: :[[@LINE-1]]:1: warning: 'TCBox3<t_CType *>' should be declared with 'struct', like Malterlib types are [malterlib-struct-not-class]
// CHECK-FIXES: class TCBox3<t_CType *>
{
public:
	t_CType *m_pValue;
};

// The primary template can be fixed, its explicit specialization can't.
template <typename t_CType>
class TCBox4
// CHECK-MESSAGES: :[[@LINE-1]]:1: warning: 'TCBox4' should be declared with 'struct', like Malterlib types are [malterlib-struct-not-class]
// CHECK-FIXES: class TCBox4
{
public:
	t_CType m_Value;
};
template <>
class TCBox4<int>
// CHECK-MESSAGES: :[[@LINE-1]]:1: warning: 'TCBox4<int>' should be declared with 'struct', like Malterlib types are [malterlib-struct-not-class]
// CHECK-FIXES: class TCBox4<int>
{
	int m_Value;
public:
	int f_Get() const;
};

// A template-id of an instance of a partial specialization, with both fixed.
template <typename t_CType>
class TCBox5
// CHECK-MESSAGES: :[[@LINE-1]]:1: warning: 'TCBox5' should be declared with 'struct', like Malterlib types are [malterlib-struct-not-class]
// CHECK-FIXES: struct TCBox5
{
public:
	t_CType m_Value;
};
template <typename t_CType>
class TCBox5<t_CType *>
// CHECK-MESSAGES: :[[@LINE-1]]:1: warning: 'TCBox5<t_CType *>' should be declared with 'struct', like Malterlib types are [malterlib-struct-not-class]
// CHECK-FIXES: struct TCBox5<t_CType *>
{
public:
	t_CType *m_pValue;
};
class TCBox5<int *> *g_pBox5;
// CHECK-FIXES: struct TCBox5<int *> *g_pBox5;

// A friend template, fixed with the template.
template <typename t_CType>
class TCBox6
// CHECK-MESSAGES: :[[@LINE-1]]:1: warning: 'TCBox6' should be declared with 'struct', like Malterlib types are [malterlib-struct-not-class]
// CHECK-FIXES: struct TCBox6
{
public:
	t_CType m_Value;
};
struct CBox6Friend
{
	template <typename t_CType>
	friend class TCBox6;
// CHECK-FIXES: {{^}}	friend struct TCBox6;
};

// A forward declaration of the primary template, fixed with it.
template <typename t_CType>
class TCBox7;
// CHECK-FIXES: struct TCBox7;
template <typename t_CType>
class TCBox7
// CHECK-MESSAGES: :[[@LINE-1]]:1: warning: 'TCBox7' should be declared with 'struct', like Malterlib types are [malterlib-struct-not-class]
// CHECK-FIXES: struct TCBox7
{
public:
	t_CType m_Value;
};

// An explicit instantiation of an instance of the primary template, fixed
// with it.
template <typename t_CType>
class TCBox8
// CHECK-MESSAGES: :[[@LINE-1]]:1: warning: 'TCBox8' should be declared with 'struct', like Malterlib types are [malterlib-struct-not-class]
// CHECK-FIXES: struct TCBox8
{
public:
	t_CType m_Value;
};
template class TCBox8<int>;
// CHECK-FIXES: template struct TCBox8<int>;
