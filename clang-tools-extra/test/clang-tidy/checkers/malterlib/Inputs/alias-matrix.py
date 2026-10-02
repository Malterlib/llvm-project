# Writes the alias matrix: where a write through another way can land on a
# place a function sets, so that what the place holds after the function
# isn't only what it sets. `alias` writes the code, `alias-expect` the lines
# that are reported, a line and the check each.
#
# Each row is a function that first sets a place to static storage on every
# way, then writes the borrowed object's address through one way of writing,
# and a coroutine that holds the borrow in the place, calls it, and reads the
# place after a suspension. The coroutine is reported where that write can
# land on the place: where its storage can overlap the place's, by type (the
# same, a base or derived class, one that holds it by value, or bytes) and by
# member (the same member, members of one union where the paths part, or a
# write of a whole that holds the place); unless the write's object is new
# storage of the function, a parameter stated distinct (DMibDistinct) whose
# path crosses no pointer, another member, or an object of another type. A
# write whose place can't be told, of what only a function that can't be seen
# gives, can land on what code that can't be seen reaches: static storage and
# what has escaped to it, not a local of the coroutine whose address doesn't
# escape.
#
# The dimensions: the ways to write (a named member of another object through
# a pointer or a reference parameter, the whole object, a base or derived
# class, an object that holds it, bytes, placement new, an element, another
# member of a union, also one at an outer level, a pointer to the member
# itself, a reference member, a closure's capture and parameter, the object
# through its own pointer member, what an unseen function gives, a member
# function called on another object, a local alias of it), the places (a
# member of the object of a member function, of what a reference or a pointer
# parameter refers to, a nested member, an element of an array member, the
# pointer a setter is given to set, a member of a local record in the
# coroutine itself, a local of the coroutine that another one points to), and
# the facts that separate (none, DMibDistinct, new storage, another member,
# another type, and of a write whose place can't be told an address that
# escapes).
#
# A row that should report and doesn't yet is pending: written with a
# `// RESTORE-MESSAGES: <check>` comment that the gate counts, and not
# expected. A row reported on safe code that isn't fixed yet is written with
# `// KNOWN-FALSE-POSITIVE: <reason>` and is expected, so that the gate counts
# it (lbt/known-fp-check.sh).
import re
import sys

Mode = sys.argv[1]
if Mode not in ('alias', 'alias-expect'):
    sys.exit('Unknown mode: ' + Mode)
Check = 'malterlib-coroutine-reference-parameter'
Lines = []
Expected = []


def pending(Name):
    # What a row the binary doesn't report yet waits for.
    if Name == 'NHeld_member':
        return ('the walk from the parameter to keep what is stored through a member that points elsewhere where the '
                'caller\'s record points (it keeps it nowhere)')
    if Name == 'NFrame_bytes':
        return 'the walk from the parameter to carry what a copy of bytes reads to where it writes'
    return None


def knownFalsePositive(Name):
    # Why a row the binary reports on safe code does.
    Parts = Name.split('_')
    if Name == 'Nmember_pointer_this_element_other':
        return ('the walk from the parameter keeps what is stored in an element of a member, where a pointer member '
                'points, in the member as a whole (CParameterKept::MemberPaths has no element)')
    if Name.startswith('Nmember_call_') and Name.endswith('_nested_other'):
        return ('a call on another object of the type changes the first member of each path it writes '
                '(changedMembers), m_Inner, which holds the place too')
    if Name.startswith('Nmember_pointer_') and Parts[2] in ('referent', 'pointee') and Name.endswith('_other'):
        return ('the address of a part of what a parameter refers to, kept in a member of it, escapes where the '
                'flow can\'t follow it (findEscapes), so what the function leaves there can\'t be told')
    if Name.startswith('Nreference_member_') and Name.endswith('_other'):
        return ('a write through a reference member can be of any member of its type: which one its default '
                'initializer binds it to is no fact yet (constructors, step 4)')
    if Name.startswith('Nclosure_parameter_') and Name.endswith('_other'):
        return ('what a closure writes through its parameter is its summary\'s, but the value, a capture, isn\'t '
                'one of its sources, and which members the call can change through the argument isn\'t told, so '
                'the call is a change of the argument that can\'t be told')
    if Name.startswith('Nself_pointer_this_') and Name.endswith('_other'):
        return ('the walk from the parameter takes the receiver as kept where the function keeps `this` (a closure '
                'that captures it, its own member), whatever the member holds after the call')
    if Name.startswith('Nself_pointer_') and Parts[2] in ('referent', 'pointee'):
        return ('what a pointer member of what a parameter refers to is given in the function isn\'t followed '
                'where a write goes through it, so the write can be of the object itself')
    return None


