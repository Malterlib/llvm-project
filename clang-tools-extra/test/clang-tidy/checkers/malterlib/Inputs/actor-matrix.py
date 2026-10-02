# Writes the matrix of the three actor checks: malterlib-actor-dispatch-this,
# malterlib-actor-phase and malterlib-continue-running-on-actor. `code` writes
# the code, `expect` the lines that are reported, a line and the check each.
#
# Each row is a namespace with an actor and one member function. What is
# expected comes from the rules (`answer` of each block), not from a run:
# - actor-dispatch-this: a closure that runs on another actor and uses what
#   designates the object of the current actor where it is captured, the
#   object itself or what points into it, is reported.
# - actor-phase: a change of an annotated collection of the actor's own object
#   is reported outside its phase, unless it runs in the constructor while the
#   object is made; a collection of a local copy is the copy's own.
# - continue-running-on-actor: a use of the object of the actor, or of what
#   points into it, after the coroutine may have switched to another actor and
#   not back, is reported.
#
# Where today's checks differ from the rule, the row says so and how it is
# expected, by TODAY: `// KNOWN-FALSE-POSITIVE: <reason>` (reported, the rule
# says clean; expected), `// RESTORE-MESSAGES: <check> <reason>` (the rule
# reports, the check doesn't; not expected), `// QUESTION: <question>` (a rule
# question that isn't decided; expected as the check is today).
# lbt/actor-matrix-check.sh counts the marks; the counts only go down.
import sys

Mode = sys.argv[1]
if Mode not in ('code', 'expect'):
    sys.exit('Unknown mode: ' + Mode)
Lines = []
Expected = []

Dispatch = 'malterlib-actor-dispatch-this'
Phase = 'malterlib-actor-phase'
Continue = 'malterlib-continue-running-on-actor'

