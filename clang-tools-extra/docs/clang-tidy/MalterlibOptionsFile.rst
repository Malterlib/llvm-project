.. title:: clang-tidy - Malterlib options file

The Malterlib options file
==========================

The Malterlib checks, ``malterlib-*``, know the names of the language and the
standard library that their analyses depend on, like ``std::move``, the
``begin`` of a range and ``std::span``. The names of Malterlib, like
``fg_Move``, ``fg_CallSafe`` and ``CStrPtr``, are in an options file that
Malterlib has, ``Malterlib/Core/ClangTidyOptions.yaml``, so a project that
uses Malterlib checks its code with the names of the Malterlib it uses.

A configuration that enables a Malterlib check must name the file in the
option ``malterlib.OptionsFile``, relative to the directory of the
configuration file. Each Malterlib module's ``.clang-tidy`` names it:

.. code-block:: yaml

  Checks: '-*,malterlib-*'
  CheckOptions:
    malterlib.OptionsFile: ../Core/ClangTidyOptions.yaml

and the ``.clang-tidy`` at the root of a project made with Malterlib does,
relative to the root:

.. code-block:: yaml

  CheckOptions:
    malterlib.OptionsFile: Malterlib/Core/ClangTidyOptions.yaml

A relative path is made absolute when the configuration file is read, by
clang-tidy, clangd and the plugin, so a configuration that another inherits
refers to the same file. One in the ``CheckOptions`` of a ``.clangd`` file is
relative to the directory of that file. One given on the command line, ``--config``, is
relative to the current directory, and so is the plugin's
``-fplugin-arg-malterlib-options-file=<path>``, which names the file instead
of the configuration. Without the option, or with a file that can't be read,
the checks report an error, which fails the run of clang-tidy and the compile
with the plugin.

The file is one YAML document, a mapping of option names, each once, to lists
of names, each written as a sequence or joined by ``;``:

.. code-block:: yaml

  MoveFunctions:
    - fg_Move
  malterlib-no-zero-bit-scan.CheckMacros: 'DMibCheck;DMibFastCheck'

A list edits the one built in: a name adds one, ``-Name`` removes one, and
``-*`` removes all those before it. A plain name, like ``MoveFunctions``,
applies to every check; one with the name of a check, like
``malterlib-no-zero-bit-scan.CheckMacros``, to that check only, after the
plain one. The ``CheckOptions`` of a configuration then edit the result the
same way, so a module can add a name without repeating the file, and a value
option, like ``MinimumNameLength``, of a configuration takes the place of the
one of the file. Of a configuration that inherits another's
(``InheritParentConfig``), its own come after the other's, also a plain one
after one with the name of a check, like clang-tidy's options.

The lists that the analyses of several checks share are of the translation
unit, which its checks analyze the same way, and only take the plain name. One
with the name of a check is an option of that check, where it has one, like
``malterlib-coroutine-lambda-call.SafeCallees``, which takes the edits of the
shared list and its own in the order above; ``--verify-config`` reports others
as unknown options. The shared lists, besides the options of each
check:

- ``MoveFunctions``, ``ForwardFunctions``, ``ExchangeFunctions``,
  ``SwapFunctions`` and ``CopyFunctions``: the functions, by unqualified
  name, that move, forward, exchange, swap or copy what they are given. Built
  in are ``move``, ``forward``, ``exchange`` and ``swap``.
- ``SafeCallees``: what keeps a closure alive while it runs it, also of
  ``malterlib-coroutine-lambda-call``. None is built in.
- ``BlockingCallees``: the classes whose member functions run a closure to
  completion before they return. None is built in.
- ``ViewTypes``: the types that refer to data without owning it, also of
  ``malterlib-coroutine-reference-parameter``. Built in are ``std::span``,
  ``std::basic_string_view`` and ``std::initializer_list``.
- ``RemovingMethods``: the member functions that remove entries of a
  collection, also of ``malterlib-actor-phase``; one given only the constant
  0, ``f_SetLen(0)``, is taken to remove every entry. Built in are those of
  the standard library's containers.
- ``FixedArrayTypes``: the classes that hold their elements in them, fixed in
  number, like a built-in array, whose elements are parts of what holds them,
  not entries that can be removed. Built in is ``std::array``.
