// RUN: %check_clang_tidy -std=c++20-or-later %s malterlib-struct-not-class %t -- -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml', malterlib.FixIts: true}}" -exclude-header-filter='struct-not-class\.h' -- -I %S/Inputs

struct CStruct
{
	int m_Value;
};

struct CBase
{
};

class CClass
// CHECK-MESSAGES: :[[@LINE-1]]:1: warning: 'CClass' should be declared with 'struct', like Malterlib types are [malterlib-struct-not-class]
// CHECK-FIXES: struct CClass
{
public:
	void f_Function();
};
// CHECK-FIXES-NOT: public:

class CPrivateFirst
// CHECK-MESSAGES: :[[@LINE-1]]:1: warning: 'CPrivateFirst' should be declared with 'struct', like Malterlib types are [malterlib-struct-not-class]
// CHECK-FIXES: struct CPrivateFirst
{
private:
	int mp_Value;
};

// Members before the first access specifier are private in a class, so there
// is no fix.
class CImplicitlyPrivate
// CHECK-MESSAGES: :[[@LINE-1]]:1: warning: 'CImplicitlyPrivate' should be declared with 'struct', like Malterlib types are [malterlib-struct-not-class]
// CHECK-FIXES: class CImplicitlyPrivate
{
	int mp_Value;
};

class CImplicitBase : CBase
// CHECK-MESSAGES: :[[@LINE-1]]:1: warning: 'CImplicitBase' should be declared with 'struct', like Malterlib types are [malterlib-struct-not-class]
// CHECK-FIXES: class CImplicitBase : CBase
{
};

class CExplicitBase : public CBase
// CHECK-MESSAGES: :[[@LINE-1]]:1: warning: 'CExplicitBase' should be declared with 'struct', like Malterlib types are [malterlib-struct-not-class]
// CHECK-FIXES: struct CExplicitBase : public CBase
{
};

template <typename t_CType>
class TCTemplate
// CHECK-MESSAGES: :[[@LINE-1]]:1: warning: 'TCTemplate' should be declared with 'struct', like Malterlib types are [malterlib-struct-not-class]
// CHECK-FIXES: struct TCTemplate
{
};

// A declaration that names a struct with `class` is reported there.
class CForward;
// CHECK-MESSAGES: :[[@LINE-1]]:1: warning: 'CForward' is a struct, but is named with 'class' here; name it with 'struct' [malterlib-struct-not-class]
struct CForward
{
	friend class CStruct;
// CHECK-MESSAGES: :[[@LINE-1]]:9: warning: 'CStruct' is a struct, but is named with 'class' here; name it with 'struct' [malterlib-struct-not-class]
// CHECK-FIXES: friend struct CStruct;
};

// The declarations of a class that can't be fixed keep its tag. They are
// reported with its definition.
class CKeptForward;
// CHECK-FIXES: class CKeptForward;
class CKeptForward
// CHECK-MESSAGES: :[[@LINE-1]]:1: warning: 'CKeptForward' should be declared with 'struct', like Malterlib types are [malterlib-struct-not-class]
{
	int mp_Value;
};

#define DTag class
DTag CFromMacro
// CHECK-MESSAGES: :[[@LINE-2]]:14: warning: 'CFromMacro' should be declared with 'struct', like Malterlib types are [malterlib-struct-not-class]
// CHECK-MESSAGES: :[[@LINE-3]]:14: warning: 'CMacroDefined' should be declared with 'struct', like Malterlib types are [malterlib-struct-not-class]
{
public:
	int m_Value;
};
// CHECK-FIXES: #define DTag class

// A forward declaration keeps the tag of a definition whose keyword comes
// from a macro.
class CMacroDefined;
// CHECK-FIXES: class CMacroDefined;
DTag CMacroDefined
{
public:
	int m_Value;
};

// A type that is not defined here may be external, with a tag that has to
// match its definition.
class CExternal;

struct CPublicFirst
{
public:
// CHECK-MESSAGES: :[[@LINE-1]]:1: warning: 'public:' repeats the access in effect in 'CPublicFirst', the default of a struct [malterlib-struct-not-class]
	int m_Value;
};

// Also after members, `struct CActor { using CType = int; public: ...`.
struct CPrivateSection
{
	int m_Value;

public:
// CHECK-MESSAGES: :[[@LINE-1]]:1: warning: 'public:' repeats the access in effect in 'CPrivateSection', the default of a struct [malterlib-struct-not-class]
	int m_Other;
};
// CHECK-FIXES: {{^}}struct CPrivateSection{{$}}
// CHECK-FIXES-NOT: {{^}}public:
// CHECK-FIXES: {{^}}	int m_Other;{{$}}

template <template <typename> class t_TCTemplate>
struct TCTemplateTemplate;

enum class EScoped
{
	mc_Value
};

class NoLint // NOLINT(malterlib-struct-not-class)
{
};

// The definition is in a header, whose fixes are not applied, so the
// declaration keeps its tag.
#include "struct-not-class.h"
class CInHeader;
// CHECK-FIXES: class CInHeader;

// A forward declaration in a header, whose fixes are not applied, keeps its
// tag, so the definition keeps its own.
class CDeclaredInHeader
// CHECK-MESSAGES: :[[@LINE-1]]:1: warning: 'CDeclaredInHeader' should be declared with 'struct', like Malterlib types are [malterlib-struct-not-class]
// CHECK-FIXES: class CDeclaredInHeader
{
public:
	int m_Value;
};

// The definition fixes the tags of every declaration, so suppressing the
// diagnostic of the forward declaration can't leave them mismatched.
class CSuppressedForward; // NOLINT(malterlib-struct-not-class)
// CHECK-FIXES: struct CSuppressedForward; // NOLINT(malterlib-struct-not-class)
class CSuppressedForward
// CHECK-MESSAGES: :[[@LINE-1]]:1: warning: 'CSuppressedForward' should be declared with 'struct', like Malterlib types are [malterlib-struct-not-class]
// CHECK-FIXES: struct CSuppressedForward
{
public:
	int m_Value;
};