# Today's deviations from the rule, by row name: (mark, reason). A row whose
# answer is a rule question is QUESTION whatever the check does.
TODAY = {
    'form_dispatch_wrapper_assigned_in_closure': ('RESTORE-MESSAGES', "what a closure that refers to the function object assigns it isn't followed (A2 keeps the old answer: Stream_Call.hpp's safe dispatch reported otherwise)"),
    'continue_record_helper_closure_after': ('RESTORE-MESSAGES', 'a record carrying the object, used in a closure called after the switch (A3c)'),
    'continue_record_read_closure_after': ('RESTORE-MESSAGES', 'a record carrying the object, used in a closure called after the switch (A3c)'),
    'continue_record_write_closure_after': ('RESTORE-MESSAGES', 'a record carrying the object, used in a closure called after the switch (A3c)'),
    'handle_dispatch_assigned_member_before': ('RESTORE-MESSAGES', 'a handle retargeted by a non-const member function (Q3: until the members a member function changes are told)'),
    'phase_pointer_assign_constructor': ('KNOWN-FALSE-POSITIVE', "an alias of the own object's state in its constructor (A4)"),
    'phase_pointer_clear_constructor': ('KNOWN-FALSE-POSITIVE', "an alias of the own object's state in its constructor (A4)"),
    'phase_pointer_move_constructor': ('KNOWN-FALSE-POSITIVE', "an alias of the own object's state in its constructor (A4)"),
    'phase_pointer_remove_constructor': ('KNOWN-FALSE-POSITIVE', "an alias of the own object's state in its constructor (A4)"),
    'phase_pointer_to_copy_assign_constructor': ('KNOWN-FALSE-POSITIVE', 'an alias of a local copy of the state (round 215 #1, A4)'),
    'phase_pointer_to_copy_assign_other': ('KNOWN-FALSE-POSITIVE', 'an alias of a local copy of the state (round 215 #1, A4)'),
    'phase_pointer_to_copy_assign_phase': ('KNOWN-FALSE-POSITIVE', 'an alias of a local copy of the state (round 215 #1, A4)'),
    'phase_pointer_to_copy_clear_constructor': ('KNOWN-FALSE-POSITIVE', 'an alias of a local copy of the state (round 215 #1, A4)'),
    'phase_pointer_to_copy_clear_other': ('KNOWN-FALSE-POSITIVE', 'an alias of a local copy of the state (round 215 #1, A4)'),
    'phase_pointer_to_copy_clear_phase': ('KNOWN-FALSE-POSITIVE', 'an alias of a local copy of the state (round 215 #1, A4)'),
    'phase_pointer_to_copy_move_constructor': ('KNOWN-FALSE-POSITIVE', 'an alias of a local copy of the state (round 215 #1, A4)'),
    'phase_pointer_to_copy_move_other': ('KNOWN-FALSE-POSITIVE', 'an alias of a local copy of the state (round 215 #1, A4)'),
    'phase_pointer_to_copy_move_phase': ('KNOWN-FALSE-POSITIVE', 'an alias of a local copy of the state (round 215 #1, A4)'),
    'phase_pointer_to_copy_remove_constructor': ('KNOWN-FALSE-POSITIVE', 'an alias of a local copy of the state (round 215 #1, A4)'),
    'phase_pointer_to_copy_remove_other': ('KNOWN-FALSE-POSITIVE', 'an alias of a local copy of the state (round 215 #1, A4)'),
    'phase_pointer_to_copy_remove_phase': ('KNOWN-FALSE-POSITIVE', 'an alias of a local copy of the state (round 215 #1, A4)'),
    'phase_reference_assign_constructor': ('KNOWN-FALSE-POSITIVE', "an alias of the own object's state in its constructor (A4)"),
    'phase_reference_clear_constructor': ('KNOWN-FALSE-POSITIVE', "an alias of the own object's state in its constructor (A4)"),
    'phase_reference_member_assign_other': ('RESTORE-MESSAGES', "a reference member of a local refers to the actor's state (A4)"),
    'phase_reference_member_clear_other': ('RESTORE-MESSAGES', "a reference member of a local refers to the actor's state (A4)"),
    'phase_reference_member_move_other': ('RESTORE-MESSAGES', "a reference member of a local refers to the actor's state (A4)"),
    'phase_reference_member_remove_other': ('RESTORE-MESSAGES', "a reference member of a local refers to the actor's state (A4)"),
    'phase_reference_move_constructor': ('KNOWN-FALSE-POSITIVE', "an alias of the own object's state in its constructor (A4)"),
    'phase_reference_remove_constructor': ('KNOWN-FALSE-POSITIVE', "an alias of the own object's state in its constructor (A4)"),
    'phase_reference_to_copy_assign_constructor': ('KNOWN-FALSE-POSITIVE', 'an alias of a local copy of the state (round 215 #1, A4)'),
    'phase_reference_to_copy_assign_other': ('KNOWN-FALSE-POSITIVE', 'an alias of a local copy of the state (round 215 #1, A4)'),
    'phase_reference_to_copy_assign_phase': ('KNOWN-FALSE-POSITIVE', 'an alias of a local copy of the state (round 215 #1, A4)'),
    'phase_reference_to_copy_clear_constructor': ('KNOWN-FALSE-POSITIVE', 'an alias of a local copy of the state (round 215 #1, A4)'),
    'phase_reference_to_copy_clear_other': ('KNOWN-FALSE-POSITIVE', 'an alias of a local copy of the state (round 215 #1, A4)'),
    'phase_reference_to_copy_clear_phase': ('KNOWN-FALSE-POSITIVE', 'an alias of a local copy of the state (round 215 #1, A4)'),
    'phase_reference_to_copy_move_constructor': ('KNOWN-FALSE-POSITIVE', 'an alias of a local copy of the state (round 215 #1, A4)'),
    'phase_reference_to_copy_move_other': ('KNOWN-FALSE-POSITIVE', 'an alias of a local copy of the state (round 215 #1, A4)'),
    'phase_reference_to_copy_move_phase': ('KNOWN-FALSE-POSITIVE', 'an alias of a local copy of the state (round 215 #1, A4)'),
    'phase_reference_to_copy_remove_constructor': ('KNOWN-FALSE-POSITIVE', 'an alias of a local copy of the state (round 215 #1, A4)'),
    'phase_reference_to_copy_remove_other': ('KNOWN-FALSE-POSITIVE', 'an alias of a local copy of the state (round 215 #1, A4)'),
    'phase_reference_to_copy_remove_phase': ('KNOWN-FALSE-POSITIVE', 'an alias of a local copy of the state (round 215 #1, A4)'),
}


