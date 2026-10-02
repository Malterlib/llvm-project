# Writes the matrices of Rule K, what keeps storage alive by type, and of
# Rule R, what keeps a reference parameter by type, a section for each mode.
# `<section>` writes the code, `<section>-expect` the lines that are reported,
# a line and the check each; `expect` is the expectation of `cpp`. A line the
# check should report and doesn't yet is pending: it isn't expected, and it
# carries a `// RESTORE-MESSAGES: <check>` comment that the gate counts.
#
# Rule K: a borrow is alive where the context holds by value a keeper that
# fits one of its anchors: of what an owner owns, what a shared or unique
# owner owns, or a buffer or sharedbuffer owner itself; of the object of a
# member function, its class; of what a reference or pointer parameter refers
# to, its type; a base or a derived class fits too. Keepers of the `shared`,
# `unique`, `buffer` and `sharedbuffer` kinds keep, weak ones never. A keeper
# is held by the context itself, in a field, a base, an array, a container or
# a nested record. What a context holds is its declarations by type: a
# parameter or a local held by value keeps through any kind, whatever
# initializes it. A take is only where a holding is made from an expression,
# a capture, a result and an argument, by the value category as written: a
# copy of an lvalue keeps through `shared` and `sharedbuffer` owners, a move
# or a new value through any; the name of a local or by-value parameter
# returned is a move, and a result made as a whole, `CResult{...}`, is a new
# value. Takes that don't compile, a copy of a unique owner, aren't written.
#
# `cpp` writes the captures and the parameters of closures that borrow what an
# owner owns, the keepers made of that owner, and `result` the results of
# lambda coroutines, with controls; `capture-owned`, `capture-parameter` and
# `capture-object` the captures, parameters and locals of closures that borrow
# what an owner owns, what a parameter refers to, and the object, with
# keepers of each type; `frame` the parameters, locals and results of
# coroutines that borrow what a parameter refers to, and one row each of the
# other holders, declarations and takes; `this-frame` and `this-closure`
# those of member coroutines and the closures of member functions that use
# their object; `runs` the arguments of calls that run to completion;
# `retention` Rule R, also by the callee's type and const-ness; `getters` what
# a call gives relative to its object and its arguments (the call-result
# relation), under each check that asks, with views captured through a
# reference and reference init captures bound to a call; `landed` where a
# borrow from an entry of actor state lands (actor-reference's stale data),
# by each derivation shape, kept by a keeper of what the owner in the entry
# owns, with the controls; `joined` what a lambda that code that runs to
# completion starts and joins in the full expression borrows, alive until the
# join, by each kind of storage and derivation shape; `values` and
# `values-capture` what a use does to the storage it designates or points to,
# by each root (a pointer or a reference), each shape that hands the value on
# unchanged, each consumer and each way to it (built in, a callee that is seen
# or one that isn't).
import sys

Mode = sys.argv[1]
if Mode == 'expect':
    Section = 'cpp'
elif Mode.endswith('-expect'):
    Section = Mode[:-len('-expect')]
else:
    Section = Mode
Sections = ('cpp', 'result', 'capture-owned', 'capture-parameter', 'capture-object', 'frame', 'this-frame',
            'this-closure', 'runs', 'retention', 'getters', 'landed', 'joined', 'values', 'values-capture')
if Section not in Sections:
    sys.exit('Unknown mode: ' + Mode)
Lines = []
Expected = []


def emit(Text, Check=None, Pending=None):
    # A line, and the check that reports it, or a list of them, once each. Pending is a check, or a list of them,
    # that should report the line and doesn't yet: the line is written with a `// RESTORE-MESSAGES: <check>` comment
    # for each, which the gate counts (lbt/pending-check.py), and isn't expected.
    for One in [Pending] if isinstance(Pending, str) else Pending or []:
        Text += ' // RESTORE-MESSAGES: ' + One
    Lines.append(Text)
    for One in [Check] if isinstance(Check, str) else Check or []:
        Expected.append('%d %s' % (len(Lines), One))


Kinds = {
    # Kind: owner type, made, borrow, keeper type, keeper made of the owner.
    'shared': ('TCSharedPointer<CJob>', 'fg_MakeJob()', 'pOwner->m_Result', 'TCSharedPointer<CJob>'),
    'unique': ('TCUniquePointer<CJob>', 'TCUniquePointer<CJob>(new CJob)', 'pOwner->m_Result', 'TCUniquePointer<CJob>'),
    'buffer': ('TCVector<int>', 'TCVector<int>()', 'pOwner[0]', 'TCVector<int>'),
    'sharedbuffer': ('CStr', 'fg_ReadText()', '*pOwner.f_GetStr()', 'CStr'),
    'weak': ('TCSharedPointer<CJob>', 'fg_MakeJob()', 'pOwner->m_Result', 'TCWeakPointer<CJob>'),
}
Holders = ['itself', 'field', 'base', 'array', 'container', 'nested']
Takes = ['copy', 'move', 'new']


def keeps(Kind, Take):
    # Whether a keeper of the kind Kind keeps, taken as Take: `copy`, `move`,
    # `new`, or `held`, a parameter or local held by value.
    if Kind == 'weak':
        return False
    if Take == 'copy':
        return Kind in ('shared', 'sharedbuffer')
    return True


def emitHolders(Tag, Keeper):
    # The records that hold a keeper, and the type of each holder.
    emit('struct CField%s { %s m_Keep; int m_Other = 0; };' % (Tag, Keeper))
    emit('struct CBase%s : CField%s {};' % (Tag, Tag))
    emit('struct CArray%s { %s m_Keeps[1]; };' % (Tag, Keeper))
    emit('struct CNested%s { CField%s m_Field; };' % (Tag, Tag))
    return {
        'itself': Keeper,
        'field': 'CField%s' % Tag,
        'base': 'CBase%s' % Tag,
        'array': 'CArray%s' % Tag,
        'container': 'TCVector<%s>' % Keeper,
        'nested': 'CNested%s' % Tag,
    }


def madeOfOwner(Kind, K, Keeper):
    # The keeper made of the owner, a new one of each holder; none of a
    # container, which can't be made of it.
    Of = 'TCWeakPointer<CJob>()' if Kind == 'weak' else 'fg_Move(pOwner)'
    return {
        'itself': '%s(%s)' % (Keeper, Of),
        'field': 'CField%s{%s}' % (K, Of),
        'base': 'CBase%s{{%s}}' % (K, Of),
        'array': 'CArray%s{{%s}}' % (K, Of),
        'container': None,
        'nested': 'CNested%s{{%s}}' % (K, Of),
    }


emit('#include "malterlib-concurrency.h"')
emit('using namespace NMib::NConcurrency;')
emit('using namespace NMib::NStorage;')
emit('TCFuture<int> fg_Work();')
emit('struct CJobBase {}; struct CJob : CJobBase { int m_Result = 0; CJob &f_Self() { return *this; } }; struct CJobDerived : CJob {};')
emit('DMibReturnsNew TCSharedPointer<CJob> fg_MakeJob();')
emit('template <typename t_CType>')
emit('struct TCUniquePointer {')
emit('  t_CType *m_pPointTo;')
emit('  explicit TCUniquePointer(t_CType *_pPointTo) : m_pPointTo(_pPointTo) {}')
emit('  TCUniquePointer(TCUniquePointer &&_Other) : m_pPointTo(_Other.m_pPointTo) { _Other.m_pPointTo = nullptr; }')
emit('  ~TCUniquePointer() { delete m_pPointTo; }')
emit('  t_CType *operator->() const { return m_pPointTo; }')
emit('};')
emit('template <typename t_CData>')
emit('struct TCVector {')
emit('  TCVector();')
emit('  TCVector(TCVector const &_Other);')
emit('  TCVector(TCVector &&_Other);')
emit('  ~TCVector();')
emit('  t_CData &operator[](int _Index) { return mp_pData[_Index]; }')
emit('  t_CData *mp_pData;')
emit('};')
emit('struct CStrTraits_CStr {};')
emit('template <typename t_CTraits>')
emit('struct TCStr {')
emit('  TCStr();')
emit('  TCStr(TCStr const &_Other);')
emit('  TCStr(TCStr &&_Other);')
emit('  ~TCStr();')
emit('  char const *f_GetStr() const { return mp_pStr; }')
emit('  char const *mp_pStr;')
emit('};')
emit('using CStr = TCStr<CStrTraits_CStr>;')
emit('CStr fg_ReadText();')
if Section not in ('cpp', 'result'):
    # A new value of any type, and a class no anchor is of.
    emit('template <typename t_CType> DMibReturnsNew t_CType fg_Make();')
    emit('struct COther { int m_Other = 0; };')

# cpp: a capture and a parameter of a closure that borrows what an owner
# owns, with a keeper made of that owner.
Check = 'malterlib-coroutine-reference-capture'
for Kind, (Owner, Made, Borrow, Keeper) in Kinds.items() if Section == 'cpp' else []:
    K = Kind.capitalize()
    Types = emitHolders(K, Keeper)
    News = madeOfOwner(Kind, K, Keeper)
    for Holder in Holders:
        Type, New = Types[Holder], News[Holder]
        for Take in Takes:
            if Take == 'copy' and Kind == 'unique':
                continue
            if Holder == 'container' and Take == 'new':
                continue
            Name = 'fg_%s_%s_%s' % (Kind, Holder, Take)
            emit('TCFuture<int> %s(%s Keep) {' % (Name, Type) if Holder == 'container' else 'TCFuture<int> %s() {' % Name)
            emit('  %s pOwner = %s;' % (Owner, Made))
            emit('  auto &Result = %s;' % Borrow)
            if Take == 'new':
                Init = New
            else:
                if Holder != 'container':
                    emit('  %s Keep = %s;' % (Type, New))
                else:
                    emit('  (void)0;')
                Init = 'Keep' if Take == 'copy' else 'fg_Move(Keep)'
            # A container parameter moved into the capture holds other objects
            # than a `unique` or `buffer` owner made here (Rule K's identity),
            # and copied or moved, than the new job `fg_MakeJob` states it
            # gives back (DMibReturnsNew).
            bOther = Holder == 'container' and ((Take == 'move' and Kind in ('unique', 'buffer')) or Kind == 'shared')
            emit('  return fg_CallSafe([&Result, Kept = %s]() -> TCFuture<int> { co_await fg_Work(); co_return Result; });' % Init,
                 None if keeps(Kind, Take) and not bOther else Check)
            emit('}')
        # A parameter of the closure held by value.
        emit('TCFuture<int> fg_%s_%s_parameter(%s _Keep) {' % (Kind, Holder, Type))
        emit('  %s pOwner = %s;' % (Owner, Made))
        emit('  auto &Result = %s;' % Borrow)
        # The parameter is given `_Keep`, another object than `pOwner`, which
        # is made here: of a `unique` or `buffer` owner, and of the new job
        # `fg_MakeJob` states it gives back (DMibReturnsNew), which no
        # parameter of the function can own (Rule K's identity), reported.
        emit('  auto fRun = [&Result](%s _Kept) -> TCFuture<int> { co_await fg_Work(); co_return Result; };' % Type,
             None if keeps(Kind, 'held') and Kind not in ('unique', 'buffer', 'shared') else Check)
        emit('  return fRun(fg_Move(_Keep));')
        emit('}')
# cpp, identity: whether a keeper owns the object a borrow is into is the
# same, another, or can't be told. Each way the keeper and the borrowed owner
# are made (two results of a call that can't be seen, of a function that
# states it gives back a new object, of a factory whose body is seen, a copy
# of the owner, an owner made from a raw pointer to the owner's object or to
# a new one, an earlier and a later declarator of one declaration), crossed
# with where the keeper is held (a capture of the closure, a parameter of the
# closure it is called with): reported only
# where the two are told to be different objects.
if Section == 'cpp':
    emit('TCSharedPointer<CJob> fg_CurrentJob();')
    emit('TCSharedPointer<CJob> fg_SeenFactory() { return TCSharedPointer<CJob>(new CJob); }')
    Identities = [
        # How the borrowed owner and the keeper are made, the statements, the
        # keeper, whether they are told to be different objects.
        ('unseen_twice', 'auto pOwner = fg_CurrentJob(); auto &Result = pOwner->m_Result; ', 'fg_CurrentJob()', False),
        ('stated_new', 'auto pOwner = fg_MakeJob(); auto &Result = pOwner->m_Result; ', 'fg_MakeJob()', True),
        ('seen_factory', 'auto pOwner = fg_SeenFactory(); auto &Result = pOwner->m_Result; ', 'fg_SeenFactory()', True),
        ('copy', 'auto pOwner = fg_CurrentJob(); auto &Result = pOwner->m_Result; ', 'pOwner', False),
        ('raw_pointer', 'auto pOwner = fg_CurrentJob(); auto &Result = pOwner->m_Result; ',
         'TCSharedPointer<CJob>(pOwner.f_Get())', False),
        ('raw_address', 'auto pOwner = fg_CurrentJob(); auto &Result = pOwner->m_Result; ',
         'TCSharedPointer<CJob>(&*pOwner)', False),
        ('raw_new', 'auto pOwner = fg_CurrentJob(); auto &Result = pOwner->m_Result; ',
         'TCSharedPointer<CJob>(new CJob)', True),
    ]
    for Make, Before, Keeper, bOther in Identities:
        emit('TCFuture<int> fg_identity_%s_capture() { %sreturn fg_CallSafe([&Result, pKeep = %s]() -> TCFuture<int> '
             '{ co_await fg_Work(); co_return Result; }); }' % (Make, Before, Keeper), Check if bOther else None)
        emit('TCFuture<int> fg_identity_%s_parameter() { %sauto fRun = [&Result](TCSharedPointer<CJob> _Kept) -> '
             'TCFuture<int> { co_await fg_Work(); co_return Result; }; return fRun(%s); }' % (Make, Before, Keeper),
             Check if bOther else None)
# result: a lambda coroutine's result that holds a borrow of what an owner it
# captures owns, with a keeper in a field, its member initialized by each
# take, which isn't asked: the result made as a whole is a new value. Not of
# a buffer, whose data the result check reaches through its pointer member,
# which it takes to be elsewhere.
for Kind, (Owner, Made, Borrow, Keeper) in Kinds.items() if Section == 'result' else []:
    if Kind == 'buffer':
        continue
    K = Kind.capitalize()
    Of = 'TCWeakPointer<CJob>()' if Kind == 'weak' else 'fg_Move(pOwner)'
    Pointee = 'char const' if Kind == 'sharedbuffer' else 'int'
    emit('struct CResult%s { %s m_Keep; %s *m_pResult; };' % (K, Keeper, Pointee))
    for Take in Takes:
        if Take == 'copy' and Kind == 'unique':
            continue
        emit('void fg_%s_result_%s() {' % (Kind, Take))
        emit('  (void)(g_Dispatch / [pOwner = %s]() mutable -> TCFuture<CResult%s> {' % (Made, K),
             None if keeps(Kind, 'new') else 'malterlib-coroutine-lambda-call')
        emit('    co_await fg_Work();')
        emit('    auto &Result = %s;' % Borrow)
        if Take == 'new':
            emit('    (void)0;')
            Init = '%s(%s)' % (Keeper, Of)
        else:
            emit('    %s Keep = %s;' % (Keeper, Of))
            Init = 'Keep' if Take == 'copy' else 'fg_Move(Keep)'
        emit('    co_return CResult%s{%s, &Result};' % (K, Init))
        emit('  });')
        emit('}')