// A friend declaration of a class is reported and fixed with the definition.
namespace NFriends
{
class CFriendTarget
// CHECK-MESSAGES: :[[@LINE-1]]:1: warning: 'CFriendTarget' should be declared with 'struct', like Malterlib types are [malterlib-struct-not-class]
// CHECK-FIXES: struct CFriendTarget
{
public:
	int m_Value;
};

}

struct CHasFriend
{
	friend class NFriends::CFriendTarget;
// CHECK-FIXES: friend struct NFriends::CFriendTarget;
};

// An elaborated type specifier of a class is fixed with the definition too, or
// the tags would not match.
class CElaborated
// CHECK-MESSAGES: :[[@LINE-1]]:1: warning: 'CElaborated' should be declared with 'struct', like Malterlib types are [malterlib-struct-not-class]
// CHECK-FIXES: struct CElaborated
{
public:
	int m_Value;
};

void fg_UseElaborated(class CElaborated *_pValue);
// CHECK-FIXES: void fg_UseElaborated(struct CElaborated *_pValue);

// A use of a specialization of a class template has the tag of the template.
template <typename t_CType>
class TCSpecialized
// CHECK-MESSAGES: :[[@LINE-1]]:1: warning: 'TCSpecialized' should be declared with 'struct', like Malterlib types are [malterlib-struct-not-class]
// CHECK-FIXES: struct TCSpecialized
{
public:
	t_CType m_Value;
};

class TCSpecialized<int> *g_pSpecialized;
// CHECK-FIXES: struct TCSpecialized<int> *g_pSpecialized;

struct CRound35Thing
{
};

// A struct named with `class`.
void fg_Round35(class CRound35Thing *_pThing);
// CHECK-MESSAGES: :[[@LINE-1]]:17: warning: 'CRound35Thing' is a struct, but is named with 'class' here; name it with 'struct' [malterlib-struct-not-class]
// CHECK-FIXES: void fg_Round35(struct CRound35Thing *_pThing);

template <typename t_CType>
struct TCRound35Thing
{
};

// A dependent friend.
template <typename t_CType>
struct TCRound35Holder
{
	friend class TCRound35Thing<t_CType>;
// CHECK-MESSAGES: :[[@LINE-1]]:9: warning: 'TCRound35Thing' is a struct, but is named with 'class' here; name it with 'struct' [malterlib-struct-not-class]
};

// Unnamed types.
typedef class
// CHECK-MESSAGES: :[[@LINE-1]]:9: warning: {{.*}} should be declared with 'struct', like Malterlib types are [malterlib-struct-not-class]
{
public:
	int m_Value;
} CRound35Unnamed;

struct
{
public:
// CHECK-MESSAGES: :[[@LINE-1]]:1: warning: 'public:' repeats the access in effect in {{.*}}, the default of a struct [malterlib-struct-not-class]
	int m_Value;
} g_Round35Unnamed;

// A dependent specialization has the tag of its template.
template <typename t_CType>
void fg_DependentElaborated(class TCRound35Thing<t_CType> *_pThing);
// CHECK-MESSAGES: :[[@LINE-1]]:29: warning: 'TCRound35Thing' is a struct, but is named with 'class' here; name it with 'struct' [malterlib-struct-not-class]
// CHECK-FIXES: void fg_DependentElaborated(struct TCRound35Thing<t_CType> *_pThing);
template <typename t_CType>
void fg_DependentClassElaborated(class TCSpecialized<t_CType> *_pValue);
// CHECK-FIXES: void fg_DependentClassElaborated(struct TCSpecialized<t_CType> *_pValue);

// A keyword from a macro, which other types can share, isn't fixed.
struct CMacroFriendTarget
{
};
#define DFriendTag class
struct CMacroFriendHolder
{
	friend DFriendTag CMacroFriendTarget;
// CHECK-MESSAGES: :[[@LINE-4]]:20: warning: 'CMacroFriendTarget' is a struct, but is named with 'class' here; name it with 'struct' [malterlib-struct-not-class]
};
// CHECK-FIXES: #define DFriendTag class

// The explicit instantiations of a class template are fixed with it.
template <typename t_CType>
class TCInstantiated
// CHECK-MESSAGES: :[[@LINE-1]]:1: warning: 'TCInstantiated' should be declared with 'struct', like Malterlib types are [malterlib-struct-not-class]
// CHECK-FIXES: struct TCInstantiated
{
public:
	t_CType m_Value;
};
extern template class TCInstantiated<int>;
// CHECK-FIXES: extern template struct TCInstantiated<int>;
template class TCInstantiated<long>;
// CHECK-FIXES: template struct TCInstantiated<long>;

// The explicit instantiations of a struct template written with 'class'.
template <typename t_CType>
struct TCInstantiatedStruct {
	t_CType m_Value;
};
extern template class TCInstantiatedStruct<int>;
// CHECK-MESSAGES: :[[@LINE-1]]:17: warning: 'TCInstantiatedStruct' is a struct, but is named with 'class' here; name it with 'struct' [malterlib-struct-not-class]
// CHECK-FIXES: extern template struct TCInstantiatedStruct<int>;
template class TCInstantiatedStruct<long>;
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: 'TCInstantiatedStruct' is a struct, but is named with 'class' here; name it with 'struct' [malterlib-struct-not-class]
// CHECK-FIXES: template struct TCInstantiatedStruct<long>;

// A class template with an explicit instantiation whose tag comes from a
// macro is reported but not fixed.
#define DInstantiationTag class
template <typename t_CType>
class TCMacroInstantiated
// CHECK-MESSAGES: :[[@LINE-1]]:1: warning: 'TCMacroInstantiated' should be declared with 'struct', like Malterlib types are [malterlib-struct-not-class]
// CHECK-FIXES: class TCMacroInstantiated
{
public:
	t_CType m_Value;
};
extern template DInstantiationTag TCMacroInstantiated<int>;
template DInstantiationTag TCMacroInstantiated<long>;