def emit(Text):
    Lines.extend(Text.split('\n'))


def row(Name, Body, Reports, Question=None):
    """Writes the row `Name` with the code `Body`, whose lines that the rule
    reports end with `// @<check>`; Reports is the rule's answer (a list of
    checks, by the order of the marked lines)."""
    Mark = TODAY.get(Name)
    if Question:
        Mark = ('QUESTION', Question)
    Text = 'namespace N%s\n{\n%s\n}\n' % (Name, Body)
    Header = []
    if Mark:
        Kind, Reason = Mark
        if Kind == 'RESTORE-MESSAGES':
            Header.append('// RESTORE-MESSAGES: %s %s' % (Reports[0] if Reports else '', Reason))
        else:
            Header.append('// %s: %s' % (Kind, Reason))
    Start = len(Lines) + 1 + len(Header)
    emit('\n'.join(Header + [Text]))
    for Offset, Line in enumerate(Text.split('\n')):
        for Check in (Dispatch, Phase, Continue):
            if Line.endswith('// @' + Check):
                Reported = Check in Reports
                if Mark and Mark[0] == 'RESTORE-MESSAGES':
                    Reported = False
                if Mark and Mark[0] in ('KNOWN-FALSE-POSITIVE', 'QUESTION'):
                    Reported = True
                if Reported:
                    Expected.append('%d %s' % (Start + Offset, Check))


emit('''#include "malterlib-actor-phase.h"

static int g_Value = 0;
static int const gc_Value = 0;

struct CHolder
{
	int *m_p = nullptr;
};

// A future whose continuation is kept until its result is set.
template <typename t_FFunctor>
struct TCResultCall
{
	t_FFunctor m_fFunctor;
};
struct CWorkFuture
{
	template <typename t_FFunctor>
	void f_Set(TCResultCall<t_FFunctor> &&_Call);
	template <typename t_FFunctor>
	void operator > (t_FFunctor &&_fFunctor) &&
	{
		f_Set(TCResultCall<t_FFunctor>{static_cast<t_FFunctor &&>(_fFunctor)});
	}
};
CWorkFuture fg_Work();

inline void fg_SetPointer(int *&o_p, int *_pNew)
{
	o_p = _pNew;
}

inline int fg_ReadInt(int const &_Value)
{
	return _Value;
}
''')

