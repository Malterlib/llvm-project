.. title:: clang-tidy - malterlib-struct-not-class

malterlib-struct-not-class
==========================

Checks that Malterlib types are declared with ``struct``, not ``class``, and
that no access label repeats the access already in effect: the default of a
struct or a union, ``public``, at its start or after its first members,
``struct CActor { using CType = int; public: ...``, the default of a class,
``private``, of one kept as a class, like a specialization of an external
template, or the access of the label before it, ``private: ... private:``.
The fix removes the label. One isn't reported after conditional preprocessing
since the brace or the label before it, ``#if DPrivateLayout private:
#endif``, which another configuration needs it for, nor after any use
of a macro there, also one that expands to nothing here, or one that gives only
part of a member, ``DMemberType mp_Value;``, or an include, also one a guard
skips, up to the end of its colon, ``private DLayout:``, which the fix removes
too, nor when it, or its colon, is from a macro itself, since another
configuration can give those another access. The labels of a class are
checked also when its keyword isn't reported, like one a macro gives, but not
its default, which a struct, as it is reported to be or a macro can make it
elsewhere, needs. Nor is the default of any type whose tag isn't written in the file of its
opening brace, which a macro or an include gives, or which conditional
preprocessing, an include or a macro between it and its opening brace can
choose, ``struct DTypeName {`` of ``#define DTypeName CForward; class
CSelected``,
also a
struct or a specialization of an external template, which can be the other
in another configuration. A label with attributes, like an annotation of its
section, ``private __attribute__((annotate("..."))):``, is a barrier: neither
it nor the label after it, which ends that section, is compared with what is
before it. Nor are any labels compared in Microsoft mode, ``-fms-extensions``,
where a block of ``__if_exists`` or ``__if_not_exists``, whose labels are only
there when what it names is, can be in any class, also through macros, nor
the labels of a class whose body includes a file, nor the labels of a class whose definition comes from a
precompiled header or a module, whose preprocessing the translation unit
didn't see, nor of one whose braces aren't both written in one file, a macro
giving one. Only labels written in the file of the braces are compared: one
of an included fragment of the body isn't, nor the label after it with it.
A label is reported but not removed when a comment follows it on its line or
is in it, ``private /* ... */ :``, also one whose opening a line splice
divides, or a preprocessing directive is in it, which removing it would
remove.

.. code-block:: c++

  class CExample   // warning: use 'struct'
  {
  public:
    void f_Run();
  };

  struct CExample  // fixed
  {
    void f_Run();
  };

The fix replaces ``class`` with ``struct`` and removes a leading ``public:``.
It is only offered when the change keeps the meaning: every base names its
access and no member comes before the first access specifier, since those are
private in a class and public in a struct, and its body has no conditional
preprocessing, ``#ifdef``, whose skipped branches can have such members, nor
a macro before its first access specifier that expands to nothing here, which
another configuration can make declare members, nor a first access specifier
that comes from a macro, which another configuration can leave out. The leading
``private:`` of a class isn't reported.

Definitions, also of unnamed types like ``typedef class { ... } CType;``, are
checked. The other declarations of a class, ``friend class`` declarations,
also of dependent specializations like ``friend class TCThing<t_CType>``,
and specializations named with ``class``, also qualified, ``class TCThing<int>
const &``,
and of classes nested in them, also in a base of them, also one that
depends on the parameters of the template, ``friend class
TCOuter<t_CType>::CInner`` (one through a template that is specialized,
explicitly or partially, can name a class of a specialization that declares
it with another tag, so it keeps a class from being fixed, and isn't reported
for a struct), and
elaborated type specifiers, like ``class CThing *``, also of a class of an
instantiation, ``class TCOuter<int>::CInner *``, which is fixed with the class
of the template, also of a dependent one, ``class TCOuter<t_CType>::CInner
*``, also a class template nested in it, ``class TCOuter<t_CType>::template
TCInner<int> *``, and the explicit
instantiations of a class template, like ``template class TCThing<int>;``
(when one's tag comes from a macro, or another class template has the same
name, so that its instantiations can't be told apart in the text, or one
instantiates a partial specialization that is a class, whose tag it keeps,
``template class TCBox<int *>;``, the class is not fixed, nor is the partial
specialization),
are reported and fixed with its definition, so that the tags keep matching; where one of them names
a struct with ``class``, it is reported there, like ``template class
TCStruct<int>;`` of a struct template (by the tag of what it instantiates,
also a partial specialization that is a class, ``template <typename T> class
TCBox<T *>``). The tag of a type that is defined in a system header, or
specializes a template of one, or is not defined in the translation unit, is
not checked, since it has to match its definition; its repeated access
labels are, of one written here, also of one that specializes a template of a
system header. Use ``NOLINT`` where an external API requires ``class``.
