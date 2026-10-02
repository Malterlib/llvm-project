.. title:: clang-tidy - malterlib-type-naming

malterlib-type-naming
=====================

Checks that type, enum, namespace and concept names have the prefix of the
Malterlib coding standard, followed by upper camel case.

=========== ==============================================================
Prefix      Name
=========== ==============================================================
``C``       Class, struct, union and type alias, also nested in a template
``IC``      Interface class
``TC``      Class template and alias template, also nested in a template
``TIC``     Interface class template
``F``       Function type alias
``TF``      Function type alias template, not ``F``
``E``       Enum, enumerator, enum alias and enum alias template
``N``       Namespace
``c``       Concept
=========== ==============================================================

The name of an enum has no underscore, which separates the names of its
enumerators from it: an enumerator has the name of its enum, an underscore and
upper camel case (``EColor_Red`` of ``EColor``). The enumerators of a scoped
enum use the ``mc_`` prefix of the member constants they are used as
(``EFlag::mc_None``). An unnamed enum is what constants were before
``constexpr``: its enumerators are reported, to be ``constexpr`` variables
(``enum { mc_Value = 1 };``), or the enumerators of a named enum (``enum {
EMode_Read };``).

.. code-block:: c++

  namespace NExample
  {
    enum EColor
    {
      EColor_Red
    };

    enum class EFlag
    {
      mc_None = 0
    };

    template <typename t_CType>
    struct TCBox;

    using FCallback = void (int _Value);

    template <typename t_CType>
    concept cBoxable = true;
  }

The built-in types of Malterlib, like ``uint32``, and the protocols of the
language and the standard library, like ``promise_type`` and ``value_type``,
are lower case, from ``LowerCaseTypes``; other lower case names are checked,
except those of the standard library in ``namespace std``. ``F`` is for
function types, pointers to them and function objects, like ``TCFunction<void
()>``: an alias of a function type takes it, and an alias of anything else
doesn't. That a class is a function object is what its author means by it: one
that declares a call operator or a conversion to a pointer or reference to a
function, ``operator FCallback *()``, also in a base, whatever they are, also
deleted or constrained (but not a conversion template, which a call never goes
through), can take it, and one that declares neither can't. What
a class is, is told as it is defined in the translation unit: a
specialization of a template as it is instantiated there, so one that isn't
instantiated, or that is only declared, or that has a base that depends on
parameters of a template, can be either. An alias of a template, a member or
a local, of what depends on its parameters is what its instantiations make
it: one of a member of what depends on them, ``using CCallback = typename
t_CTraits::FSignature;``, also one a using declaration brings in from a base
that depends on them, ``using typename TCParent<t_CType>::FSignature``, also
through an alias template, ``TCIdentity<typename t_CTraits::FSignature>``, or
another alias that depends on them, ``using CCallback = FSignature;``, also
in parentheses, ``typedef typename t_CTraits::FSignature (CCallback);``, also
of what an expression that depends on them
is, ``decltype(t_CTraits::fs_Run)``, or a transformation of what depends on
them, ``std::remove_reference_t<typename t_CTraits::FSignature>``, also of an
alias template, also through another alias template,
which each makes a function type, also one used nested in what another
stands for, ``TCFunction<TCSignature<t_CTraits>>``, or in the default of a
template parameter, ``typename tf_FCallback =
TCFunction<TCSignature<tf_CTraits>>``, whose call leaves it out and doesn't
deduce it, also where a parameter names it only through an alias of a
qualified type, ``TCIdentity<tf_CValue>``, which doesn't deduce, also a local one of a friend defined in a
class template, takes
``F``, but
not one of a parameter, ``using CValue = t_CType;``, also through the ``type``
of a template given one, ``typename std::enable_if<true, t_CType>::type``,
also its ``CType``, ``typename TCIdentity<t_CType>::CType``, when that member
depends on what it is given, which is generic. ``E`` is for enums, also for an alias, also one that each
instantiation makes an enum, ``using EState = typename t_CTraits::EState;``. The lower case names are of
classes and aliases, not of namespaces, concepts or enums. Namespace aliases
are named like namespaces. Groups that underscores separate, like
``CCategory_Value``, are upper camel case too, or a built-in type of Malterlib,
like ``DMibPCanDo_fp32``, also one with an underscore, ``DMibPBits_char8_t``, and a name doesn't end with an underscore. A type,
also an enum, is checked at its definition, or at its first declaration when it
is not defined in the translation unit, and not at all when it is defined in a
system header.

A name is at least `MinimumNameLength` characters long after its prefix,
``CNode`` but not ``CX``, except the value of an enumerator after the name of
its enum, ``ELevel_2``.

Options
-------

.. option:: MinimumNameLength

   The least number of characters a name has after its prefix, like `Node`
   of `CNode`. Default is `2`.

.. option:: LowerCaseTypes

   Semicolon-separated list of the lower case type names that are allowed.
   Built in are the names of the protocols of the language and the standard library, like `value_type` and `promise_type`, and the built-in types of the vocabulary, `BuiltinTypes` of the Malterlib options file (:doc:`/clang-tidy/MalterlibOptionsFile`), like Malterlib's `uint32`.