# actor-dispatch-this: each carrier of the object into the closure, each use of
# it in the body, each actor it runs on.
# Carrier: name, the code before, the capture, the code between the capture
# and the end, an expression of the int it reaches, of the pointer it holds,
# whether it designates the object where it is captured.
Carriers = [
    ('this', '', 'this', '', 'm_Value', 'this', True),
    ('init_this', '', 'pThis = this', '', 'pThis->m_Value', 'pThis', True),
    ('member_address', '', 'p = &m_Value', '', '*p', 'p', True),
    ('member_reference', '', '&Value = m_Value', '', 'Value', '&Value', True),
    ('local_this', 'auto *p = this;', 'p', '', 'p->m_Value', 'p', True),
    ('replaced_before', 'int *p = &m_Value;\n\t\t\tp = &g_Value;', 'p', '', '*p', 'p', False),
    ('replaced_after', 'int *p = &m_Value;', 'p', '\t\t\tp = &g_Value;', '*p', 'p', True),
    ('one_branch', 'int *p = &g_Value;\n\t\t\tif (_b)\n\t\t\t\tp = &m_Value;', 'p', '', '*p', 'p', True),
    ('both_branches', 'int *p = &m_Value;\n\t\t\tif (_b)\n\t\t\t\tp = &g_Value;\n\t\t\telse\n\t\t\t\tp = nullptr;', 'p', '', '*p', 'p', False),
    ('set_visible', 'int *p = &m_Value;\n\t\t\tfg_SetPointer(p, &g_Value);', 'p', '', '*p', 'p', False),
    ('record', 'CHolder Holder{&m_Value};', 'Holder', '', '*Holder.m_p', 'Holder.m_p', True),
    ('record_overwritten', 'CHolder Holder{&m_Value};\n\t\t\tHolder.m_p = &g_Value;', 'Holder', '', '*Holder.m_p', 'Holder.m_p', False),
]
# Use: name, the body as a function of the int and the pointer, whether it
# uses what the pointer points to.
Uses = [
    ('read', lambda I, P: 'return %s;' % I, True),
    ('write', lambda I, P: '%s = 1;\n\t\t\t\t\treturn 0;' % I, True),
    ('helper', lambda I, P: 'return fg_ReadInt(%s);' % I, True),
    ('compare', lambda I, P: 'return %s != nullptr ? 1 : 0;' % P, False),
    ('number', lambda I, P: 'return int(reinterpret_cast<__UINTPTR_TYPE__>(%s));' % P, False),
]
# Target: name, the code before the dispatch, the dispatcher, whether it is
# another actor.
Targets = [
    ('other', '', 'g_Dispatch(_Other)', True),
    ('own', '', 'g_Dispatch(fg_ThisActor(this))', False),
    ('own_changed_before', 'auto Actor = fg_ThisActor(this);\n\t\t\tActor = _Other;', 'g_Dispatch(Actor)', True),
    ('own_changed_after', 'auto Actor = fg_ThisActor(this);', 'g_Dispatch(Actor)', False),
]


def dispatchRow(Name, Carrier, Use, Target, Before='', After='', Reach=None, Runs=True):
    CName, CBefore, Capture, CAfter, Int, Pointer, Designates = Carrier
    UName, UBody, UsesPointee = Use
    TName, TBefore, Dispatcher, Other = Target
    Code = []
    for Part in (CBefore, TBefore, Before):
        if Part:
            Code.append('\t\t\t' + Part)
    Dispatched = '(void)(%s / [%s] // @%s\n\t\t\t\t{\n\t\t\t\t\t%s\n\t\t\t\t});' % (
        Dispatcher, Capture, Dispatch, UBody(Int, Pointer))
    if Reach:
        Dispatched = Reach(Dispatched)
    Code.append('\t\t\t' + Dispatched)
    if TName == 'own_changed_after':
        Code.append('\t\t\tActor = _Other;')
    for Part in (CAfter, After):
        if Part:
            Code.append(Part if Part.startswith('\t') else '\t\t\t' + Part)
    Body = '''	enum class EKind
	{
		A,
		B
	};
	struct CApp : CActor
	{
		int m_Value = 0;

		void f_Row(TCActor<> _Other, bool _b, EKind _Kind)
		{
			(void)_b;
			(void)_Kind;
%s
		}
	};''' % '\n'.join(Code)
    Reported = Other and Designates and UsesPointee and Runs
    Question = None
    if Other and Designates and not UsesPointee and Runs and CName not in ('record',):
        Question = 'RQ1 (an identity-only use of what designates the object)'
    row(Name, Body, [Dispatch] if Reported else [], Question)


for Carrier in Carriers:
    for Use in Uses:
        for Target in Targets:
            dispatchRow('dispatch_%s_%s_%s' % (Carrier[0], Use[0], Target[0]), Carrier, Use, Target)