if Section == 'result':
    # Controls, reported: a result whose keeper owns an unrelated type, and
    # one that holds only a pointer to the captured owner.
    emit('struct COther { int m_Other = 0; };')
    emit('template <typename t_CType> t_CType fg_Make();')
    emit('struct CResultOther { TCSharedPointer<COther> m_Keep; int *m_pResult; };')
    emit('struct CResultPointer { TCSharedPointer<CJob> const *m_pKeep; int *m_pResult; };')
    for Name, Type, Init in [('other', 'CResultOther', 'fg_Make<TCSharedPointer<COther>>()'),
                             ('pointer', 'CResultPointer', '&pOwner')]:
        emit('void fg_shared_result_%s() {' % Name)
        emit('  (void)(g_Dispatch / [pOwner = fg_MakeJob()]() mutable -> TCFuture<%s> {' % Type,
             'malterlib-coroutine-lambda-call')
        emit('    co_await fg_Work();')
        emit('    auto &Result = pOwner->m_Result;')
        emit('    co_return %s{%s, &Result};' % (Type, Init))
        emit('  });')
        emit('}')


def anchoredKeepers(Anchor):
    # The keepers tried for what is anchored by the class Anchor, the object
    # or what a parameter refers to: a tag, the type, its owner kind, and
    # whether it fits. Shared and unique owners of the class, of a base and of
    # a derived class fit, buffer and sharedbuffer owners only what they own
    # themselves, an owner of another class nothing.
    return [
        ('Shared', 'TCSharedPointer<%s>' % Anchor, 'shared', True),
        ('Unique', 'TCUniquePointer<%s>' % Anchor, 'unique', True),
        ('Base', 'TCSharedPointer<%sBase>' % Anchor, 'shared', True),
        ('Derived', 'TCUniquePointer<%sDerived>' % Anchor, 'unique', True),
        ('Weak', 'TCWeakPointer<%s>' % Anchor, 'weak', True),
        ('Buffer', 'TCVector<int>', 'buffer', False),
        ('SharedBuffer', 'CStr', 'sharedbuffer', False),
        ('Other', 'TCSharedPointer<COther>', 'shared', False),
    ]


# Whether the borrow is of the object of a member function, whose owner
# parameters keep it only where they state that they own it (DMibKeepsThis).
ObjectLine = Section in ('capture-object', 'this-frame', 'this-closure')


def keepsThis(Kind, bFits):
    # The annotation of an owner parameter of a member function that owns the
    # object: of a keeper that fits and keeps, where the borrow is the object.
    return 'DMibKeepsThis ' if ObjectLine and bFits and Kind != 'weak' else ''


def closureTakes(T, Annotation=''):
    # Each take of a keeper of the type T by a closure: the case, the
    # parameters of the function it is made in, with Annotation, its
    # captures, the parameters of its call operator, what its body runs
    # before the suspension, the argument it is called with, and the take
    # Rule K decides by. A parameter and a local are held, whatever
    # initializes them.
    Keep = '%s%s &_Keep' % (Annotation, T)
    return [
        ('capture_copy', [Keep], ['Kept = _Keep'], [], '', None, 'copy'),
        ('capture_bycopy', [Keep], ['_Keep'], [], '', None, 'copy'),
        ('capture_move', [Keep], ['Kept = fg_Move(_Keep)'], [], '', None, 'move'),
        ('capture_new', [], ['Kept = fg_Make<%s>()' % T], [], '', None, 'new'),
        ('parameter', [], [], ['%s _Kept' % T], '', 'fg_Make<%s>()' % T, 'held'),
        ('local_copy', [Keep], [], ['%s &_Given' % T], '%s Kept = _Given; ' % T, '_Keep', 'held'),
        ('local_move', [Keep], [], ['%s &_Given' % T], '%s Kept = fg_Move(_Given); ' % T, '_Keep', 'held'),
        ('local_new', [], [], [], '%s Kept = fg_Make<%s>(); ' % (T, T), None, 'held'),
    ]


def closureCase(Root, Name, Params, Captures, LambdaParams, LambdaPre, Argument, Local=''):
    # One line: a function that gives fg_CallSafe a closure that borrows as
    # Root does and uses the borrow after a suspension. Root is the start of
    # the function, its own parameters, what it runs first, the capture that
    # borrows, and what the closure uses; Local what the function runs next.
    Start, RootParams, Pre, Borrow, Use = Root
    return '%s%s(%s) { %s%sreturn fg_CallSafe([%s](%s) -> TCFuture<int> { %sco_await fg_Work(); co_return %s; }%s); }' % (
        Start, Name, ', '.join(RootParams + Params), Pre, Local, ', '.join([Borrow] + Captures),
        ', '.join(LambdaParams), LambdaPre, Use, ', ' + Argument if Argument else '')


def closureMatrix(Root, Prefix, Keepers, Types, Check):
    # Each keeper, holder and take of a closure that borrows as Root does.
    for Tag, Keeper, Kind, bFits in Keepers:
        for Holder, T in Types[Tag].items():
            for Case, Params, Captures, LambdaParams, LambdaPre, Argument, Take in closureTakes(T, keepsThis(Kind, bFits)):
                if Case.endswith('copy') and Kind == 'unique':
                    continue
                # A new keeper, or one in a part of a new record, owns another
                # object than the owner, which the owner was made by (Rule K's
                # identity), also one given to the closure's parameter by
                # fg_CallSafe, which runs it with its other arguments
                # (DMibRunsGivenFunction); of the object, what a call that isn't
                # on it gives owns it only where the call states that.
                bNew = (Section == 'capture-owned' or ObjectLine) and \
                    Case in ('capture_new', 'local_new', 'parameter')
                # A `unique` or `buffer` keeper taken from what a reference
                # parameter refers to, which is there before the owner is made
                # here, owns another object; not yet told of an array member
                # moved into a capture.
                bOther = Section == 'capture-owned' and Kind in ('unique', 'buffer') and \
                    Case in ('capture_move', 'local_move', 'local_copy') and \
                    not (Holder == 'array' and Case == 'capture_move')
                emit(closureCase(Root, '%s%s_%s_%s' % (Prefix, Tag, Holder, Case), Params, Captures, LambdaParams,
                                 LambdaPre, Argument),
                     None if bFits and keeps(Kind, Take) and not bNew and not bOther else Check)
                # Of the object, the same owner parameter of the member function
                # without DMibKeepsThis may own another object: reported.
                if keepsThis(Kind, bFits) and Params and Holder == 'itself':
                    emit(closureCase(Root, '%s%s_%s_%s_untold' % (Prefix, Tag, Holder, Case),
                                     [Param.replace('DMibKeepsThis ', '') for Param in Params], Captures,
                                     LambdaParams, LambdaPre, Argument), Check)


def closureControls(Root, Prefix, Keeper, Check):
    # Controls of a closure that borrows as Root does, each reported: a fitting
    # keeper held by reference, through a pointer, and as a parameter by
    # reference. A local held by value counts where it is in scope at the use.
    Start, RootParams, Pre, Borrow, Use = Root
    Local = '%s Keep = fg_Make<%s>(); ' % (Keeper, Keeper)
    emit(closureCase(Root, Prefix + 'by_reference', [], ['&Keep'], [], '', None, Local), Check)
    emit(closureCase(Root, Prefix + 'by_pointer', [], ['pKeep = &Keep'], [], '', None, Local), Check)
    emit(closureCase(Root, Prefix + 'parameter_by_reference', ['%s &_Keep' % Keeper], [], ['%s const &_Kept' % Keeper], '',
                     '_Keep'), Check)
    # A keeper declared after the use, and one in a sibling scope that ends
    # before the suspension, keep nothing at the use.
    emit('%s%slocal_after(%s) { %sreturn fg_CallSafe([%s]() -> TCFuture<int> { co_await fg_Work(); int Read = %s; %s Kept = fg_Make<%s>(); co_return Read; }); }' % (
        Start, Prefix, ', '.join(RootParams), Pre, Borrow, Use, Keeper, Keeper), Check)
    emit('%s%slocal_sibling(%s) { %sreturn fg_CallSafe([%s]() -> TCFuture<int> { { %s Kept = fg_Make<%s>(); } co_await fg_Work(); co_return %s; }); }' % (
        Start, Prefix, ', '.join(RootParams), Pre, Borrow, Keeper, Keeper, Use), Check)


def emitAnchor(Anchor):
    # A class, a base of it and a derived class.
    emit('struct %sBase { int m_Base = 0; };' % Anchor)
    emit('struct %s;' % Anchor)
    emit('struct %sDerived;' % Anchor)


def anchoredTypes(Keepers, Prefix):
    # The holders of each keeper, by tag.
    return {Tag: emitHolders(Prefix + Tag, Keeper) for Tag, Keeper, Kind, bFits in Keepers}


# capture-owned: a closure that borrows what an owner owns, with keepers of
# each type given to the function, also of an unrelated type.
CaptureCheck = 'malterlib-coroutine-reference-capture'
if Section == 'capture-owned':
    for Kind, (Owner, Made, Borrow, Keeper) in Kinds.items():
        Root = ('TCFuture<int> fg_', [], '%s pOwner = %s; ' % (Owner, Made), '&Value = %s' % Borrow, 'Value')
        Keepers = [(Kind.capitalize(), Keeper, Kind, True)]
        closureMatrix(Root, '', Keepers, anchoredTypes(Keepers, 'Owned'), CaptureCheck)
        if Kind != 'weak':
            closureControls(Root, '%s_' % Kind, Keeper, CaptureCheck)
    # A function that runs the closure with its other arguments without
    # stating it (DMibRunsGivenFunction): what its parameter is given can't
    # be told, and a fitting keeper there keeps by type.
    emit('template <typename t_FFunction, typename t_CParam> TCFuture<int> fg_DirectDispatch(t_FFunction &&_fFunction, t_CParam &&_Param) { return {}; }')
    emit('TCFuture<int> fg_Shared_itself_parameter_unstated() { TCSharedPointer<CJob> pOwner = fg_MakeJob(); return fg_DirectDispatch([&Value = pOwner->m_Result](TCSharedPointer<CJob> _Kept) -> TCFuture<int> { co_await fg_Work(); co_return Value; }, fg_Make<TCSharedPointer<CJob>>()); }')
    # Owners of what doesn't fit: of an unrelated class, and a buffer of
    # another element type.
    Root = ('TCFuture<int> fg_', [], 'TCSharedPointer<CJob> pOwner = fg_MakeJob(); ', '&Value = pOwner->m_Result', 'Value')
    emit(closureCase(Root, 'shared_other', [], ['Kept = fg_Make<TCSharedPointer<COther>>()'], [], '', None), CaptureCheck)
    Root = ('TCFuture<int> fg_', [], 'TCVector<int> pOwner; ', '&Value = pOwner[0]', 'Value')
    emit(closureCase(Root, 'buffer_other', [], ['Kept = fg_Make<TCVector<char>>()'], [], '', None), CaptureCheck)
    # Frame storage, which no keeper keeps.
    emit('TCFuture<int> fg_frame() { CJob Job; TCSharedPointer<CJob> pOther = fg_MakeJob(); return fg_CallSafe([&Job, pOther]() -> TCFuture<int> { co_await fg_Work(); co_return Job.m_Result; }); }',
         CaptureCheck)
    # A record held by value carries what is put in each member: a copy of
    # a record with a pointer into a local, captured and used in each way
    # under each consumer, is clean where the capture's own part is used or
    # the pointer only tested, reported where the copy's pointer member is
    # dereferenced, and a known miss where the copy is given whole; captured
    # through a reference to the record, whose pointer member's target can't
    # be told (DMibSetByOwnClass), and a reference member of it, captured by
    # reference, are reported; the pointer captured itself is reported as
    # control.
    emit('struct CRecordCopy { int const *m_pValue; int m_Count; int f_Count() const { return m_Count; } };')
    emit('struct CRecordReference { int const &m_Value; };')
    emit('struct CRecordStrSpan { char const *m_pData; int m_Len; int f_GetLen() const { return m_Len; } };')
    emit('int fg_ReadRecord(CRecordCopy const &_Record);')
    Uses = [('count', 'Info.m_Count'), ('deref', '*Info.m_pValue'), ('test', 'Info.m_pValue != nullptr'),
            ('getter', 'Info.f_Count()'), ('whole', 'fg_ReadRecord(Info)')]
    Consumers = [('dispatch', 'return g_Dispatch / %s;'), ('callsafe', 'return fg_CallSafe(%s);'),
                 ('self', 'auto fRun = %s; return fRun();')]
    for ConsumerName, Consumer in Consumers:
        for UseName, Use in Uses:
            Closure = '[Info]() -> TCFuture<int> { co_await fg_Work(); co_return %s; }' % Use
            emit('TCFuture<int> fg_record_%s_%s() { int Value = 1; CRecordCopy Info{&Value, 5}; %s }' % (
                ConsumerName, UseName, Consumer % Closure), CaptureCheck if UseName == 'deref' else None)
        Closure = '[Info]() -> TCFuture<int> { co_await fg_Work(); co_return *Info.m_pValue; }'
        emit('TCFuture<int> fg_record_%s_from_reference() { int Value = 1; CRecordCopy Data{&Value, 5}; auto &Info = Data; %s }' % (
            ConsumerName, Consumer % Closure), CaptureCheck)
        Closure = '[pValue]() -> TCFuture<int> { co_await fg_Work(); co_return *pValue; }'
        emit('TCFuture<int> fg_record_%s_pointer() { int Value = 1; int const *pValue = &Value; %s }' % (
            ConsumerName, Consumer % Closure), CaptureCheck)
        # A member read through a pointer to the record is a value, a known
        # miss; a reference member of a local record is what it is bound to;
        # a record an init capture holds carries what it is made with.
        Closure = '[pValue = pInfo->m_pValue]() -> TCFuture<int> { co_await fg_Work(); co_return *pValue; }'
        emit('TCFuture<int> fg_record_%s_through_pointer() { int Value = 1; CRecordCopy Data{&Value, 5}; auto *pInfo = &Data; %s }' % (
            ConsumerName, Consumer % Closure))
        Closure = '[&Read = Info.m_Value]() -> TCFuture<int> { co_await fg_Work(); co_return Read; }'
        emit('TCFuture<int> fg_record_%s_reference_member() { int Value = 1; CRecordReference Info{Value}; %s }' % (
            ConsumerName, Consumer % Closure), CaptureCheck)
        Closure = '[Info = CRecordCopy{&Value, 5}]() -> TCFuture<int> { co_await fg_Work(); co_return *Info.m_pValue; }'
        emit('TCFuture<int> fg_record_%s_init_capture() { int Value = 1; %s }' % (ConsumerName, Consumer % Closure),
             CaptureCheck)
        # Of a view captured by copy, what reads only its own storage, a
        # getter of its length, isn't a use of what it refers to (the
        # predicate of by-value view parameters); its pointer is, as control.
        # A dispatched closure that holds a view of a local is reported
        # whatever it uses, as one that holds a pointer is.
        for UseName, Use, Used in [('length', 'Text.f_GetLen()', False), ('data', '*Text.m_pData', True)]:
            Closure = '[Text]() -> TCFuture<int> { co_await fg_Work(); co_return %s; }' % Use
            emit('TCFuture<int> fg_view_%s_%s() { char Buffer[8]{}; CRecordStrSpan Text{Buffer, 8}; %s }' % (
                ConsumerName, UseName, Consumer % Closure),
                CaptureCheck if Used or ConsumerName == 'dispatch' else None)
    # What a getter gives out of its object is anchored by the owners the
    # object holds by value, whether or not its body can be seen: a copy of
    # the member owner keeps it; reported without it as control.
    emit('struct CGetterOwner { TCSharedPointer<CJob> m_pJob; int &f_Result() { return m_pJob->m_Result; } int &f_ResultUnseen(); TCFuture<int> f_Seen(); TCFuture<int> f_Unseen(); TCFuture<int> f_None(); };')
    emit('TCFuture<int> CGetterOwner::f_Seen() { auto &Result = f_Result(); return fg_CallSafe([&Result, pKeep = m_pJob]() -> TCFuture<int> { co_await fg_Work(); co_return Result; }); }')
    emit('TCFuture<int> CGetterOwner::f_Unseen() { auto &Result = f_ResultUnseen(); return fg_CallSafe([&Result, pKeep = m_pJob]() -> TCFuture<int> { co_await fg_Work(); co_return Result; }); }')
    emit('TCFuture<int> CGetterOwner::f_None() { auto &Result = f_Result(); return fg_CallSafe([&Result]() -> TCFuture<int> { co_await fg_Work(); co_return Result; }); }',
         CaptureCheck)
    # Owners registered by their own names, spelled through an alias in a
    # class template (`CJobPointer`) and an alias template (`TCGrowingVector`).
    emit('template <typename t_CJob> struct TCJobTypes { using CJobPointer = TCSharedPointer<t_CJob>; };')
    emit('using CPointer = TCJobTypes<CJob>::CJobPointer;')
    emit('CPointer fg_MakeKeep();')
    emit('TCFuture<int> fg_alias_class_template() { CPointer Keep = fg_MakeKeep(); auto &Result = Keep->m_Result; return fg_CallSafe([&Result, Keep]() -> TCFuture<int> { co_await fg_Work(); co_return Result; }); }')
    emit('template <typename t_CData> using TCGrowingVector = TCVector<t_CData>;')
    emit('TCFuture<int> fg_alias_template(TCGrowingVector<int> Values) { int &Value = Values[0]; return fg_CallSafe([&Value, Keep = fg_Move(Values)]() -> TCFuture<int> { co_await fg_Work(); co_return Value; }); }')
    emit('TCFuture<int> fg_alias_template_none(TCGrowingVector<int> Values) { int &Value = Values[0]; return fg_CallSafe([&Value]() -> TCFuture<int> { co_await fg_Work(); co_return Value; }); }',
         CaptureCheck)
