.. title:: clang-tidy - malterlib-conceptual-prefix

malterlib-conceptual-prefix
===========================

Checks the concept prefix that follows the storage prefix of a variable,
member, parameter or non-type template parameter, where the declared type
tells the concept.

========= ==================================================================
Prefix    Type
========= ==================================================================
``b``     ``bool``
``p``     Pointers, ``nullptr_t`` and smart pointers
``f``     Function pointers, lambdas and function objects
``i``     Iterators
========= ==================================================================

A name doesn't have a concept prefix its type, which doesn't depend on
template parameters, can't be: ``b`` on what isn't an integer, a ``bool``, an
enumeration or a wrapper of one, ValueWrapperTypes, like ``TCAtomic<bool>``,
which is a set of flags, or a class that is a flag, FlagTypes, like
``CAtomicFlag m_bReplied``, which may have ``b`` but needn't, ``i`` on what isn't one of IteratorTypes or an
integer, an index, ``p`` on what isn't a pointer or one of SmartPointerTypes,
and ``f`` on a number or a class without a call operator (``double
g_fRatio``). ``n`` and ``r`` can be on anything, the number of something and a
range. Malterlib's built-in types that are classes, like ``fp64``, are numbers,
of BuiltinTypes. ``b`` on an integer is allowed, as a set of
flags. In a template, that is of what it is in any instantiation, also of a
structured binding, ``auto [fValue] = _Entry`` of an ``int`` member, and of a
partial specialization of a member variable template: a prefix wrong in one of
them is wrong, as a prefix claims a concept of every use, while no prefix fits
any. A member that takes no space where it is empty, ``[[no_unique_address]]``,
can be an empty stand-in in an instantiation, ``TCConditional<t_bStore, CStore
*, CStoreEmpty> m_pStore``, which then excludes no prefix.

When a name models several concepts, only the outer one is used, so a pointer
to a function is ``f`` and a pointer to a ``bool`` is ``p``, not ``pb``, and a
pointer to a pointer is ``p``, not ``pp``. A raw pointer is always ``p``, also
one that walks over elements, like ``for (int *pValue = pBegin; ...)``, also
through an alias named as an iterator, like ``using CIntIterator = int *``:
that it is a pointer matters more than that it is an iterator. An iterator that is
a range, which knows its end, may use ``r``, like ``for (auto rByte =
_rBytes; rByte; ++rByte)``.

.. code-block:: c++

  void fg_Find(bool _bExact, CNode *_pRoot, TCFunction<void ()> const &_fOnFound);

The other concepts of the coding standard, ``n`` (number of) and ``r``
(range), depend on what a variable means, not on its type, so they are not
checked. ``c`` is not a concept prefix of variables: it is the prefix of C++
concepts, ``cIterable``. A type that depends on a template parameter requires a concept prefix only
where no argument can change it, as the template is written: a
specialization of a listed smart pointer or function object template written
as it is, also in parentheses or annotated, ``[[clang::annotate_type("...")]]``,
when each of its partial specializations and those declared explicitly is one
too, like ``TCSharedPointer<tf_CType> _pValue``, a pointer to a type that
can't be a function, like ``tf_CType *_pValue``, a function or a pointer to
one, like ``void (&_fCallback)(tf_CValue)``, a pointer to a member, a lambda
declared in the template, ``auto fRun = [&] { ... }``, a type named as a
function type (below), a local deduced from what has one of those
types as written, ``auto pValue = static_cast<tf_CType *>(nullptr)``, also an
init capture, a static member or a variable template, and a copy of what is
named as a function type, ``auto fCopy = _fAction``. Where an argument can
change it, a name takes no concept prefix that the instantiations would
require, which can be mutually exclusive: unprefixed it is always allowed.
That is a member of what depends on template parameters, ``typename
tf_CTraits::CPointer _Value``, also through an alias template,
``TCIdentity<typename t_CTraits::CValue>``, a specialization given what
depends on them, ``TCAtomic<typename t_CTraits::CBool>``, a non-type template
parameter of such a type, ``typename t_CTraits::CBool t_Flag``, a type
computed from them, ``decltype(_pValue)``, what a call, a member, an element or
an iterator that depends on them gives, ``auto Empty = _Vector.f_IsEmpty()``,
a structured binding in a template, and a pointer to what can be a function
type. Its instantiations only exclude a prefix that is wrong in one of them,
``auto pValue = _Value`` that one makes an ``int``, also a prefix on what one
makes an array, but not ``i`` on what one makes a pointer, which generic code
walks like any other iterator, ``tf_CIterator _iBegin`` given an ``int *``,
wherever it is: a parameter of a lambda in a template, of a
friend defined in a class template, of a member function defined outside of
its class template, a member of a partial specialization or of an anonymous
member union, a non-type template parameter, also a pack of them, a variable
template, also a partial specialization of one, a static member, the
variable of a ``catch``. A parameter of a lambda in the requires clause of a
function template, which clang keeps only as it is written, isn't checked,
nor is a non-type template parameter of an alias template or a concept, whose
uses clang doesn't keep. A pointer to a parameter named as a function type, like
``tf_FFunction *``, also an alias or alias template named as one,
``FCallback<tf_CType> *``, or to a dependent member type or member alias template
named as one, ``typename TCSignatures<tf_CType>::template FCallback<int> *``,
can be a function, or a pointer to a function object, so
it needs either ``f`` or ``p``, like ``_fCallback`` or ``_pCallback``. A type
named as a function type, with the ``F`` or ``TF`` type prefix, is a function
object, as its name says: a template parameter, ``tf_FFunction &&_fFunction``,
a dependent member type or member alias template, ``typename
t_CTraits::FCallback _fCallback``, or an alias, ``FCallback _fCallback``, also
of a reference, ``using FCallbackReference = CAction &``, also referred to
again, ``FCallbackReference &``, which is the alias, also one a using
declaration brings in from what depends on template parameters, ``using
typename TCParent<t_CType>::FCallback``. The
other way around, one named as a function object whose type is a template
parameter, ``tf_CType &&_fValue``, has that parameter named as a function
type, ``tf_FValue``. A template parameter named otherwise, ``tf_CType
&&_Value``, can be anything, also a function object, so its name isn't
checked. Malterlib's ``zbool``, ``TCAutoClearInt<bool>`` and
``TCAtomic<bool>`` are booleans, and a ``TCAtomic`` or ``TCAutoClear`` of a
pointer, ``TCAtomic<int *>``, is a pointer, or of a pointer to a function,
``TCAtomic<void (*)()>``, a function object. A smart pointer is a pointer also
through an alias, ``using CNodePtr = TCSharedPointer<CNode>``, except through
one of `SmartPointerHandleTypes`, a handle named for what it gives, like
``CActorSubscription``, which takes no concept prefix, also not ``p``. A
value deduced of the type such an alias names, ``auto Subscription =
_fMake()``, is that handle, also where what it is deduced from doesn't spell
the alias, or spells the smart pointer, ``auto &Subscription = _pOriginal``,
also in each instantiation of a template, ``auto Subscription =
fg_Make<tf_CType>()``: the alias is looked up in the namespaces and classes
of the smart pointer and of its arguments.