- ``OwnerTypes``: the owners, each ``Type=kind``, also hand-written ones
  whose bodies can't tell that they own what they hold, by what keeps what
  they own alive, a property of the type and the operation, never of how the type is
  written: ``shared``, a copy and a move, counting references; ``weak``,
  nothing, referring to what a shared one owns; ``unique``, a move;
  ``buffer``, the data held in a buffer on the heap, which a move gives to the
  one moved to at the same address, and which one made of anything other than
  one of its own copies; ``sharedbuffer``, a buffer that copies share,
  counting references, and copy before they change it: a copy and a move keep
  it, one made of anything other than one of its own copies, and a member
  function that isn't const can give the one it is called on another. A type
  of the ``buffer`` or ``sharedbuffer`` kind whose layout has room for its
  data, an array member, also of a base or of a member held by value, unions
  included, isn't one: moving it moves the data, like a ``TCVector`` with an
  inline or static allocator. An entry names a class or a template by its own
  name, which a type matches however it is written; an entry that names an
  alias registers nothing.
  ``malterlib-coroutine-reference-capture``,
  ``malterlib-coroutine-unsafe-this``, ``malterlib-coroutine-lambda-call``
  and ``malterlib-runs-to-completion`` tell by it, by type, whether what a
  closure, a result or the arguments of a call hold by value keeps alive what
  a pointer or reference into what an owner owns, or into an object, refers
  to (a keeper); ``malterlib-coroutine-actor-reference`` that a
  copy of a shared or weak one points to what the source does, and
  ``malterlib-coroutine-reference-parameter`` that a ``buffer`` or
  ``sharedbuffer`` one made of a view copies it. Built in are
  ``std::shared_ptr=shared``, ``std::weak_ptr=weak``,
  ``std::unique_ptr=unique`` and ``std::vector=buffer``; Malterlib's options
  file adds ``TCSharedPointer=shared``, ``TCWeakPointer=weak``,
  ``TCUniquePointer=unique``, ``TCVector=buffer`` and
  ``TCStr<CStrTraits_CStr*>=sharedbuffer``.
- ``SynchronousFunctionTypes``: the function objects that keep a callable and
  call it where they are called, on that thread, before the call returns, of
  ``malterlib-thread-local-storage`` and
  ``malterlib-coroutine-thread-local-reference``: a local or a temporary of
  one made of a closure or assigned it is an alias of that closure, whose
  uses are its own, and a call of it calls that closure; a local one holds
  the closure until it is assigned another callable or cleared, by a member
  function that can change it, has no parameters and gives nothing,
  ``f_Clear()``.
  Built in are ``std::function``, ``std::move_only_function``,
  ``std::copyable_function`` and ``std::function_ref``; Malterlib's options
  file adds ``TCFunction`` and its variants, not the actor functors, which
  run on an actor.
- ``BuiltinTypes``: the types that the naming checks take as types of the
  language, like Malterlib's ``uint32``. Built in are those of the language.

The options of the checks are in the file too, by a plain name for all
checks that have one, or with the name of a check for that check only. None
of their built-in lists names Malterlib; Malterlib's options file has its
names:

- ``CallbackLookupFunctions``, of ``malterlib-coroutine-actor-reference``:
  the member functions whose calls look up again, ``f_FindEqual``. Built in
  is ``operator[]``.
- ``ThreadLocalFunctions``, ``ThreadLocalTypes``, ``ThreadLocalTemplates``,
  ``ThreadLocalScopeTypes`` and ``ThreadLocalHandlerTypes``, of
  ``malterlib-coroutine-thread-local-reference`` and
  ``malterlib-thread-local-storage``: what is thread-local data, like
  ``fg_ConcurrencyThreadLocal``, ``CConcurrencyThreadLocal`` and
  ``TCThreadLocal``, the scopes that keep it, ``CCurrentActorScope``, and
  the bases of those the coroutine runtime moves,
  ``CCoroutineThreadLocalHandler``. None is built in.
- ``ScopeExitHelpers``, of ``malterlib-thread-local-storage``: the helpers
  that make a local guard of a closure, ``g_OnScopeExit`` and
  ``fg_OnScopeExit``. None is built in.

``--dump-config`` gives each list as it is, ``-*;Name;...``, which takes the
place of the one built in and of the options file when the configuration is
read again, with ``malterlib.OptionsFile`` and the shared lists, so the checks
of the configuration it gives find the same.

Stated facts
------------

Some of what the checks need to know can't be seen in the code they analyze:
what a body that can't be seen does, or what other code may do with a member.
Where the analysis can't tell a safe case from an unsafe one, it reports, and
the code states the fact with an annotation of
``Malterlib/Core/Source/Malterlib_Core_TidyAnnotations.h``,
``[[clang::annotate("malterlib-...")]]`` under clang and nothing for other
compilers. A fact is stated only where it is true of the code; the checks
then rely on it.

- ``DMibRunsGivenFunction`` (``malterlib-runs-given-function``), on a
  function, like ``fg_CallSafe``: it calls its first callable argument with
  its remaining arguments, and keeps both until the result of that call is
  done. The parameters of a closure given to it there are initialized from
  those arguments, so what a parameter held by value or by reference is
  given is what the call gives after the closure, ``fg_CallSafe([&Value =
  pOwner[0]](TCVector<int> _Kept) {...}, fg_Make<TCVector<int>>())``: a
  keeper made there of another object doesn't keep the borrow. Without it,
  what a function that doesn't call the closure where it is made gives its
  parameters can't be told, and a fitting keeper there keeps by type.