# capture-parameter: a closure that borrows what a reference parameter
# refers to, also a pointer parameter.
if Section == 'capture-parameter':
    Keepers = anchoredKeepers('CJob')
    Types = anchoredTypes(Keepers, 'Job')
    Root = ('TCFuture<int> fg_', ['CJob const &_Job'], '', '&Value = _Job.m_Result', 'Value')
    closureMatrix(Root, '', Keepers, Types, CaptureCheck)
    Pointer = ('TCFuture<int> fg_', ['CJob const *_pJob'], '', '&Value = _pJob->m_Result', 'Value')
    closureMatrix(Pointer, 'pointer_', Keepers, {Tag: {'itself': Types[Tag]['itself']} for Tag in Types},
                  CaptureCheck)
    closureControls(Root, '', 'TCSharedPointer<CJob>', CaptureCheck)


def objectClass(Name, Cases):
    # The non-actor class Name, with a member m_Result and the member
    # functions Cases writes, one a line.
    emit('struct %s : %sBase {' % (Name, Name))
    emit('  int m_Result = 0;')
    Cases()
    emit('};')
    emit('struct %sDerived : %s {};' % (Name, Name))


# capture-object: a closure in a member function that borrows a member of
# its object by reference.
if Section == 'capture-object':
    emitAnchor('CTask')
    Keepers = anchoredKeepers('CTask')
    Types = anchoredTypes(Keepers, 'Task')
    Root = ('  TCFuture<int> f_', [], '', '&Value = m_Result', 'Value')

    def captureObjectCases():
        closureMatrix(Root, '', Keepers, Types, CaptureCheck)
        closureControls(Root, '', 'TCSharedPointer<CTask>', CaptureCheck)

    objectClass('CTask', captureObjectCases)


def frameCases(Start, RootParams, Use, Prefix, Keepers, Types, Check):
    # Each keeper, holder and take of a coroutine whose frame borrows Use:
    # a parameter held by value, a local before the suspension, held whatever
    # initializes it, and a part of a result of a coroutine without one,
    # whose member is initialized by each take: the result made as a whole is
    # a new value. The keeper's own initializer, `_Keep`, isn't on the
    # derivation of what it keeps, so an owner of an unrelated type is still
    # reported.
    Params = RootParams + ', ' if RootParams else ''
    for Tag, Keeper, Kind, bFits in Keepers:
        for Holder in Types[Tag]:
            T = Types[Tag][Holder]
            Name = '%s%s_%s' % (Prefix, Tag, Holder)

            def Report(Take):
                return None if bFits and keeps(Kind, Take) else Check

            # Of the object, a keeper a call that isn't on it makes, a new
            # one, owns it only where the call states that: reported.
            def ReportNew(Take):
                return Check if ObjectLine else Report(Take)

            # Of the object, an owner parameter of the member function keeps it
            # where it states that it owns it (DMibKeepsThis); the same without
            # that is reported.
            Annotation = keepsThis(Kind, bFits)
            Forms = [('', Annotation, Report)]
            if Annotation and Holder == 'itself':
                Forms.append(('_untold', '', lambda Take: Check))
            for Suffix, Given, Reported in Forms:
                emit('TCFuture<int> %s%s_parameter%s(%s%s%s _Keep) { co_await fg_Work(); co_return %s; }' % (
                    Start, Name, Suffix, Params, Given, T, Use), Reported('held'))
            for Take, Init in [('copy', '_Keep'), ('move', 'fg_Move(_Keep)'), ('new', 'fg_Make<%s>()' % T)]:
                if Take == 'copy' and Kind == 'unique':
                    continue
                for Suffix, Given, Reported in Forms if Take != 'new' else [('', '', ReportNew)]:
                    Keep = '' if Take == 'new' else '%s%s &_Keep' % (Given, T)
                    emit('TCFuture<int> %s%s_local_%s%s(%s) { %s Kept = %s; co_await fg_Work(); co_return %s; }' % (
                        Start, Name, Take, Suffix, (Params + Keep).rstrip(', '), T, Init, Use), Reported('held'))
            # A local reference that extends the temporary it is bound to
            # holds it like a local held by value.
            emit('TCFuture<int> %s%s_local_extended(%s) { %s const &Kept = fg_Make<%s>(); co_await fg_Work(); co_return %s; }' % (
                Start, Name, Params.rstrip(', '), T, T, Use), ReportNew('held'))
            for Take, Init in [('copy', '_Keep'), ('move', 'fg_Move(_Keep)'), ('new', 'fg_Make<%s>()' % T)]:
                if Take == 'copy' and Kind == 'unique':
                    continue
                for Suffix, Given, Reported in Forms if Take != 'new' else [('', '', ReportNew)]:
                    Keep = '' if Take == 'new' else '%s%s &_Keep' % (Given, T)
                    emit('TCFuture<CResult%s%s> %s%s_result_%s%s(%s) { co_return CResult%s%s{%s, &%s}; }' % (
                        Tag, Holder.capitalize(), Start, Name, Take, Suffix, (Params + Keep).rstrip(', '), Tag,
                        Holder.capitalize(), Init, Use), Reported('new'))


def emitResults(Types):
    # The results that hold each holder of a keeper and a pointer to an int.
    for Tag in Types:
        for Holder, T in Types[Tag].items():
            emit('struct CResult%s%s { %s m_Keep; int const *m_pResult; };' % (Tag, Holder.capitalize(), T))


def frameControls(Start, RootParams, Use, Prefix, Keeper, Check):
    # Controls of a coroutine whose frame borrows Use, each reported: a
    # fitting keeper as a parameter by reference and through a pointer, and a
    # result that holds only a pointer to one, a static one, which is alive
    # itself. A local held by value counts where it is in scope at the use.
    Params = RootParams + ', ' if RootParams else ''
    emit('TCFuture<int> %s%sby_reference(%s%s const &_Keep) { co_await fg_Work(); co_return %s; }' % (
        Start, Prefix, Params, Keeper, Use), Check)
    emit('TCFuture<int> %s%sby_pointer(%s%s const *_pKeep) { co_await fg_Work(); co_return %s; }' % (
        Start, Prefix, Params, Keeper, Use), Check)
    emit('TCFuture<CResultPointer> %s%sresult_pointer(%s) { static %s s_pKeep; co_return CResultPointer{&s_pKeep, &%s}; }' % (
        Start, Prefix, RootParams, Keeper, Use), Check)
    # A keeper declared after the use, and one in a sibling scope that ends
    # before the suspension, keep nothing at the use.
    emit('TCFuture<int> %s%slocal_after(%s) { co_await fg_Work(); int Read = %s; %s Kept = fg_Make<%s>(); co_return Read; }' % (
        Start, Prefix, RootParams, Use, Keeper, Keeper), Check)
    emit('TCFuture<int> %s%slocal_sibling(%s) { { %s Kept = fg_Make<%s>(); } co_await fg_Work(); co_return %s; }' % (
        Start, Prefix, RootParams, Keeper, Keeper, Use), Check)
    # A local reference to a keeper that lives elsewhere is that keeper, not
    # a holding of its own; one that extends a temporary in a sibling scope
    # keeps nothing at the use.
    emit('TCFuture<int> %s%slocal_reference(%s%s const &_Keep) { %s const &Kept = _Keep; co_await fg_Work(); co_return %s; }' % (
        Start, Prefix, Params, Keeper, Keeper, Use), Check)
    emit('TCFuture<int> %s%slocal_extended_sibling(%s) { { %s const &Kept = fg_Make<%s>(); } co_await fg_Work(); co_return %s; }' % (
        Start, Prefix, RootParams, Keeper, Keeper, Use), Check)


# frame: a coroutine that borrows what a reference parameter refers to, also
# a pointer parameter, after a suspension or in its result.
ParameterCheck = 'malterlib-coroutine-reference-parameter'
if Section == 'frame':
    Keepers = anchoredKeepers('CJob')
    Types = anchoredTypes(Keepers, 'Job')
    emitResults(Types)
    emit('struct CResultPointer { TCSharedPointer<CJob> const *m_pKeep; int const *m_pResult; };')
    frameCases('fg_', 'CJob const &_Job', '_Job.m_Result', '', Keepers, Types, ParameterCheck)
    frameCases('fg_', 'CJob const *_pJob', '_pJob->m_Result', 'pointer_', Keepers,
               {Tag: {'itself': Types[Tag]['itself']} for Tag in Types}, ParameterCheck)
    frameControls('fg_', 'CJob const &_Job', '_Job.m_Result', '', 'TCSharedPointer<CJob>', ParameterCheck)