def emit(Text, Reported=False, Name=None):
    Waits = pending(Name) if Name and Reported else None
    Reason = knownFalsePositive(Name) if Name and not Reported else None
    if Waits:
        Text += ' // RESTORE-MESSAGES: %s (waits for %s)' % (Check, Waits)
        Reported = False
    if Reason:
        Text += ' // KNOWN-FALSE-POSITIVE: %s' % Reason
        Reported = True
    Lines.append(Text)
    if Reported:
        Expected.append('%d %s' % (len(Lines), Check))


emit('#include "malterlib-concurrency.h"')
emit('void *operator new(decltype(sizeof(0)), void *_pPlace) noexcept;')
emit('TCFuture<int> fg_Suspend();')
emit('struct CJob { int m_Value = 0; };')
emit('CJob g_Job;')
emit('void fg_CopyBytes(void *_pTo, void const *_pFrom, unsigned long _nBytes);')

# The places a function sets: by the object of a member function (`this`),
# by what a reference parameter refers to (`referent`), by what a pointer
# parameter points to (`pointee`); each of a member (`member`), a nested
# member (`nested`) and an element of an array member (`element`).
Roots = [
    # Root, the place's object in the function as a prefix and as an object,
    # the function's own parameter, how the coroutine calls it.
    ('this', '', '(*this)', None, 'View.f_Set(&g_Job, &_Job%s)'),
    ('referent', '_View.', '_View', 'CView &_View', 'fg_Set(View, &g_Job, &_Job%s)'),
    ('pointee', '_pView->', '(*_pView)', 'CView *_pView', 'fg_Set(&View, &g_Job, &_Job%s)'),
]
Paths = [
    # Path, the place, another member of the same object, the members of the
    # record that holds it, and records it needs before.
    ('member', 'm_pJob', 'm_pSaved', 'CJob const *m_pJob = nullptr; CJob const *m_pSaved = nullptr;', ''),
    ('nested', 'm_Inner.m_pJob', 'm_Inner.m_pSaved', 'CInner m_Inner;',
     'struct CInner { CJob const *m_pJob = nullptr; CJob const *m_pSaved = nullptr; };'),
    ('element', 'm_pJobs[0]', 'm_pJobs[1]', 'CJob const *m_pJobs[2] = {};', ''),
]

# The facts that separate: none (the write is of the place), a parameter
# stated distinct, new storage of the function, another member, an object of
# another type; of a write whose place can't be told, the object's address
# in a global first (escaped), which unseen code reaches, else it doesn't.
Facts = ['none', 'distinct', 'fresh', 'other', 'unrelated', 'escaped']


class Row:
    # What a way writes of a row: the records it needs, of a base, a derived
    # class or an object that holds the place, whether the place's members
    # are in a union (Union, OuterUnion: True a union, False a struct, None
    # neither), the member a reference member is bound to, the function's
    # parameters, the coroutine's arguments and what it does before, the
    # function's statements, and whether the coroutine links the object first.
    def __init__(self):
        self.Records = []
        self.Base = False
        self.Derived = False
        self.Outer = False
        self.Union = None
        self.OuterUnion = None
        self.Reference = None
        self.Params = []
        self.Arguments = []
        self.Before = ''
        self.Statements = ''
        self.Link = False