// Both explicit instantiations of a specialization are fixed with it.
template <typename t_CType>
class TCTwiceInstantiated
// CHECK-MESSAGES: :[[@LINE-1]]:1: warning: 'TCTwiceInstantiated' should be declared with 'struct', like Malterlib types are [malterlib-struct-not-class]
// CHECK-FIXES: struct TCTwiceInstantiated
{
public:
	t_CType m_Value;
};
extern template class TCTwiceInstantiated<int>;
// CHECK-FIXES: extern template struct TCTwiceInstantiated<int>;
template class TCTwiceInstantiated<int>;
// CHECK-FIXES: template struct TCTwiceInstantiated<int>;

// Of two templates of the same name, the explicit instantiations of each are
// fixed with it: those of one that is fixed, not those of one that can't be.
namespace NFirstThing
{
	template <typename t_CType>
	class TCSameName
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: 'TCSameName' should be declared with 'struct', like Malterlib types are [malterlib-struct-not-class]
// CHECK-FIXES: {{^}}	struct TCSameName{{$}}
	{
	public:
		int m_Value;
	};
	extern template class TCSameName<int>;
// CHECK-FIXES: {{^}}	extern template struct TCSameName<int>;
}
namespace NSecondThing
{
	template <typename t_CType>
	class TCSameName // NOLINT(malterlib-struct-not-class)
	{
		int mp_Value;
	};
	extern template class TCSameName<int>;
// CHECK-FIXES: {{^}}	extern template class TCSameName<int>;
}

// An explicit instantiation is a declaration of its own, whose tag follows
// `template`, also with an attribute or a macro of one after it, which is
// fixed with the class.
#define DTagExport __attribute__((visibility("default")))
template <typename t_CType>
class TCExported
// CHECK-MESSAGES: :[[@LINE-1]]:1: warning: 'TCExported' should be declared with 'struct', like Malterlib types are [malterlib-struct-not-class]
// CHECK-FIXES: {{^}}struct TCExported{{$}}
{
public:
	t_CType m_Value;
};
extern template class DTagExport TCExported<int>;
// CHECK-FIXES: {{^}}extern template struct DTagExport TCExported<int>;
template class __attribute__((visibility("default"))) TCExported<int>;
// CHECK-FIXES: {{^}}template struct __attribute__((visibility("default"))) TCExported<int>;

// One explicit instantiation whose tag a macro gives keeps the class and the
// others from being fixed.
#define DTagClass class
template <typename t_CType>
class TCOneFromMacro
// CHECK-MESSAGES: :[[@LINE-1]]:1: warning: 'TCOneFromMacro' should be declared with 'struct', like Malterlib types are [malterlib-struct-not-class]
// CHECK-FIXES: {{^}}class TCOneFromMacro{{$}}
{
public:
	t_CType m_Value;
};
extern template DTagClass TCOneFromMacro<int>;
template class TCOneFromMacro<int>;
// CHECK-FIXES: {{^}}template class TCOneFromMacro<int>;

// The explicit instantiation of a class of a template is fixed with the class,
// and one of a struct written with `class` is reported.
template <typename t_CType>
struct TCHolder
{
	class CInner
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: 'CInner' should be declared with 'struct', like Malterlib types are [malterlib-struct-not-class]
// CHECK-FIXES: {{^}}	struct CInner{{$}}
	{
	public:
		t_CType m_Value;
	};
	struct CPlain
	{
		t_CType m_Value;
	};
	class CFromMacro
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: 'CFromMacro' should be declared with 'struct', like Malterlib types are [malterlib-struct-not-class]
// CHECK-FIXES: {{^}}	class CFromMacro{{$}}
	{
	public:
		t_CType m_Value;
	};
};
extern template class TCHolder<int>::CInner;
// CHECK-FIXES: {{^}}extern template struct TCHolder<int>::CInner;
template class TCHolder<int>::CInner;
// CHECK-FIXES: {{^}}template struct TCHolder<int>::CInner;
template class TCHolder<int>::CPlain;
// CHECK-MESSAGES: :[[@LINE-1]]:10: warning: 'CPlain' is a struct, but is named with 'class' here; name it with 'struct' [malterlib-struct-not-class]
// CHECK-FIXES: {{^}}template struct TCHolder<int>::CPlain;
template DTagClass TCHolder<int>::CFromMacro;

// Also qualified explicit instantiations.
namespace NQualifiedThing
{
	template <typename t_CType>
	class TCQualifiedThing
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: 'TCQualifiedThing' should be declared with 'struct', like Malterlib types are [malterlib-struct-not-class]
// CHECK-FIXES: struct TCQualifiedThing
	{
	public:
		int m_Value;
	};
}
extern template class NQualifiedThing::TCQualifiedThing<int>;
// CHECK-FIXES: extern template struct NQualifiedThing::TCQualifiedThing<int>;
template class NQualifiedThing::TCQualifiedThing<int>;
// CHECK-FIXES: template struct NQualifiedThing::TCQualifiedThing<int>;

// An instantiation of another template of the same name in text the
// preprocessor skipped isn't fixed.
template <typename t_CType>
class TCRound64Thing
// CHECK-MESSAGES: :[[@LINE-1]]:1: warning: 'TCRound64Thing' should be declared with 'struct', {{.*}}
{
public:
};
template class TCRound64Thing<int>;
// CHECK-FIXES: template struct TCRound64Thing<int>;
// CHECK-FIXES: {{^}}	template class TCRound64Thing<int>;
#ifdef DROUND64_VENDOR
namespace NRound64Vendor
{
	template <typename t_CType>
	class TCRound64Thing // NOLINT(malterlib-struct-not-class)
	{
	public:
	};
	template class TCRound64Thing<int>;
}
#endif