# Reach of the dispatch: the rule reports only a dispatch that can run.
This, Read, Other = Carriers[0], Uses[0], Targets[0]
Reaches = [
    ('runs', None, True),
    ('after_return', lambda D: 'return;\n\t\t\t' + D, False),
    ('constant_false', lambda D: 'if constexpr (false)\n\t\t\t\t' + D, False),
    ('constant_condition', lambda D: 'constexpr bool c_bUse = false;\n\t\t\tif (c_bUse)\n\t\t\t\t' + D, False),
    ('runtime_branch', lambda D: 'if (_b)\n\t\t\t\t' + D, True),
    ('after_maybe_return', lambda D: 'if (_b)\n\t\t\t\treturn;\n\t\t\t' + D, True),
    ('enum_switch_after', lambda D: 'switch (_Kind)\n\t\t\t{\n\t\t\tcase EKind::A:\n\t\t\t\treturn;\n\t\t\tcase EKind::B:\n\t\t\t\treturn;\n\t\t\t}\n\t\t\t' + D, True),
    ('enum_switch_default', lambda D: 'switch (_Kind)\n\t\t\t{\n\t\t\tcase EKind::A:\n\t\t\t\treturn;\n\t\t\tcase EKind::B:\n\t\t\t\treturn;\n\t\t\tdefault:\n\t\t\t\t' + D + '\n\t\t\t\treturn;\n\t\t\t}', True),
    ('switch_default_returns', lambda D: 'switch (_Kind)\n\t\t\t{\n\t\t\tcase EKind::A:\n\t\t\t\treturn;\n\t\t\tdefault:\n\t\t\t\treturn;\n\t\t\t}\n\t\t\t' + D, False),
    ('bool_switch_after', lambda D: 'switch (int(_b))\n\t\t\t{\n\t\t\tcase 0:\n\t\t\t\treturn;\n\t\t\tdefault:\n\t\t\t\treturn;\n\t\t\t}\n\t\t\t' + D, False),
    ('closure_made_never', lambda D: 'if constexpr (false)\n\t\t\t{\n\t\t\t\tauto fRun = [&]\n\t\t\t\t\t{\n\t\t\t\t\t\t' + D + '\n\t\t\t\t\t}\n\t\t\t\t;\n\t\t\t\tfRun();\n\t\t\t}', False),
    ('closure_made_after_return', lambda D: 'return;\n\t\t\tauto fRun = [&]\n\t\t\t\t{\n\t\t\t\t\t' + D + '\n\t\t\t\t}\n\t\t\t;\n\t\t\tfRun();', False),
    ('closure_called_maybe', lambda D: 'auto fRun = [&]\n\t\t\t\t{\n\t\t\t\t\t' + D + '\n\t\t\t\t}\n\t\t\t;\n\t\t\tif (_b)\n\t\t\t\tfRun();', True),
]
for Name, Reach, Runs in Reaches:
    dispatchRow('reach_dispatch_%s' % Name, This, Read, Other, Reach=Reach, Runs=Runs)