- ``DMibKeepsArgument`` (``malterlib-keeps-argument``), on a parameter: the
  function keeps the pointer, reference or view it is given beyond the call,
  ``void fg_SaveActor(DMibKeepsArgument TCActor<> const *_pActor)``, so what
  it refers to must outlive the call; of a constructor, the object it makes
  keeps it as a whole, whatever its members are, and uses it where the object
  is used or destroyed: a destructor that isn't trivial, seen or not, uses it,
  ``CGuard Guard(Value);`` of ``CGuard(DMibKeepsArgument int const &)`` with
  ``~CGuard()``. Of any other function, what it is given escapes to code that
  can't be seen, which can then write it, ``fg_Register(&View)``. Without it,
  a body that can't be seen keeps what its types can hold (Rule R): nothing
  past the call, and nothing in an object that has no member that can hold
  it.
- ``DMibSetByOwnClass`` (``malterlib-set-by-own-class``), on a pointer or
  reference member: it only points where the constructors and member
  functions of its class point it, also as the default member initializer,
  and an aggregate's initialization, of the class or of a class that holds it
  as a member, give it. What it points to is then where those point it, each
  value the class gives it, a null one nothing. Without it, what a pointer or
  a reference member points to can't be told, as other code can point it
  anywhere: what is read through it, of a record that isn't a local, is
  reported where its storage may be gone, a copy of a member record of the
  object that is read through it is a use of the object, and an owner made of
  what it points to, ``TCSharedPointer<CState>{m_pOther}``, doesn't keep the
  object. A local record's member is still what the function gives it, by an
  aggregate or a constructor's argument; what its class's own code gives it,
  a default member initializer or a null the constructor sets, needs the
  annotation too. The members of an owner of ``OwnerTypes`` and of a view of
  ``ViewTypes`` are told by those, not by this.
- ``DMibKeepsThis`` (``malterlib-keeps-this``), on an owner parameter of a
  member function, or a parameter that holds owners by value: it owns the
  object the function runs on. Without it, an owner parameter, also as what
  a parameter by reference refers to, keeps nothing of the object (``this``)
  where the function, or a closure it makes, uses the object after a
  suspension, as it may own another object of the class. An owner the body
  makes of ``this``, ``TCSharedPointer<CState>{this}`` or
  ``fg_ThisActor(this)``, needs nothing. A parameter paired with a pointer or
  a reference parameter it may own, ``fg_Read(CJob *_pJob,
  TCSharedPointer<CJob> _pKeep)``, keeps it by type, as before. On a member
  function, ``DMibKeepsThis TCSharedPointer<CState> f_Shared();``: what it
  returns owns the object it runs on, so an owner it gives, called on the
  object, keeps it. Without it, an owner a member function whose body can't
  be seen gives keeps nothing of the object, and neither does a member of
  another object, ``_pHolder->m_pState``, nor what a call that isn't on an
  object gives, ``fg_Other()``.
- ``DMibDistinct`` (``malterlib-distinct``), on a parameter: what it refers
  to, and what it owns, is no object that any other parameter of the
  function, or the object a member function runs on, refers to or owns. A keeper made of an annotated parameter, its
  value, what it refers to or its address, ``TCSharedPointer<CJob>{
  fg_Explicit(&_Other)}`` or ``CReadResult{_pOther, &_pJob->m_Value}``,
  then doesn't own what another parameter's borrow is derived from, nor a
  keeper made of another parameter what an annotated one refers to. Without
  it, two parameters of one type may be the same object, and a fitting
  keeper made of one keeps what the other is or owns by type.
- ``DMibReturnsNew`` (``malterlib-returns-new``), on a function: it gives
  back an owner of an object it makes each time it is called, which no other
  owner owns, ``DMibReturnsNew TCSharedPointer<CJob> fg_MakeJob();``. What
  two of its calls give, or one of its calls and an owner made before it, are
  different objects, so a keeper of one doesn't keep what the other owns.
  Without it, what a function whose body can't be seen gives back may be what
  another of its calls gives, ``fg_CurrentJob()``, and a fitting keeper made
  of one keeps what the other owns by type; a factory whose body is seen is
  told by its body.

Fix-its
-------

The fixes of the Malterlib checks are experimental, and the checks attach
them only when the option ``malterlib.FixIts`` is true in the
``CheckOptions`` of a configuration: a ``.clang-tidy`` or ``.clangd`` file, or
``--config``, where ``InheritParentConfig: true`` keeps the configuration of
the files:

.. code-block:: console

  clang-tidy --fix --config="{InheritParentConfig: true, CheckOptions: {malterlib.FixIts: true}}" Source.cpp

It is false by default, and the checks then report the same warnings and
notes and produce no fix, so ``--fix`` changes nothing and a tool that
applies the fixes ``--export-fixes`` writes applies none of theirs. It applies
to every Malterlib check, not to one, and isn't read from the options file.
With it on, a fix can produce code that doesn't compile where its edits would
have to agree with others across declarations, macros or diagnostics
suppressed with ``NOLINT``. The plugin offers no fixes either way.

The version of the checks
-------------------------

The compiler, clang-tidy and clangd of the fork that has the Malterlib checks
predefine ``__malterlib_tidy_version__``, the version of the checks, which is
raised when a check stops reporting what code worked around. Code that works
around a limit of an older version can tell it is no longer needed:

.. code-block:: c++

  #ifdef __malterlib_tidy_version__
  #	error "Remove the NOLINTs this file needs only with checks before version 1"
  #endif