def way(Way, Fact, R, Obj, S, Other):
    # What the way writes, for the root R/Obj and the place S: the row's
    # parameters, the coroutine's arguments and what it does before, the
    # statements, and what the records need. None where the fact doesn't
    # apply to the way.
    W = Row()
    if Fact == 'escaped' and Way != 'untold':
        return None
    Distinct = 'DMibDistinct ' if Fact == 'distinct' else ''
    Target = Other if Fact == 'other' else S
    Type = 'COther' if Fact == 'unrelated' else 'CView'
    Given = '&Other' if Fact in ('distinct', 'unrelated') else '&View'
    GivenRef = Given[1:]
    if Way in ('pointer', 'reference', 'whole', 'element', 'member_call', 'local_alias', 'closure_parameter'):
        if Way == 'member_call' and Fact == 'unrelated':
            return None
        if Fact == 'fresh':
            W.Statements = '%s Fresh; %s *pOther = &Fresh; ' % (Type, Type)
            P = 'pOther'
        elif Way == 'reference':
            W.Params.append('%s%s &_Other' % (Distinct, Type))
            W.Arguments.append(GivenRef)
            P = '(&_Other)'
        else:
            W.Params.append('%s%s *_pOther' % (Distinct, Type))
            W.Arguments.append(Given)
            P = '_pOther'
        if Way in ('pointer', 'reference'):
            W.Statements += '%s->%s = _pOld;' % (P, Target)
        elif Way == 'whole':
            if Fact == 'other':
                return None
            W.Params.append('%s const &_Old' % Type)
            W.Arguments.append('Saved')
            W.Before = '%s Saved; Saved.%s = &_Job; ' % (Type, S)
            W.Statements += '*%s = _Old;' % P
        elif Way == 'element':
            W.Statements += '%s[0].%s = _pOld;' % (P, Target)
        elif Way == 'member_call':
            W.Statements += '%s->f_Put%s(_pOld);' % (P, 'Other' if Fact == 'other' else '')
        elif Way == 'local_alias':
            W.Statements += 'auto &Alias = *%s; Alias.%s = _pOld;' % (P, Target)
        elif Way == 'closure_parameter':
            W.Statements += 'auto fWrite = [&](%s &_Written) { _Written.%s = _pOld; }; fWrite(*%s);' % (
                Type, Target, P)
        return W
    if Fact in ('distinct', 'unrelated') and Way in ('member_pointer', 'self_pointer', 'union', 'outer_union',
                                                     'reference_member', 'closure_capture', 'untold'):
        return None
    if Way in ('base', 'derived', 'containing', 'placement', 'bytes', 'bytes_builtin'):
        if Fact in ('other', 'unrelated'):
            return None
        if Way == 'base':
            W.Base = True
            Of = 'CBase'
        elif Way == 'derived':
            W.Derived = True
            Of = 'CDerived'
        elif Way == 'containing':
            W.Outer = True
            Of = 'COuter'
        else:
            Of = 'CView'
        if Way in ('base', 'derived', 'containing'):
            if Fact == 'fresh':
                W.Statements = '%s Fresh; %s *pOther = &Fresh; ' % (Of, Of)
                P = 'pOther'
            else:
                W.Params.append('%s%s *_pOther' % (Distinct, Of))
                W.Arguments.append('&Outer' if Way == 'containing' and Fact != 'distinct' else
                                   '&Other' if Fact == 'distinct' else '&View')
                P = '_pOther'
            W.Params.append('%s const &_Old' % Of)
            W.Arguments.append('Saved')
            if Way == 'containing':
                W.Before = 'COuter Saved; Saved.m_Inner.%s = &_Job; ' % S
            else:
                W.Before = '%s Saved; Saved.%s = &_Job; ' % (Of, S)
            W.Statements += '*%s = _Old;' % P
        elif Way == 'placement':
            if Fact == 'fresh':
                W.Statements = 'CView Fresh; CView *pOther = new (&Fresh) CView; '
            else:
                W.Params.append('%svoid *_pStorage' % Distinct)
                W.Arguments.append(Given)
                W.Statements = 'CView *pOther = new (_pStorage) CView; '
            W.Statements += 'pOther->%s = _pOld;' % S
        else:
            if Fact == 'fresh':
                W.Statements = 'CView Fresh; void *pBytes = &Fresh; '
                P = 'pBytes'
            else:
                W.Params.append('%svoid *_pBytes' % Distinct)
                W.Arguments.append(Given)
                P = '_pBytes'
            W.Params.append('CView const &_Old')
            W.Arguments.append('Saved')
            W.Before = 'CView Saved; Saved.%s = &_Job; ' % S
            W.Statements += ('fg_CopyBytes(%s, &_Old, sizeof(CView));' if Way == 'bytes' else
                             '__builtin_memcpy(%s, &_Old, sizeof(CView));') % P
        return W
    if Way == 'union':
        # Another member of a union the member is in, of the same object or
        # through another one; other: a struct, whose members are apart.
        if Fact == 'fresh':
            return None
        W.Union = Fact != 'other'
        W.Statements = '%sm_pOther = _pOld;' % R
        return W
    if Way == 'union_other':
        if Fact not in ('none', 'distinct', 'other'):
            return None
        W.Union = Fact != 'other'
        W.Params.append('%sCView *_pOther' % Distinct)
        W.Arguments.append(Given)
        W.Statements = '_pOther->m_pOther = _pOld;'
        return W
    if Way == 'outer_union':
        # Another member of a union a record that holds the member is in.
        if Fact not in ('none', 'distinct', 'other'):
            return None
        W.OuterUnion = Fact != 'other'
        W.Params.append('%sCView *_pOther' % Distinct)
        W.Arguments.append(Given)
        W.Statements = '_pOther->m_B.m_pX = _pOld;'
        return W
    if Way == 'member_pointer':
        # A pointer member that points to the place, set where the function
        # can't see it; other: set in the function to another member.
        if Fact == 'none':
            W.Link = True
            W.Statements = '*%sm_ppJob = _pOld;' % R
        elif Fact == 'other':
            W.Statements = '%sm_ppJob = &%s%s; *%sm_ppJob = _pOld;' % (R, R, Other, R)
        elif Fact == 'fresh':
            W.Statements = 'CJob const *pFresh = nullptr; CJob const **ppJob = &pFresh; *ppJob = _pOld;'
        return W
    if Way == 'pointer_parameter':
        # Another member given there isn't told in the function.
        if Fact == 'other':
            return None
        if Fact == 'fresh':
            W.Statements = 'CJob const *pFresh = nullptr; CJob const **_ppJob = &pFresh; '
        elif Fact == 'unrelated':
            W.Params.append('int **_ppJob')
            W.Arguments.append('&pCount')
            W.Before = 'int *pCount = nullptr; '
            W.Statements = '*_ppJob = nullptr;'
            return W
        else:
            W.Params.append('%sCJob const **_ppJob' % Distinct)
            W.Arguments.append('&Other.%s' % S if Fact == 'distinct' else '&View.%s' % Target)
        W.Statements += '*_ppJob = _pOld;'
        return W
    if Way == 'subscript_address':
        if Fact not in ('none', 'other'):
            return None
        W.Statements = 'auto ppJob = &%s%s; ppJob[0] = _pOld;' % (R, Target)
        return W
    if Way == 'reference_member':
        if Fact == 'fresh':
            return None
        W.Reference = Target
        W.Statements = '%sm_rpJob = _pOld;' % R
        return W
    if Way == 'self_pointer':
        if Fact == 'none':
            W.Link = True
            W.Statements = '%sm_pSelf->%s = _pOld;' % (R, S)
        elif Fact == 'other':
            W.Statements = '%sm_pSelf->%s = _pOld;' % (R, Other)
            W.Link = True
        elif Fact == 'fresh':
            W.Statements = 'CView Fresh; %sm_pSelf = &Fresh; %sm_pSelf->%s = _pOld;' % (R, R, S)
        return W
    if Way == 'closure_capture':
        if Fact == 'fresh':
            W.Statements = 'CView Fresh; auto fWrite = [&] { Fresh.%s = _pOld; }; fWrite();' % S
        else:
            W.Statements = 'auto fWrite = [&] { %s%s = _pOld; }; fWrite();' % (R, Target)
        return W
    if Way == 'untold':
        if Fact not in ('none', 'escaped'):
            return None
        W.Records.append('CView *fg_OtherView();')
        W.Statements = 'fg_OtherView()->%s = _pOld;' % S
        if Fact == 'escaped':
            W.Records.append('CView *g_pView;')
            W.Before = 'g_pView = &View; '
        return W
    raise Exception(Way)


