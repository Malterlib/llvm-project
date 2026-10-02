.. title:: clang-tidy - malterlib-struct-not-class

malterlib-struct-not-class
==========================

Checks that Malterlib types are declared with ``struct``, not ``class``, and
that a struct does not start with a ``public:`` section, which is the default
of a struct, unless conditional preprocessing is before it, ``#if
DPrivateLayout private: #endif``, which another configuration needs it for.

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
that comes from a macro, which another configuration can leave out. A leading
``public:`` after such a macro isn't reported.

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
not checked, since it has to match its definition; a leading ``public:`` of a
struct written here is, also of one that specializes a template of a system
header. Use ``NOLINT`` where an external API requires ``class``.
