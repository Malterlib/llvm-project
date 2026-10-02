// RUN: %check_clang_tidy -std=c++20-or-later %s malterlib-struct-not-class %t -- -config="{CheckOptions: {malterlib.OptionsFile: '%S/Inputs/ClangTidyOptions.yaml'}}" -exclude-header-filter='struct-not-class\.h' -- -I %S/Inputs

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
// CHECK-MESSAGES: :[[@LINE-1]]:1: warning: 'CPublicFirst' starts with a 'public:' section, which is the default of a struct [malterlib-struct-not-class]
	int m_Value;
};

struct CPrivateSection
{
	int m_Value;

public:
	int m_Other;
};

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
// CHECK-MESSAGES: :[[@LINE-1]]:1: warning: {{.*}} starts with a 'public:' section, which is the default of a struct [malterlib-struct-not-class]
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

// Another template of the same name keeps the instantiations from being
// fixed by their text.
namespace NFirstThing
{
	template <typename t_CType>
	class TCSameName
// CHECK-MESSAGES: :[[@LINE-1]]:2: warning: 'TCSameName' should be declared with 'struct', like Malterlib types are [malterlib-struct-not-class]
// CHECK-FIXES: class TCSameName
	{
	public:
		int m_Value;
	};
	extern template class TCSameName<int>;
}
namespace NSecondThing
{
	template <typename t_CType>
	class TCSameName // NOLINT(malterlib-struct-not-class)
	{
		int mp_Value;
	};
	extern template class TCSameName<int>;
}

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
// CHECK-MESSAGES: :[[@LINE-1]]:1: warning: 'TCSystemExternal<CRound166Value>' starts with a 'public:' section, which is the default of a struct [malterlib-struct-not-class]
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