Ways = ['pointer', 'reference', 'whole', 'base', 'derived', 'containing', 'bytes', 'bytes_builtin', 'placement',
        'element', 'union', 'union_other', 'outer_union', 'member_pointer', 'pointer_parameter',
        'subscript_address', 'reference_member', 'closure_capture', 'closure_parameter', 'self_pointer', 'untold',
        'member_call', 'local_alias']

nRow = 0
for Way in Ways:
    for Root, R, Obj, RootParameter, Call in Roots:
        for PathName, S, Other, Members, Needs in Paths:
            for Fact in Facts:
                W = way(Way, Fact, R, Obj, S, Other)
                if W is None:
                    continue
                Union = W.Union
                OuterUnion = W.OuterUnion
                Reference = W.Reference
                if (Union is not None or OuterUnion is not None) and PathName != 'member':
                    continue
                nRow += 1
                Name = 'N%s_%s_%s_%s' % (Way, Root, PathName, Fact)
                emit('namespace %s {' % Name)
                if Needs:
                    emit(Needs)
                if Fact == 'unrelated':
                    emit('struct COther { %s };' % Members)
                if W.Outer or W.Derived:
                    emit('struct COuter; struct CDerived;')
                Body = Members
                if Union is True:
                    Body = 'union { CJob const *m_pJob; CJob const *m_pOther; }; CJob const *m_pSaved = nullptr;'
                elif Union is False:
                    Body = 'CJob const *m_pJob = nullptr; CJob const *m_pOther = nullptr; CJob const *m_pSaved = nullptr;'
                elif OuterUnion is True:
                    Body = ('union { struct { CJob const *m_pJob; } m_A; struct { CJob const *m_pX; } m_B; }; '
                            'CJob const *m_pSaved = nullptr;')
                elif OuterUnion is False:
                    Body = ('struct { CJob const *m_pJob; } m_A; struct { CJob const *m_pX; } m_B; '
                            'CJob const *m_pSaved = nullptr;')
                Place = 'm_A.m_pJob' if OuterUnion is not None else S
                Extra = ''
                if Way == 'member_pointer':
                    Extra += ' CJob const **m_ppJob = nullptr; void f_Link() { m_ppJob = &%s; }' % S
                if Way == 'self_pointer':
                    Extra += ' struct CView *m_pSelf = nullptr; void f_Link();'
                if Reference is not None:
                    Extra += ' CJob const *&m_rpJob = %s;' % Reference
                if Way == 'member_call':
                    Extra += ' void f_Put(CJob const *_p) { %s = _p; } void f_PutOther(CJob const *_p) { %s = _p; }' % (
                        S, Other)
                Parameters = ''.join(', ' + Each for Each in W.Params)
                Arguments = ''.join(', ' + Each for Each in W.Arguments)
                Setter = '{ %s%s = _pJob; %s }' % (R, Place, W.Statements)
                Declaration = 'void %%s(%sCJob const *_pJob, CJob const *_pOld%s)' % (
                    RootParameter + ', ' if RootParameter else '', Parameters)
                for Each in W.Records:
                    if Each.startswith('struct'):
                        emit(Each)
                if W.Base:
                    emit('struct CBase { %s };' % Body)
                    Body = ''
                    Head = 'struct CView : CBase {'
                else:
                    Head = 'struct CView {'
                if Root == 'this':
                    emit('%s %s%s %s; };' % (Head, Body, Extra, Declaration % 'f_Set'))
                else:
                    emit('%s %s%s };' % (Head, Body, Extra))
                if Way == 'self_pointer':
                    emit('void CView::f_Link() { m_pSelf = this; }')
                if W.Derived:
                    emit('struct CDerived : CView { int m_nExtra = 0; };')
                if W.Outer:
                    emit('struct COuter { CView m_Inner; };')
                for Each in W.Records:
                    if not Each.startswith('struct'):
                        emit(Each)
                if Root == 'this':
                    emit((Declaration % 'CView::f_Set') + ' ' + Setter)
                else:
                    emit((Declaration % 'fg_Set') + ' ' + Setter)
                # The coroutine: the borrow in the place, the call, a read after
                # a suspension.
                if W.Outer:
                    Decl = 'COuter Outer, Other; '
                elif W.Derived:
                    Decl = 'CDerived View; CDerived Other; '
                else:
                    Decl = 'CView View; CView Other; '
                if Fact == 'unrelated':
                    Decl = Decl.replace('CView Other; ', 'COther Other; ')
                Link = 'View.f_Link(); ' if W.Link else ''
                Reported = Fact == 'escaped' or (Fact == 'none' and Way != 'untold')
                Coroutine = ('TCFuture<int> fg_Run(CJob const &_Job) { %s%sView.%s = &_Job; %s%s; co_await fg_Suspend(); '
                             'co_return View.%s->m_Value; }' % (Decl, W.Before, Place, Link, Call % Arguments, Place))
                # Of a record held in another, that member of it.
                if W.Outer:
                    Coroutine = re.sub(r'\bView\b', 'Outer.m_Inner', Coroutine)
                emit(Coroutine, Reported, Name)
                emit('} // namespace %s' % Name)