def frameForms(Check):
    # One row each, with a control that is reported, of a coroutine that uses
    # what a reference parameter refers to after a suspension, or makes its
    # result of it, kept by a `shared` keeper held as other holders and
    # declarations do, or by a result taken as each expression makes it.
    Keeper = 'TCSharedPointer<CJob>'
    New = 'fg_Make<%s>()' % Keeper
    Wait = 'co_await fg_Work(); co_return _Job.m_Result;'

    # A row reported once where nothing keeps `_Job`, and once more of each
    # reference parameter of Kept that a local refers to across the
    # suspension.
    def Row(Name, Params, Body, bKeeps, Result='int', Kept=0, bPending=False):
        emit('TCFuture<%s> fg_%s(%s) { %s }' % (Result, Name, ', '.join(['CJob const &_Job'] + Params), Body),
             ([] if bKeeps or bPending else [Check]) + [Check] * Kept, Check if bPending else None)

    # Holders: a virtual base, with a weak keeper as control; an array of
    # keepers, initialized by `{}` or in part, with weak ones as control; a
    # local closure that captures a keeper by copy, by reference as control.
    emit('struct CKeepField { TCSharedPointer<CJob> m_pKeep; }; struct CKeepVirtual : virtual CKeepField {};')
    emit('struct CWeakField { TCWeakPointer<CJob> m_pKeep; }; struct CWeakVirtual : virtual CWeakField {};')
    Row('holder_virtual_base', ['CKeepVirtual _Keep'], Wait, True)
    Row('holder_virtual_base_weak', ['CWeakVirtual _Keep'], Wait, False)
    Row('holder_array_empty', [], '%s pKeeps[2]{}; %s' % (Keeper, Wait), True)
    Row('holder_array_partial', [], '%s pKeeps[2]{%s}; %s' % (Keeper, New, Wait), True)
    Row('holder_array_weak', [], 'TCWeakPointer<CJob> pKeeps[2]{}; %s' % Wait, False)
    Row('holder_closure', ['%s const &_pKeep' % Keeper], 'auto fKeep = [_pKeep] {}; %s' % Wait, True)
    Row('holder_closure_by_reference', ['%s const &_pKeep' % Keeper], 'auto fKeep = [&_pKeep] {}; %s' % Wait, False,
        Kept=1)
    # Declarations: a by-value explicit object parameter, by reference as
    # control; a local declared under a `case` or `default` label or in an
    # enclosing scope, in scope at the use, one whose scope ends before it as
    # control; a structured binding by value, by reference as control.
    emit('struct CReader {')
    emit('  TCSharedPointer<CJob> m_pKeep;')
    emit('  TCFuture<int> f_self_by_value(this CReader _Self, CJob const &_Job) { %s }' % Wait)
    emit('  TCFuture<int> f_self_by_reference(this CReader const &_Self, CJob const &_Job) { %s }' % Wait, Check)
    emit('};')
    Row('scope_case', ['int _Which'], 'switch (_Which) { default: co_return 0; case 1: %s Kept = %s; %s }' % (
        Keeper, New, Wait), True)
    Row('scope_default', ['int _Which'], 'switch (_Which) { case 0: co_return 0; default: %s Kept = %s; %s }' % (
        Keeper, New, Wait), True)
    Row('scope_enclosing', ['bool _bWait'], '%s Kept = %s; if (_bWait) { %s } co_return 0;' % (Keeper, New, Wait),
        True)
    # A keeper under a `default` label of a switch that ends before the use,
    # and in a nested scope that ends before it, keep nothing at the use.
    Row('scope_case_after', ['int _Which'], 'switch (_Which) { default: %s Kept = %s; } %s' % (Keeper, New, Wait),
        False)
    Row('scope_nested', ['bool _bWait'], 'if (_bWait) { %s Kept = %s; } %s' % (Keeper, New, Wait), False)
    emit('template <typename t_CFirst, typename t_CSecond> struct TCPair { t_CFirst m_First; t_CSecond m_Second; };')
    emit('template <typename t_CFirst, typename t_CSecond> TCPair<t_CFirst, t_CSecond> fg_Pair(t_CFirst _First, t_CSecond _Second);')
    Row('binding_by_value', [], 'auto [pKeep, iValue] = fg_Pair(%s, 0); %s' % (New, Wait), True)
    # The binding by reference refers to `_Pair` across the suspension but
    # does nothing with it, like a pointer to it that is never used.
    Row('binding_by_reference', ['TCPair<%s, int> &_Pair' % Keeper], 'auto &[pKeep, iValue] = _Pair; %s' % Wait,
        False)
    # Takes at a result: a result made as a whole is a new value whatever its
    # members are initialized by. The name of a local result, its `fg_Move`
    # and the name of a reference to it are of a record held by value, which
    # is a value (Rule V): what its members refer to isn't followed, whatever
    # the take. A `buffer` keeper fits a borrow of what a parameter owner
    # owns, reported without a keeper as control.
    emit('struct CResultUnique { TCUniquePointer<CJob> m_pKeep; int const *m_pResult; };')
    emit('struct CResultBuffer { TCVector<int> m_Keep; int *m_pResult; };')
    emit('struct CResultNone { int *m_pResult; };')
    Made = 'CResultUnique Result{fg_Move(_pKeep), &_Job.m_Result};'
    Row('take_unique_named', ['TCUniquePointer<CJob> &_pKeep'], '%s co_return Result;' % Made, True, 'CResultUnique')
    Row('take_unique_moved', ['TCUniquePointer<CJob> &_pKeep'], '%s co_return fg_Move(Result);' % Made, True,
        'CResultUnique')
    Made = 'CResultBuffer Result{fg_Move(_Keep), &_Values[0]};'
    # Reported: the result through a reference to it, whose `buffer` keeper
    # is a copy of another buffer than the one the pointer is into.
    for Name, Body, bKeeps, bPending in [
        ('take_buffer_named', '%s co_return Result;' % Made, True, False),
        ('take_buffer_moved', '%s co_return fg_Move(Result);' % Made, True, False),
        ('take_buffer_copy', '%s auto &Kept = Result; co_return Kept;' % Made, False, False),
        ('take_buffer_whole', 'co_return CResultBuffer{_Keep, &_Values[0]};', True, False),
    ]:
        emit('TCFuture<CResultBuffer> fg_%s(TCVector<int> &_Values, TCVector<int> &_Keep) { %s }' % (Name, Body),
             None if bKeeps else Check, Check if bPending else None)
    emit('TCFuture<CResultNone> fg_take_buffer_none(TCVector<int> &_Values) { co_return CResultNone{&_Values[0]}; }',
         Check)
    # A `co_return` of another type than the result's makes the result, a new
    # value whatever the operand's category: its `unique` and `buffer`
    # keepers keep, also of an lvalue through a local reference, reported
    # without a keeper as control.
    emit('struct CResultUniqueFrom { TCUniquePointer<CJob> m_pKeep; int const *m_pResult; CResultUniqueFrom(CJob const &_Job); CResultUniqueFrom(int const *_pResult); };')
    emit('struct CResultBufferFrom { TCVector<int> m_Keep; int *m_pResult; CResultBufferFrom(int *_pResult); };')
    emit('struct CResultNoneFrom { int const *m_pResult; CResultNoneFrom(CJob const &_Job); CResultNoneFrom(int *_pResult); };')
    emit('TCFuture<CResultUniqueFrom> fg_take_unique_convert(CJob const &_Job) { co_return _Job; }')
    emit('TCFuture<CResultUniqueFrom> fg_take_unique_convert_reference(CJob const &_Job) { int const *pResult = &_Job.m_Result; int const *&rResult = pResult; co_return rResult; }')
    emit('TCFuture<CResultNoneFrom> fg_take_unique_convert_none(CJob const &_Job) { co_return _Job; }', Check)
    emit('TCFuture<CResultBufferFrom> fg_take_buffer_convert(TCVector<int> &_Values) { int *pResult = &_Values[0]; int *&rResult = pResult; co_return rResult; }')
    emit('TCFuture<CResultNoneFrom> fg_take_buffer_convert_none(TCVector<int> &_Values) { int *pResult = &_Values[0]; int *&rResult = pResult; co_return rResult; }',
         Check)
    # Contexts nest through lambdas that aren't coroutines, called where they
    # are made or later through a local: the frame's keeper keeps what they
    # use, reported without one as control. A coroutine lambda has only what
    # it holds itself, reported with the frame's keeper.
    Read = '[&_Job] { return _Job.m_Result; }'
    Row('nested_immediate', ['%s _pKeep' % Keeper], 'co_await fg_Work(); co_return %s();' % Read, True)
    Row('nested_stored', ['%s _pKeep' % Keeper], 'auto fRead = %s; co_await fg_Work(); co_return fRead();' % Read,
        True)
    Row('nested_stored_none', [], 'auto fRead = %s; co_await fg_Work(); co_return fRead();' % Read, False)
    Row('nested_coroutine', ['%s _pKeep' % Keeper],
        'co_return co_await fg_CallSafe([&_Job]() -> TCFuture<int> { co_await fg_Work(); co_return _Job.m_Result; });',
        False)
    # A generic lambda holds the locals of the specialization that runs, of a
    # type the pattern can't tell, reported without one as control.
    emit('template <typename t_CType> TCSharedPointer<t_CType> fg_Share(t_CType const &_Value);')
    emit('TCFuture<int> fg_generic_local(CJob const &_Job) { auto fRead = [](auto const &_Job) -> TCFuture<int> { auto pKeep = fg_Share(_Job); %s }; return fRead(_Job); }' % Wait)
    emit('TCFuture<int> fg_generic_none(CJob const &_Job) { auto fRead = [](auto const &_Job) -> TCFuture<int> { %s }; return fRead(_Job); }' % Wait,
         Check)
    # An entry names a type by its own name: a type registered so is an
    # owner however it is written, through an alias, also as the element of
    # a container, an alias in a class template and an alias template. An
    # entry that names an alias (`CAliasEntryPointer=shared`) registers
    # nothing, reported as control.
    emit('using CJobPointer = TCSharedPointer<CJob>;')
    Row('alias_owner', ['CJobPointer _pKeep'], Wait, True)
    Row('alias_container', ['TCVector<CJobPointer> _Keeps'], Wait, True)
    emit('template <typename t_CJob> struct TCContext { using CJobPointer = TCSharedPointer<t_CJob>; };')
    emit('template <typename t_CType> using TCJobPointer = TCSharedPointer<t_CType>;')
    Row('alias_class_template', ['TCContext<CJob>::CJobPointer _pKeep'], Wait, True)
    Row('alias_template', ['TCJobPointer<CJob> _pKeep'], Wait, True)
    emit('template <typename t_CType> struct TCPlainPointer { t_CType *operator->() const; t_CType &operator*() const; };')
    emit('using CAliasEntryPointer = TCPlainPointer<CJob>;')
    Row('alias_entry', ['CAliasEntryPointer _pKeep'], Wait, False)
    # A type no entry registers, written itself and through an alias
    # template, keeps nothing: reported as control.
    emit('template <typename t_CType> struct TCOtherPointer { t_CType *operator->() const; t_CType &operator*() const; };')
    emit('template <typename t_CType> using TCOtherAlias = TCOtherPointer<t_CType>;')
    Row('alias_unregistered', ['TCOtherPointer<CJob> _pKeep'], Wait, False)
    Row('alias_template_unregistered', ['TCOtherAlias<CJob> _pKeep'], Wait, False)
    # A buffer borrowed through a reference parameter is anchored by the
    # owner's type, which a `buffer` or `sharedbuffer` keeper of that type
    # fits; reported without it as control.
    for Name, Params, Keep, Body in [
            ('buffer_parameter_shared', 'CStr const &_Text', 'CStr Keep = _Text; ', 'auto pText = _Text.f_GetStr(); co_await fg_Work(); co_return *pText;'),
            ('buffer_parameter', 'TCVector<int> &_Values', 'TCVector<int> Keep = _Values; ', 'int *pValue = &_Values[0]; co_await fg_Work(); co_return *pValue;')]:
        emit('TCFuture<int> fg_%s(%s) { %s%s }' % (Name, Params, Keep, Body))
        emit('TCFuture<int> fg_%s_none(%s) { %s }' % (Name, Params, Body), Check)
    # A record held by value carries what is put in each member: a result
    # that names a local record, also through a reference to it, and one
    # made in the `co_return`, of what the parameter refers to, are
    # reported.
    emit('struct CReadContext { int const *m_pValue; };')
    emit('TCFuture<CReadContext> fg_record_result_named(int const &_Value) { CReadContext Context{&_Value}; co_return Context; }', Check)
    emit('TCFuture<CReadContext> fg_record_result_reference(int const &_Value) { CReadContext Context{&_Value}; auto &Alias = Context; co_return Alias; }', Check)
    emit('TCFuture<CReadContext> fg_record_result_made(int const &_Value) { co_return CReadContext{&_Value}; }', Check)
    # A pointer member read through a pointer to the local record is a
    # value, a known miss; a reference member of a local record, also as a
    # getter returns it, is what it is bound to.
    emit('struct CReadReference { int const &m_Value; int const &f_Value() const { return m_Value; } };')
    emit('TCFuture<int const *> fg_record_member_through_pointer(int const &_Value) { CReadContext Context{&_Value}; auto *pContext = &Context; co_return pContext->m_pValue; }')
    emit('TCFuture<int const *> fg_record_reference_member(int const &_Value) { CReadReference Context{_Value}; co_return &Context.m_Value; }', Check)
    emit('TCFuture<int const *> fg_record_reference_getter(int const &_Value) { CReadReference Context{_Value}; co_return &Context.f_Value(); }', Check)
    # A value kept in a capture is alive by what its closure holds, a copy of
    # the owner; reported without it as control.
    emit('TCFuture<int> fg_capture_destination(TCSharedPointer<CJob> const &_pJob) { co_return co_await fg_CallSafe([pValue = &_pJob->m_Result, pKeep = _pJob]() -> TCFuture<int> { co_await fg_Work(); co_return *pValue; }); }')
    emit('TCFuture<int> fg_capture_destination_none(TCSharedPointer<CJob> const &_pJob) { co_return co_await fg_CallSafe([pValue = &_pJob->m_Result]() -> TCFuture<int> { co_await fg_Work(); co_return *pValue; }); }', Check)
    # A result is judged by the type the promise stores, the `T` of its
    # `TCFuture<T>`: a string made of a view keeps nothing of it, the view
    # itself as control.
    emit('struct CStrSpan { char const *m_pData; int m_Length; operator TCStr<CStrTraits_CStr>() const; };')
    emit('TCFuture<TCStr<CStrTraits_CStr>> fg_result_type_string(CStrSpan _Text) { co_return _Text; }')
    emit('TCFuture<CStrSpan> fg_result_type_view(CStrSpan _Text) { co_return _Text; }', Check)
    # The anchors of a borrow are every object type on its derivation, past
    # explicit casts and getters of an explicit object, reported without the
    # keeper as control.
    emit('struct CJobContext { TCSharedPointer<CJob> m_pJob; TCSharedPointer<CJob> const &f_Job(this CJobContext const &_This) { return _This.m_pJob; } };')
    emit('struct CJobContextDerived : CJobContext {};')
    Cast = 'auto *pValue = &static_cast<%s const &>(_pJob)->m_Result; co_await fg_Work(); co_return *pValue;' % Keeper
    Base = 'auto *pValue = &static_cast<CJobContext const &>(_Context).m_pJob->m_Result; co_await fg_Work(); co_return *pValue;'
    Getter = 'auto *pValue = &_Context.f_Job()->m_Result; co_await fg_Work(); co_return *pValue;'
    for Name, Params, Body in [('derivation_cast', '%s &_pJob' % Keeper, Cast),
                               ('derivation_base_cast', 'CJobContextDerived const &_Context', Base),
                               ('derivation_getter', 'CJobContext const &_Context', Getter)]:
        emit('TCFuture<int> fg_%s(%s, %s _pKeep) { %s }' % (Name, Params, Keeper, Body))
        emit('TCFuture<int> fg_%s_none(%s) { %s }' % (Name, Params, Body), Check)


if Section == 'frame':
    frameForms(ParameterCheck)
# this-frame: a member coroutine of a non-actor class that uses its object
# after a suspension or in its result.
ThisCheck = 'malterlib-coroutine-unsafe-this'
if Section == 'this-frame':
    emitAnchor('CTask')
    Keepers = anchoredKeepers('CTask')
    Types = anchoredTypes(Keepers, 'Task')
    emitResults(Types)
    emit('struct CResultPointer { TCSharedPointer<CTask> const *m_pKeep; int const *m_pResult; };')

    def thisFrameCases():
        frameCases('f_', '', 'm_Result', '', Keepers, Types, ThisCheck)
        frameControls('f_', '', 'm_Result', '', 'TCSharedPointer<CTask>', ThisCheck)

    # The member functions are indented under the class.
    Start = len(Lines)
    objectClass('CTask', thisFrameCases)
    for Index in range(Start + 2, len(Lines) - 2):
        Lines[Index] = '  ' + Lines[Index]