# The forms that run a closure on another actor, and function objects that
# hold the closure on the way.
Forms = [
    ('operator_actor', 'auto Actor = _Other;', '(void)(Actor / [this] { return m_Value; }); // @' + Dispatch, True),
    ('fg_dispatch', '', '(void)fg_Dispatch(_Other, [this] { return m_Value; }); // @' + Dispatch, True),
    ('concurrent', '', '(void)(g_ConcurrentDispatch / [this] { return m_Value; }); // @' + Dispatch, True),
    ('fg_concurrent', '', '(void)fg_ConcurrentDispatch([this] { return m_Value; }); // @' + Dispatch, True),
    ('wrapper', 'TCFunction<int ()> fRead = [this] { return m_Value; }; // @' + Dispatch, '(void)(g_Dispatch(_Other) / fg_Move(fRead));', True),
    ('wrapper_cleared', 'TCFunction<int ()> fRead = [this] { return m_Value; }; // @' + Dispatch + '\n\t\t\tfRead.f_Clear();', 'if (fRead)\n\t\t\t\t(void)(g_Dispatch(_Other) / fg_Move(fRead));', False),
    ('wrapper_cleared_maybe', 'TCFunction<int ()> fRead = [this] { return m_Value; }; // @' + Dispatch + '\n\t\t\tif (_b)\n\t\t\t\tfRead.f_Clear();', '(void)(g_Dispatch(_Other) / fg_Move(fRead));', True),
    ('wrapper_replaced', 'TCFunction<int ()> fRead = [this] { return m_Value; }; // @' + Dispatch + '\n\t\t\tfRead = [] { return 0; };', '(void)(g_Dispatch(_Other) / fg_Move(fRead));', False),
    ('wrapper_alias_assigned', 'TCFunction<int ()> fRead;\n\t\t\tauto &fAlias = fRead;\n\t\t\tfAlias = [this] { return m_Value; }; // @' + Dispatch, '(void)(g_Dispatch(_Other) / fg_Move(fRead));', True),
    ('wrapper_assigned_after', 'TCFunction<int ()> fRead = [] { return 0; };', '(void)(g_Dispatch(_Other) / TCFunction<int ()>(fRead));\n\t\t\tfRead = [this] { return m_Value; }; // @' + Dispatch, False),
    ('wrapper_assigned_in_closure', 'TCFunction<int ()> fRead;\n\t\t\tauto fSet = [&]\n\t\t\t\t{\n\t\t\t\t\tfRead = [this] { return m_Value; }; // @' + Dispatch + '\n\t\t\t\t}\n\t\t\t;\n\t\t\tfSet();', '(void)(g_Dispatch(_Other) / fg_Move(fRead));', True),
]
for Name, Before, Dispatched, Reported in Forms:
    Body = '''	struct CApp : CActor
	{
		int m_Value = 0;

		void f_Row(TCActor<> _Other, bool _b)
		{
			(void)_b;
%s
			%s
		}
	};''' % (('\t\t\t' + Before) if Before else '', Dispatched)
    row('form_dispatch_%s' % Name, Body, [Dispatch] if Reported else [])

# The actor a handle is of where it is read, and the object a parameter that
# is captured points to there.
Handles = [
    ('assigned_member_before', 'auto Actor = fg_ThisActor(this);\n\t\t\tActor.f_Assign(_Other);\n\t\t\t(void)(g_Dispatch(Actor) / [this] { return m_Value; }); // @' + Dispatch, True),
    ('assigned_member_after', 'auto Actor = fg_ThisActor(this);\n\t\t\t(void)(g_Dispatch(Actor) / [this] { return m_Value; }); // @' + Dispatch + '\n\t\t\tActor.f_Assign(_Other);', False),
]
for Name, Code, Reported in Handles:
    row('handle_dispatch_%s' % Name, '''	struct CApp : CActor
	{
		int m_Value = 0;

		void f_Row(TCActor<> _Other)
		{
			%s
		}
	};''' % Code, [Dispatch] if Reported else [])
Objects = [
    ('one_branch', 'CApp *pActor = _pOther;\n\t\tif (_b)\n\t\t\tpActor = _pOwn;', True),
    ('replaced', 'CApp *pActor = _pOther;\n\t\tpActor = _pOwn;', False),
]
for Name, Code, Reported in Objects:
    row('object_dispatch_%s' % Name, '''	struct CApp : CActor
	{
		int m_Value = 0;
	};
	void fg_Row(CApp *_pOwn, CApp *_pOther, bool _b)
	{
		(void)_b;
		%s
		(void)fg_Dispatch(pActor->self, [_pOwn] { return _pOwn->m_Value; }); // @%s
	}''' % (Code, Dispatch), [Dispatch] if Reported else [])

# actor-phase: each way to the collection, each change of it, each context.
Aliases = [
    ('direct', '', 'mp_State.m_Items', True),
    ('reference', 'auto &State = mp_State;', 'State.m_Items', True),
    ('pointer', 'auto *pState = &mp_State;', 'pState->m_Items', True),
    ('copy', 'auto Copy = mp_State;', 'Copy.m_Items', False),
    ('reference_to_copy', 'auto Copy = mp_State;\n\t\t\tauto &State = Copy;', 'State.m_Items', False),
    ('pointer_to_copy', 'auto Copy = mp_State;\n\t\t\tauto *pState = &Copy;', 'pState->m_Items', False),
    ('reference_member', 'CStateRef Ref{mp_State};', 'Ref.m_State.m_Items', True),
]
Changes = [
    ('clear', '%s.f_Clear();'),
    ('remove', '%s.f_Remove(1);'),
    ('assign', '%s = {};'),
    ('move', 'auto Moved = fg_Move(%s);\n\t\t\t(void)Moved;'),
]
Contexts = [
    ('other', 'void f_Row()', True),
    ('phase', 'DMibActorPhase(Destroy) void f_Row()', False),
    ('constructor', 'CApp(int)', False),
]