# The pointer a function is given to set, by reference or by its address:
# written through another way, it holds what that writes too.
Targets = [
    ('reference', 'CJob const *&o_pJob', 'o_pJob', '%s'),
    ('address', 'CJob const **o_ppJob', '*o_ppJob', '&%s'),
]
PointerWays = [
    # Way, the parameters, the statements, the coroutine's arguments for none
    # and for distinct, what it does before, and the pointer it holds the
    # borrow in.
    ('pointer', '%sCJob const **_ppOther', '*_ppOther = _pOld;', '&pJob', '&pOther', '', 'pJob'),
    ('reference', '%sCJob const *&_rOther', '_rOther = _pOld;', 'pJob', 'pOther', '', 'pJob'),
    ('element', '%sCJob const **_ppJobs', '_ppJobs[0] = _pOld;', '&pJob', '&pOther', '', 'pJob'),
    ('bytes', '%svoid *_pBytes', '__builtin_memcpy(_pBytes, &_pOld, sizeof(_pOld));', '&pJob', '&pOther', '', 'pJob'),
    ('containing', '%sCHolder *_pHolder, CHolder const &_Old', '*_pHolder = _Old;', '&Holder, Saved',
     '&OtherHolder, Saved', 'CHolder Saved{&_Job}; ', 'Holder.m_pJob'),
    ('holder_member', '%sCHolder *_pHolder', '_pHolder->m_pJob = _pOld;', '&Holder', '&OtherHolder', '',
     'Holder.m_pJob'),
    # What an unseen function gives, of a pointer whose address doesn't escape
    # (none) and of one whose address is in a global first (escaped).
    ('untold', '', '*fg_OtherPointer() = _pOld;', '', None, '', 'pJob'),
    # Its address kept where the flow can't follow it, before it is set: code
    # that can't be followed can write it after.
    ('escaped', '', 'g_ppSaved = &%s; %s = _pJob; fg_Fire();', '', None, '', 'pJob'),
]
emit('struct CHolder { CJob const *m_pJob = nullptr; };')
emit('CJob const **fg_OtherPointer();')
emit('CJob const **g_ppSaved;')
emit('void fg_Fire();')
for TargetName, TargetParameter, TargetName2, TargetArgument in Targets:
    for Way, Parameters, Statements, ArgumentNone, ArgumentDistinct, Before, Held in PointerWays:
        for Fact in ('none', 'distinct', 'escaped'):
            if Fact == 'distinct' and ArgumentDistinct is None:
                continue
            if Fact == 'escaped' and Way != 'untold':
                continue
            nRow += 1
            Name = 'NPointer_%s_%s_%s' % (TargetName, Way, Fact)
            Parameter = ', ' + Parameters % ('DMibDistinct ' if Fact == 'distinct' else '') if Parameters else ''
            Argument = ArgumentNone if Fact == 'none' else ArgumentDistinct
            emit('namespace %s {' % Name)
            if Way == 'escaped':
                Statements = Statements % ('o_pJob' if TargetName == 'reference' else '*o_ppJob', TargetName2)
            emit('void fg_Set(%s, CJob const *_pJob, CJob const *_pOld%s) { %s = _pJob; %s }'
                 % (TargetParameter, Parameter, TargetName2, Statements))
            Reported = Fact == 'escaped' or (Fact == 'none' and Way != 'untold')
            emit('TCFuture<int> fg_Run(CJob const &_Job) { CJob const *pJob = &_Job; CJob const *pOther = nullptr; '
                 'CHolder Holder{&_Job}; CHolder OtherHolder; %sfg_Set(%s, &g_Job, &_Job%s); co_await fg_Suspend(); '
                 'co_return %s->m_Value; }'
                 % (Before + ('g_ppSaved = &pJob; ' if Fact == 'escaped' else ''), TargetArgument % Held,
                    ', ' + Argument if Argument else '', Held), Reported, Name)
            emit('} // namespace %s' % Name)