# this-closure: a closure in a member function of a non-actor class that
# captures `this` and uses its object after a suspension.
if Section == 'this-closure':
    emitAnchor('CTask')
    Keepers = anchoredKeepers('CTask')
    Types = anchoredTypes(Keepers, 'Task')
    Root = ('  TCFuture<int> f_', [], '', 'this', 'm_Result')
    emit('struct CTaskRecord { int const *m_pResult; int m_Count; };')
    emit('struct CTaskKnownRecord { DMibSetByOwnClass int const *m_pResult; int m_Count; };')
    emit('int g_TaskResult = 0;')

    def thisClosureCases():
        closureMatrix(Root, '', Keepers, Types, ThisCheck)
        closureControls(Root, '', 'TCSharedPointer<CTask>', ThisCheck)
        # A record held by value is a value of its own (Rule V): a copy of the
        # object or of a member record with a pointer into the object,
        # captured under each consumer, is reported where its pointer is read,
        # clean where only its own part is; the pointer captured itself is
        # reported as control. One whose pointer member states that only its
        # class points it (DMibSetByOwnClass), where its initializer in the
        # object's class points it at static storage, is clean.
        emit('  CTaskRecord m_Record{&m_Result, 1};')
        emit('  CTaskKnownRecord m_KnownRecord{&g_TaskResult, 1};')
        Consumers = [('dispatch', 'return g_Dispatch / %s;'), ('callsafe', 'return fg_CallSafe(%s);'),
                     ('self', 'auto fRun = %s; return fRun();')]
        for ConsumerName, Consumer in Consumers:
            for Name, Alias, Use, bReported in [('object', '*this', '*State.m_Record.m_pResult', True),
                                                ('member', 'm_Record', '*State.m_pResult', True),
                                                ('member_count', 'm_Record', 'State.m_Count', False),
                                                ('known', 'm_KnownRecord', '*State.m_pResult', False)]:
                Closure = '[State]() -> TCFuture<int> { co_await fg_Work(); co_return %s; }' % Use
                emit('  TCFuture<int> f_record_%s_%s() { auto const &State = %s; %s }' % (
                    ConsumerName, Name, Alias, Consumer % Closure), ThisCheck if bReported else None)
            Closure = '[pResult = &m_Result]() -> TCFuture<int> { co_await fg_Work(); co_return *pResult; }'
            emit('  TCFuture<int> f_record_%s_pointer() { %s }' % (ConsumerName, Consumer % Closure), ThisCheck)

    objectClass('CTask', thisClosureCases)
# runs: an argument of a call that runs to completion, joined where the
# coroutine finishes, after its locals are gone, that borrows what a local
# owner owns, with a keeper made of that owner as another argument held by
# value.
RunsCheck = 'malterlib-runs-to-completion'
if Section == 'runs':
    emit('#include "malterlib-runs-to-completion.h"')
    Join = 'TCFutureVector<int> Results; auto Join = co_await fg_AsyncDestroy([&]() -> TCFuture<int> { co_await fg_AllDone(Results); co_return 0; });'
    for Kind, (Owner, Made, Borrow, Keeper) in Kinds.items():
        K = Kind.capitalize()
        Types = emitHolders(K, Keeper)
        News = madeOfOwner(Kind, K, Keeper)
        Pointee = 'char const' if Kind == 'sharedbuffer' else 'int const'
        for Holder in Holders:
            Type, New = Types[Holder], News[Holder]
            emit('DMibRunsToCompletion TCFuture<int> fg_Read%s%s(%s _Keep, %s *_pResult);' % (K, Holder.capitalize(), Type, Pointee))
            for Take in Takes:
                if Take == 'copy' and Kind == 'unique':
                    continue
                if Take == 'new' and Holder == 'container':
                    continue
                Params = '%s Keep' % Type if Holder == 'container' else ''
                Keep = '' if Take == 'new' or Holder == 'container' else '%s Keep = %s; ' % (Type, New)
                Init = New if Take == 'new' else 'Keep' if Take == 'copy' else 'fg_Move(Keep)'
                # The container, a parameter of the caller, is there until the
                # join itself, a keeper of other objects of a fitting type: of
                # a `unique` or `buffer` owner made here, and of the new job
                # `fg_MakeJob` states it gives back (DMibReturnsNew), which no
                # parameter of the function can own, reported (Rule K's
                # identity); of the others clean unless it is of weak owners.
                emit('DMibRunsToCompletion TCFuture<int> fg_%s_%s_%s(%s) { %s %s pOwner = %s; auto &Result = %s; %sfg_Read%s%s(%s, &Result) > Results; co_return 0; }' % (
                    Kind, Holder, Take, Params, Join, Owner, Made, Borrow, Keep, K, Holder.capitalize(), Init),
                    RunsCheck if Holder == 'container' and Kind in ('unique', 'buffer', 'weak', 'shared') else
                    None if keeps(Kind, Take) or Holder == 'container' else RunsCheck)
    # The keeper and the borrow in one argument.
    emit('struct CArguments { TCSharedPointer<CJob> m_pKeep; int const *m_pResult; };')
    emit('DMibRunsToCompletion TCFuture<int> fg_ReadArguments(CArguments _Arguments);')
    emit('DMibRunsToCompletion TCFuture<int> fg_shared_arguments_new() { %s TCSharedPointer<CJob> pOwner = fg_MakeJob(); auto &Result = pOwner->m_Result; fg_ReadArguments(CArguments{fg_Move(pOwner), &Result}) > Results; co_return 0; }' % Join)
    # Controls, reported: a keeper of an unrelated class, and one given by
    # reference and through a pointer, a reference parameter, which the
    # callers keep while they join.
    emit('DMibRunsToCompletion TCFuture<int> fg_ReadOther(TCSharedPointer<COther> _Keep, int const *_pResult);')
    emit('DMibRunsToCompletion TCFuture<int> fg_ReadByReference(TCSharedPointer<CJob> const &_Keep, int const *_pResult);')
    emit('DMibRunsToCompletion TCFuture<int> fg_ReadByPointer(TCSharedPointer<CJob> const *_pKeep, int const *_pResult);')
    emit('DMibRunsToCompletion TCFuture<int> fg_shared_other() { %s TCSharedPointer<CJob> pOwner = fg_MakeJob(); auto &Result = pOwner->m_Result; fg_ReadOther(fg_Make<TCSharedPointer<COther>>(), &Result) > Results; co_return 0; }' % Join,
         RunsCheck)
    for Name, Init in [('ByReference', '_pKeep'), ('ByPointer', '&_pKeep')]:
        emit('DMibRunsToCompletion TCFuture<int> fg_shared_%s(TCSharedPointer<CJob> const &_pKeep) { %s TCSharedPointer<CJob> pOwner = _pKeep; auto &Result = pOwner->m_Result; fg_Read%s(%s, &Result) > Results; co_return 0; }' % (
            Name, Join, Name, Init), RunsCheck)
    # A record held by value carries what is put in each member: a local
    # record given by value, and one made in the argument, of a local of a
    # block the join is outside of, are reported.
    emit('struct CRequest { int const *m_pValue; };')
    emit('DMibRunsToCompletion TCFuture<int> fg_ReadRequest(CRequest _Request);')
    emit('DMibRunsToCompletion TCFuture<int> fg_record_named() { %s { int Value = 42; CRequest Request{&Value}; fg_ReadRequest(Request) > Results; } co_return 0; }' % Join,
         RunsCheck)
    emit('DMibRunsToCompletion TCFuture<int> fg_record_made() { %s { int Value = 42; fg_ReadRequest(CRequest{&Value}) > Results; } co_return 0; }' % Join,
         RunsCheck)
    # A pointer member read through a pointer to the local record, whose
    # target can't be told (DMibSetByOwnClass), is reported.
    emit('DMibRunsToCompletion TCFuture<int> fg_ReadRequestPointer(int const *_pValue);')
    emit('DMibRunsToCompletion TCFuture<int> fg_record_through_pointer() { %s { int Value = 42; CRequest Request{&Value}; auto *pRequest = &Request; fg_ReadRequestPointer(pRequest->m_pValue) > Results; } co_return 0; }' % Join,
         RunsCheck)
# retention, Rule R: a reference parameter of a coroutine given, before the
# first suspension, to what can keep a reference to it, by type, or made
# into its result. Kept only by what can hold a borrow by type: the result of
# a function stored or returned, a class constructed, a receiver, another
# argument by non-const reference, a visible body that stores it; what is
# copied when given by reference, a shared, a sharedbuffer, a weak owner and
# an actor handle, only by a visible body that stores its address, not of a
# handle, which is copied wherever it is given; never a future, nor what is
# given as an xvalue or a prvalue, which is taken. Where what keeps it is a record held by value in
# the frame, a named result, object made, receiver or argument, the record
# uses it where it is used or destroyed after the suspension: one trivially
# destroyed and not touched after it is clean, the same one read after it is
# reported. An unseen function that returns nothing keeps nothing, a known
# miss.
if Section == 'retention':
    emit('struct CSomeActor : CActor {};')
    emit('struct CRefSink { int f_Count() const; void *m_p; };')
    emit('struct CNoRefSink { int m_Value; };')
    Given = [
        # Tag, type, and whether a callee copies it.
        ('Handle', 'TCActor<CSomeActor>', True),
        ('Shared', 'TCSharedPointer<CJob>', True),
        ('SharedBuffer', 'CStr', True),
        ('Weak', 'TCWeakPointer<CJob>', True),
        ('Unique', 'TCUniquePointer<CJob>', False),
        ('Buffer', 'TCVector<int>', False),
        ('Object', 'CJob', False),
        ('Scalar', 'int', False),
    ]
    for Tag, T, bCopied in Given:
        emit('void fg_Take%s(%s const &_Value);' % (Tag, T))
        emit('struct CRefHolder%s { %s const *m_p; };' % (Tag, T))
        emit('CRefHolder%s fg_Hold%s(%s const &_Value);' % (Tag, Tag, T))
        emit('int fg_Count%s(%s const &_Value);' % (Tag, T))
        emit('TCFuture<int> fg_Start%s(%s const &_Value);' % (Tag, T))
        emit('struct CKeepsRef%s { CKeepsRef%s(%s const &_Value); ~CKeepsRef%s(); void *m_p; };' % (Tag, Tag, T, Tag))
        emit('struct CNoRef%s { CNoRef%s(%s const &_Value); ~CNoRef%s(); int m_Value; };' % (Tag, Tag, T, Tag))
        emit('struct CReceiverRef%s { void f_Take(%s const &_Value); int f_Count() const; void *m_p; };' % (Tag, T))
        emit('struct CReceiverNoRef%s { void f_Take(%s const &_Value); int m_Value; };' % (Tag, T))
        emit('void fg_Store%s(%s const &_Value, CRefSink &_Sink);' % (Tag, T))
        emit('void fg_StoreNoRef%s(%s const &_Value, CNoRefSink &_Sink);' % (Tag, T))
        emit('inline void fg_Register%s(%s const &_Value) { static %s const *s_pValue; s_pValue = &_Value; }' % (Tag, T, T))
        emit('inline void fg_Peek%s(%s const &_Value) { %s const *pValue = &_Value; (void)pValue; }' % (Tag, T, T))
        Before = [
            # Case, what is given before the suspension, and whether it keeps.
            ('take', 'fg_Take%s(_Value);' % Tag, False),
            ('hold_stored', 'auto Held = fg_Hold%s(_Value);' % Tag, True),
            ('hold_discarded', 'fg_Hold%s(_Value);' % Tag, False),
            ('count_stored', 'int Count = fg_Count%s(_Value);' % Tag, False),
            ('count_discarded', 'fg_Count%s(_Value);' % Tag, False),
            ('future_stored', 'auto Started = fg_Start%s(_Value);' % Tag, False),
            ('constructor_ref', 'CKeepsRef%s Keeps(_Value);' % Tag, True),
            ('constructor_noref', 'CNoRef%s Keeps(_Value);' % Tag, False),
            ('receiver_ref', 'CReceiverRef%s Receiver; Receiver.f_Take(_Value);' % Tag, True),
            ('receiver_noref', 'CReceiverNoRef%s Receiver; Receiver.f_Take(_Value);' % Tag, False),
            ('sink_ref', 'CRefSink Sink; fg_Store%s(_Value, Sink);' % Tag, True),
            ('sink_noref', 'CNoRefSink Sink; fg_StoreNoRef%s(_Value, Sink);' % Tag, False),
            ('visible_stores', 'fg_Register%s(_Value);' % Tag, True),
            ('visible_reads', 'fg_Peek%s(_Value);' % Tag, False),
        ]
        # What is given as an xvalue or a prvalue (a copy) is taken, never
        # referred to, by each form that keeps a reference to an lvalue.
        Categories = [('xvalue', 'static_cast<%s const &&>(_Value)' % T)]
        if Tag != 'Unique':
            Categories.append(('prvalue', '%s(_Value)' % T))
        for Case, Give, bKeeps in list(Before):
            if bKeeps:
                for Category, Value in Categories:
                    Before.append(('%s_%s' % (Case, Category), Give.replace('(_Value', '(' + Value), False))
        # Kept in a record held by value in the frame, which uses it only
        # where it is used or destroyed after the suspension: one whose
        # destructor can't be seen is reported; one trivially destroyed and
        # never touched after the suspension uses nothing of it, clean; the
        # same record read after the suspension, its kept member dereferenced
        # or a member function called on it, is reported.
        HeldByValue = {
            # Case, and the use of the record after the suspension.
            'hold_stored': 'fg_Count%s(*Held.m_p)' % Tag,
            'receiver_ref': 'Receiver.f_Count()',
            'sink_ref': 'Sink.f_Count()',
        }
        for Case, Give, bKeeps in Before:
            if bKeeps and not bCopied and Case in HeldByValue:
                # Clean: the record is trivially destroyed and isn't touched after the suspension, so the borrow it
                # holds is never used.
                emit('TCFuture<int> fg_%s_%s(%s const &_Value) { %s co_await fg_Work(); co_return 0; }' % (Tag, Case, T, Give))
                emit('TCFuture<int> fg_%s_%s_used(%s const &_Value) { %s co_await fg_Work(); co_return %s; }' % (
                    Tag, Case, T, Give, HeldByValue[Case]), ParameterCheck)
                continue
            bStores = Case == 'visible_stores' and Tag != 'Handle'
            emit('TCFuture<int> fg_%s_%s(%s const &_Value) { %s co_await fg_Work(); co_return 0; }' % (Tag, Case, T, Give),
                 ParameterCheck if bKeeps and (not bCopied or bStores) else None)
        Results = [
            # Case, the result type, what is returned, and whether it keeps.
            ('hold', 'CRefHolder%s' % Tag, 'fg_Hold%s(_Value)' % Tag, True),
            ('count', 'int', 'fg_Count%s(_Value)' % Tag, False),
            ('constructor_ref', 'CKeepsRef%s' % Tag, 'CKeepsRef%s(_Value)' % Tag, True),
            ('constructor_noref', 'CNoRef%s' % Tag, 'CNoRef%s(_Value)' % Tag, False),
            # The conversion `co_return` makes into another type, by type: a
            # result that can hold a borrow keeps an lvalue, not an xvalue; one
            # that can't keeps nothing.
            ('convert_lvalue', 'CKeepsRef%s' % Tag, '_Value', True),
            ('convert_xvalue', 'CKeepsRef%s' % Tag, 'static_cast<%s const &&>(_Value)' % T, False),
            ('convert_noref', 'CNoRef%s' % Tag, '_Value', False),
        ]
        for Case, Result, Made, bKeeps in Results:
            emit('TCFuture<%s> fg_%s_result_%s(%s const &_Value) { co_return %s; }' % (Result, Tag, Case, T, Made),
                 ParameterCheck if bKeeps and not bCopied else None)
    # An object given by reference to a callee without a body of each form,
    # decided by its type: a function pointer, a member function pointer, a
    # function object, a member function, and a function with another
    # argument. A receiver keeps it only through a member function that isn't
    # const, another argument only when given by reference or pointer to
    # non-const, and a result whatever the const-ness of the call.
    emit('struct CReceiverConst { void f_Take(CJob const &_Value) const; void *m_p; };')
    emit('struct CFunctorConst { void operator()(CJob const &_Value) const; void *m_p; };')
    emit('struct CFunctorMutable { void operator()(CJob const &_Value); int f_Count() const; void *m_p; };')
    emit('struct CFunctorHold { CRefHolderObject operator()(CJob const &_Value) const; };')
    emit('void fg_StoreConstReference(CJob const &_Value, CRefSink const &_Sink);')
    emit('void fg_StorePointer(CJob const &_Value, CRefSink *_pSink);')
    emit('void fg_StoreConstPointer(CJob const &_Value, CRefSink const *_pSink);')
    Forms = [
        # Case, the callee parameter, what is given before the suspension,
        # whether it keeps, and the use of what keeps it after the suspension.
        ('function_pointer_hold', 'CRefHolderObject (*_pHold)(CJob const &)', 'auto Held = _pHold(_Value);', True),
        ('function_pointer_count', 'int (*_pCount)(CJob const &)', 'int Count = _pCount(_Value);', False),
        ('function_pointer_sink', 'void (*_pStore)(CJob const &, CRefSink &)', 'CRefSink Sink; _pStore(_Value, Sink);',
         True),
        ('function_pointer_const_sink', 'void (*_pStore)(CJob const &, CRefSink const &)',
         'CRefSink Sink; _pStore(_Value, Sink);', False),
        ('member_pointer', 'void (CReceiverRefObject::*_pTake)(CJob const &)',
         'CReceiverRefObject Receiver; (Receiver.*_pTake)(_Value);', True),
        ('member_pointer_const', 'void (CReceiverRefObject::*_pTake)(CJob const &) const',
         'CReceiverRefObject Receiver; (Receiver.*_pTake)(_Value);', False),
        ('receiver_const', None, 'CReceiverConst Receiver; Receiver.f_Take(_Value);', False),
        ('functor_mutable', None, 'CFunctorMutable fTake; fTake(_Value);', True),
        ('functor_const', None, 'CFunctorConst fTake; fTake(_Value);', False),
        ('functor_const_hold', None, 'CFunctorHold fHold; auto Held = fHold(_Value);', True),
        ('sink_const_reference', None, 'CRefSink Sink; fg_StoreConstReference(_Value, Sink);', False),
        ('sink_pointer', None, 'CRefSink Sink; fg_StorePointer(_Value, &Sink);', True),
        ('sink_const_pointer', None, 'CRefSink Sink; fg_StoreConstPointer(_Value, &Sink);', False),
    ]
    # Each keeps it in a record held by value in the frame, trivially
    # destroyed: clean where the record isn't touched after the suspension,
    # reported where the form keeps it and the record is read after it.
    FormUses = {
        'function_pointer_hold': 'fg_CountObject(*Held.m_p)',
        'function_pointer_sink': 'Sink.f_Count()',
        'member_pointer': 'Receiver.f_Count()',
        'functor_mutable': 'fTake.f_Count()',
        'functor_const_hold': 'fg_CountObject(*Held.m_p)',
        'sink_pointer': 'Sink.f_Count()',
    }
    for Case, Callee, Give, bKeeps in Forms:
        Parameters = ', '.join(['CJob const &_Value'] + ([Callee] if Callee else []))
        if bKeeps:
            # Clean: the record is trivially destroyed and isn't touched after the suspension, so the borrow it holds
            # is never used.
            emit('TCFuture<int> fg_Object_%s(%s) { %s co_await fg_Work(); co_return 0; }' % (Case, Parameters, Give))
            emit('TCFuture<int> fg_Object_%s_used(%s) { %s co_await fg_Work(); co_return %s; }' % (
                Case, Parameters, Give, FormUses[Case]), ParameterCheck)
            continue
        emit('TCFuture<int> fg_Object_%s(%s) { %s co_await fg_Work(); co_return 0; }' % (Case, Parameters, Give))