// A friend declaration that introduces the class is fixed once.
struct CRound66Holder
{
	friend class CRound66Target;
};
// CHECK-FIXES: friend struct CRound66Target;
class CRound66Target
// CHECK-MESSAGES: :[[@LINE-1]]:1: warning: 'CRound66Target' should be declared with 'struct', like Malterlib types are [malterlib-struct-not-class]
{
public:
	int m_Value;
};

// A friend of a class nested in a template, which the fix changes with it.
template <typename t_CType>
struct TCRound81Outer
{
	class CInner
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: 'CInner' should be declared with 'struct', like Malterlib types are [malterlib-struct-not-class]
// CHECK-FIXES: struct CInner
	{
	public:
		int m_Value;
	};
};
template <typename t_CType>
struct TCRound81Friend
{
	friend class TCRound81Outer<t_CType>::CInner;
// CHECK-FIXES: friend struct TCRound81Outer<t_CType>::CInner;
};
TCRound81Friend<int> g_Round81Friend;

// A friend of a struct nested in a base of a template.
struct CRound85Base
{
	struct CInner
	{
	};
};
template <typename t_CType>
struct TCRound85Outer : CRound85Base
{
};
template <typename t_CType>
struct TCRound85Friend
{
	friend class TCRound85Outer<t_CType>::CInner;
// CHECK-MESSAGES: :[[@LINE-1]]:9: warning: 'CInner' is a struct, but is named with 'class' here; name it with 'struct' [malterlib-struct-not-class]
// CHECK-FIXES: friend struct TCRound85Outer<t_CType>::CInner;
};
TCRound85Friend<int> g_Round85Friend;

// A specialization named with a qualified `class` is fixed with its template.
template <typename t_CType>
class TCRound87Box
// CHECK-MESSAGES: :[[@LINE-1]]:1: warning: 'TCRound87Box' should be declared with 'struct', like Malterlib types are [malterlib-struct-not-class]
// CHECK-FIXES: struct TCRound87Box
{
};
int fg_Round87Size(class TCRound87Box<int> const &_Box)
// CHECK-FIXES: int fg_Round87Size(struct TCRound87Box<int> const &_Box)
{
	return sizeof(_Box);
}

// A friend named through a template that is specialized keeps its tag, which
// a specialization can declare differently.
template <typename t_CType>
struct TCRound90Outer
{
	class CInner
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: 'CInner' should be declared with 'struct', like Malterlib types are [malterlib-struct-not-class]
// CHECK-FIXES: class CInner
	{
	public:
		void f_Run();
	};
};
template <>
struct TCRound90Outer<int>
{
	class CInner
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: 'CInner' should be declared with 'struct', like Malterlib types are [malterlib-struct-not-class]
	{
		int mp_Private;
	};
};
template <typename t_CType>
struct TCRound90Friend
{
	friend class TCRound90Outer<t_CType>::CInner;
// CHECK-FIXES: friend class TCRound90Outer<t_CType>::CInner;
};
TCRound90Friend<int> g_Round90Friend;

// A friend of a class nested in a base that depends on the template.
template <typename t_CType>
struct TCRound92Base
{
	class CInner
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: 'CInner' should be declared with 'struct', like Malterlib types are [malterlib-struct-not-class]
// CHECK-FIXES: struct CInner
	{
	public:
		int m_Value;
	};
};
template <typename t_CType>
struct TCRound92Outer : TCRound92Base<t_CType>
{
};
template <typename t_CType>
struct TCRound92Friend
{
	friend class TCRound92Outer<t_CType>::CInner;
// CHECK-FIXES: friend struct TCRound92Outer<t_CType>::CInner;
};
TCRound92Friend<int> g_Round92Friend;

// A struct named through a specialized template can be a class of a
// specialization, so the friend's keyword isn't reported.
template <typename t_CType>
struct TCRound93Outer
{
	struct CInner
	{
	};
};

template <>
struct TCRound93Outer<int>
{
	class CInner // NOLINT(malterlib-struct-not-class)
	{
	public:
	};
};

template <typename t_CType>
struct TCRound93Friend
{
	friend class TCRound93Outer<t_CType>::CInner;
};
TCRound93Friend<int> g_Round93Friend;

// A class of an instantiation is fixed with the class of the template.
template <typename t_CType>
struct TCRound94Outer
{
	class CInner
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: 'CInner' should be declared with 'struct', like Malterlib types are [malterlib-struct-not-class]
// CHECK-FIXES: struct CInner
	{
	public:
		int m_Value;
	};
};
class TCRound94Outer<int>::CInner *g_pRound94Inner;
// CHECK-FIXES: struct TCRound94Outer<int>::CInner *g_pRound94Inner;

// Round 113: a friend of a specialization made from a partial specialization matches the partial specialization
namespace NRound113Partial
{
template<typename t_CType> struct TCBox {};
template<typename t_CType>
class TCBox<t_CType *> { // NOLINT(malterlib-struct-not-class)
  int mp_Value;
};
TCBox<int *> g_Box;
struct CFriend {
  friend class TCBox<int *>;
};
}

// Round 120: a struct nested in a dependent class, named with `class`.
namespace NRound120Dependent {
template <typename t_CType>
struct TCOuter {
  struct CInner {};
};

template <typename t_CType>
void fg_Visit(class TCOuter<t_CType>::CInner *_pValue) {}
// CHECK-MESSAGES: :[[@LINE-1]]:15: warning: 'CInner' is a struct, but is named with 'class' here; name it with 'struct' [malterlib-struct-not-class]

void fg_Use() { fg_Visit<int>(nullptr); }
}

// Round 122: a class nested deeper in a dependent one of a template that is
// specialized can be a class of the specialization.
namespace NRound122Nested {
template <typename t_CType>
struct TCOuter {
  struct CNested { struct CInner {}; };
};

template <>
struct TCOuter<int> {
  struct CNested { class CInner {}; }; // NOLINT(malterlib-struct-not-class)
};

template <typename t_CType>
struct TCUse {
  class TCOuter<t_CType>::CNested::CInner *m_pInner;
};

TCUse<int> g_Use;
}

