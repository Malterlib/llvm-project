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
  collection, also of ``malterlib-actor-phase``. Built in are those of the
  standard library's containers.
- ``FixedArrayTypes``: the classes that hold their elements in them, fixed in
  number, like a built-in array, whose elements are parts of what holds them,
  not entries that can be removed. Built in is ``std::array``.
- ``SharedPointerTypes``: the owning pointers whose copies share what they
  point to, counting references, of
  ``malterlib-coroutine-actor-reference``: a copy of one keeps what it
  points to alive, though what is looked up in that can still be erased.
  Built in are ``std::shared_ptr`` and ``std::weak_ptr``; Malterlib's
  options file adds ``TCSharedPointer`` and ``TCWeakPointer``.
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
  that make a local guard of a closure, ``g_OnScopeExit``. None is built in.

``--dump-config`` gives each list as it is, ``-*;Name;...``, which takes the
place of the one built in and of the options file when the configuration is
read again, with ``malterlib.OptionsFile`` and the shared lists, so the checks
of the configuration it gives find the same.

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
