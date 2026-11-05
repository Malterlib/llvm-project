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
upper camel case (``EColor_Red`` of ``EColor``), or, like any group, a built-in
type of Malterlib (``EResultDataType_fp64``). The enumerators of a scoped
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
parameters of a template, can be either. An alias of a template, a member, a local or an alias
template, of what depends on its parameters, also computed from them,
``TCConditional<..., typename t_CMap::CUserData, CKey const>``, is generic:
its generic name, ``C``, ``TC`` or ``IC``, is always allowed, also when every
instantiation of the translation unit makes it a function type or an enum,
and ``F``, ``TF`` and ``E`` are allowed when each makes it what they are for,
``using FCallback = typename t_CTraits::FSignature;``; an instantiation only
excludes what is wrong in it. ``E`` is for enums, also for an alias, also one that each
instantiation makes an enum, ``using EState = typename t_CTraits::EState;``. The lower case names are of
classes and aliases, not of namespaces, concepts or enums. Namespace aliases
are named like namespaces. Groups that underscores separate, like
``CCategory_Value``, are upper camel case too, or a built-in type of Malterlib,
like ``DMibPCanDo_fp32``, also one with an underscore, ``DMibPBits_char8_t``, and a name doesn't end with an underscore. A type,
also an enum, is checked at its definition, or at its first declaration when it
is not defined in the translation unit, and not at all when it is defined in a
system header.

A type of a C library that the code only declares in ``extern "C"``,
``struct x509_store_st;``, has the library's name, also when the library's
header, a system header, defines it in the translation unit, and so does a
name a typedef in ``extern "C"`` gives it, ``typedef struct x509_store_st
X509_STORE;``. A typedef in ``extern "C"`` that gives a type declared in
``extern "C"`` its own name, ``typedef struct DBusError DBusError;``, is named
by the type, which is checked where it is defined, also when that is a header
that isn't a system header, like libdbus's found through a search path.

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