Options
-------

.. option:: SmartPointerTypes

   Semicolon-separated list of smart pointer class templates. Qualified names
   match qualified names. Built in are `std::unique_ptr;std::shared_ptr;std::weak_ptr`, and the Malterlib options file (:doc:`/clang-tidy/MalterlibOptionsFile`) adds Malterlib's, like `TCSharedPointer`.

.. option:: SmartPointerHandleTypes

   Semicolon-separated list of the aliases of smart pointers that are handles,
   named for what they give rather than as pointers, which take no concept
   prefix, like Malterlib's `CActorSubscription` that the Malterlib options file
   lists. Default is empty: every alias of a smart pointer is a pointer.

.. option:: FunctionObjectTypes

   Semicolon-separated list of function object class templates, including the
   classes derived from them, also without a name, ``struct : TCFunction<void
   ()> {} g_fCallback;``, matched like `IteratorTypes`, like `*Functor*`.
   A class is told as it is defined in the translation unit: a
   specialization of a template as it is instantiated there, with its own
   bases, and one that isn't instantiated only by the name of its template;
   a base that depends on parameters of a template isn't followed. A
   specialization written with parameters, ``TCWrapper<tf_CType *>``, is one
   only when each specialization it can be, also partial or declared
   explicitly, is, not one whose arguments can't be what is written, like
   ``TCWrapper<int>``, also an iterator, ``TCVectorIterator<tf_CType>``. A class whose name a pattern only contains, like
   ``*Functor*``, is one when it declares a call operator or a conversion to
   a function, whatever they are, also deleted or constrained, but not a
   conversion template, which a call never goes through. A class with
   a call operator isn't one by itself, like `TCActor`, whose call operator
   calls the actor. Built in are `std::function;std::move_only_function;std::copyable_function`, and the Malterlib options file (:doc:`/clang-tidy/MalterlibOptionsFile`) adds Malterlib's, like `TCFunction` and `*Functor*`.

.. option:: IteratorTypes

   Semicolon-separated list of iterator types and aliases. A leading `*`
   matches the end of a name, and with a trailing `*` too, a name containing
   the rest. A class that such a pattern matches, also through an alias it
   matches, is one only when it, or a base, has what the concept needs: a unary
   `operator*`, `operator->` or
   `operator++` for an iterator, also a hidden friend or one in its namespace,
   or in the namespace of a template argument of it, also of an enum, a
   template, or the parameters and result of a function type,
   or in the parent of an inline namespace it is in,
   also a template instantiated for it,
   that takes it, also a member template whose constraints can't be told
   without what it is instantiated with that is instantiated, and a call operator for a function object,
   so `CIteratorEndSentinel` and `CActorFunctorInfo` are neither, and an alias
   of what depends on template parameters, which can be either, isn't
   required to be named as one. Built in is `*iterator`, and the Malterlib options file (:doc:`/clang-tidy/MalterlibOptionsFile`) adds `*Iterator*`.

.. option:: ValueWrapperTypes

   Semicolon-separated list of templates that wrap a value whose concept is
   the wrapper's, like ``TCAtomic<bool>``, a boolean, and ``TCAtomic<int *>``,
   a pointer. Default is `std::atomic`; Malterlib's options file adds
   `TCAtomic`, `TCAutoClear` and `TCAutoClearInt`.

.. option:: FlagTypes

   Semicolon-separated list of classes that are a flag, which a name may give
   the ``b`` prefix, but needn't, like ``CAtomicFlag m_bReplied``, but not a
   class with a conversion to ``bool`` that isn't one, like an optional. None
   are built in; the Malterlib options file
   (:doc:`/clang-tidy/MalterlibOptionsFile`) adds `CAtomicFlag`.

.. option:: IgnoredNames

   Semicolon-separated list of names that are not checked. None are built in; the Malterlib options file (:doc:`/clang-tidy/MalterlibOptionsFile`) adds `mc_Value`, the result of a type trait.