def phaseRow(Name, Body, Reported):
    row(Name, '''	struct CState
	{
		DMibActorEntries(Destroy) TCMap<int, int> m_Items;
	};
	struct CStateRef
	{
		CState &m_State;
	};
	struct CApp : CPhaseActor
	{
		CState mp_State;

%s
	};''' % Body, [Phase] if Reported else [])


for AName, ABefore, Path, Own in Aliases:
    for CName, Change in Changes:
        for XName, Signature, Outside in Contexts:
            Lines2 = []
            if ABefore:
                Lines2.append('\t\t\t' + ABefore)
            Changed = (Change % Path).split('\n')
            Changed[0] += ' // @' + Phase
            Lines2.append('\t\t\t' + '\n'.join(Changed))
            phaseRow('phase_%s_%s_%s' % (AName, CName, XName),
                     '\t\t%s\n\t\t{\n%s\n\t\t}' % (Signature, '\n'.join(Lines2)),
                     Own and Outside)

# The callbacks a constructor makes: run before it returns, or kept to run
# later.
Guards = [
    ('scope_exit_operator', 'auto Cleanup = g_OnScopeExit / [this]\n\t\t\t\t{\n\t\t\t\t\tmp_State.m_Items.f_Clear(); // @%s\n\t\t\t\t}\n\t\t\t;' % Phase, False),
    ('scope_exit_function', 'auto Cleanup = fg_OnScopeExit([this]\n\t\t\t\t{\n\t\t\t\t\tmp_State.m_Items.f_Clear(); // @%s\n\t\t\t\t});' % Phase, False),
    ('scope_exit_shared', 'auto Cleanup = g_OnScopeExitShared / [this]\n\t\t\t\t{\n\t\t\t\t\tmp_State.m_Items.f_Clear(); // @%s\n\t\t\t\t}\n\t\t\t;' % Phase, True),
    ('scope_exit_kept', 'auto Cleanup = g_OnScopeExit / [this]\n\t\t\t\t{\n\t\t\t\t\tmp_State.m_Items.f_Clear(); // @%s\n\t\t\t\t}\n\t\t\t;\n\t\t\tmp_Cleanup = fg_Move(Cleanup);' % Phase, True),
    ('called_local', 'auto fInit = [this]\n\t\t\t\t{\n\t\t\t\t\tmp_State.m_Items.f_Clear(); // @%s\n\t\t\t\t}\n\t\t\t;\n\t\t\tfInit();' % Phase, False),
    ('continuation', 'fg_Work() > [this]\n\t\t\t\t{\n\t\t\t\t\tmp_State.m_Items.f_Clear(); // @%s\n\t\t\t\t}\n\t\t\t;' % Phase, True),
    ('kept_in_member', 'mp_fLater = [this]\n\t\t\t\t{\n\t\t\t\t\tmp_State.m_Items.f_Clear(); // @%s\n\t\t\t\t};' % Phase, True),
]
for Name, Code, Reported in Guards:
    row('phase_guard_%s' % Name, '''	struct CState
	{
		DMibActorEntries(Destroy) TCMap<int, int> m_Items;
	};
	struct CApp : CPhaseActor
	{
		CState mp_State;
		COnScopeExit mp_Cleanup;
		TCFunction<void ()> mp_fLater;

		CApp(int)
		{
			%s
		}
	};''' % Code, [Phase] if Reported else [])
row('phase_guard_member_initializer_kept', '''	struct CState
	{
		DMibActorEntries(Destroy) TCMap<int, int> m_Items;
	};
	struct CApp : CPhaseActor
	{
		CState mp_State;
		TCFunction<void ()> mp_fLater = [this]
			{
				mp_State.m_Items.f_Clear(); // @%s
			}
		;
	};''' % Phase, [Phase])