// Round 125: an explicit instantiation of a partial specialization is of
// that, a class here, whose definition is reported.
template <typename t_CType>
struct TCRound125Box
{
};
template <typename t_CType>
class TCRound125Box<t_CType *>
// CHECK-MESSAGES: :[[@LINE-1]]:1: warning: 'TCRound125Box<t_CType *>' should be declared with 'struct', like Malterlib types are [malterlib-struct-not-class]
{
	int m_Value;
};
template class TCRound125Box<int *>;

// Round 127: the primary template isn't fixed when an explicit instantiation
// is of a partial specialization that is a class, whose tag it keeps.
template <typename t_CType>
class TCRound127Box
// CHECK-FIXES: {{^}}class TCRound127Box{{$}}
// CHECK-MESSAGES: :[[@LINE-2]]:1: warning: 'TCRound127Box' should be declared with 'struct', like Malterlib types are [malterlib-struct-not-class]
{
public:
	t_CType m_Value;
};
template <typename t_CType>
class TCRound127Box<t_CType *>
// CHECK-MESSAGES: :[[@LINE-1]]:1: warning: 'TCRound127Box<t_CType *>' should be declared with 'struct', like Malterlib types are [malterlib-struct-not-class]
{
	t_CType *mp_pValue;
};
template class TCRound127Box<int *>;

// Round 131: a partial specialization that is explicitly instantiated isn't
// fixed, since the tag of the instantiation can't be told apart.
template <typename t_CType>
struct TCRound131Box
{
};
template <typename t_CType>
class TCRound131Box<t_CType *>
// CHECK-MESSAGES: :[[@LINE-1]]:1: warning: 'TCRound131Box<t_CType *>' should be declared with 'struct', like Malterlib types are [malterlib-struct-not-class]
// CHECK-FIXES: {{^}}class TCRound131Box<t_CType *>{{$}}
{
public:
	t_CType *mp_pValue;
};
template class TCRound131Box<int *>;

// A class with conditional preprocessing in its body, whose skipped branches
// can have members before the first access specifier, isn't fixed.
class CPreprocessed
// CHECK-MESSAGES: :[[@LINE-1]]:1: warning: 'CPreprocessed' should be declared with 'struct', like Malterlib types are [malterlib-struct-not-class]
{
#ifdef DPrivateLayout
	int mp_Value;
#else
public:
	int m_Value;
#endif
};
// CHECK-FIXES: class CPreprocessed

// A 'public:' after conditional preprocessing, which another configuration
// needs, isn't reported.
struct CConditionalAccess
{
#if DPrivateLayout
private:
#endif
public:
	int m_Value;
};

// Nor is a class fixed whose directives are after comments.
class CCommented
// CHECK-MESSAGES: :[[@LINE-1]]:1: warning: 'CCommented' should be declared with 'struct', like Malterlib types are [malterlib-struct-not-class]
{
/* Layout */ #if DPrivateLayout
	int mp_Private;
/* Otherwise */ #else
public:
/* End */ #endif
	int m_Value;
};
// CHECK-FIXES: class CCommented

namespace NRound146 {
template <typename t_CType>
struct TCOuter
{
	template <typename t_CInner>
	struct TCInner
	{
	};
};

// A class template nested in a dependent one.
template <typename t_CType>
void fg_Run(class TCOuter<t_CType>::template TCInner<int> *_pValue)
// CHECK-MESSAGES: :[[@LINE-1]]:13: warning: 'TCInner' is a struct, but is named with 'class' here; name it with 'struct' [malterlib-struct-not-class]
// CHECK-FIXES: void fg_Run(struct TCOuter<t_CType>::template TCInner<int> *_pValue)
{
}

void fg_Use()
{
	TCOuter<int>::TCInner<int> Value;
	fg_Run<int>(&Value);
}
}

namespace NRound150 {
#ifdef DRound150Private
#define DRound150Members private: int mp_Value;
#else
#define DRound150Members
#endif
// A section after a macro that another configuration can make private.
struct CExample
{
	DRound150Members
public:
	int m_Value;
};
}

namespace NRound151 {
#ifdef DRound151Layout
#define DRound151Members int mp_Secret;
#else
#define DRound151Members
#endif
// A class with what expands to nothing before its first access specifier,
// which another configuration can make private members, isn't fixed.
class CExample
// CHECK-MESSAGES: :[[@LINE-1]]:1: warning: 'CExample' should be declared with 'struct', like Malterlib types are [malterlib-struct-not-class]
// CHECK-FIXES: {{^}}class CExample{{$}}
{
	DRound151Members
public:
	void f_Run();
};
}

// Not fixed when conditional preprocessing in its bases can change their
// access.
struct CRound160Base
{
};
class CRound160Object :
// CHECK-MESSAGES: :[[@LINE-1]]:1: warning: 'CRound160Object' should be declared with 'struct', like Malterlib types are [malterlib-struct-not-class]
// CHECK-FIXES: {{^}}class CRound160Object :
#ifdef DRound160Private
	CRound160Base
#else
	public CRound160Base
#endif
{
public:
	void f_Work();
};

// A leading 'public:' isn't removed when the opening brace is from a macro,
// which can declare members before it in another configuration.
#if DRound163Private
#define DRound163Open { private: int mp_Secret;
#else
#define DRound163Open {
#endif
struct CRound163Object DRound163Open
public:
	int m_Value;
};

// Nor a class whose first access specifier comes from a macro, which another
// configuration can leave out.
#ifdef DRound165Private
#define DRound165Interface
#else
#define DRound165Interface public:
#endif
class CRound165Thing
// CHECK-MESSAGES: :[[@LINE-1]]:1: warning: 'CRound165Thing' should be declared with 'struct', like Malterlib types are [malterlib-struct-not-class]
{
	DRound165Interface
	int m_Value;
};
// CHECK-FIXES: class CRound165Thing