# getters: what a call gives relative to its object, by the returned roots of
# its body: (a) into the object's own storage, (b) what an owner member of it
# owns, (c) a pointer member stored in it, also of a non-owning wrapper like
# `TCPointer`, where its class gives it, as the member states that only its
# class points it (DMibSetByOwnClass), which it never does here; without
# that, what it points to can't be told, reported where it is read (the
# untold rows), (d) the object itself, (e)
# what has static storage, and a body that can't be seen, which can't be
# told. A pointer made with it before a suspension and read after, of a
# reference parameter (reference-parameter), of the object of a member
# coroutine (unsafe-this), of a local captured by copy (reference-capture),
# a reference init capture bound to it (reference-capture), and a pointer a
# cleanup reads retargeted at it (async-destroy): (a), (b) and (d) are
# reported, (c) and (e) are clean; one that can't be told is reported where
# its root is (reference-parameter, unsafe-this, the init capture), not by
# the walks of a copied pointer and a cleanup's target, which follow only
# what is told. Then the
# same kinds relative to an argument, given by pointer and by reference, to a
# free function and to a member function of another object, a pointer made
# with it read after a suspension (reference-parameter): (a), (b), (d) and
# one that can't be told are reported, (c) and (e) are clean. Then the
# views: a
# by-value view parameter whose getter gives a reference into its own
# storage is clean, what it points to isn't; a view captured by copy through
# a reference declaration holds a view, whose length is clean. Then a
# reference init capture bound to a call is used like a local reference bound
# to it: clean before the first suspension, reported after.
if Section == 'getters':
    Parameter, Capture = 'malterlib-coroutine-reference-parameter', 'malterlib-coroutine-reference-capture'
    This, Destroy = 'malterlib-coroutine-unsafe-this', 'malterlib-async-destroy'
    emit('template <typename t_CType> struct TCPointer { DMibSetByOwnClass t_CType *mp_pPointTo = nullptr; t_CType *f_Get() const { return mp_pPointTo; } };')
    emit('template <typename t_CType> struct TCRawPointer { t_CType *mp_pPointTo = nullptr; t_CType *f_Get() const { return mp_pPointTo; } };')
    emit('struct CStatic { CJob *f_Job() const { static CJob s_Job; return &s_Job; } };')
    # Where what an untold pointer member points to is read: by the object, a
    # reference parameter and a reference init capture, not by a copied
    # pointer nor a cleanup's target, which follow only what is told.
    Untold = {'read', 'parameter', 'init_capture'}
    Kinds = [
        # Kind, class members, the getter as a pointer, reported (or where).
        ('own', 'CJob m_Job;', 'CJob const *f_Job() const { return &m_Job; }', 'X.f_Job()', True),
        ('own_reference', 'CJob m_Job;', 'CJob const &f_Job() const { return m_Job; }', '&X.f_Job()', True),
        ('owned', 'TCUniquePointer<CJob> m_pJob = TCUniquePointer<CJob>(new CJob);', 'CJob *f_Job() const { return m_pJob.operator->(); }', 'X.f_Job()', True),
        ('stored', 'DMibSetByOwnClass CJob *m_pJob = nullptr;', 'CJob *f_Job() const { return m_pJob; }', 'X.f_Job()', False),
        ('stored_reference', 'DMibSetByOwnClass CJob *m_pJob = nullptr;', 'CJob *const &f_Job() const { return m_pJob; }', 'X.f_Job()', False),
        ('pointer', 'TCPointer<CJob> m_pJob;', 'CJob *f_Job() const { return m_pJob.f_Get(); }', 'X.f_Job()', False),
        ('stored_untold', 'CJob *m_pJob = nullptr;', 'CJob *f_Job() const { return m_pJob; }', 'X.f_Job()', Untold),
        ('stored_reference_untold', 'CJob *m_pJob = nullptr;', 'CJob *const &f_Job() const { return m_pJob; }', 'X.f_Job()', Untold),
        ('pointer_untold', 'TCRawPointer<CJob> m_pJob;', 'CJob *f_Job() const { return m_pJob.f_Get(); }', 'X.f_Job()', Untold),
        ('self', 'CJob m_Job;', 'auto const &f_Job() const { return *this; }', '&X.f_Job().m_Job', True),
        ('static', 'CStatic m_Static;', 'CJob *f_Job() const { return m_Static.f_Job(); }', 'X.f_Job()', False),
        ('unseen', 'CJob m_Job;', 'CJob *f_Job() const;', 'X.f_Job()', True),
    ]
    for Kind, Members, Getter, Use, Reported in Kinds:
        def At(Site, Check, bAlso=True):
            # The check where it reports the row at Site.
            if isinstance(Reported, set):
                return Check if Site in Reported else None
            return Check if Reported and bAlso else None
        Class = 'CGet_' + Kind
        emit('struct %s {' % Class)
        emit('  %s ~%s(); %s' % (Members, Class, Getter))
        emit('  TCFuture<int> f_Read() { auto *pJob = %s; co_await fg_Work(); co_return pJob->m_Result; }' % Use.replace('X.', 'this->'),
             At('read', This))
        emit('};')
        emit('TCFuture<int> fg_parameter_%s(%s const &_Object) {' % (Kind, Class), At('parameter', Parameter))
        emit('  auto *pJob = %s; co_await fg_Work(); co_return pJob->m_Result; }' % Use.replace('X.', '_Object.'))
        emit('TCFuture<int> fg_capture_%s() { %s Object; auto *pJob = %s;' % (Kind, Class, Use.replace('X.', 'Object.')))
        emit('  return fg_CallSafe([pJob]() -> TCFuture<int> { co_await fg_Work(); co_return pJob->m_Result; }); }',
             At('capture', Capture, Kind != 'unseen'))
        emit('TCFuture<int> fg_init_capture_%s() { %s Object;' % (Kind, Class))
        emit('  return fg_CallSafe([&Job = *%s]() -> TCFuture<int> { co_await fg_Work(); co_return Job.m_Result; }); }' % Use.replace('X.', 'Object.'),
             At('init_capture', Capture))
        # A pointer a cleanup reads, pointed at what a later local gives
        # (async-destroy's retarget report).
        emit('TCFuture<int> fg_destroy_%s() {' % Kind)
        emit('  CJob const *pJob = nullptr; auto Guard = co_await fg_AsyncDestroy([&]() -> TCFuture<int> { co_return pJob->m_Result; });')
        emit('  %s Late; pJob = %s;' % (Class, Use.replace('X.', 'Late.')),
             At('destroy', Destroy, Kind != 'unseen'))
        emit('  co_return 0; }')
    # The same kinds relative to an argument, given by pointer and by
    # reference, to a free function and to a member function of another
    # object; the coroutine's parameter is what is reported.
    ArgumentKinds = [
        # Kind, members, the getter's result type, its return of the record R reached by `R.`, the use, reported.
        ('own', 'CJob m_Job;', 'CJob const *', 'return &R.m_Job;', 'G', True),
        ('owned', 'TCUniquePointer<CJob> m_pJob = TCUniquePointer<CJob>(new CJob);', 'CJob *', 'return R.m_pJob.operator->();', 'G', True),
        ('stored', 'DMibSetByOwnClass CJob *m_pJob = nullptr;', 'CJob *', 'return R.m_pJob;', 'G', False),
        ('stored_reference', 'DMibSetByOwnClass CJob *m_pJob = nullptr;', 'CJob *const &', 'return R.m_pJob;', 'G', False),
        ('stored_untold', 'CJob *m_pJob = nullptr;', 'CJob *', 'return R.m_pJob;', 'G', True),
        ('stored_reference_untold', 'CJob *m_pJob = nullptr;', 'CJob *const &', 'return R.m_pJob;', 'G', True),
        ('self', 'CJob m_Job;', 'CArgument_self const *', 'return &R;', '&G->m_Job', True),
        ('static', 'CStatic m_Static;', 'CJob *', 'return R.m_Static.f_Job();', 'G', False),
        ('unseen', 'CJob m_Job;', 'CJob *', None, 'G', True),
    ]
    for Kind, Members, Result, Return, Use, bReported in ArgumentKinds:
        Class = 'CArgument_' + Kind
        emit('struct %s { %s };' % (Class, Members))
        # Form, the parameter, its name, a member of the record, its address.
        Forms = [('pointer', '%s const *_pContext' % Class, '_pContext', '_pContext->', '_pContext'),
                 ('reference', '%s const &_Context' % Class, '_Context', '_Context.', '&_Context')]
        Helper = []
        for Form, Declaration, Name, Member, Address in Forms:
            Body = '{ %s }' % Return.replace('R.', Member).replace('&R', Address) if Return else ';'
            emit('%s fg_Argument_%s_by_%s(%s) %s' % (Result, Kind, Form, Declaration, Body))
            Helper.append('%s f_By_%s(%s) const %s' % (Result, Form, Declaration, Body))
        emit('struct CArgumentHelper_%s { %s };' % (Kind, ' '.join(Helper)))
        for Form, Declaration, Name, Member, Address in Forms:
            for Function, Call in [('free', 'fg_Argument_%s_by_%s(%s)' % (Kind, Form, Name)),
                                   ('member', 'CArgumentHelper_%s().f_By_%s(%s)' % (Kind, Form, Name))]:
                emit('TCFuture<int> fg_argument_%s_%s_%s(%s) {' % (Kind, Form, Function, Declaration),
                     Parameter if bReported else None)
                emit('  auto *pJob = %s; co_await fg_Work(); co_return pJob->m_Result; }' % Use.replace('G', Call))
    emit('TCFuture<int> fg_parameter_wrapper(TCPointer<CJob> const &_pJob) { auto *pJob = _pJob.f_Get(); co_await fg_Work(); co_return pJob->m_Result; }')
    emit('TCFuture<int> fg_parameter_raw_wrapper(TCRawPointer<CJob> const &_pJob) { auto *pJob = _pJob.f_Get(); co_await fg_Work(); co_return pJob->m_Result; }',
         Parameter)
    emit('TCFuture<int> fg_capture_wrapper(TCPointer<CJob> const &_pJob) { auto *pJob = _pJob.f_Get();')
    emit('  return fg_CallSafe([pJob]() -> TCFuture<int> { co_await fg_Work(); co_return pJob->m_Result; }); }')
    emit('struct CPosition { int m_Line; };')
    emit('struct CMatrixStrSpan { char const *m_pData; int m_Len; CPosition m_Position; CPosition const &f_GetPosition() const { return m_Position; } char const *f_GetData() const { return m_pData; } int f_GetLen() const { return m_Len; } };')
    emit('TCFuture<int> fg_view_position(CMatrixStrSpan _Source) { co_await fg_Work(); co_return _Source.f_GetPosition().m_Line; }')
    emit('TCFuture<int> fg_view_data(CMatrixStrSpan _Source) { co_await fg_Work(); co_return *_Source.f_GetData(); }', Parameter)
    emit('TCFuture<int> fg_view_reference_length(CMatrixStrSpan const &_Span) { auto const &Text = _Span;')
    emit('  return fg_CallSafe([Text]() -> TCFuture<int> { co_await fg_Work(); co_return Text.f_GetLen(); }); }')
    emit('TCFuture<int> fg_view_reference_data() { char Buffer[8]{}; CMatrixStrSpan Span{Buffer, 8, {}}; auto const &Text = Span;')
    emit('  return fg_CallSafe([Text]() -> TCFuture<int> { co_await fg_Work(); co_return *Text.f_GetData(); }); }', Capture)
    emit('struct CThreadLocalData { bool m_bForceNonLocal; };')
    emit('CThreadLocalData &fg_ConcurrencyThreadLocal();')
    emit('TCFuture<int> fg_init_call() { return fg_CallSafe([&ThreadLocal = fg_ConcurrencyThreadLocal()]() -> TCFuture<int> { co_return ThreadLocal.m_bForceNonLocal; }); }')
    emit('TCFuture<int> fg_init_call_after() { return fg_CallSafe([&ThreadLocal = fg_ConcurrencyThreadLocal()]() -> TCFuture<int> { co_await fg_Work(); co_return ThreadLocal.m_bForceNonLocal; }); }', Capture)
    emit('TCFuture<int> fg_local_reference_call() { auto &ThreadLocal = fg_ConcurrencyThreadLocal(); return fg_CallSafe([&ThreadLocal]() -> TCFuture<int> { co_return ThreadLocal.m_bForceNonLocal; }); }')