# A member of a local record of the coroutine itself, set to static storage
# and then written through another way in the coroutine; and read through
# another way after the suspension.
Frame = 'struct CFrameView { CJob const *m_pJob = nullptr; CJob const *m_pSaved = nullptr; }; '
FrameRows = [
    # Name, the records, the statements before the suspension, the read,
    # whether reported.
    ('pointer', Frame, 'CFrameView View; View.m_pJob = &g_Job; CFrameView *pAlias = &View; pAlias->m_pJob = &_Job;',
     'View.m_pJob', True),
    ('pointer_fresh', Frame, 'CFrameView View, Other; View.m_pJob = &g_Job; CFrameView *pAlias = &Other; '
     'pAlias->m_pJob = &_Job;', 'View.m_pJob', False),
    ('reference', Frame, 'CFrameView View; auto &Alias = View; View.m_pJob = &g_Job; Alias.m_pJob = &_Job;',
     'View.m_pJob', True),
    ('reference_replaced', Frame, 'CFrameView View; auto &Alias = View; Alias.m_pJob = &_Job; '
     'Alias.m_pJob = &g_Job;', 'View.m_pJob', False),
    ('reference_other', Frame, 'CFrameView View, Other; auto &Alias = Other; View.m_pJob = &g_Job; '
     'Alias.m_pJob = &_Job;', 'View.m_pJob', False),
    ('member_reference', Frame, 'CFrameView View; auto &pJob = View.m_pJob; View.m_pJob = &g_Job; pJob = &_Job;',
     'View.m_pJob', True),
    ('member_reference_replaced', Frame, 'CFrameView View; auto &pJob = View.m_pJob; pJob = &_Job; pJob = &g_Job;',
     'View.m_pJob', False),
    ('member_pointer', Frame, 'CFrameView View; View.m_pJob = &g_Job; CJob const **ppJob = &View.m_pJob; '
     '*ppJob = &_Job;', 'View.m_pJob', True),
    ('member_pointer_other', Frame, 'CFrameView View; View.m_pJob = &g_Job; CJob const **ppJob = &View.m_pSaved; '
     '*ppJob = &_Job;', 'View.m_pJob', False),
    ('subscript_address', Frame, 'CFrameView View; View.m_pJob = &g_Job; (&View.m_pJob)[0] = &_Job;',
     'View.m_pJob', True),
    ('record_element', Frame, 'CFrameView View; View.m_pJob = &g_Job; (&View)[0].m_pJob = &_Job;', 'View.m_pJob',
     True),
    ('record_element_other', Frame, 'CFrameView View; View.m_pJob = &g_Job; (&View)[0].m_pSaved = &_Job;',
     'View.m_pJob', False),
    ('record_element_read', Frame, 'CFrameView View; View.m_pJob = &_Job;', '(&View)[0].m_pJob', True),
    ('record_element_read_replaced', Frame, 'CFrameView View; View.m_pJob = &_Job; View.m_pJob = &g_Job;',
     '(&View)[0].m_pJob', False),
    ('record_pointer_read', Frame, 'CFrameView View; View.m_pJob = &_Job; CFrameView *pView = &View;',
     'pView->m_pJob', True),
    ('record_pointer_read_replaced', Frame, 'CFrameView View; View.m_pJob = &_Job; CFrameView *pView = &View; '
     'View.m_pJob = &g_Job;', 'pView->m_pJob', False),
    ('union', 'union CFrameUnion { CJob const *m_pJob; CJob const *m_pOther; };',
     'CFrameUnion View; View.m_pJob = &g_Job; View.m_pOther = &_Job;', 'View.m_pJob', True),
    ('union_struct', 'struct CFrameStruct { CJob const *m_pJob; CJob const *m_pOther; };',
     'CFrameStruct View{}; View.m_pJob = &g_Job; View.m_pOther = &_Job;', 'View.m_pJob', False),
    ('outer_union', 'struct CFrameUnion { union { struct { CJob const *m_pJob; } m_A; struct { CJob const *m_pX; } '
     'm_B; }; };', 'CFrameUnion View; View.m_A.m_pJob = &g_Job; View.m_B.m_pX = &_Job;', 'View.m_A.m_pJob', True),
    ('bytes', Frame, 'CFrameView View, Saved; Saved.m_pJob = &_Job; View.m_pJob = &g_Job; '
     '__builtin_memcpy(&View, &Saved, sizeof(View));', 'View.m_pJob', True),
    ('placement', Frame, 'CFrameView View; View.m_pJob = &g_Job; CFrameView *pNew = new (&View) CFrameView; '
     'pNew->m_pJob = &_Job;', 'View.m_pJob', True),
    ('closure_capture', Frame, 'CFrameView View; View.m_pJob = &g_Job; auto fWrite = [&] { View.m_pJob = &_Job; }; '
     'fWrite();', 'View.m_pJob', True),
    ('closure_capture_other', Frame, 'CFrameView View; View.m_pJob = &g_Job; '
     'auto fWrite = [&] { View.m_pSaved = &_Job; }; fWrite();', 'View.m_pJob', False),
    ('reference_member', 'struct CFrameView { CJob const *m_pJob = nullptr; CJob const *&m_rpJob = m_pJob; };',
     'CFrameView View; View.m_pJob = &g_Job; View.m_rpJob = &_Job;', 'View.m_pJob', True),
    ('self_pointer', 'struct CFrameView { CJob const *m_pJob = nullptr; CFrameView *m_pSelf = nullptr; };',
     'CFrameView View; View.m_pSelf = &View; View.m_pJob = &g_Job; View.m_pSelf->m_pJob = &_Job;', 'View.m_pJob',
     True),
    ('self_pointer_other', 'struct CFrameView { CJob const *m_pJob = nullptr; CFrameView *m_pSelf = nullptr; };',
     'CFrameView View, Other; View.m_pSelf = &Other; View.m_pJob = &g_Job; View.m_pSelf->m_pJob = &_Job;',
     'View.m_pJob', False),
]
for Name, Records, Before, Read, Reported in FrameRows:
    nRow += 1
    Name = 'NFrame_' + Name
    emit('namespace %s {' % Name)
    emit(Records)
    emit('TCFuture<int> fg_Run(CJob const &_Job) { %s co_await fg_Suspend(); co_return %s->m_Value; }'
         % (Before, Read), Reported, Name)
    emit('} // namespace %s' % Name)