// A specialization written here of a template of a system header: its tag
// matches the template's, but a leading `public:` is redundant.
#include "struct-not-class-system.h"
struct CRound166Value
{
};
template <>
struct TCSystemExternal<CRound166Value>
{
public:
// CHECK-MESSAGES: :[[@LINE-1]]:1: warning: 'public:' repeats the access in effect in 'TCSystemExternal<CRound166Value>', the default of a struct [malterlib-struct-not-class]
	void f_Run();
};
template <>
class TCSystemExternalClass<CRound166Value>
{
public:
	void f_Run();
};

// A friend class template, `template <...> friend class TCList;`, is fixed
// with the definition, and one that names a struct with `class` is reported.
template <typename t_CData>
class TCRound354List
// CHECK-MESSAGES: :[[@LINE-1]]:1: warning: 'TCRound354List' should be declared with 'struct', like Malterlib types are [malterlib-struct-not-class]
// CHECK-FIXES: struct TCRound354List
{
};
template <typename t_CData>
struct TCRound354Member
{
	template <typename t_CData2> friend class TCRound354List;
// CHECK-FIXES: template <typename t_CData2> friend struct TCRound354List;
};
template <typename t_CData>
struct TCRound354Node
{
};
struct CRound354Holder
{
	template <typename t_CData2> friend class TCRound354Node;
// CHECK-MESSAGES: :[[@LINE-1]]:38: warning: 'TCRound354Node' is a struct, but is named with 'class' here; name it with 'struct' [malterlib-struct-not-class]
// CHECK-FIXES: template <typename t_CData2> friend struct TCRound354Node;
};

struct CRound9Value
{
};
template <>
class TCSystemExternalClass<CRound9Value>
{
private:
// CHECK-MESSAGES: :[[@LINE-1]]:1: warning: 'private:' repeats the access in effect in 'TCSystemExternalClass<CRound9Value>', the default of a class [malterlib-struct-not-class]
	int mp_Value;

public:
	void f_Run();
};

// A label that repeats the access in effect is redundant: also that of the
// label before it, of a struct, a union or a class, and the default of a union,
// or of a class kept as one, an external type's.
namespace NRepeatedAccess {
struct CRepeated
{
	void f_Run();

private:
	void fp_Run();
private:
// CHECK-MESSAGES: :[[@LINE-1]]:1: warning: 'private:' repeats the access in effect in 'CRepeated', that of the label before it [malterlib-struct-not-class]
	int mp_Value;
};
union URepeated
{
public:
// CHECK-MESSAGES: :[[@LINE-1]]:1: warning: 'public:' repeats the access in effect in 'URepeated', the default of a union [malterlib-struct-not-class]
	int m_Integer;
	float m_Float;
};
class CClassRepeated
// CHECK-MESSAGES: :[[@LINE-1]]:1: warning: 'CClassRepeated' should be declared with 'struct', like Malterlib types are [malterlib-struct-not-class]
{
private:
	int mp_Value;
protected:
	int mp_Protected;
protected:
// CHECK-MESSAGES: :[[@LINE-1]]:1: warning: 'protected:' repeats the access in effect in 'CClassRepeated', that of the label before it [malterlib-struct-not-class]
	int mp_Other;
};
// Of a class template as written, not its instantiations.
template <typename t_CType>
struct TCRepeated
{
	t_CType m_Value;
public:
// CHECK-MESSAGES: :[[@LINE-1]]:1: warning: 'public:' repeats the access in effect in 'TCRepeated', the default of a struct [malterlib-struct-not-class]
	void f_Run();
};
template struct TCRepeated<int>;

// Not after conditional preprocessing since the label before it, which
// another configuration needs, nor after what the AST doesn't have, a macro
// that expands to nothing here, nor after members from a macro, also
// only their type or what is before it, nor a label
// from a macro, nor one after it.
struct CConditional
{
private:
	int mp_Value;
#if DPrivateLayout
public:
	int m_Public;
#endif
private:
	int mp_Other;
};
#ifdef DRepeatedPublic
#define DRepeatedMembers public: int m_Public;
#define DRepeatedMember int m_Public;
#define DRepeatedLabel public:
#else
#define DRepeatedMembers
#define DRepeatedMember int mp_Hidden;
#define DRepeatedLabel private:
#endif
struct CEmptyMacro
{
private:
	int mp_Value;
	DRepeatedMembers
private:
	int mp_Other;
};
struct CMacroMember
{
private:
	DRepeatedMember
private:
	int mp_Other;
};
#ifdef DRepeatedPublic
#define DRepeatedType public: int
#define DRepeatedPrefix public:
#else
#define DRepeatedType int
#define DRepeatedPrefix
#endif
struct CMacroType
{
private:
	DRepeatedType mp_First;
private:
	int mp_Secret;
};
struct CMacroPrefix
{
private:
	DRepeatedPrefix int mp_First;
private:
	int mp_Secret;
};
// Any use of a macro between them is: also inside a member, after its name,
// in an alias, a member template or an initializer, and a colon from a macro.
#define DRepeatedNothing
#define DRepeatedSuffix
#define DRepeatedValue 1
#define DRepeatedColon :
struct CMacroInside
{
private:
	int DRepeatedNothing mp_First;
private:
	int mp_Secret;
};
struct CMacroSuffix
{
private:
	int mp_First DRepeatedSuffix;
private:
	int mp_Secret;
};
struct CMacroAlias
{
private:
	using CType = DRepeatedType;
private:
	int mp_Secret;
};
struct CMacroTemplate
{
private:
	template <typename t_CType>
	DRepeatedType fp_Run(t_CType _Value);
private:
	int mp_Secret;
};
struct CMacroInitializer
{
private:
	int mp_First = DRepeatedValue;
private:
	int mp_Secret;
};
struct CMacroColon
{
private:
	int mp_First;
private DRepeatedColon
	int mp_Secret;
};
// Also a macro between the label and its colon, which the fix removes too,
// and an include, also one a guard skips, which another configuration can
// make give another access.
#define DRepeatedLayout
struct CMacroBeforeColon
{
private:
	int mp_First;
private DRepeatedLayout:
	int mp_Last;
};
struct CFragmentIncluder
{
	int m_First;
#include "struct-not-class-fragment.h"
	int m_Last;
};
struct CSkippedInclude
{
private:
	int mp_First;
#include "struct-not-class-fragment.h"
private:
	int mp_Last;
};
struct CMacroLabel
{
private:
	int mp_Value;
	DRepeatedLabel
	int mp_Other;
private:
	int mp_Last;
};
}