if Section == 'landed':
    # A borrow from an entry of actor state, `mp_Jobs[_Key]` of an owner of a
    # CJob, used after a suspension: alive where the frame keeps what the
    # owner in the entry owns, `auto pKeep = mp_Jobs[_Key];`, whatever way the
    # derivation is written; reported with no keeper, a keeper of the actor
    # only, and for a borrow of the entry itself.
    Actor = 'malterlib-coroutine-actor-reference'
    emit('struct CLandedJob { int m_Result = 0; int &f_Result() { return m_Result; } int *f_ResultPointer() { return &m_Result; } };')
    emit('struct CLandedDerivedJob : CLandedJob { int m_Derived = 0; };')
    emit('struct CLandedJobs { TCSharedPointer<CLandedJob> &operator[](int _Key); };')
    E = 'mp_Jobs[_Key]'
    Shapes = [
        # Shape, the declarations, the use.
        ('direct', 'int &Result = %s->m_Result;' % E, 'Result'),
        ('reference', 'CLandedJob &Job = *%s;' % E, 'Job.m_Result'),
        ('pointer', 'CLandedJob *pJob = &*%s;' % E, 'pJob->m_Result'),
        ('auto_pointer', 'auto *pJob = &*%s;' % E, 'pJob->m_Result'),
        ('chain', 'CLandedJob &First = *%s; CLandedJob &Job = First;' % E, 'Job.m_Result'),
        ('member', 'CLandedJob &Job = *%s; int &Result = Job.m_Result;' % E, 'Result'),
        ('identity_cast', 'CLandedJob const &Job = static_cast<CLandedJob const &>(*%s);' % E, 'Job.m_Result'),
        ('downcast', 'CLandedDerivedJob &Job = static_cast<CLandedDerivedJob &>(*%s);' % E, 'Job.m_Derived'),
        ('getter_reference', 'int &Result = %s->f_Result();' % E, 'Result'),
        ('getter_pointer', 'int *pResult = %s->f_ResultPointer();' % E, '*pResult'),
        ('conditional', 'CLandedJob &Job = _bChoose ? *%s : *mp_Jobs[_Key + 1];' % E, 'Job.m_Result'),
    ]
    Keepers = [
        # Keeper, its declaration, whether it keeps.
        ('kept', 'auto pKeep = %s;' % E, True),
        ('not_kept', '', False),
        ('actor_kept', 'TCActor<CLandedApp> Self;', False),
    ]
    # A borrow of the entry itself, the owner in it, which erasing the entry destroys.
    EntryShapes = [
        ('entry_reference', 'TCSharedPointer<CLandedJob> &pJob = %s;' % E, 'pJob->m_Result'),
        ('entry_pointer', 'TCSharedPointer<CLandedJob> *pJob = &%s;' % E, '(*pJob)->m_Result'),
        ('entry_chain', 'TCSharedPointer<CLandedJob> &pFirst = %s; TCSharedPointer<CLandedJob> &pJob = pFirst;' % E, 'pJob->m_Result'),
        ('entry_cast', 'TCSharedPointer<CLandedJob> const &pJob = static_cast<TCSharedPointer<CLandedJob> const &>(%s);' % E, 'pJob->m_Result'),
        ('entry_conditional', 'TCSharedPointer<CLandedJob> &pJob = _bChoose ? %s : mp_Jobs[_Key + 1];' % E, 'pJob->m_Result'),
    ]
    emit('struct CLandedApp : CActor {')
    for Shape, Declarations, Use in Shapes:
        for Keeper, Keeping, bKeeps in Keepers:
            emit('  TCFuture<int> f_%s_%s(int _Key, bool _bChoose) { %s %s co_await fg_Work(); co_return %s; }'
                 % (Shape, Keeper, Declarations, Keeping, Use), None if bKeeps else Actor)
    for Shape, Declarations, Use in EntryShapes:
        emit('  TCFuture<int> f_%s_kept(int _Key, bool _bChoose) { %s auto pKeep = %s; co_await fg_Work(); co_return %s; }'
             % (Shape, Declarations, E, Use), Actor)
    emit('  CLandedJobs mp_Jobs;')
    emit('};')
if Section == 'joined':
    # A lambda that code that runs to completion starts and joins in the full
    # expression, `co_await fg_CallSafe([...]() ...)`: what it borrows is alive
    # until the join (the context of a callee joined by the end of an
    # expression), by the kind of storage it is, whatever way the derivation is
    # written: a local or a parameter declared before the full expression, a
    # temporary of that full expression, static storage and what an owner owns
    # with a keeper there are; a local declared after it, a temporary of an
    # earlier statement, what an owner owns with no keeper and thread-local
    # storage aren't, nor anything where the code doesn't run to completion.
    Capture = 'malterlib-coroutine-reference-capture'
    emit('#include "malterlib-runs-to-completion.h"')
    emit('struct CJoinedBase { int m_Result = 0; int const &f_Result() const { return m_Result; } int const *f_ResultPointer() const { return &m_Result; } };')
    emit('struct CJoinedJob : CJoinedBase { int m_Derived = 0; };')
    emit('CJoinedJob const &fg_JoinedConst(CJoinedJob const &_Job) { return _Job; }')
    emit('TCSharedPointer<CJoinedJob> fg_MakeJoinedJob();')
    emit('CJoinedJob g_JoinedJob;')
    emit('thread_local CJoinedJob t_JoinedJob;')
    Roots = [
        # Root, parameters, declarations, the storage, statements after the join, whether it runs to completion,
        # whether it is reported.
        ('local_before', '', 'CJoinedJob Job;', 'Job', '', True, False),
        # Of the last lap of a loop, declared after the full expression.
        ('local_after', '', 'CJoinedJob *pRoot = &g_JoinedJob; for (int i = 0; i < 2; ++i) {', '*pRoot',
         'CJoinedJob Late; pRoot = &Late; }', True, True),
        ('parameter', 'CJoinedJob _Job, ', '', '_Job', '', True, False),
        ('reference_parameter', 'CJoinedJob const &_Job, ', '', '_Job', '', True, False),
        ('temporary_same', '', '', 'fg_JoinedConst(CJoinedJob{})', '', True, False),
        ('temporary_earlier', '', 'CJoinedJob const &Earlier = fg_JoinedConst(CJoinedJob{});', 'Earlier', '', True, True),
        ('static', '', '', 'g_JoinedJob', '', True, False),
        ('owned_kept', '', 'TCSharedPointer<CJoinedJob> pJob = fg_MakeJoinedJob();', '*pJob', '', True, False),
        ('owned_not_kept', 'TCSharedPointer<CJoinedJob> const &_pJob, ', '', '*_pJob', '', True, True),
        ('thread_local', '', '', 't_JoinedJob', '', True, True),
        ('not_runs', '', 'CJoinedJob Job;', 'Job', '', False, True),
    ]
    Shapes = [
        # Shape, declarations of locals made of the storage, the capture, the use; the shapes with locals put the
        # storage in an earlier statement.
        ('direct', '', '[&Value = (%s).m_Result]', 'Value'),
        ('reference', 'CJoinedJob const &Ref = %s;', '[&Ref]', 'Ref.m_Result'),
        ('pointer', '', '[pJoined = &*&(%s)]', 'pJoined->m_Result'),
        ('chain', 'CJoinedJob const &First = %s; CJoinedJob const &Ref = First;', '[&Ref]', 'Ref.m_Result'),
        ('member', '', '[pValue = &(%s).m_Result]', '*pValue'),
        ('identity_cast', '', '[&Value = static_cast<CJoinedJob const &>(%s)]', 'Value.m_Result'),
        ('downcast', '', '[&Value = static_cast<CJoinedJob const &>(static_cast<CJoinedBase const &>(%s))]', 'Value.m_Derived'),
        ('getter_reference', '', '[&Value = (%s).f_Result()]', 'Value'),
        ('getter_pointer', '', '[pValue = (%s).f_ResultPointer()]', '*pValue'),
        ('conditional', '', '[&Value = _bChoose ? (%s) : (%s)]', 'Value.m_Result'),
    ]
    # A copy of a pointer to a temporary of an earlier statement isn't a borrow
    # of a local to reference-capture (borrowedLocal: a temporary only of the
    # init capture's own initializer), a miss of that check, not of the join.
    NotBorrowed = [('temporary_earlier', 'pointer'), ('temporary_earlier', 'member'),
                   ('temporary_earlier', 'getter_pointer')]
    for Root, Params, Declarations, Storage, After, bRuns, bReported in Roots:
        for Shape, Locals, Captures, Use in Shapes:
            if Locals and Root == 'temporary_same':
                continue
            Made = Locals % Storage if Locals else ''
            Taken = Captures.replace('%s', Storage)
            emit('%sTCFuture<int> fg_%s_%s(%sbool _bChoose) { int Result = 0; %s %s Result += co_await fg_CallSafe(%s() -> TCFuture<int> { co_await fg_Work(); co_return %s; }); %s co_return Result; }'
                 % ('DMibRunsToCompletion ' if bRuns else '', Root, Shape, Params, Declarations, Made, Taken, Use, After),
                 Capture if bReported and (Root, Shape) not in NotBorrowed else None)
