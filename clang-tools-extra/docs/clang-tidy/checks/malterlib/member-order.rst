.. title:: clang-tidy - malterlib-member-order

malterlib-member-order
======================

Checks the order of a class's sections and of the members in each, as the
Malterlib coding standard has them.

A class has its public section first, then its protected section, then its
private section. A section that comes after one that should follow it is
reported at its label. The private section a class starts with is a section
only when it has members or a label, so ``class CExample { public: ...
private: ... };`` is in order. The check doesn't try to tell whether an earlier
section is needed to compile, like a private type that the public
declarations use: such a class keeps its order and has a
``NOLINT(malterlib-member-order)`` on the label, with the reason. The order
of sections is that of the labels written in the class: a label or member a
macro writes, like the ``public:`` and ``private:`` of Qt's ``Q_OBJECT``, is
neither reported nor what the next written label is compared with, and a
member written after it is in the section of the written labels before it. A
label written as an argument of a macro, ``DSection(public):``, is written.

Each section has, in this order:

1. Types: nested classes, enums and aliases, also templates of them, and
   types brought in from what depends on the parameters, ``using typename
   t_CBase::CType;``.
2. Constructors and the destructor, also templates of constructors and
   inherited constructors, ``using CBase::CBase;``, also of a base a class
   template is given, ``using t_CBase::t_CBase;``.
3. Other functions, also operators, conversions and function templates.

A using declaration of a member of what depends on the parameters, ``using
t_CBase::f_Run;``, is of the kind the instantiations of the class make of it,
when they agree, and isn't ordered when none is instantiated or they don't.
That is only in a class template's own pattern, also a partial
specialization written outside a class template, by its specializations; in
any other class that depends on parameters, a member class of a class
template, an explicit or partial specialization written in one, or a class
in those, it isn't ordered.

4. Constants: static members that are ``constexpr``, also those declared
   ``const`` without their value whose definition is ``constexpr``. When the
   translation unit doesn't have the definition, like a header's member
   defined in one source, one declared ``const`` without its value, also a
   reference to ``const``, ``static CStr const &mc_Empty;``, is taken as a
   constant, as variable-naming takes it (one answer for both). Of a class template, and of a member variable template,
   whose instantiations and specializations can each be defined their own
   way, also one a using declaration brings in, also from a specialization
   that doesn't depend on the parameters, ``using TCBase<int>::ms_Value;``, of
   a member variable template as the class template declares it,
   and a partial specialization of a member variable template, only what
   its declaration in the class says, also when what a using declaration
   leads to is the definition out of it, decides: a constant when it
   is declared ``constexpr``, a variable when it is given a value that isn't,
   and one declared without its value, ``static int const ms_Value;``, isn't
   ordered; its definitions out of the class, instantiations and
   specializations aren't looked at. An enumerator a using declaration brings in,
   ``using EKind::EKind_First;``, and a ``using enum EKind;`` are constants.
   One whose type depends on the parameters of its template,
   ``static t_CType ms_Value;``, can be either as it is instantiated, and
   isn't ordered.
5. Variables: the members of the objects, also an anonymous struct or union,
   and a member of one of a base a using declaration brings in,
   and static members that aren't constants, like one declared ``const``
   whose definition isn't ``constexpr``, ``int const CExample::ms_Value =
   fg_Read();``.

The fewest members that have to move are reported: those outside the
section's longest run of members in order, each with the kind it should come
before or after. So a member kept early for what needs it first, like a
constant that an alias above the functions uses, is the one reported, and like
a section it has a ``NOLINT`` with the reason. A member a ``NOLINT`` or
``NOLINTNEXTLINE`` comment on the line before, not one before a blank line, or a ``NOLINTBEGIN`` and ``NOLINTEND`` block,
covers for this check, by clang-tidy's globs, ``NOLINT(*-member-order)``, is
left out of the run, so it doesn't make another the one reported; a
``NOLINTBEGIN`` that isn't ended covers nothing, as clang-tidy reads it. An unnamed bit-field, ``int : 0;``, is no member. The order starts again in each
section, and a label that repeats the access of its section continues it.
Static assertions, what a macro writes and what a use of a member
template instantiates, ``mc_Value<int>``, aren't ordered, nor a tag that an
elaborated type specifier of a member introduces, ``struct COther
*m_pOther;``, which is of the namespace around the class, nor a using
declaration that brings in members of several kinds; a member that only starts
with a macro, like an attribute, is. A class template is checked as written,
not its instantiations, implicit or explicit, ``template struct
TCOuter<int>;``, nor the member classes of those, also the copies they make
of explicit specializations written in the class template, also nested in
one another.

A friend declaration that only declares, ``friend struct CExample;``,
``friend void fg_Run();``, also a template, goes first in the private
section, before its types, or, in a class with protected members but no
private ones, first in the protected section; it is reported anywhere else.
Whether a class has private members is of those with an access of their own,
not a partial or explicit specialization of a member template, also of a
member function or variable template, wherever it is written. A friend
class, ``friend struct CExample;``, also a template, in a class that has no
bases at all and no private or protected members of its own grants nothing
and is reported as unneeded; any base leaves its friends unreported. A friend
function there isn't reported, nor ordered: it can be what makes the function
found, by argument-dependent lookup, where no declaration of it in its
namespace is. A friend a macro writes, and one that defines a function in
the class, a hidden friend, aren't ordered.

.. code-block:: c++

  struct CExample // fine
  {
    using CValue = int;

    CExample();

    void f_Run();

    static constexpr int mc_Limit = 10;
    int m_Value = 0;

  private:
    friend struct CExampleTest;

    void fp_Run();

    int mp_Count = 0;
  };

.. example-check

  struct COrdered // fine
  {
    using CType = int;
    COrdered();
    void f_Run();
    static constexpr int mc_Limit = 1;
    int m_Value = 0;
  };
  struct CFunctionFirst
  {
    void f_Run();
    using CType = int; // warning: a type after a function
  };
  struct CMemberFirst
  {
    int m_Value = 0;
    void f_Run(); // warning: a function after a member
  };
  struct CPrivateFirst
  {
  private:
    int mp_Value = 0;
  public: // warning: the public section after the private one
    void f_Run();
  };
  class CDefaultPrivate // fine: the private section a class starts with is one only with members or a label
  {
  public:
    void f_Run();
  private:
    int mp_Value = 0;
  };