// Repeated labels of a class whose keyword a macro gives, which isn't
// reported to be a struct, are reported too.
#define DRoundTwelveTag(a, b) a##b
DRoundTwelveTag(cl, ass) CRoundTwelvePasted
{
private:
	int mp_First;
private:
// CHECK-MESSAGES: :[[@LINE-1]]:1: warning: 'private:' repeats the access in effect in 'CRoundTwelvePasted', that of the label before it [malterlib-struct-not-class]
	int mp_Second;
};

// Not the default of one whose tag a macro gives, which can be `struct` or
// `class` in another configuration, also of an external template's
// specialization.
#define DRoundFourteenTag struct
DRoundFourteenTag CRoundFourteenTagged
{
	int m_First;
public:
	void f_Run();
};
#define DRoundFourteenClass class
struct CRoundFourteenValue
{
};
template <>
DRoundFourteenClass TCSystemExternalClass<CRoundFourteenValue>
{
private:
	int mp_Value;

public:
	void f_Run();
};

// A label with an annotation of its section is a barrier: neither it nor the
// label after it, which ends that section, is compared with what is before it.
struct CAnnotatedLabels
{
private:
	int mp_AnnotatedFirst;
private __attribute__((annotate("Section"))):
	int mp_AnnotatedSecond;
private:
	int mp_AnnotatedThird;
};
// A label is reported but not removed when a comment follows it.
struct CCommentedLabels
{
private:
	int mp_CommentedFirst;
private: // The rest of the state.
// CHECK-MESSAGES: :[[@LINE-1]]:1: warning: 'private:' repeats the access in effect in 'CCommentedLabels', that of the label before it [malterlib-struct-not-class]
	int mp_CommentedSecond;
};
// CHECK-FIXES: {{^}}	int mp_CommentedFirst;{{$}}
// CHECK-FIXES-NEXT: {{^}}private: // The rest of the state.{{$}}
struct CInsideCommentLabels
{
private:
	int mp_InsideFirst;
private /* The rest of the state. */ :
// CHECK-MESSAGES: :[[@LINE-1]]:1: warning: 'private:' repeats the access in effect in 'CInsideCommentLabels', that of the label before it [malterlib-struct-not-class]
	int mp_InsideSecond;
};
// CHECK-FIXES: {{^}}	int mp_InsideFirst;{{$}}
// CHECK-FIXES-NEXT: {{^}}private /* The rest of the state. */ :{{$}}
// Also a comment whose opening a line splice divides.
struct CSplicedCommentLabels
{
private:
	int mp_SplicedFirst;
private /\
* The rest. */ :
// CHECK-MESSAGES: :[[@LINE-2]]:1: warning: 'private:' repeats the access in effect in 'CSplicedCommentLabels', that of the label before it [malterlib-struct-not-class]
	int mp_SplicedSecond;
};
// CHECK-FIXES: {{^}}	int mp_SplicedFirst;{{$}}
// CHECK-FIXES-NEXT: {{^}}private /\{{$}}
// Nor when a preprocessing directive is in it, which removing it would remove.
struct CDirectiveLabel
{
private:
	int mp_DirectiveFirst;
private
#define DRoundTwentyValue 42
:
// CHECK-MESSAGES: :[[@LINE-3]]:1: warning: 'private:' repeats the access in effect in 'CDirectiveLabel', that of the label before it [malterlib-struct-not-class]
	int mp_DirectiveSecond = DRoundTwentyValue;
};
// CHECK-FIXES: {{^}}#define DRoundTwentyValue 42{{$}}
// Nor the default of one whose tag conditional preprocessing chooses, which
// can be `struct` or `class` in another configuration.
#ifdef DRoundTwentyThreeClass
class CChosenTag
#else
struct CChosenTag
#endif
{
	using CType = int;
public:
	void f_Run();
};
// Nor of one with a macro between its keyword and its brace, which can end the
// class and start another in another configuration, `CForward; class
// CSelected`, also one that only gives its name, but not one before the
// keyword.
#define DRoundTwentyNineName CRoundTwentyNineSelected
struct DRoundTwentyNineName
{
	using CType = int;
public:
	void f_Run();
};
#define DRoundTwentyNineAttribute alignas(8)
struct DRoundTwentyNineAttribute CRoundTwentyNineAligned
{
	using CType = int;
public:
	void f_Run();
};
#define DRoundTwentyNineExport
DRoundTwentyNineExport struct CRoundTwentyNineExported
{
	using CType = int;
public:
// CHECK-MESSAGES: :[[@LINE-1]]:1: warning: 'public:' repeats the access in effect in 'CRoundTwentyNineExported', the default of a struct [malterlib-struct-not-class]
	void f_Run();
};

// Round 203 (llvm-naming-struct-3): a first access label that comes from an
// included fragment, which can declare members before it in another
// configuration, `DPrivateMembers`: reported without the fix. Fixed: the same
// label written in the class's own file.
#define DPrivateMembers
class CFragmentFirst
// CHECK-MESSAGES: :[[@LINE-1]]:1: warning: {{.*}} [malterlib-struct-not-class]
// CHECK-FIXES: {{^}}class CFragmentFirst{{$}}
{
#include "struct-not-class-members-fragment.h"
};
class CWrittenFirst
// CHECK-MESSAGES: :[[@LINE-1]]:1: warning: {{.*}} [malterlib-struct-not-class]
// CHECK-FIXES: {{^}}struct CWrittenFirst{{$}}
{
public:
	void f_Run();
};

