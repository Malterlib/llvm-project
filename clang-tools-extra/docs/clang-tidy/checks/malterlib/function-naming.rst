.. title:: clang-tidy - malterlib-function-naming

malterlib-function-naming
=========================

Checks that function names have the storage prefix of the Malterlib coding
standard, followed by upper camel case.

=========== ==============================================================
Prefix      Function
=========== ==============================================================
``f_``      Member function
``fp_``     Private or protected member function
``fs_``     Static member function
``fsp_``    Private or protected static member function
``fg_``     Global function, also in an anonymous namespace
``fsg_``    Global function declared ``static``, which can also be ``fg_``
=========== ==============================================================

A name with the prefix of another declaration is reported with both fixes,
the prefix and the declaration it has the prefix of, like ``member function
'fp_Run' should have the 'f_' prefix, or be moved into the private or
protected section``.

A recursive function has no prefix of its own: ``fr_Name``, ``fgr_Name`` and
``fg_rName`` are reported, as a variable of a function object can't be named
that way either.

.. code-block:: c++

  struct CExample
  {
    void f_Run();
    static CExample fs_Create();

  private:
    void fp_Step();
  };

  static void fsg_Helper();
  void fg_Run();

These are not checked:

- Constructors, destructors, operators and conversion functions.
- Overrides, which take the name of the function they override, also of a
  dependent base, ``t_CBase``, that an instantiation of the template, also of
  a partial specialization, ``TCDerived<t_CBase *>``, or of a class nested in
  a template, ``TCOuter<t_CBase>::CInner``, overrides.
- The protocols of the language and the standard library, which are lower
  case, like ``await_ready``, ``begin``, ``lock`` and the member functions of
  standard containers, from ``LowerCaseFunctions``: public member functions,
  which what uses the protocol calls, and the free functions that it finds by
  argument dependent lookup, ``begin``, ``end`` and their ``c``, ``r`` and
  ``cr`` variants, ``swap``, ``iter_move``, ``iter_swap`` and ``get``, from
  ``LowerCaseFreeFunctions``. A
  private or protected member function, ``count``, another free function,
  ``clear``, and other lower case names are checked.
- Functions of the standard library, declared in ``namespace std``.
- ``main`` and functions with C linkage in C++, but not member functions in an
  ``extern "C"`` block, which keep C++ linkage.
- Template specializations and instantiations.
- Members of specializations of templates from system headers, like ``parse``
  of a ``fmt::formatter<CValue>``, which the template names.

A function is checked where it is first declared. A name written in the body
of a macro is reported in the macro definition. A name made by token pasting,
``g_##d_Name``, isn't checked: macros make such names for themselves, like a
log category per line, ``l_Cat##__LINE__``, or the names of a library's
variants, which the code that uses the macro can't change, and which can't be
told apart from a name it gives. This is so for all the Malterlib naming
checks.

The Malterlib checks also run inside the compile, with the plugin that is
linked into clang: ``-fplugin-arg-malterlib-checks=malterlib-*``. A
``.clang-tidy`` file that can't be parsed is an error of the compile, when
the source file uses it, not one of a parent directory that its own doesn't
inherit, which isn't printed either. What a precompiled header declares is
checked when it is built, and in a compile that uses it, the definitions that
compile instantiates of its templates. An explicit instantiation
declaration, ``extern template struct TCBox<int>;``, still instantiates the
class, whose members, like an alias ``using FValue = t_CType``, tell what the
template is in this file too. The definitions of its functions, which it says
another file makes, are checked only in that file, by its explicit
instantiation definition, also by clang-tidy. The plugin doesn't look into the
headers it doesn't report, which keeps the compile fast, so what is told by
uses there isn't seen: an alias template of the file used only in such a
header, ``FValue<int>``, which tells it isn't a function type, is checked by
clang-tidy, which looks into the whole translation unit.

Options
-------

.. option:: LowerCaseFunctions

   Semicolon-separated list of the lower case function names that are allowed.
   Default is
   `allocate;allocate_at_least;assign;at;await_ready;await_resume;await_suspend;await_transform;back;before_begin;begin;bucket;bucket_count;bucket_size;c_str;capacity;cbefore_begin;cbegin;cend;clear;compare;compare_exchange_strong;compare_exchange_weak;construct;copy;count;crbegin;crend;data;deallocate;destroy;emplace;emplace_after;emplace_back;emplace_front;emplace_hint;empty;end;equal_range;erase;erase_after;exchange;fetch_add;fetch_and;fetch_or;fetch_sub;fetch_xor;fill;final_suspend;find;find_first_not_of;find_first_of;find_last_not_of;find_last_of;front;get;get_allocator;get_return_object;get_return_object_on_allocation_failure;hash_function;initial_suspend;insert;insert_after;is_lock_free;iter_move;iter_swap;key_comp;key_eq;length;load;load_factor;lock;lock_shared;lower_bound;max_bucket_count;max_load_factor;max_size;merge;pointer_to;pop;pop_back;pop_front;push;push_back;push_front;rbegin;rehash;remove;remove_if;rend;reserve;resize;return_value;return_void;reverse;rfind;select_on_container_copy_construction;shrink_to_fit;size;sort;splice;splice_after;store;substr;swap;top;try_lock;try_lock_for;try_lock_shared;try_lock_shared_for;try_lock_shared_until;try_lock_until;unhandled_exception;unique;unlock;unlock_shared;upper_bound;value_comp;yield_value`.

.. option:: LowerCaseFreeFunctions

   Semicolon-separated list of the lower case free functions that are
   allowed, those of protocols that find them by argument dependent lookup.
   Default is `begin;end;cbegin;cend;rbegin;rend;crbegin;crend;swap;iter_move;iter_swap;get`.

.. option:: MinimumNameLength

   The least number of characters a name has after its prefixes, like
   `Value` of `fg_Value`. Default is `2`.