if Section in ('values', 'values-capture'):
    # What a use does to the storage it designates or points to, the same for
    # a reference and a pointer, and the same where the consumer is built in
    # or is the body of a function the value is given to that is seen:
    # compared or tested, nothing of what it points to is touched;
    # dereferenced, given to a function that can't be seen or returned, it
    # is. Each root (a pointer, or the address of a reference), crossed with
    # each shape that hands the value on unchanged, each consumer and each
    # way to it (built in, a seen callee, a callee that isn't seen), after a
    # suspension: `values` a parameter under reference-parameter,
    # `values-capture` a local captured by a closure called in the scope,
    # under reference-capture.
    Check = 'malterlib-coroutine-reference-parameter' if Section == 'values' else \
        'malterlib-coroutine-reference-capture'
    emit('namespace std {')
    emit('struct strong_ordering {')
    emit('  signed char m_Value;')
    emit('  static const strong_ordering less, equal, equivalent, greater;')
    emit('  friend constexpr bool operator<(strong_ordering _Ordering, int) { return _Ordering.m_Value < 0; }')
    emit('};')
    emit('inline constexpr strong_ordering strong_ordering::less{-1}, strong_ordering::equal{0}, '
         'strong_ordering::equivalent{0}, strong_ordering::greater{1};')
    emit('} // namespace std')
    emit('int fg_Count();')
    emit('void fg_Use(void const *_pValue);')
    # Helpers that give their argument back, by value, by reference, forwarded
    # and through a recursion, a sink and a job of static storage.
    emit('template <typename t_CPointer> t_CPointer fg_ByValue(t_CPointer _pValue) { return _pValue; }')
    emit('template <typename t_CPointer> t_CPointer const &fg_ByReference(t_CPointer const &_pValue) '
         '{ return _pValue; }')
    emit('template <typename t_CPointer> t_CPointer &&fg_Forwarded(t_CPointer &&_pValue) '
         '{ return static_cast<t_CPointer &&>(_pValue); }')
    emit('template <typename t_CPointer> t_CPointer fg_Recursed(t_CPointer _pValue, int _Depth) '
         '{ return _Depth > 0 ? fg_Recursed(_pValue, _Depth - 1) : _pValue; }')
    emit('void fg_VisitState(void *_pState, int _Depth) { if (_Depth > 0) fg_VisitState(&_pState, _Depth - 1); }')
    emit('using umint = unsigned long;')
    emit('CJob g_OtherJob;')
    emit('int g_Sink;')
    # Records held by value: an aggregate, one whose seen constructor stores
    # it, one whose seen constructor only compares it, and one whose
    # constructor can't be seen.
    emit('struct CPair { CJob *m_pJob; int m_Count; };')
    emit('struct CStoreSeen { CJob *m_pJob; CStoreSeen(CJob *_pJob) : m_pJob(_pJob) {} };')
    emit('struct CCompareSeen { bool m_bSame; CCompareSeen(CJob *_pJob, CJob *_pOther) : m_bSame(_pJob == _pOther) {} };')
    emit('struct CUnseenHolder { CUnseenHolder(CJob *_pJob); CJob *m_pJob; };')
    Consumers = [
        # Consumer, the result type, the statements built in, the body of a
        # callee, the callee's result type, whether it dereferences, whether
        # it is reported.
        ('equal', 'bool', 'co_return %s == nullptr;', 'return _pValue == nullptr;', 'bool', False, False),
        ('not', 'bool', 'co_return !%s;', 'return !_pValue;', 'bool', False, False),
        ('if', 'bool', 'if (%s) co_return true; co_return false;', 'if (_pValue) return true; return false;', 'bool',
         False, False),
        ('three_way', 'bool', 'co_return (%s <=> static_cast<void const *>(_pOther)) < 0;',
         'return (_pValue <=> _pOther) < 0;', 'bool', False, False),
        ('arrow', 'int', 'co_return %s->m_Result;', 'return _pValue->m_Result;', 'int', True, True),
        ('star', 'int', 'co_return (*%s).m_Result;', 'return (*_pValue).m_Result;', 'int', True, True),
        ('subscript', 'int', 'co_return %s[0].m_Result;', 'return _pValue[0].m_Result;', 'int', True, True),
        ('given', 'int', 'fg_Use(%s); co_return 0;', 'fg_Use(_pValue); return 0;', 'int', False, True),
        ('returned', 'void const *', 'co_return %s;', 'return _pValue;', 'void const *', False, True),
    ]
    # The callees, a template for each consumer, seen with its body or only
    # declared.
    for Consumer, Result, Builtin, Body, CalleeResult, bDereferences, bReported in Consumers:
        Other = ', void const *_pOther' if Consumer == 'three_way' else ''
        emit('template <typename t_CPointer> %s fg_Seen_%s(t_CPointer _pValue%s) { %s }'
             % (CalleeResult, Consumer, Other, Body))
        emit('template <typename t_CPointer> %s fg_Unseen_%s(t_CPointer _pValue%s);' % (CalleeResult, Consumer, Other))
    Roots = [
        # Root, the parameter, the closure's capture, the value.
        ('pointer', 'CJob *_pJob', '_pJob = &Job', '_pJob'),
        ('reference', 'CJob &_Job', '&_Job = Job', '(&_Job)'),
    ]
    Shapes = [
        # Shape, the value, the value where it is dereferenced, the
        # statements before, whether it is still the value (an integer isn't).
        ('direct', '%s', '%s', '', True),
        ('parentheses', '(%s)', '(%s)', '', True),
        ('void_cast', 'static_cast<void *>(%s)', 'static_cast<CJob *>(static_cast<void *>(%s))', '', True),
        ('conditional', '(_bEnabled ? %s : nullptr)', '(_bEnabled ? %s : nullptr)', '', True),
        ('comma', '(fg_Count(), %s)', '(fg_Count(), %s)', '', True),
        ('binary_conditional', '(%s ?: nullptr)', '(%s ?: nullptr)', '', True),
        ('by_value', 'fg_ByValue(%s)', 'fg_ByValue(%s)', '', True),
        ('by_reference', 'fg_ByReference(%s)', 'fg_ByReference(%s)', '', True),
        ('forwarded', 'fg_Forwarded(%s)', 'fg_Forwarded(%s)', '', True),
        ('recursed', 'fg_Recursed(%s, 2)', 'fg_Recursed(%s, 2)', '', True),
        # Through a local copied on and then pointed at other storage, which
        # is dereferenced after.
        ('overwritten', 'pPassed', 'pPassed',
         'CJob *pLocal = %s; CJob *pPassed = pLocal; pLocal = &g_OtherJob; g_Sink = pLocal->m_Result; ', True),
        # Through an integer and back: an integer made of a pointer is its
        # value, converted back it is the pointer again.
        ('integer', 'reinterpret_cast<CJob *>(reinterpret_cast<umint>(%s))',
         'reinterpret_cast<CJob *>(reinterpret_cast<umint>(%s))', '', True),
        # Through an integer local given another address after, which the
        # names after it hold, by the definitions that reach them.
        ('integer_overwritten', 'reinterpret_cast<CJob *>(Address)', 'reinterpret_cast<CJob *>(Address)',
         'umint Address = reinterpret_cast<umint>(%s); Address = reinterpret_cast<umint>(&g_OtherJob); ', False),
        ('integer_local', 'reinterpret_cast<CJob *>(Address)', 'reinterpret_cast<CJob *>(Address)',
         'umint Address = reinterpret_cast<umint>(%s); ', True),
        # Put in a member of a record held by value, which that member
        # carries, so a load of that member is the value again: an
        # aggregate, an object a seen constructor stores it in, a record
        # assigned a copy of one. One a seen constructor only compares it in
        # carries nothing of it.
        ('aggregate', 'Pair.m_pJob', 'Pair.m_pJob', 'CPair Pair{%s, 1}; ', True),
        ('constructor_stores', 'Stored.m_pJob', 'Stored.m_pJob', 'CStoreSeen Stored(%s); ', True),
        ('constructor_compares', '(Compared.m_bSame ? &g_OtherJob : nullptr)',
         '(Compared.m_bSame ? &g_OtherJob : nullptr)', 'CCompareSeen Compared(%s, nullptr); ', False),
        ('record_assigned', 'Pair.m_pJob', 'Pair.m_pJob', 'CPair Pair{}; Pair = CPair{%s, 1}; ', True),
        # Given to a constructor that can't be seen, which uses what it
        # points to, whatever is done after.
        ('constructor_unseen', 'Unseen.m_pJob', 'Unseen.m_pJob', 'CUnseenHolder Unseen(%s); ', None),
        # Through a member function that gives back its own object, `return
        # *this;`, which hands it on.
        ('self_returned', '(&(*%s).f_Self())', '(&(*%s).f_Self())', '', True),
    ]
    Ways = ['builtin', 'seen', 'unseen']
    for Root, Parameter, Capture, Named in Roots:
        # Given to a recursive helper that passes its own parameter's address
        # on: past the levels of `void *` it is a body that can't be seen, in
        # bounded time and memory.
        Statements = 'fg_VisitState(%s, 2); co_return 0;' % Named
        if Section == 'values':
            emit('TCFuture<int> fg_%s_visit(%s, CJob *_pOther, bool _bEnabled) { co_await fg_Work(); %s }'
                 % (Root, Parameter, Statements), Check)
        else:
            emit('TCFuture<int> fg_%s_visit(bool _bEnabled) { CJob Job; CJob Other; auto fRun = [%s, '
                 '_pOther = &Other, _bEnabled]() -> TCFuture<int> { co_await fg_Work(); %s }; '
                 'co_return co_await fRun(); }' % (Root, Capture, Statements), Check)
        for Way in Ways:
            for Shape, Value, Dereferenced, Before, bStill in Shapes:
                for Consumer, Result, Builtin, Body, CalleeResult, bDereferences, bReported in Consumers:
                    Form = Dereferenced if bDereferences else Value
                    Argument = Form % Named if '%s' in Form else Form
                    Prefix = Before % Named if Before else ''
                    if Way == 'builtin':
                        Statements = Builtin % Argument
                    else:
                        Other = ', static_cast<void const *>(_pOther)' if Consumer == 'three_way' else ''
                        Call = 'fg_%s_%s(%s%s)' % ('Seen' if Way == 'seen' else 'Unseen', Consumer, Argument, Other)
                        Statements = 'co_return %s;' % Call
                    # Given to a callee that isn't seen, the value is used,
                    # and of `<=>` the other pointer too; given to a
                    # constructor that can't be seen, it is used there.
                    Checks = [Check] * (0 if not bStill or not (bReported or Way == 'unseen') else
                                        2 if Way == 'unseen' and Consumer == 'three_way' else 1)
                    if not bStill and Way == 'unseen' and Consumer == 'three_way':
                        Checks = [Check]
                    if bStill is None:
                        Checks = Checks + [Check]
                    Pending = None
                    Name = '%s_%s_%s_%s' % (Root, Way, Shape, Consumer)
                    if Section == 'values':
                        emit('TCFuture<%s> fg_%s(%s, CJob *_pOther, bool _bEnabled) { co_await fg_Work(); %s%s }'
                             % (Result, Name, Parameter, Prefix, Statements), Checks, Pending)
                    else:
                        emit('TCFuture<%s> fg_%s(bool _bEnabled) { CJob Job; CJob Other; auto fRun = [%s, '
                             '_pOther = &Other, _bEnabled]() -> TCFuture<%s> { co_await fg_Work(); %s%s }; '
                             'co_return co_await fRun(); }' % (Result, Name, Capture, Result, Prefix, Statements),
                             Checks, Pending)
    # A place is a variable and a member path: put in one member of a record
    # held by value, the other member doesn't carry it, however the record
    # is reached. Each way of reaching it (directly, through a local
    # reference, through a pointer, made by a constructor that is seen, set by
    # a setter that is seen called through `.` and through `->`, a copy of
    # it), crossed with a read of the member that got it after the
    # suspension, reported, and of the other member, not.
    emit('struct CTwo { CJob *m_pJob; CJob *m_pOther; };')
    emit('struct CTwoSeen { CTwoSeen(CJob *_pJob) : m_pJob(_pJob), m_pOther(&g_OtherJob) {} '
         'CJob *m_pJob; CJob *m_pOther; };')
    emit('struct CTwoSet { CJob *m_pJob = nullptr; CJob *m_pOther = &g_OtherJob; '
         'void f_Set(CJob *_pJob) { m_pJob = _pJob; } };')
    Reaches = [
        # How it is reached, the statements before, the record's name with
        # the member operator.
        ('direct', 'CTwo Two{%s, &g_OtherJob}; ', 'Two.'),
        ('reference_alias', 'CTwo Two{%s, &g_OtherJob}; auto &Alias = Two; ', 'Alias.'),
        ('pointer', 'CTwo Two{%s, &g_OtherJob}; auto *pTwo = &Two; ', 'pTwo->'),
        ('seen_constructor', 'CTwoSeen Two(%s); ', 'Two.'),
        ('seen_setter', 'CTwoSet Two; Two.f_Set(%s); ', 'Two.'),
        ('seen_setter_arrow', 'CTwoSet Two; auto *pTwo = &Two; pTwo->f_Set(%s); ', 'pTwo->'),
        ('copy', 'CTwo Source{%s, &g_OtherJob}; CTwo Two = Source; ', 'Two.'),
    ]
    for Root, Parameter, Capture, Named in Roots:
        for Reach, Before, Record in Reaches:
            for Read, bReported in (('member', True), ('other_member', False)):
                Member = 'm_pJob' if Read == 'member' else 'm_pOther'
                Statements = '%sco_return %s%s->m_Result;' % (Before % Named, Record, Member)
                Name = '%s_place_%s_%s' % (Root, Reach, Read)
                Checks = [Check] if bReported else []
                if Section == 'values':
                    emit('TCFuture<int> fg_%s(%s) { co_await fg_Work(); %s }' % (Name, Parameter, Statements),
                         Checks)
                else:
                    emit('TCFuture<int> fg_%s() { CJob Job; auto fRun = [%s]() -> TCFuture<int> { '
                         'co_await fg_Work(); %s }; co_return co_await fRun(); }' % (Name, Capture, Statements),
                         Checks)
    # The value of a place at a point: what a member holds after the
    # suspension is what the definitions of that member that reach there give,
    # however each consumer asks. Each way a member gets a value before the
    # suspension and keeps or loses it (initialized, assigned, set by a setter
    # that is seen, by a setter that forwards to one, in a nested record, by a
    # constructor that is seen that initializes a nested record, overwritten,
    # through each of those, on one way only, cleared), crossed with each
    # consumer after it (a read of the member, a read through a getter, a
    # destructor that reads it where the scope ends, a copy of the record a
    # closure captures and reads when it is called): reported where the
    # borrow can still be the member's value there.
    for Suffix, Destructor in (('', ''), ('Destroy', ' ~%s() { g_Sink = %sm_pJob->m_Result; }')):
        emit('struct CHold%s { CJob *m_pJob; CJob *m_pOther; CJob *f_Get() const { return m_pJob; } '
             'void f_Set(CJob *_pJob) { m_pJob = _pJob; } void fp_Set(CJob *_pJob) { m_pJob = _pJob; } '
             'void f_SetForward(CJob *_pJob) { fp_Set(_pJob); }%s };'
             % (Suffix, Destructor % ('CHold' + Suffix, '') if Destructor else ''))
        emit('struct CNested%s { CHold m_Inner;%s };'
             % (Suffix, Destructor % ('CNested' + Suffix, 'm_Inner.') if Destructor else ''))
        emit('struct CNestedSeen%s { CNestedSeen%s(CJob *_pJob) : m_Inner{_pJob, &g_OtherJob} {} CHold m_Inner;%s };'
             % (Suffix, Suffix, Destructor % ('CNestedSeen' + Suffix, 'm_Inner.') if Destructor else ''))
    Gets = [
        # How the member gets and keeps or loses the value, the statements
        # (%(T)s the record type, %(N)s the nested one, %(S)s the nested one
        # a seen constructor makes, %(R)s the borrow), the record the member is
        # in, whether the borrow can be its value after.
        ('initialized', '%(T)s Hold{%(R)s, &g_OtherJob}; ', 'Hold', True),
        ('assigned', '%(T)s Hold{nullptr, &g_OtherJob}; Hold.m_pJob = %(R)s; ', 'Hold', True),
        ('seen_setter', '%(T)s Hold{}; Hold.f_Set(%(R)s); ', 'Hold', True),
        ('forwarding_setter', '%(T)s Hold{}; Hold.f_SetForward(%(R)s); ', 'Hold', True),
        ('nested', '%(N)s Hold{{%(R)s, &g_OtherJob}}; ', 'Hold.m_Inner', True),
        ('nested_constructor', '%(S)s Hold(%(R)s); ', 'Hold.m_Inner', True),
        ('overwritten', '%(T)s Hold{%(R)s, &g_OtherJob}; Hold.m_pJob = &g_OtherJob; ', 'Hold', False),
        ('overwritten_setter', '%(T)s Hold{}; Hold.f_Set(%(R)s); Hold.f_Set(&g_OtherJob); ', 'Hold', False),
        ('overwritten_forwarding', '%(T)s Hold{}; Hold.f_SetForward(%(R)s); Hold.f_SetForward(&g_OtherJob); ',
         'Hold', False),
        ('overwritten_nested', '%(N)s Hold{{%(R)s, &g_OtherJob}}; Hold.m_Inner.m_pJob = &g_OtherJob; ',
         'Hold.m_Inner', False),
        ('overwritten_one_way', '%(T)s Hold{%(R)s, &g_OtherJob}; if (_bEnabled) Hold.m_pJob = &g_OtherJob; ',
         'Hold', True),
        ('cleared', '%(T)s Hold{%(R)s, &g_OtherJob}; Hold.m_pJob = nullptr; ', 'Hold', False),
    ]
    Asks = [
        # The consumer, whether its records destroy by reading the member, the
        # statements after the suspension (%s the record the member is in).
        ('read', False, 'co_return %s.m_pJob->m_Result;'),
        ('getter', False, 'co_return %s.f_Get()->m_Result;'),
        ('destructor', True, 'co_return 0;'),
        ('closure', False, None),
    ]
    for Root, Parameter, Capture, Named in Roots:
        for Get, Before, Record, bKept in Gets:
            for Ask, bDestroys, After in Asks:
                Suffix = 'Destroy' if bDestroys else ''
                Types = {'T': 'CHold' + Suffix, 'N': 'CNested' + Suffix, 'S': 'CNestedSeen' + Suffix, 'R': Named}
                Prefix = Before % Types
                if After is None:
                    Prefix += 'auto fRead = [Hold] { return %s.m_pJob->m_Result; }; ' % Record
                    Statements = 'co_return fRead();'
                else:
                    Statements = After % Record if '%s' in After else After
                Name = '%s_member_%s_%s' % (Root, Get, Ask)
                Checks = [Check] if bKept else []
                if Section == 'values':
                    emit('TCFuture<int> fg_%s(%s, bool _bEnabled) { %sco_await fg_Work(); %s }'
                         % (Name, Parameter, Prefix, Statements), Checks)
                else:
                    emit('TCFuture<int> fg_%s(bool _bEnabled) { CJob Job; auto fRun = [%s, _bEnabled]() '
                         '-> TCFuture<int> { %sco_await fg_Work(); %s }; co_return co_await fRun(); }'
                         % (Name, Capture, Prefix, Statements), Checks)
print('\n'.join(Expected if Mode.endswith('expect') else Lines))
