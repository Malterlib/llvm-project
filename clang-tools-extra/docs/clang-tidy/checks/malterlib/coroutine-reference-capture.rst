.. title:: clang-tidy - malterlib-coroutine-reference-capture

malterlib-coroutine-reference-capture
=====================================

Finds lambdas whose call operators are ``TCFuture`` coroutines and that
capture variables by reference where the variables can be gone when the
lambda uses them. A name that isn't an odr-use, the value of a constant read
as a constant, ``c_Size`` of ``constexpr int c_Size = 4096;`` in a ``[&]``
lambda, uses no storage; its address, or a constant made at run time, does. A copy of a pointer into a local, like ``[pValue = &Value]``
or ``[pValue]`` of an unchanged ``int *pValue = &Value``, also through a
local reference to the pointer, ``[pAlias]`` of ``int *&pAlias = pValue``, also taken by
``std::addressof`` or ``__builtin_addressof``, also cast to another
pointer, ``static_cast<int const *>(&Value)``, also into a member
or an element of it, also of what a local pointer points to, ``&pValues[1]``,
also of a structured binding, ``auto [Value] = ...``, also given back by a
function whose body returns what it is given, ``fg_Data(Values)``, or by a
member function of it whose body returns what is in its object, what an
owner member of it or it, an owner, owns, or the object itself,
``Local.f_Get()`` of ``int *f_Get() { return &m_Value; }``, not a pointer
member stored in it, also moved
along it, ``return _pValue + 1;``, also
through a local reference to it, also through any number of them, also one bound
to a temporary that it extends, ``int const &Value = 42``, or either of a
conditional, ``[pValue = bFirst ? &First : &Second]``, each of which is
checked, also through a conditional reference, ``int *&pAlias = bFirst ? pFirst
: pSecond``, of what the pointers point to where it is captured, also the first value
of ``pValue ?: &g_Value``, which is its condition (the one declared last,
which goes first; of a constant condition, only the one it chooses), or what a changed
pointer was last assigned where it is captured, ``pValue = &Value; [pValue]``,
also moved, ``[pCopy = fg_Move(pValue)]``, borrows the local the
same way, and so does a pointer to a local captured by reference whose value
is copied before a suspension, ``int *pSaved = pValue`` in ``[&pValue]``, and is checked like ``[&Value]``, with the uses of the copy, and so
does a view captured by value, like ``[Span = CStrSpan{Buffer, 8}]``, also a
copy of one, ``[Copy]`` of ``CStrSpan Copy = Span``, and a
``std::initializer_list``, whose array lives as long as the local it
initializes, ``std::initializer_list<int> Values{1, 2}``, also an init capture,
``[Values = {1, 2}]``, whose array is a temporary of the full expression that
makes the closure, which an init capture doesn't extend, so a closure that
runs after that expression can't use it. Of a copy that a closure called where it is made uses, a
use that reads only what the copy has of its own, a pointer only compared or
tested, also by the body of a function it is given to that is seen, or through what such a body gives back,
``pValue != nullptr``,
``(bEnabled ? pValue : nullptr) != nullptr`` or ``fg_Compare(pValue, pOther)``, or what a view's const member function that
reads only its own storage gives, ``[Text] ... co_return Text.f_GetLen();``,
by the type the closure holds, also of ``[Text]`` of ``auto const &Text =
Span``, which holds a view, isn't a use of what it points to, by the predicate of a view parameter taken
by value (:doc:`coroutine-reference-parameter`); one given to run later that
holds such a copy is reported whatever it uses. Where a copy points is
where provenance says it does, the one model of where storage lives that
every check asks, also through each override a virtual call can run, to a
local, to what a local owner owns (a raw pointer into it with no keeper in the
closure, ``[pGate = &pStorage->m_Gate]``, is reported, also when the closure is
stored in that allocation, which isn't told), to what a reference parameter
refers to, or to what is ``thread_local``. A reference parameter to a const
array of characters, ``char const (&_Error)[N]`` (also deduced from ``auto const
&``), is taken to designate a string literal, of static storage, so ``[Error =
_Error]`` isn't reported; one bound to a local character array is a known miss.
A pointer into a temporary made in an init capture's initializer, ``[pValue =
CValue{}.f_Get()]``, is gone with the full-expression that makes the closure,
like the array of ``[Values = {1, 2}]`` of a closure that is itself a temporary,
and is reported as that init capture; the array of a named closure, ``auto fRead
= [Values = {1, 2}]...``, is extended by that local (the language's rule), and is
judged as that local. Provenance tells each expression once for a query, so
a chain of copies costs its length; past 200,000 expressions told in one query,
what it reaches can't be told and isn't reported. One into a temporary of another statement isn't, and a helper that points a
pointer it is given by reference at what isn't one of its parameters,
``fg_Reset(pValue)`` with ``_pValue = &g_Value``, isn't followed: the
pointer can still have what it had before. What a member function of a local returns into it is followed
through each override a virtual one can run.

.. example-before
   struct CStr { CStr(); CStr(CStr const &_Other); ~CStr(); };
   TCFuture<void> fg_Timeout(double _Seconds);
   TCFuture<int> fg_Example()
   {

.. example-after
     co_return 0;
   }

.. code-block:: c++

  CStr Name;
  co_await
    (
      g_Dispatch / [&Name]() -> TCFuture<CStr>  // warning
      {
        co_return Name;
      }
    )
  ;

  co_await fg_CallSafe
    (
      [&Name]() -> TCFuture<CStr>  // warning
      {
        co_await fg_Timeout(1.0);
        co_return Name;
      }
    )
  ;

A lambda that is called directly, or given as the function to call to
``fg_CallSafe``, runs right away, where the variables are, until it first
suspends, so it may use them until then. Where it goes is followed as
:doc:`coroutine-lambda-call` tells, through a local function wrapper at the
uses the definition that gives it the closure reaches. Before the first suspension, the
variables may only be used as values: read as scalars, copied or moved into
values that are not views, or given to functions that return a ``TCFuture``,
whose coroutines are held to the same rules. Any other use counts as a use after the suspension where what it makes keeps it
past it, told by the same rules as what a use does to the storage (below): kept
beyond the frame, in a member, a global, the result or a closure that runs
later, or held in a name of the frame, a local reference or pointer, a binding
or a capture of a closure that isn't called where it is made, that reads,
writes, gives or keeps it, wherever that name is. The address of a reference
follows the rules of a pointer: kept in a local that is only compared, ``CJob
*pLocal = &_Job; co_return pLocal == nullptr;``, or converted to an integer,
``reinterpret_cast<umint>(&_Job) != 0``, it touches nothing; an integer made of
it is its value, compared, tested, logged or returned as a number it touches
nothing, and converted back to a pointer it is the pointer again, ``auto
Address = reinterpret_cast<umint>(&_Value); co_await ...; co_return
*reinterpret_cast<int *>(Address);`` reads it. What is put in a
record held by value in the frame that isn't a view, an owner nor an actor is
carried by the member it is put in, by an aggregate, an assignment to the
member or a copy or move assignment the compiler defines: a later load of
that member, also through a getter whose body is seen, a copy the compiler
defines or a structured binding, is it again, and a load of another member,
the record's own storage, isn't; what a constructor or a member function
keeps of it (Rule R) is in the record as a whole. Such a record uses it where it is used or destroyed: a
call on it, a destructor that isn't trivial (seen, its body; not seen, it
uses what the record holds). One trivially destroyed and never used again
uses nothing of it, and so is an object returned by value. An awaiter
keeps what it holds while the coroutine is suspended; one that can't hold a
pointer, a reference or a view keeps nothing. Handles, like
``TCActor`` and ``TCPromise``, are copied by what they are given to, and so is
what is given to them, like the arguments of an actor call. When it is made in
another lambda, that lambda must keep the variables too: be called directly and
not be a coroutine, or one that makes it before it first suspends, or hold a
copy of them. One that returns it, a factory, keeps nothing for it: it runs
where what the factory returns goes, which is followed only where each call
of the factory is in the function around the factory; a factory called in
another lambda, ``co_await fg_AsyncDestroy(fMakeHook(Location))`` in an
awaited lambda, is reported in code that doesn't run to completion. One that is dispatched, stored or
given to anything else can run after the scope of the variables has ended, even
before it suspends, so it is reported for capturing them at all. What the first
``co_await`` awaits runs before it suspends; a use in a lambda the coroutine
makes counts as after a suspension (but not one called where it is made, ``[&Value] { return Value; }()``,
that isn't a coroutine itself, which runs there). An awaiter whose ``await_ready`` only returns true, like that of ``co_await
g_AccessCoroutine``, doesn't suspend, unless it is virtual, which an
override can make suspend, and neither does one whose ``await_suspend`` only
returns false, which resumes right away.

Whether what a capture refers to, or a copied pointer points to, is alive
while the closure runs is decided by where it lives, by provenance (past
copies, moves, local references and pointers, members, elements and bases,
getters and forwarding functions whose bodies can be seen, the arms of
conditionals that run, and views made of a pointer and a length), and then by
type:

- Static storage is alive, and so is the object of an actor in a lambda of
  one of its member functions, also through its explicit object parameter or
  a pointer or reference parameter of a helper that each call in the
  translation unit gives the object of the actor the caller runs on,
  ``fs_Read(this)``, and the object of a class of ``CAllowUnsafeThis``, also
  through a reference or pointer parameter.
- A local or a temporary is not, whatever else is captured, ``CJob Job;
  [&Job, pOther]``, nor is what is ``thread_local``, which is there on its
  thread only.
- What an owner of ``OwnerTypes`` (:doc:`/clang-tidy/MalterlibOptionsFile`)
  owns, another object, and what a reference or pointer parameter refers to,
  are alive where the closure uses them when its context there holds by
  value a keeper of it, asked at each use about what that use reaches. What
  a context holds is its declarations by their declared types, never how
  they are initialized: each capture by copy and init capture, each
  parameter of its call operator held by value, the explicit object too,
  and each local of its body held by value in scope at the use, declared
  before it in a scope still open there (of a value kept in a local of the
  frame, read where that local is, each whose scope is open at the use),
  also a structured binding by value; of a generic lambda, those
  of the specialization that runs. A lambda that isn't a coroutine also has
  what the functions and lambdas around it hold, their locals in scope
  where it is, out to the nearest coroutine or the outermost function,
  wherever it is called; a coroutine
  lambda has only its own. A keeper is an owner that is one of them or a
  part of one by type: a member, a base, also a virtual one, an element of
  an array whatever initializes it, ``TCSharedPointer<CJob> pJobs[2]{}``, an
  element of a container by the arguments of its template, a capture of a
  closure. What is an owner is one classification, by type: a type whose
  own name matches an entry of ``OwnerTypes`` is an owner of that kind
  however it is written, through an alias, an alias in a class template,
  ``TCContext<CJob>::CJobPointer``, or an alias template, also as a
  template argument or a result type; an entry that names an alias
  registers nothing. A keeper keeps by
  its kind as it is taken where a capture is made of an expression, by its
  category as written: a copy, of an lvalue, keeps through ``shared`` and
  ``sharedbuffer`` owners; a move or a new value, an xvalue or a prvalue,
  through any. A ``co_return`` of another type than the result's makes the
  result, a new value, whatever its category. A parameter or a local is
  held: it keeps through any. A ``weak`` owner never keeps. Of the object of
  a member function, ``[&Value = m_Result, Kept = _Keep]``, an owner given by
  a parameter of the function, also through what a parameter by reference
  refers to, keeps it only where the parameter states that it owns the
  object (``DMibKeepsThis``), and one whose object can't be told keeps
  nothing of it: a member of another object, what a call that isn't on an
  object gives, also a new owner, ``Kept = fg_Make<TCSharedPointer<CTask>>()``,
  and what a member function of the object whose body can't be seen gives,
  unless it states that it gives an owner of its object (``DMibKeepsThis``
  on the function). A parameter of the closure is what the call
  that runs it gives it: where the closure is called where it is made, each
  call's argument, and where it is given to a function that runs it with
  its other arguments (``DMibRunsGivenFunction``, ``fg_CallSafe``), the
  argument after it, the same part of it for a keeper in a member, a base or
  an element; elsewhere what it is given can't be told and a fitting keeper
  there keeps by type.
  It fits by type, an anchor being a type: one of the ``shared`` or
  ``unique`` kind owns the anchor, a base of it or a class derived from it,
  and one of the ``buffer`` or ``sharedbuffer`` kind is of the anchor's type,
  ``CStr Keep = _Text`` of ``_Text.f_GetStr()``. The
  anchors are every object type on the borrow's derivation, of every
  subexpression whatever its kind, also past local references and
  pointers and copies captured, to what they are initialized and assigned
  with: the object each glvalue designates and what each pointer value
  points to, so what an owner owns where it is dereferenced, casts and
  getters too, ``[&Result, pContext]`` of ``int &Result =
  pContext->m_pJob->m_Result``, ``pContext->m_pRawJob->m_Value``,
  ``pContext->f_GetJob()->m_Value``, and the object and what the parameter
  refers to; and of each of those types, what the owners it holds by value
  own, by the one classification of what holds owners, so what a getter
  gives of its object, ``auto &Result = f_Result()``, is kept by
  ``pKeep = m_pJob`` whether or not the getter's body can be seen. The
  derivation of a capture is its initializer, of a use in
  the body the use itself. So ``[&Result, pJob]`` of ``int &Result =
  pJob->m_Result``, ``[pText, Text = fg_Move(Text)]`` of a buffer,
  ``[&Result, Jobs]`` of a container of shared owners and ``[&_Job, pKeep]``
  of a ``TCSharedPointer<CJob>`` are clean; ``[&pJob]``, ``[pJob]`` of a
  weak owner, ``[pValue, Values]`` of a ``buffer`` copied and an owner of an
  unrelated type are reported.
- A record held by value carries what is put in each of its members. Of a
  local record, the value of a pointer, a reference or a view member is what
  the member was given, by an aggregate, an assignment or a constructor
  whose body only initializes it, followed where the member is loaded: through its name, a
  member function whose body can be seen that returns it, ``Context.f_Job()``
  of ``return m_Job;``, a structured binding, also a reference member where
  what it refers to is designated, ``[&Job = Context.m_Job]``; a member
  given nothing but a null pointer refers to nothing. A copy of a local
  record a closure captures, also an init capture, ``[Context =
  CReadContext{&Value}]``, is a borrow where the closure's body reads what a
  member of it points to, by the uses of the capture that read more than
  its own part: ``[Info]`` of ``CInfo Info{&Value, 5}`` is reported where
  the body reads ``*Info.m_pValue`` after a suspension, wherever the closure
  runs, and clean where it reads ``Info.m_Count``, ``Info.f_Count()`` or
  tests ``Info.m_pValue != nullptr``; a keeper of what the member points
  into keeps it as for a pointer, ``[Ref, pJob]`` of ``CResultRef
  Ref{&pJob->m_Result}``, unless it owns another object. One captured
  through a local reference to the record, ``auto &Info = Data; [Info]``, is
  followed to what the record was given, reported. Of a record reached
  through a pointer to a local, ``[pValue]`` of ``int *pValue =
  pInfo->m_pValue`` is clean, a known miss. Of a parameter held by value, of
  the object, and of a record a shared or unique owner owns, a pointer or a
  reference member points where its class points it when it states that only
  its class points it (``DMibSetByOwnClass``), and else can't be told:
  ``[&RequestID = _Context.m_RequestID]`` of a ``CRequestContext
  _Context`` and ``[pWeak, &Value = pContext->m_pRawJob->m_Value]`` are
  reported, ``[pContext, &Value = pContext->m_pRawJob->m_Value]`` is kept by
  the owner of the record (Rule K); a pointer copied from such a member,
  ``[pJob = _Context.m_pJob]``, follows only what is told and isn't
  reported. ``[&Value = *Info.m_pValue]``, ``[pValue]`` of ``int *pValue =
  Info.m_pValue`` and of ``auto [pValue] = Info``, and ``[&Job =
  Context.m_Job]`` of a ``CJob &m_Job`` given a local, are reported. A raw
  pointer member that doesn't state ``DMibSetByOwnClass``, read from a local
  record whose type isn't an owner and has a destructor of its own, and that
  a constructor of its class or what else can't be told made, can point into
  storage the record manages, which ends with it: ``[pValue, Values =
  fg_Move(Values)]`` of ``auto *pValue = Values.f_GetFirst()`` of a
  ``TCList`` that OwnerTypes doesn't list is reported as pointing to
  ``Values``; one made by a constructor the compiler defines, other than a
  copy or a move, or by a list of its members, isn't. Of a record held by value, its own storage is
  followed as storage of the frame, which no keeper keeps: its address, a
  member's, an array member, what a member function whose body can be seen
  returns into it, and what a ``buffer`` or ``sharedbuffer`` type whose
  layout has an array member (also of a base or a member held by value)
  points to, which holds its data in itself, ``TCVector<int,
  CAllocator_Inline>``. What an owner member of it
  owns is followed as what that owner owns, by its anchor,
  ``Buffer.m_Values.f_GetArray()`` and a member function whose body returns
  that, kept by a fitting keeper, ``[pData, Buffer = fg_Move(Buffer)]``; a
  member function whose body can't be seen gives what can't be told.
  Pointers, references and views captured themselves are followed, and so
  is what a result or an argument is made of in its own expression, handed
  off at once, ``co_return CRef{&Value}``, where a direct initializer that
  holds owners and can't hold a pointer, a reference or a view, also of a
  record made in place in it, is a keeper put in, not on the derivation of
  what it keeps: ``co_return CResult{_Keep, &_Job.m_Result}`` with a
  ``TCVector<int> m_Keep`` is reported.
- A keeper keeps what an owner owns only when it owns the same object where
  it is taken (Rule K's identity and flow): each value of it that reaches
  the take, by the provenance's definitions of the slots it is copied
  through, a copy, a move, a copy function, an exchange, a swap, a seen
  getter or helper, the arguments of a closure called where it is made,
  must be the owner's own value, unchanged between the borrow and the take
  (an assignment, a release, a member function that can change it, a write
  through an alias or a reference parameter's name); a new object, an empty
  one, another variable's own value or a change whose value can't be told
  doesn't keep, ``pJob.f_Clear()`` or ``pJob = fg_MakeJob()`` between
  ``int &Result = pJob->m_Result;`` and ``[&Result, pJob]``. A parameter of
  the function itself, by value or what one by reference refers to while
  nothing writes it through its names, is there before the function makes
  anything, so it doesn't own an owner made in the function, ``pOwner =
  TCVector<int>()`` given ``_Keep``; a member, a base or an element of what
  a call whose bodies can't be seen gives, and isn't given what the owner
  goes through, is what the whole is, another value than a new object or
  another call's; a member of a local record copied whole is that member of
  what it is copied from. An owner that
  is a member of the object, ``mp_pJob`` or an element of one bound to a
  local reference, ``auto &pJob = mp_Jobs[Key];``, is a slot of the member
  function's flow: read at the borrow and at the take, it keeps where
  nothing between can change it; an assignment, a member function that can
  change it called on it or on what it is in, one called on the object that
  can (``f_Reset()``; a virtual one that isn't final, whatever its body,
  unless it is called by its qualified name or on an object whose class is
  exactly its own), the object given where it can be changed, and a
  suspension, after which other code can have run on the object, change it.
  A part of what a reference parameter refers to is read the same way: a
  member of it, also through a pointer member or a shared or unique owner
  member, ``_Batch.m_pContext->m_pJob``, or an element a member function gives a
  reference or a pointer to, ``_Jobs[0]``, ``*_Jobs.f_FindEqual(_Key)``,
  also bound to a local reference; it keeps where no write through the
  parameter's names between can change it: a write of it or of what holds
  it, ``_Jobs[_iOther] = ...``, ``pJob = ...`` of ``auto &pJob =
  _Jobs[_iJob];``, a member function called on what holds it that can
  change it, ``_Jobs.f_Clear()``, ``_Jobs.f_Remove(_Key)``; a write of an
  element of another constant index doesn't. Two elements of one container
  are the same where their indices are the same constant or the same
  variable unchanged between, and other entries where they are other
  constants or other variables, ``_Jobs[0]`` and ``_Jobs[1]``, or a lookup
  repointed, ``ppJob = Jobs.f_FindEqual(_Other)``, also where the keeper is
  made of the lookup, ``ppJob->f_ShareAsConst()``; a constant and a
  variable can't be told. What a member function of a container of owners
  gives a pointer to, of the type of an owner it holds, is an element of
  that container, through a local pointer too. A container moved from is
  changed where it is moved: ``auto Moved = fg_Move(Jobs);`` before
  ``int &Result = Jobs[0]->m_Result;`` doesn't keep with ``[&Result,
  Moved]``. A container a member function of ``RemovingMethods`` is given
  only the constant 0, ``Jobs.f_SetLen(0)``, has no element left: an element
  it was given before, ``Jobs[0] = _pJob``, doesn't keep; given another
  length, ``f_SetLen(1)``, which element is left can't be told and the
  container keeps by type. One cleared, ``Jobs.f_Clear()`` or
  ``Jobs.clear()`` of ``RemovingMethods`` given nothing, has no element left
  either, and an element of it keeps nothing whatever object it would own,
  ``[&Result, Jobs = fg_Move(_Jobs)]`` after ``_Jobs.f_Clear()``. An element
  a member function whose body can be seen gives a reference to, of a member
  of its object, ``return m_Elements[_Index];``, is that element of that
  member of the container, ``Jobs[0]`` of a ``TCArray``, which the keeper
  holds there too: a container's elements are told by the arguments of its
  template only where no member holds one by value. A function whose body
  can be seen that gives back a part of what a reference parameter refers
  to, ``fg_GetFirst(Jobs)``, gives that part of the local, which a write
  through what it gives changes, ``fg_GetFirst(Jobs) = {}``. A call given a
  local's address or location whose body can be seen changes only the
  members, and of an array member the elements by their constant indices,
  that its body writes through that parameter, ``_pBatch->m_pJobs[1] =
  {}``: an element of another constant index is left as it was. An array,
  or a container a construction is given only a ``std::initializer_list``
  of its elements, ``TCList<TCSharedPointer<CJob>> Jobs{_pOther}``, holds
  the elements it is made of: a keeper of it owns what one of them owns,
  and none where none can, also of a closure's copy of a local array. A
  keeper made of a parameter that states it is distinct from the others
  (``DMibDistinct``) doesn't own what another parameter is or owns,
  ``fg_ListOther(_pJob, DMibDistinct _pOther)``; unstated, the two may be
  the same object and the keeper keeps by type. Whether a keeper owns the
  object a borrow is into is the same, another, or can't be told: two
  constructions, a construction and a parameter or what a call gives, two
  results of a function that states it gives back a new object
  (``DMibReturnsNew``) or of a factory whose body makes one, are different
  objects; two results of calls that can't be seen, ``fg_CurrentJob()``
  twice, can't be told, and the keeper keeps by type. An owner made from a
  raw pointer to what another owner owns, ``TCSharedPointer<CJob>(
  _pJob.f_Get())`` or ``(&*_pJob)``, owns that object, while one made from
  ``new CJob`` owns a new one. An owner declared by an earlier declarator of
  a declaration keeps in a later one's initializer, ``TCSharedPointer<CJob>
  pKeep(this), pSelected = ...;``. A capture of a structured
  binding the closure's code can change, ``pThis.f_Clear()``, keeps
  nothing. A copy a closure keeps of the record a local reference refers to
  shares what its pointer, reference and view members refer to: given whole
  where more than its own part can be read, ``fg_Read(Request)``, or read
  through a reference member, ``Ref.m_Ref``, it uses what they refer to. A member of a record a
  constructor whose body can be seen makes is what reaches the end of that
  body on a way that doesn't throw: what its initializer, an assignment, the
  constructor it delegates to or that of its base give it, of a parameter
  what the construction gives; ``if (_bKeep) m_pJob = _pJob;`` doesn't keep.
  An inheriting constructor, ``using CBase::CBase;``, is the base's
  constructor it inherits, given the same arguments. Of an array member made
  element by element, ``m_pJobs{_pJob, _pJob}``, the keeper holds each
  element and keeps what any of them owns; ``m_pJobs{fg_MakeJob(),
  fg_MakeJob()}`` doesn't keep ``_pJob``'s object. An owner further out on
  the derivation than the one the borrow is into, ``pRequest`` of
  ``_pRequest->m_pJob->m_Result``, keeps only where each owner nearer the
  borrow is const, ``TCSharedPointer<CJob> const m_pJob``, or is in what a
  ``unique`` owner the closure takes owns alone: each owner from it out to
  the one the closure holds is a ``unique`` owner of what the one before is
  in, the outermost a local of the function that makes the closure, or a
  parameter of it by value or by ``&&``, and neither that function, other
  than where the borrow reads it and the capture takes it, nor the closure
  writes what it owns or keeps it beyond the frame, ``[&Result, pKeep =
  fg_Move(pContext)]`` of ``int &Result = pContext->m_pJob->m_Result;``;
  else whoever shares the outer object can replace it, and so can the code
  that does, ``pKeep->m_pJob = ...``.
  A member function the constructor calls on the object whose body can be
  seen and that no override replaces, also with an explicit object
  parameter, ``f_SetJob(this CKeep &_Self, ...)``, gives the member what
  reaches the end of its body, of a parameter of it what the call gives it,
  when it assigns the member on each way; else what it gives can't be told.
  A member of a record a closure copies is that member of the copied record
  where the closure is made, also where it is read in the closure's body
  and the body doesn't change it; of an element of an array member only
  where each definition of the record that reaches there is known. A swap
  of two records gives each member what the other's member held. A function
  whose body can be seen changes what it is given by a pointer or a
  reference that isn't to const only where the body assigns, steps or
  changes what is named through it, or gives it on where it can be changed
  (``fg_GetID(&Context)`` that only reads changes nothing,
  ``fg_ResetThrough(&Context)`` changes it), also by a reference to const
  of an owner or a pointer, whose constness doesn't cover what it owns or
  points to, ``_pContext->m_pJob = {}`` of ``TCSharedPointer<CJobContext>
  const &_pContext``; what such a function gives
  back of a member of what it is given is that member where it is called,
  ``fg_GetJob(&Context)``, and what it can't tell where the body changes
  it, ``fg_ResetAndGet(&Context)``. A reference given to a call whose
  result is dropped, or that gives back a reference only copied, ``auto
  pKeep = fg_GetJob(pContext)``, isn't kept by it; nor is the owner where
  only an address of a number in what it owns is kept, ``CResultRef
  Ref{&pJob->m_Result}``. A member function with an explicit
  object parameter that is a reference tells the members it changes like
  one with an implicit object. A capture is a slot of the closure's body like
  a local: one the body can change keeps only at the uses each value of it
  that reaches keeps at, ``[this, pThis]() mutable { pThis = {}; ... }``,
  also in each specialization of a generic lambda's call operator; what the
  body assigns reaches its start again only where it can be called again:
  not of a closure given, as all that is done with it, to a function that
  runs what it is given (``DMibRunsGivenFunction``), which calls it once, so
  ``fg_CallSafe([pValue, pKeep = _pJob]() mutable -> TCFuture<int> { co_await
  fg_Work(); int Value = *pValue; pKeep.f_Clear(); co_return Value; })``
  keeps at the use. A change
  is also one made by a closure the body runs or gives where it can be run,
  ``[&pThis] { pThis.f_Clear(); }``, and what a function whose body can be
  seen does to what it is given by a reference to const of an owner or a
  pointer, casting the const away, ``const_cast<TCSharedPointer<CJob> &>(_pJob)
  = {};``, also through the functions it gives it on to and each override a
  virtual call can run. Where the provenance can't tell, an owner
  that isn't in a local slot nor a member of the object, a parameter, what a
  body that can't be seen gives, a member a setter called on the object
  gives on some ways only, an element of a container changed as a whole, the type decides.
- Seven misses are known: an owner released or reassigned between the
  borrow and the capture where it isn't in a local slot, a member of the
  object nor a part of what a reference parameter refers to (reached
  through a pointer parameter or a member of another object), a keeper that owns
  a different object of a fitting type where the provenance can't tell, a
  member of a record read through a pointer to the record, a record that is
  a member of the object given the address of a local, a copy of a record captured by value that is given whole to a function, an awaiter whose type can't hold a pointer, a reference or a view (list its type in ``ViewTypes`` to have it checked), and what a
  function or constructor whose body can't be seen keeps elsewhere than in
  its result, the object it makes or is called on by what isn't a const
  member function, or what its other arguments by reference or pointer to
  what isn't const refer to, each by its type, aren't reported; through a
  pointer to a function or a member function, or any other callee, its
  function type decides the same. A parameter held by value given the
  address of a local, ``_Info.m_pValue = &Value; [_Info]``, is reported. What is given as a prvalue or an xvalue is
  moved or copied, never referred to. The promise is plumbing, never looked
  into: the conversion a ``co_return`` performs into the result is judged by
  type.

A reference init capture is a capture by reference of what its initializer
designates, whatever that is: a variable it reaches, through members,
elements and what a call gives of its object, is followed; what else it
designates, ``[&ThreadLocal = fg_ConcurrencyThreadLocal()]``, is there while
the function that makes the lambda runs, as what a local reference bound to it
is, so a closure called where it is made may use it before its first
suspension.

A reference init capture of what has static storage, like ``[&Value =
s_Value]``, also of a member or an element of it, ``[&Value =
g_Data.m_Value]``, also through a cast to a reference, ``[&Value =
static_cast<int &>(g_Value)]``, refers to what lives as long as the program, and is fine, but not
one of ``thread_local`` storage, which is gone when the thread that made the
lambda ends, and neither is a pointer to one, ``[pData = &t_Data]``.

Nothing is reported in a translation unit with an error that stops it from
compiling: its control flow isn't built, and what the errors leave can't be
relied on.

.. example-check

  TCFuture<void> fg_Timeout(double _Seconds);
  struct CJob
  {
    int m_Value = 0;
  };
  struct CInfo
  {
    int *m_pValue;
    int m_Count;
  };
  struct CHolder
  {
    TCSharedPointer<CJob> m_pJob;
  };
  struct CIdAwaiter
  {
    CIdAwaiter(int const *_pValue);
    bool await_ready();
    void await_suspend(std::coroutine_handle<> _Handle);
    int await_resume();
    unsigned long m_Id;
  };
  TCSharedPointer<CJob> fg_NewJob();
  template <typename t_CType> t_CType fg_Make();
  void fg_Register(int const *_pValue);
  void fg_Give(CInfo _Info);
  void fg_Reset(int *&_pValue);
  int s_Value = 0;
  thread_local int t_Data = 0;
  template <unsigned long tf_Size>
  TCFuture<void> fg_Literal(char const (&_Error)[tf_Size])
  {
    co_await fg_CallSafe([Error = _Error]() -> TCFuture<int> { co_await fg_Timeout(1.0); co_return Error[0]; }); // fine: a string literal
    co_return {};
  }
  TCFuture<void> fg_Literals()
  {
    co_await fg_Literal("Error");
    char Local[] = "A local";
    co_await fg_Literal(Local); // clean: a known miss, a local character array
    co_return {};
  }
  TCFuture<void> fg_Storage()
  {
    co_await fg_CallSafe([&Value = s_Value]() -> TCFuture<int> { co_await fg_Timeout(1.0); co_return Value; }); // fine: static storage
    co_await fg_CallSafe([&Value = t_Data]() -> TCFuture<int> { co_await fg_Timeout(1.0); co_return Value; }); // warning: thread_local storage
    co_await fg_CallSafe([pData = &t_Data]() -> TCFuture<int> { co_await fg_Timeout(1.0); co_return *pData; }); // warning: a pointer to thread_local storage
    co_return {};
  }
  TCFuture<void> fg_Records()
  {
    int Value = 0;
    CInfo Info{&Value, 1};
    co_await fg_CallSafe([Info]() -> TCFuture<int> { co_await fg_Timeout(1.0); co_return *Info.m_pValue; }); // warning: the member that points to a local
    co_await fg_CallSafe([Info]() -> TCFuture<int> { co_await fg_Timeout(1.0); co_return Info.m_Count; }); // fine: the record's own part
    CInfo *pInfo = &Info; // clean: a known miss, a member read through a pointer to the record
    int *pValue = pInfo->m_pValue;
    co_await fg_CallSafe([pValue]() -> TCFuture<int> { co_await fg_Timeout(1.0); co_return *pValue; });
    co_await fg_CallSafe([Info]() -> TCFuture<int> { co_await fg_Timeout(1.0); fg_Give(Info); co_return 0; }); // clean: a known miss, given whole to a function
    co_return {};
  }
  TCFuture<void> fg_Parameter(CInfo _Info)
  {
    int Value = 0;
    _Info.m_pValue = &Value; // warning: a parameter held by value given the address of a local
    co_await fg_CallSafe([_Info]() -> TCFuture<int> { co_await fg_Timeout(1.0); co_return *_Info.m_pValue; });
    co_return {};
  }
  TCFuture<void> fg_Helper()
  {
    int Value = 0; // warning: a helper that points what it is given elsewhere isn't followed
    int *pValue = &Value;
    fg_Reset(pValue);
    co_await fg_CallSafe([pValue]() -> TCFuture<int> { co_await fg_Timeout(1.0); co_return *pValue; });
    co_return {};
  }
  TCFuture<void> fg_Owners(CHolder *_pHolder, TCSharedPointer<CJob> _pJob, TCSharedPointer<CJob> _pOther)
  {
    int &Value = _pHolder->m_pJob->m_Value; // fine: a keeper of what it borrows
    co_await fg_CallSafe([pKeep = _pHolder->m_pJob, &Value]() -> TCFuture<int> { co_await fg_Timeout(1.0); co_return Value; });
    int &Replaced = _pHolder->m_pJob->m_Value; // clean: a known miss, an owner reached through a pointer parameter reassigned in between
    _pHolder->m_pJob = fg_NewJob();
    co_await fg_CallSafe([pKeep = _pHolder->m_pJob, &Replaced]() -> TCFuture<int> { co_await fg_Timeout(1.0); co_return Replaced; });
    auto pJob = _pJob; // warning: an owner in a local slot reassigned in between
    int &Local = pJob->m_Value;
    pJob = fg_NewJob();
    co_await fg_CallSafe([pJob, &Local]() -> TCFuture<int> { co_await fg_Timeout(1.0); co_return Local; });
    co_await fg_CallSafe([pKeep = _pJob, &Other = _pOther->m_Value]() -> TCFuture<int> { co_await fg_Timeout(1.0); co_return Other; }); // clean: a known miss, a keeper of another object of a fitting type
    co_return {};
  }
  TCFuture<void> fg_Unseen()
  {
    int Value = 0;
    co_await fg_CallSafe([&Value]() -> TCFuture<int> { co_return co_await CIdAwaiter(&Value); }); // clean: a known miss, an awaiter whose type can't hold a pointer
    co_await fg_CallSafe([&Value]() -> TCFuture<int> { fg_Register(&Value); co_await fg_Timeout(1.0); co_return 0; }); // clean: a known miss, kept elsewhere by a body that can't be seen
    co_return {};
  }
  struct CApp : CActor
  {
    CInfo m_Info;
    TCFuture<void> f_Member()
    {
      int Value = 0; // clean: a known miss, a record that is a member of the object
      m_Info.m_pValue = &Value;
      CInfo Copy = m_Info;
      co_await fg_CallSafe([Copy]() -> TCFuture<int> { co_await fg_Timeout(1.0); co_return *Copy.m_pValue; });
      co_return {};
    }
  };
  struct CTask
  {
    int m_Result = 0;
    DMibKeepsThis TCSharedPointer<CTask> f_Shared();
    TCFuture<int> f_NewKeeper()
    {
      return fg_CallSafe([&Value = m_Result, Kept = fg_Make<TCSharedPointer<CTask>>()]() -> TCFuture<int> { co_await fg_Timeout(1.0); co_return Value; }); // warning: a new owner keeps nothing of the object
    }
    TCFuture<int> f_StatedKeeper()
    {
      return fg_CallSafe([&Value = m_Result, Kept = f_Shared()]() -> TCFuture<int> { co_await fg_Timeout(1.0); co_return Value; }); // fine: an owner of the object, stated
    }
  };

A lambda in a template, also a generic lambda, ``[&Value](auto _Future) ->
decltype(_Future)``, is checked in the instantiations of its call operator,
which tell what it returns and whether ``t_CType Copy = Value`` copies or
refers; one that isn't instantiated isn't checked.

A lambda that is dispatched and joined with ``f_CallSync()`` isn't kept that
way: what is between the dispatch and the join can leave the function first,
so, like a coroutine given references, it must run to completion (see
:doc:`malterlib-runs-to-completion <runs-to-completion>`), or return a
``TCUnsafeFuture`` when what it refers to is known to outlive it. A lambda
given only to a test category, like ``DMibTestCategory("Name") % [&] () ->
TCFuture<void> { ... }``, which dispatches it and waits for it with nothing
in between, is joined by the end of that call and may refer to what lives
until then, as below: the locals of the function, by reference, and a
temporary of that full expression, like the array of ``[Ids = {1, 2}]``.

Capture by value instead. ``TCUnsafeFuture`` lambdas opt out of the lifetime
protections and may capture by reference, and so may the cleanup functions of
``fg_AsyncDestroy``, which :doc:`malterlib-async-destroy <async-destroy>`
checks.

A lambda that code that runs to completion (see
:doc:`malterlib-runs-to-completion <runs-to-completion>`) starts and joins may
capture what lives until the join, one rule for each kind of storage, the
context of a callee joined by the end of an expression: with an
``fg_AsyncDestroy`` guard that awaits its future, the locals declared before
an early await of the guard in a scope around it, when the guard is awaited
early in their scope on every path, like ``co_await fg_Move(Guard)`` (the
cleanup is joined only after the locals of the coroutine are gone otherwise);
awaited in its full-expression, the locals of the function declared before
that full expression and the temporaries of it, like the array of
``co_await fg_CallSafe([Ids = {1, 2}]() -> TCFuture<int> {...})``; the
parameters, of which references and pointers only of a function that runs to
completion; static storage and the object; what owners own when a keeper of
it is there until the join, held by value by the closure or the code where
it starts; never ``thread_local`` storage. What a capture refers to is where
provenance says, also through a pointer captured by reference, like
``[&pValue]``, what it is made from or pointed at, like ``pValue = &Inner``,
also cast or exchanged, and a view, like ``[&Span]``, what it is made of or
assigned, like ``Span = {Local, 5}``; with no flow, a local a pointer is
pointed at after the join, in a loop around it, counts too.

A lambda whose introducer comes from a macro, like the ``[&] ()`` of
``DMibTestCategory("Name") % [&] () { ... }``, is reported where the macro is
used, in the code that writes the rest of the lambda.

What the address of a reference parameter points to, ``[pValue =
&_Value]``, is what the caller keeps, and is reported like a reference to it,
unless it is the object of a class of ``CAllowUnsafeThis`` or of the actor
the closure runs on, or the closure holds a keeper of it, as above.