row('phase_guard_member_initializer_called', '''	struct CState
	{
		DMibActorEntries(Destroy) TCMap<int, int> m_Items;
	};
	struct CApp : CPhaseActor
	{
		CState mp_State;
		int mp_Made = [this]
			{
				mp_State.m_Items.f_Clear(); // @%s
				return 0;
			}()
		;
	};''' % Phase, [])

# continue-running-on-actor: each carrier of the object, each use, each place
# of the use against the switches.
CarriersContinue = [
    ('this', '', 'm_Value', 'this', True),
    ('local_this', 'auto *p = this;', 'p->m_Value', 'p', True),
    ('member_address', 'int *p = &m_Value;', '*p', 'p', True),
    ('replaced_before', 'int *p = &m_Value;\n\t\t\tp = &g_Value;', '*p', 'p', False),
    ('one_branch', 'int *p = &g_Value;\n\t\t\tif (_b)\n\t\t\t\tp = &m_Value;', '*p', 'p', True),
    ('set_visible', 'int *p = &m_Value;\n\t\t\tfg_SetPointer(p, &g_Value);', '*p', 'p', False),
    ('record', 'CHolder Holder{&m_Value};', '*Holder.m_p', 'Holder.m_p', True),
    ('record_overwritten', 'CHolder Holder{&m_Value};\n\t\t\tHolder.m_p = &g_Value;', '*Holder.m_p', 'Holder.m_p', False),
]
UsesContinue = [
    ('read', lambda I, P: 'int Value = %s;' % I, True),
    ('write', lambda I, P: '%s = 1;\n\t\t\tint Value = 0;' % I, True),
    ('helper', lambda I, P: 'int Value = fg_ReadInt(%s);' % I, True),
    ('compare', lambda I, P: 'int Value = %s != nullptr ? 1 : 0;' % P, False),
]
Wheres = [
    ('before', lambda U: U + '\n\t\t\tco_await fg_ContinueRunningOnActor(_Other);', False),
    ('after', lambda U: 'co_await fg_ContinueRunningOnActor(_Other);\n\t\t\t' + U, True),
    ('after_maybe', lambda U: 'if (_b)\n\t\t\t\tco_await fg_ContinueRunningOnActor(_Other);\n\t\t\t' + U, True),
    ('after_back', lambda U: 'auto Own = fg_ThisActor(this);\n\t\t\tco_await fg_ContinueRunningOnActor(_Other);\n\t\t\tco_await fg_ContinueRunningOnActor(Own);\n\t\t\t' + U, False),
    # Reported where the closure is called, which uses the object there.
    ('closure_after', lambda U: 'auto fUse = [&]\n\t\t\t\t{\n\t\t\t\t\t' + U.replace(' // @' + Continue, '').replace('\n\t\t\t', '\n\t\t\t\t\t') + '\n\t\t\t\t\treturn Value;\n\t\t\t\t}\n\t\t\t;\n\t\t\tco_await fg_ContinueRunningOnActor(_Other);\n\t\t\tint Value = fUse(); // @' + Continue, True),
]
for CName, CBefore, Int, Pointer, Designates in CarriersContinue:
    for UName, UBody, UsesPointee in UsesContinue:
        for WName, Where, After in Wheres:
            Use = UBody(Int, Pointer).split('\n')
            Use[0] += ' // @' + Continue
            Code = Where('\n'.join(Use))
            Body = '''	struct CApp : CActor
	{
		int m_Value = 0;

		TCFuture<int> f_Row(TCActor<> _Other, bool _b)
		{
			(void)_b;
%s
			%s
			co_return Value;
		}
	};''' % (('\t\t\t' + CBefore) if CBefore else '', Code)
            row('continue_%s_%s_%s' % (CName, UName, WName), Body,
                [Continue] if Designates and UsesPointee and After else [])

if Mode == 'code':
    print('\n'.join(Lines))
else:
    print('\n'.join(Expected))