// Round 205 (llvm-naming-struct-10): a label whose colon follows a line
// splice is removed through its colon as the lexer reads it, the leading
// `public:` and a repeated one alike, which leaves no stray colon.
class CSplicedLabels
// CHECK-MESSAGES: :[[@LINE-1]]:1: warning: 'CSplicedLabels' should be declared with 'struct', like Malterlib types are [malterlib-struct-not-class]
// CHECK-FIXES: {{^}}struct CSplicedLabels{{$}}
{
public\
: void f_Run();
// CHECK-FIXES: {{^}}void f_Run();{{$}}
private:
	int m_Value;
private\
: int m_Other;
// CHECK-MESSAGES: :[[@LINE-2]]:1: warning: 'private:' repeats the access in effect in 'CSplicedLabels', that of the label before it [malterlib-struct-not-class]
// CHECK-FIXES: {{^}}int m_Other;{{$}}
};

// Round 205 (llvm-naming-struct-7): a macro from the name of a class to its
// brace can give the access of a base, a base or the base clause in
// another configuration, which the default the fix changes decides:
// reported without the fix. Fixed: the bases written out. Round 206: also a
// macro before the name, `DExportTag`, which can end a class and start
// another in another configuration, anywhere from the keyword to the brace.
#define DPublicBase public
#define DBaseList CRound205Base
#define DExportTag
struct CRound205Base
{
};
class CBaseAccessFromMacro : virtual DPublicBase CRound205Base
// CHECK-MESSAGES: :[[@LINE-1]]:1: warning: 'CBaseAccessFromMacro' should be declared with 'struct', like Malterlib types are [malterlib-struct-not-class]
// CHECK-FIXES: {{^}}class CBaseAccessFromMacro : virtual DPublicBase CRound205Base{{$}}
{
public:
	void f_Run();
};
class CBaseFromMacro : public DBaseList
// CHECK-MESSAGES: :[[@LINE-1]]:1: warning: 'CBaseFromMacro' should be declared with 'struct', like Malterlib types are [malterlib-struct-not-class]
// CHECK-FIXES: {{^}}class CBaseFromMacro : public DBaseList{{$}}
{
public:
	void f_Run();
};
class CBaseWritten : virtual public CRound205Base
// CHECK-MESSAGES: :[[@LINE-1]]:1: warning: 'CBaseWritten' should be declared with 'struct', like Malterlib types are [malterlib-struct-not-class]
// CHECK-FIXES: {{^}}struct CBaseWritten : virtual public CRound205Base{{$}}
{
public:
	void f_Run();
};
class DExportTag CBaseExported : public CRound205Base
// CHECK-MESSAGES: :[[@LINE-1]]:1: warning: 'CBaseExported' should be declared with 'struct', like Malterlib types are [malterlib-struct-not-class]
// CHECK-FIXES: {{^}}class DExportTag CBaseExported : public CRound205Base{{$}}
{
public:
	void f_Run();
};

// Round 205: a keyword is read as the compiler reads it, also divided by a
// line splice, so a struct named with it is reported and fixed.
struct CSplicedTarget
{
};
struct CSplicedFriend
{
	friend cla\
ss CSplicedTarget;
// CHECK-MESSAGES: :[[@LINE-2]]:9: warning: 'CSplicedTarget' is a struct, but is named with 'class' here; name it with 'struct' [malterlib-struct-not-class]
// CHECK-FIXES: {{^}}	friend struct CSplicedTarget;{{$}}
};
template <typename t_CType>
struct TCSplicedInstance
{
};
template cla\
ss TCSplicedInstance<int>;
// CHECK-MESSAGES: :[[@LINE-2]]:10: warning: 'TCSplicedInstance' is a struct, but is named with 'class' here; name it with 'struct' [malterlib-struct-not-class]
// CHECK-FIXES: {{^}}template struct TCSplicedInstance<int>;{{$}}

// Round 206: the head of a class, from its keyword through its brace, is all
// written in its file, or the class isn't fixed: also its name from a macro,
// which can expand to `CRound206Named : CRound205Base` in another
// configuration, which the fix would make inherit publicly.
#define DRound206ClassName CRound206Named
class DRound206ClassName
// CHECK-MESSAGES: :[[@LINE-1]]:1: warning: 'CRound206Named' should be declared with 'struct', like Malterlib types are [malterlib-struct-not-class]
// CHECK-FIXES: {{^}}class DRound206ClassName{{$}}
{
public:
	void f_Run();
};
class CRound206Unnamed
// CHECK-MESSAGES: :[[@LINE-1]]:1: warning: 'CRound206Unnamed' should be declared with 'struct', like Malterlib types are [malterlib-struct-not-class]
// CHECK-FIXES: {{^}}struct CRound206Unnamed{{$}}
{
public:
	void f_Run();
};

// Round 206: a specialization of a template a system header defines is the
// library's, also after this code redeclares the template, which the
// specialization then names: not reported, its labels checked as a class's.
namespace NRound206Library
{
	template <typename t_CType>
	class TCSerializer;
}
struct CRound206Message
{
};
template <>
class NRound206Library::TCSerializer<CRound206Message>
{
public:
	void f_Write(CRound206Message const &_Message);
};
// A template of this code, declared before it is defined, is this code's.
namespace NRound206Own
{
	template <typename t_CType>
	class TCSerializer;
	template <typename t_CType>
	class TCSerializer
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: 'TCSerializer' should be declared with 'struct', like Malterlib types are [malterlib-struct-not-class]
	{
	public:
		void f_Write(t_CType const &_Value);
	};
}
template <>
class NRound206Own::TCSerializer<CRound206Message>
// CHECK-MESSAGES: :[[@LINE-1]]:1: warning: 'TCSerializer<CRound206Message>' should be declared with 'struct', like Malterlib types are [malterlib-struct-not-class]
{
public:
	void f_Write(CRound206Message const &_Message);
};