# A member of a local of the coroutine that a pointer member of another local
# points to, written through that pointer by a member function called on the
# other local: what it writes is there after the call (member); not of a
# third local the pointer points to (third), nor of another member (other).
Held = ('struct CHeldView { CJob const *m_pJob = nullptr; CJob const *m_pSaved = nullptr; '
        'CHeldView *m_pOther = nullptr; void f_Write(CJob const *_pOld) { m_pOther->m_pJob = _pOld; } '
        'void f_WriteOther(CJob const *_pOld) { m_pOther->m_pSaved = _pOld; } };')
HeldRows = [
    ('member', 'CHeldView View, Other; Other.m_pJob = &g_Job; View.m_pOther = &Other; View.f_Write(&_Job);', True),
    ('third', 'CHeldView View, Other, Third; Other.m_pJob = &g_Job; View.m_pOther = &Third; View.f_Write(&_Job);',
     False),
    ('other', 'CHeldView View, Other; Other.m_pJob = &g_Job; View.m_pOther = &Other; View.f_WriteOther(&_Job);',
     False),
]
for Name, Before, Reported in HeldRows:
    nRow += 1
    Name = 'NHeld_' + Name
    emit('namespace %s {' % Name)
    emit(Held)
    emit('TCFuture<int> fg_Run(CJob const &_Job) { %s co_await fg_Suspend(); co_return Other.m_pJob->m_Value; }'
         % Before, Reported, Name)
    emit('} // namespace %s' % Name)

emit('// %d rows' % nRow)
print('\n'.join(Expected if Mode.endswith('expect') else Lines))
