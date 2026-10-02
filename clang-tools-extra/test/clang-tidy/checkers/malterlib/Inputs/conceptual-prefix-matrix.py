# Writes the conceptual prefix matrix: for each piece of evidence a class can
# give a concept by, in each state that evidence can be in, whether a name of
# that type is required to have the concept's prefix and whether one with it is
# reported as not being one. `code` writes the code, `expect` the diagnostics,
# a line, the kind and the message each: the warnings, the note of each that an
# instantiation decides, and no error, so that a row that doesn't compile fails.
#
# The expectations follow the rule, not what the check does: a prefix is
# required only where its evidence is known to hold whatever the template
# arguments are, and reported as wrong only where the evidence is known not to
# hold; where it can't be told, every prefix is allowed and none is required.
# The iterator and pointer concepts are read by what code can use (a deleted
# member, or one whose constraint is known false, is no evidence; one whose
# constraint clang never evaluated is unknown); the function object concept by
# what a class declares, whatever its state. Access isn't read. A name of
# SmartPointerTypes decides for the class of that name; a class derived from
# one is read by what it can do.
#
# The dimensions: the evidence (an iterator's `operator++` or unary
# `operator*`, a free `operator*` that is a hidden friend, of the class's
# namespace, of a base's namespace taking the base, or a friend template of
# another associated class; a pointer's `operator->`, its conversion to bool,
# a class SmartPointerTypes names exactly; a function object's call operator
# or conversion to a function pointer), the state (plain, deleted, constrained
# true, false, by a parameter of the class nothing uses or something uses, a
# member template, one constrained by its own parameter, inherited, inherited
# deleted, brought in with `using`, the same of a deleted one, hiding a usable
# one of a base, private, missing, not defined, deleted beside another usable
# evidence), and the context (a variable of a specialization, a parameter of a
# function template given a specialization of the parameters, which only
# requires, and an instance of a function template given it, which only
# rejects). Each row is a namespace of its own.
#
# A row the check doesn't report yet and should is written with a
# `// RESTORE-MESSAGES: malterlib-conceptual-prefix <reason>` comment and not
# expected; one it reports and shouldn't is written with
# `// KNOWN-FALSE-POSITIVE: <reason>` and expected as it is reported; one that
# waits for a decision of the user is written with `// QUESTION: <question>`
# and expected as it is reported today. The gate counts the three
# (lbt/cp-matrix-check.sh); the counts only go down.
import sys

Mode = sys.argv[1]
if Mode not in ('code', 'expect'):
    sys.exit('Unknown mode: ' + Mode)

Y, N, U = 'yes', 'no', 'unknown'

# The concepts: the prefix, the name a class of the concept is given, and what
# the diagnostic calls it.
CONCEPTS = {
    'i': ('Iterator', 'an iterator'),
    'p': ('Pointer', 'a pointer'),
    'f': ('Functor', 'a function object'),
}

# The evidence: the concept, how it is read, and the member that gives it as a
# declaration without its end, given the class's name.
MEMBER_EVIDENCE = {
    'increment': ('i', 'usable', lambda C: '%s &operator++()' % C, '++%s'),
    'dereference': ('i', 'usable', lambda C: 'int &operator*() const', '(void)*%s'),
    'arrow': ('p', 'usable', lambda C: 'int *operator->() const', '(void)%s.operator->()'),
    'conversion': ('p', 'usable', lambda C: 'explicit operator bool() const', '(void)static_cast<bool>(%s)'),
    'arrow-i': ('i', 'usable', lambda C: 'int *operator->() const', '(void)%s.operator->()'),
    'dereference-p': ('p', 'usable', lambda C: 'int &operator*() const', '(void)*%s'),
    # A conversion to what converts to bool in a condition, not to bool, can
    # make a pointer: unknown at most (decided Q-D).
    'pointer-conversion': ('p', 'contextual', lambda C: 'explicit operator int *() const',
                           '(void)static_cast<int *>(%s)'),
    'number-conversion': ('p', 'contextual', lambda C: 'explicit operator int() const', '(void)static_cast<int>(%s)'),
    'call': ('f', 'declared', lambda C: 'void operator()() const', '%s()'),
    'function-conversion': ('f', 'declared', lambda C: 'operator FCall *() const', '(void)static_cast<FCall *>(%s)'),
}

STATES = ['plain', 'deleted', 'requires-true', 'requires-false', 'requires-folded', 'requires-call', 'requires-call-used',
          'dependent-unused', 'dependent-used', 'template',
          'template-constrained', 'template-undeducible', 'template-deducing-this', 'inherited', 'inherited-deleted', 'using', 'using-deleted', 'hiding', 'private',
          'missing', 'undefined', 'other-deleted']


def answer(Reading, State, Context):
    # What the rule says the evidence is, in a context: the pattern of a class
    # template can't tell a constraint of its parameters, whatever uses an
    # instance.
    if Reading == 'declared':
        return {'missing': N, 'undefined': U}.get(State, Y)
    if Reading == 'declared-not-template':
        return {'missing': N, 'undefined': U, 'template': N, 'template-constrained': N, 'template-undeducible': N,
                'template-deducing-this': N}.get(State, Y)
    # A member template a use can't give a parameter, one without a default
    # that no parameter of the function names, can't be told without
    # instantiating it; one an explicit object parameter gives is used.
    # A constraint that calls a function is checked by clang only where it is
    # used; nothing evaluates it otherwise. One written with nothing to run is
    # known.
    Table = {'plain': Y, 'deleted': N, 'requires-true': Y, 'requires-false': N, 'requires-folded': N,
             'requires-call': U, 'requires-call-used': Y, 'dependent-unused': U,
             'dependent-used': Y, 'template': Y, 'template-constrained': U, 'template-undeducible': U,
             'template-deducing-this': Y, 'inherited': Y, 'inherited-deleted': N, 'using': Y, 'using-deleted': N,
             'hiding': N, 'private': Y, 'missing': N, 'undefined': U, 'other-deleted': Y}
    Answer = Table[State]
    if Reading == 'contextual' and Answer == Y:
        Answer = U
    if Context == 'pattern' and State in ('dependent-used', 'requires-call-used'):
        return U
    return Answer


# Where today's check differs from the rule, by row: whether it requires the
# concept of the unprefixed name and rejects the prefixed one today, and why.
# Each is a cell of agents/CONCEPTUAL-PREFIX-INVENTORY.md. None of the member
# evidence differs now.
TODAY = {}

Lines = []
Expected = []


def emit(Text):
    Lines.append(Text)


def line():
    return len(Lines) + 1


def warning(Line, Message):
    return '%d warning: %s [malterlib-conceptual-prefix]' % (Line, Message)


def instantiationNote(Line, Namespace, Class):
    # The note of a report an instantiation decides, at the use that makes it.
    return ('%d note: in instantiation of function template specialization \'%s::fg_Take<%s::%s<true>>\' requested '
            'here' % (Line, Namespace, Namespace, Class))


def required(Variable, Concept, Context):
    return "'%s' is %s and should have the '%s' concept prefix after its storage prefix" % (
        Variable, CONCEPTS[Concept][1], Concept)


def rejected(Variable, Concept, Context):
    return "'%s' has the '%s' concept prefix of %s, but is not one" % (Variable, Concept, CONCEPTS[Concept][1])


def row(Name, Concept, Answer, Context, Writes):
    # The declarations of a row and what they expect, by the rule; one the
    # check differs on today (TODAY) is marked, and expected as it is
    # reported.
    RuleRequire = Answer == Y and Context != 'instance'
    RuleReject = Answer == N and Context != 'pattern'
    TodayRequire, TodayReject, Reason = TODAY.get(Name, (RuleRequire, RuleReject, None))
    for Part, Rule, Today, Message in ((Writes[0], RuleRequire, TodayRequire, required),
                                       (Writes[1], RuleReject, TodayReject, rejected)):
        if not Part:
            continue
        Variable, Text = Part
        if Rule != Today:
            Text += (' // KNOWN-FALSE-POSITIVE: %s' % Reason) if Today else (
                ' // RESTORE-MESSAGES: malterlib-conceptual-prefix %s' % Reason)
        emit(Text)
        if Today:
            Expected.append(warning(line() - 1, Message(Variable, Concept, Context)))
    return TodayReject


def member(Evidence, State, Class):
    # The class template \p Class with \p Evidence in \p State, and the bases it
    # needs before it.
    Concept, Reading, Declaration, Use = MEMBER_EVIDENCE[Evidence]
    Decl = Declaration(Class)
    Base = 'C' + Class[2:] + 'Base'
    Before = []
    Body = []
    Bases = ''
    if State == 'plain':
        Body.append(Decl + ';')
    elif State == 'deleted':
        Body.append(Decl + ' = delete;')
    elif State == 'requires-true':
        Body.append(Decl + ' requires true;')
    elif State == 'requires-false':
        Body.append(Decl + ' requires false;')
    elif State == 'requires-folded':
        Body.append(Decl + ' requires (sizeof(int) > 64);')
    elif State in ('requires-call', 'requires-call-used'):
        Body.append(Decl + ' requires (fg_Holds());')
    elif State in ('dependent-unused', 'dependent-used'):
        Body.append(Decl + ' requires t_bEnabled;')
    elif State == 'template':
        Body.append('template <typename tf_CType = int>')
        Body.append(Decl + ';')
    elif State == 'template-constrained':
        Body.append('template <typename tf_CType = int>')
        Body.append('\trequires (sizeof(tf_CType) > 64)')
        Body.append(Decl + ';')
    elif State == 'template-undeducible':
        Body.append('template <typename tf_CType>')
        Body.append(Decl + ';')
    elif State == 'template-deducing-this':
        Qualified = Decl.endswith(' const')
        Unqualified = Decl[:-len(' const')] if Qualified else Decl
        assert Unqualified.endswith('()')
        Body.append('template <typename tf_CSelf>')
        Body.append('%s(this tf_CSelf %s_Self);' % (Unqualified[:-2], 'const &' if Qualified else '&'))
    elif State in ('inherited', 'inherited-deleted', 'using', 'using-deleted', 'hiding'):
        Deleted = ' = delete' if State in ('inherited-deleted', 'using-deleted') else ''
        Before += ['struct %s' % Base, '{', '\t' + Declaration(Base) + Deleted + ';', '};']
        Bases = ' : %s' % Base
        Name = 'operator()' if Evidence == 'call' else Decl[Decl.index('operator'):Decl.index('(')]
        if State in ('using', 'using-deleted'):
            Body.append('using %s::%s;' % (Base, Name))
        elif State == 'hiding':
            Body.append(Decl + ' = delete;')
    elif State == 'private':
        Body.append('private:')
        Body.append(Decl + ';')
    elif State == 'missing':
        pass
    elif State == 'other-deleted':
        Body.append(Decl + ' = delete;')
        Body.append('explicit operator bool() const;')
    Text = Before + ['template <bool t_bEnabled>']
    if State == 'undefined':
        Text.append('struct %s;' % Class)
    else:
        Text += ['struct %s%s' % (Class, Bases), '{'] + ['\t' + Each for Each in Body] + ['};']
    return Text, Use


Rows = []
for Evidence, (Concept, Reading, _, _) in MEMBER_EVIDENCE.items():
    for State in STATES:
        if State == 'other-deleted' and Evidence != 'arrow':
            continue
        Rows.append((Evidence, Concept, Reading, State))

emit('// Generated by conceptual-prefix-matrix.py; see its comments for what each row is.')
emit('using FCall = void ();')
emit('constexpr bool fg_Holds()')
emit('{')
emit('\treturn true;')
emit('}')
for Evidence, Concept, Reading, State in Rows:
    Suffix = CONCEPTS[Concept][0]
    Ident = ''.join(Part.capitalize() for Part in (Evidence + '-' + State).split('-'))
    Class = 'TC' + Ident + Suffix
    Prefix = Concept
    for Context in ('variable', 'pattern', 'instance'):
        if Concept == 'f' and Context == 'instance':
            # A parameter named as a function object whose type is a
            # template parameter is told by the parameter's name.
            continue
        Name = 'N%s_%s_%s' % (Evidence.replace('-', '_'), State.replace('-', '_'), Context)
        # A conversion template, which clang never calls through, gives no
        # function object.
        Answer = answer('declared-not-template' if Evidence == 'function-conversion' else Reading,
                        'other-deleted' if State == 'other-deleted' else State, Context)
        if Evidence == 'arrow' and State == 'other-deleted':
            Answer = Y
        emit('namespace %s' % Name)
        emit('{')
        Text, Use = member(Evidence, State, Class)
        for Each in Text:
            emit('\t' + Each)
        if Context == 'variable':
            Storage = 'extern ' if State == 'undefined' else ''
            row(Name, Concept, Answer, Context,
                (('g_Value', '\t%s%s<true> g_Value;' % (Storage, Class)),
                 ('g_%sValue' % Prefix, '\t%s%s<true> g_%sValue;' % (Storage, Class, Prefix))))
            if State in ('dependent-used', 'requires-call-used'):
                emit('\tinline void fg_Use()')
                emit('\t{')
                emit('\t\t' + (Use % 'g_Value') + ';')
                emit('\t}')
        elif Context == 'pattern':
            # The pattern only requires; the prefixed parameter is never
            # reported there.
            emit('\ttemplate <bool tf_bEnabled>')
            emit('\tvoid fg_Take(')
            row(Name, Concept, Answer, Context,
                (('_Value', '\t\t%s<tf_bEnabled> const &_Value,' % Class), None))
            emit('\t\t%s<tf_bEnabled> const &_%sValue' % (Class, Prefix))
            emit('\t);')
        else:
            Storage = 'extern ' if State == 'undefined' else ''
            emit('\t%s%s<true> mc_Value;' % (Storage, Class))
            if State in ('dependent-used', 'requires-call-used'):
                emit('\tinline void fg_Use()')
                emit('\t{')
                emit('\t\t' + (Use % 'mc_Value') + ';')
                emit('\t}')
            emit('\ttemplate <typename tf_CType>')
            Rejected = row(Name, Concept, Answer, Context,
                           (None, ('_%sValue' % Prefix, '\tvoid fg_Take(tf_CType const &_%sValue)' % Prefix)))
            emit('\t{')
            emit('\t}')
            emit('\tinline void fg_Run()')
            emit('\t{')
            emit('\t\tfg_Take(mc_Value);')
            if Rejected:
                Expected.append(instantiationNote(line() - 1, Name, Class))
            emit('\t}')
        emit('}')

# The free operators by the form of their operand: whether the operand can take
# an object of the class. Of a non-member operator, one that names the class or
# a base of it does, every specialization (`all`) or only the one it names
# (`one`), and one that names another class doesn't (`none`); of an operator
# template that nothing instantiates for the class, one whose operand deduction
# can make the class, also by naming it outright, is unknown, as clang decides
# it only where it is used; one instantiated for it does, also for every
# specialization when the specialization's operand is a base all of them have.
# Each form as a non-member operator, `int &operator*(...)` or `void
# operator++(...)`, a hidden friend where it names the class by its own name.
# The form: the operand given the class name and the operator's parameter
# type, whether it is a template, whose operand it takes, whether it can be
# instantiated for the class, and the declarations it needs before the class
# and as its bases.
OPERAND_FORMS = {
    'class': (lambda C, R: '%s<true> %s' % (C, R), False, 'one', False),
    'value': (lambda C, R: '%s<true>' % C, False, 'one', False),
    'injected': (lambda C, R: '%s %s' % (C, R), False, 'all', False),
    'base': (lambda C, R: 'C%sBase %s' % (C[2:], R), False, 'all', False),
    'unrelated': (lambda C, R: 'C%sOther %s' % (C[2:], R), False, 'none', False),
    'template-class': (lambda C, R: '%s<true> %s' % (C, R), True, 'one', True),
    'template-value': (lambda C, R: '%s<true>' % C, True, 'one', True),
    'template-injected': (lambda C, R: '%s %s' % (C, R), True, 'one', True),
    'template-base': (lambda C, R: 'C%sBase %s' % (C[2:], R), True, 'all', True),
    'template-parameter': (lambda C, R: 'tf_CType %s' % R, True, 'one', True),
    'template-forwarding': (lambda C, R: 'tf_CType &&', True, 'one', True),
    'template-specialization': (lambda C, R: '%s<tf_bEnabled> %s' % (C, R), True, 'one', True),
    'template-base-specialization': (lambda C, R: 'TC%sBaseTemplate<tf_CType> %s' % (C[2:], R), True, 'all', True),
    'template-template': (lambda C, R: 'tf_TTemplate<true> %s' % R, True, 'one', True),
    'template-not-deduced': (lambda C, R: 'typename TC%sIdentity<tf_CType>::CType %s' % (C[2:], R), True, 'one',
                             False),
    'template-unrelated': (lambda C, R: 'TC%sOtherTemplate<tf_CType> %s' % (C[2:], R), True, 'none', False),
}

# The operators: the concept, the operator's declaration given its operand, the
# reference the operand is taken by, and the use that instantiates it.
FREE_OPERATORS = {
    'dereference-i': ('i', lambda Operand: 'int &operator*(%s)' % Operand, 'const &', '(void)*%s'),
    'increment-i': ('i', lambda Operand: 'void operator++(%s)' % Operand, '&', '++%s'),
    'dereference-p': ('p', lambda Operand: 'int &operator*(%s)' % Operand, 'const &', '(void)*%s'),
}


def operandAnswer(Form, Instantiated, Context):
    # What the rule says the free operator gives the class, in a context: the
    # pattern only knows what holds whatever the template arguments are.
    _, Template, Takes, _ = OPERAND_FORMS[Form]
    if Takes == 'none':
        return N
    if Template and not Instantiated:
        return U
    if Context == 'pattern' and Takes != 'all':
        return U
    return Y


def freeOperator(Form, Operator, Class, Instantiated):
    # The class template \p Class with a free operator of \p Form, and what it
    # needs: the base, the other class, the template the operand names.
    Concept, Declaration, Reference, _ = FREE_OPERATORS[Operator]
    Make, Template, _, _ = OPERAND_FORMS[Form]
    Stem = Class[2:]
    Operand = Make(Class, Reference)
    Text = ['struct C%sBase' % Stem, '{', '};', 'struct C%sOther' % Stem, '{', '};', 'template <typename t_CType>',
            'struct TC%sBaseTemplate' % Stem, '{', '};', 'template <typename t_CType>', 'struct TC%sOtherTemplate' % Stem,
            '{', '};', 'template <typename t_CType>', 'struct TC%sIdentity' % Stem, '{', '\tusing CType = t_CType;',
            '};']
    Head = {'template-class': 'template <typename tf_CType = int>', 'template-value': 'template <typename tf_CType = int>',
            'template-injected': 'template <typename tf_CType = int>',
            'template-base': 'template <typename tf_CType = int>', 'template-parameter': 'template <typename tf_CType>',
            'template-forwarding': 'template <typename tf_CType>',
            'template-specialization': 'template <bool tf_bEnabled>',
            'template-base-specialization': 'template <typename tf_CType>',
            'template-template': 'template <template <bool> class tf_TTemplate>',
            'template-not-deduced': 'template <typename tf_CType = %s<true>>' % Class,
            'template-unrelated': 'template <typename tf_CType>'}.get(Form)
    Friend = Form in ('injected', 'template-injected')
    Text += ['template <bool t_bEnabled>',
             'struct %s : C%sBase, TC%sBaseTemplate<int>' % (Class, Stem, Stem), '{']
    # A hidden friend template with a default argument is a definition.
    Body = ' { static int s_Value = 0; return s_Value; }' if Concept == 'i' and 'operator*' in Declaration('') or \
        Concept == 'p' else ' {}'
    if Friend:
        Text += (['\t' + Head] if Head else []) + ['\tfriend %s%s' % (Declaration(Operand), Body)]
    Text += ['};']
    if not Friend:
        Text += ([Head] if Head else []) + ['%s;' % Declaration(Operand)]
    return Text


for Operator, (Concept, _, _, Use) in FREE_OPERATORS.items():
    Suffix = CONCEPTS[Concept][0]
    for Form, (_, Template, Takes, CanInstantiate) in OPERAND_FORMS.items():
        for Instantiated in ((False, True) if CanInstantiate else (False,)):
            Ident = ''.join(Part.capitalize() for Part in ('free-' + Operator + '-' + Form).split('-'))
            Class = 'TC' + Ident + ('Used' if Instantiated else '') + Suffix
            for Context in ('variable', 'pattern', 'instance'):
                Name = 'N%s_%s%s_%s' % (Operator.replace('-', '_'), Form.replace('-', '_'),
                                       '_used' if Instantiated else '', Context)
                Answer = operandAnswer(Form, Instantiated, Context)
                emit('namespace %s' % Name)
                emit('{')
                for Each in freeOperator(Form, Operator, Class, Instantiated):
                    emit('\t' + Each)
                if Context == 'variable':
                    row(Name, Concept, Answer, Context,
                        (('g_Value', '\t%s<true> g_Value;' % Class),
                         ('g_%sValue' % Concept, '\t%s<true> g_%sValue;' % (Class, Concept))))
                    if Instantiated:
                        emit('\tinline void fg_Use()')
                        emit('\t{')
                        emit('\t\t' + (Use % 'g_Value') + ';')
                        emit('\t}')
                elif Context == 'pattern':
                    if Instantiated:
                        emit('\tinline void fg_Use(%s<true> *_pValue)' % Class)
                        emit('\t{')
                        emit('\t\t' + (Use % '*_pValue') + ';')
                        emit('\t}')
                    emit('\ttemplate <bool tf_bEnabled>')
                    emit('\tvoid fg_Take(')
                    row(Name, Concept, Answer, Context,
                        (('_Value', '\t\t%s<tf_bEnabled> const &_Value,' % Class), None))
                    emit('\t\t%s<tf_bEnabled> const &_%sValue' % (Class, Concept))
                    emit('\t);')
                else:
                    emit('\t%s<true> mc_Value;' % Class)
                    if Instantiated:
                        emit('\tinline void fg_Use()')
                        emit('\t{')
                        emit('\t\t' + (Use % 'mc_Value') + ';')
                        emit('\t}')
                    emit('\ttemplate <typename tf_CType>')
                    Rejected = row(Name, Concept, Answer, Context,
                                   (None, ('_%sValue' % Concept, '\tvoid fg_Take(tf_CType const &_%sValue)' % Concept)))
                    emit('\t{')
                    emit('\t}')
                    emit('\tinline void fg_Run()')
                    emit('\t{')
                    emit('\t\tfg_Take(mc_Value);')
                    if Rejected:
                        Expected.append(instantiationNote(line() - 1, Name, Class))
                    emit('\t}')
                emit('}')

# The rows written by hand: the free operators, the names SmartPointerTypes
# lists exactly, what the structure leaves out (3c), the types instantiations
# name (RQ6), copies through the vocabulary in a template (RQ7), and what waits
# for the user (Q1). Each line is the code, and of a declaration the message
# the rule reports, the one today reports, and why they differ.
def hand(Name, Rows):
    emit('namespace %s' % Name)
    emit('{')
    for Row in Rows:
        if isinstance(Row, str):
            emit('\t' + Row)
            continue
        Text, Rule, Today, Reason = Row
        if Rule != Today:
            Text += (' // KNOWN-FALSE-POSITIVE: %s' % Reason) if Today and not Rule else (
                ' // RESTORE-MESSAGES: malterlib-conceptual-prefix %s' % Reason) if Rule and not Today else (
                ' // KNOWN-FALSE-POSITIVE: %s' % Reason)
        elif Reason and Reason.startswith('Q'):
            Text += ' // QUESTION: %s' % Reason
        emit('\t' + Text)
        if Today:
            Expected.append(warning(line() - 1, Today))
    emit('}')


def requiredLocal(Variable, Concept, Storage):
    return "'%s' is %s and should have the '%s' concept prefix%s" % (
        Variable, CONCEPTS[Concept][1], Concept, ' after its storage prefix' if Storage else '')


R = lambda Variable, Concept, Storage=True: requiredLocal(Variable, Concept, Storage)
X = lambda Variable, Concept: rejected(Variable, Concept, None)
hand('Nfree_friend', ['struct CFriendIterator', '{', '\tfriend int &operator*(CFriendIterator const &_iIterator);',
                      '};', ('CFriendIterator g_Value;', R('g_Value', 'i'), R('g_Value', 'i'), None),
                      ('CFriendIterator g_iValue;', None, None, None)])
hand('Nfree_friend_deleted', ['struct CFriendIterator', '{',
                              '\tfriend int &operator*(CFriendIterator const &_Iterator) = delete;', '};',
                              ('CFriendIterator g_Value;', None, None, None),
                              ('CFriendIterator g_iValue;', X('g_iValue', 'i'), X('g_iValue', 'i'), None)])
hand('Nfree_friend_template', ['template <typename t_CType>', 'struct TCFriendIterator', '{',
                               '\ttemplate <typename tf_CType>',
                               '\tfriend int &operator*(TCFriendIterator<tf_CType> const &_iIterator);', '};',
                               ('TCFriendIterator<int> g_Value;', None, None, None),
                               ('TCFriendIterator<int> g_iValue;', None, None, None)])
hand('Nfree_namespace', ['struct CNamespaceIterator', '{', '};',
                         'int &operator*(CNamespaceIterator const &_iIterator);',
                         ('CNamespaceIterator g_Value;', R('g_Value', 'i'), R('g_Value', 'i'), None),
                         ('CNamespaceIterator g_iValue;', None, None, None)])
hand('Nfree_namespace_deleted', ['struct CNamespaceIterator', '{', '};',
                                 'int &operator*(CNamespaceIterator const &_Iterator) = delete;',
                                 ('CNamespaceIterator g_Value;', None, None, None),
                                 ('CNamespaceIterator g_iValue;', X('g_iValue', 'i'), X('g_iValue', 'i'), None)])
hand('Nfree_namespace_template', ['template <typename t_CType>', 'struct TCNamespaceIterator', '{', '};',
                                  'template <typename tf_CType>',
                                  'int &operator*(TCNamespaceIterator<tf_CType> const &_iIterator);',
                                  ('TCNamespaceIterator<int> g_Value;', None, None, None),
                                  ('TCNamespaceIterator<int> g_iValue;', None, None, None)])
hand('Nfree_associated_friend', ['template <typename t_CType>', 'struct TCWalkIterator', '{', '};', 'struct CTag',
                                 '{', '\ttemplate <typename tf_CType>',
                                 '\tfriend int &operator*(TCWalkIterator<tf_CType> const &_iIterator);', '};',
                                 ('TCWalkIterator<CTag> g_Value;', None, None, None),
                                 ('TCWalkIterator<CTag> g_iValue;', None, None, None)])
hand('Nfree_base_operand', ['namespace NBase', '{', '\tstruct CCursorBase', '\t{', '\t};', '}',
                            'struct CRowCursor : NBase::CCursorBase', '{', '};',
                            'CRowCursor &operator++(NBase::CCursorBase &_iBase);',
                            ('CRowCursor g_Value;', R('g_Value', 'i'), R('g_Value', 'i'), None),
                            ('CRowCursor g_iValue;', None, None, None)])
hand('Nexact_dependent_unused', ['template <bool t_bEnabled>', 'struct TCPointer', '{',
                                 '\tint *operator->() const requires t_bEnabled;', '};',
                                 ('TCPointer<true> g_Value;', R('g_Value', 'p'), R('g_Value', 'p'), None),
                                 ('TCPointer<true> g_pValue;', None, None, None)])
hand('Nexact_deleted', ['template <bool t_bEnabled>', 'struct TCPointer', '{',
                        '\tint *operator->() const = delete;', '};',
                        ('TCPointer<true> g_Value;', R('g_Value', 'p'), R('g_Value', 'p'), None),
                        ('TCPointer<true> g_pValue;', None, None, None)])
hand('Nexact_derived_hiding', ['template <bool t_bEnabled>', 'struct TCPointer', '{', '};',
                               'struct CHolder : TCPointer<true>', '{', '\tint *operator->() const = delete;', '};',
                               ('CHolder g_Value;', None, None, None),
                               ('CHolder g_pValue;', X('g_pValue', 'p'), X('g_pValue', 'p'), None)])
hand('Nexact_derived', ['template <bool t_bEnabled>', 'struct TCPointer', '{', '};',
                        'struct CHolder : TCPointer<true>', '{', '};',
                        ('CHolder g_Value;', R('g_Value', 'p'), R('g_Value', 'p'), None),
                        ('CHolder g_pValue;', None, None, None)])
DEPENDENT_BASE = ('a base that depends on the parameters leaves NotFunctionObjectTypes and the handles unknown (3c item 2: '
                  'decided rule 4 keeps it so)')
hand('Nstructure_dependent_base', ['template <typename t_CSignature>', 'struct TCFunction', '{', '};',
                                   'template <typename t_CSignature>',
                                   'struct TCCallback : TCFunction<t_CSignature>', '{', '};',
                                   'template <typename tf_CSignature>', 'void fg_Take(',
                                   ('\tTCCallback<tf_CSignature> const &_Value,', R('_Value', 'f'), None,
                                    DEPENDENT_BASE),
                                   '\tTCCallback<tf_CSignature> const &_fValue', ');'])
PARTIAL = ('the primary template is among what a specialization can be where a partial one always wins (3c item 3: '
           'telling needs template argument deduction, which the check doesn\'t do)')
hand('Nstructure_partial', ['template <typename t_CSignature>', 'struct TCFunction', '{', '};',
                            'template <typename t_CType>', 'struct TCChoice', '{', '};', 'template <typename t_CType>',
                            'struct TCChoice<t_CType *> : TCFunction<void ()>', '{', '};', 'template <typename tf_CType>',
                            'void fg_Take(', ('\tTCChoice<tf_CType *> const &_Value,', R('_Value', 'f'), None, PARTIAL),
                            '\tTCChoice<tf_CType *> const &_fValue', ');'])
DECLTYPE = 'a pointer written through decltype of a written pointer isn\'t required (3c item 4, not built)'
hand('Nstructure_decltype', ['template <typename tf_CType>', 'void fg_Copy(tf_CType *_pValue)', '{',
                             ('\tdecltype(_pValue) Value = _pValue;', R('Value', 'p', False), None, DECLTYPE),
                             '\t(void)Value;', '}', 'inline void fg_Use(int *_pValue)', '{', '\tfg_Copy(_pValue);',
                             '}'])
ATOMIC = 'a value wrapper of a pointer to an F-named type isn\'t told by its instances (3c item 5, not built)'
hand('Nstructure_atomic', ['template <typename t_CType>', 'struct TCAtomic', '{', '\tt_CType m_Value;', '};',
                           'template <typename t_FFunction>', 'struct TCHolder', '{',
                           ('\tTCAtomic<t_FFunction *> m_fValue;', X('m_fValue', 'f'), None, ATOMIC), '};',
                           'TCHolder<int> g_Holder;'])
hand('Ninstance_named_type', ['template <typename t_FAction>', 'struct TCBox', '{', '\tusing CAction = t_FAction;',
                              '};', 'void fg_Run(',
                              ('\tTCBox<int>::CAction _Value,', None, None, None),
                              ('\tTCBox<int>::CAction _fValue', X('_fValue', 'f'), X('_fValue', 'f'), None), ');'])
hand('Ncopy_exchange', ['template <typename t_CType>', 'struct TCSharedPointer', '{',
                        '\tt_CType *operator->() const;', '};',
                        'template <typename tf_CValue, typename tf_CSetTo>',
                        'tf_CValue fg_Exchange(tf_CValue &_Value, tf_CSetTo &&_New);',
                        'template <typename tf_CNew>', 'void fg_Reset(TCSharedPointer<int> &_pValue, tf_CNew &&_New)',
                        '{', ('\tauto Old = fg_Exchange(_pValue, _New);', R('Old', 'p', False),
                              R('Old', 'p', False), None),
                        '\t(void)Old;', '}', 'template <typename tf_CNew>',
                        'void fg_ResetUnused(TCSharedPointer<int> &_pValue, tf_CNew &&_New)', '{',
                        ('\tauto Old = fg_Exchange(_pValue, _New);', R('Old', 'p', False), R('Old', 'p', False),
                         None),
                        '\t(void)Old;', '}', 'inline void fg_Use(TCSharedPointer<int> &_pValue)', '{',
                        '\tfg_Reset(_pValue, nullptr);', '}'])
hand('Nquestion_forwarder', ['template <typename t_CType>', 'struct TCIdentity', '{', '\tusing CType = t_CType;', '};',
                             'template <typename tf_FSignature>',
                             ('void fg_Call(typename TCIdentity<tf_FSignature>::CType *_fCallback);',
                              R('_fCallback', 'p'), R('_fCallback', 'p'), 'Q1'),
                             'template <typename tf_FCallback>',
                             ('void fg_Value(typename TCIdentity<tf_FCallback>::CType _Callback);',
                              R('_Callback', 'f'), R('_Callback', 'f'), 'Q1')])

# The shapes and the names the member rows leave out: a binary `operator*`,
# which isn't a dereference; a conversion template, which can be one to bool
# or to a pointer (unknown, Q-D); a conversion to a reference to bool; a
# conversion to what depends on the parameters of a template, which can be one
# to a function pointer (unknown); a call operator a using declaration of
# what depends on the parameters brings in, with a base that does, which
# decided rule 4 leaves unknown; an alias whose name `*Iterator*` or
# `*Pointer` matches, read by what the class it stands for can do.
hand('Nshape_binary', ['struct CTwinIterator', '{', '\tint operator*(int _Factor) const;', '};',
                       ('CTwinIterator g_Value;', None, None, None),
                       ('CTwinIterator g_iValue;', X('g_iValue', 'i'), X('g_iValue', 'i'), None)])
hand('Nshape_conversion_template', ['struct CAnyPointer', '{', '\ttemplate <typename tf_CType>',
                                    '\texplicit operator tf_CType() const;', '};',
                                    ('CAnyPointer g_Value;', None, None, None),
                                    ('CAnyPointer g_pValue;', None, None, None)])
hand('Nshape_bool_reference', ['struct CFlagPointer', '{', '\texplicit operator bool const &() const;', '};',
                               ('CFlagPointer g_Value;', R('g_Value', 'p'), R('g_Value', 'p'), None),
                               ('CFlagPointer g_pValue;', None, None, None)])
hand('Nshape_dependent_conversion', ['template <typename t_CType>', 'struct TCConvertFunctor', '{',
                                     '\toperator t_CType() const;', '};', 'template <typename tf_CType>', 'void fg_Take(',
                                     ('\tTCConvertFunctor<tf_CType> const &_Value,', None, None, None),
                                     '\tTCConvertFunctor<tf_CType> const &_fValue', ');'])
hand('Nshape_unresolved_using', ['template <typename t_CBase>', 'struct TCForwardFunctor : t_CBase', '{',
                                 '\tusing t_CBase::operator();', '};', 'template <typename tf_CBase>', 'void fg_Take(',
                                 ('\tTCForwardFunctor<tf_CBase> const &_Value,', None, None, None),
                                 '\tTCForwardFunctor<tf_CBase> const &_fValue', ');'])
hand('Nalias_iterator', ['struct CThing', '{', '\tint &operator*() const;', '};', 'using CThingIterator = CThing;',
                         ('CThingIterator g_Value;', R('g_Value', 'i'), R('g_Value', 'i'), None),
                         ('CThingIterator g_iValue;', None, None, None)])
hand('Nalias_iterator_deleted', ['struct CThing', '{', '\tint &operator*() const = delete;', '};',
                                 'using CThingIterator = CThing;', ('CThingIterator g_Value;', None, None, None),
                                 ('CThingIterator g_iValue;', X('g_iValue', 'i'), X('g_iValue', 'i'), None)])
hand('Nalias_iterator_unknown', ['template <bool t_bEnabled>', 'struct TCThing', '{',
                                 '\tint &operator*() const requires t_bEnabled;', '};',
                                 'using CThingIterator = TCThing<true>;', ('CThingIterator g_Value;', None, None, None),
                                 ('CThingIterator g_iValue;', None, None, None)])
hand('Nalias_pointer', ['struct CHandle', '{', '\texplicit operator bool() const;', '};', 'using CHandlePointer = CHandle;',
                        ('CHandlePointer g_Value;', R('g_Value', 'p'), R('g_Value', 'p'), None),
                        ('CHandlePointer g_pValue;', None, None, None)])
hand('Nalias_pointer_number', ['struct CHandle', '{', '\texplicit operator int() const;', '};',
                               'using CHandlePointer = CHandle;', ('CHandlePointer g_Value;', None, None, None),
                               ('CHandlePointer g_pValue;', None, None, None)])

# A parameter of a partial specialization bound to a handle and to the smart
# pointer it is an alias of, `TCPair<t_CType, t_CType>` given both: what a
# member gives of it is written as either, so neither prefix is reported
# (decided Q9); given the same twice, it is what is written. The alias is in
# a namespace the smart pointer's handle aliases aren't looked up in.
def sugarRows(Function, First, Second, Rule):
    return ['inline void %s(NPointers::TCPair<%s, %s> const &_Pair)' % (Function, First, Second), '{',
            ('\tauto pSecond = _Pair.f_Second();', Rule[0], Rule[0], None),
            ('\tauto Second = _Pair.f_Second();', Rule[1], Rule[1], None),
            '\t(void)pSecond;', '\t(void)Second;', '}']


hand('Nstructure_partial_sugar',
     ['namespace NPointers', '{', '\ttemplate <typename t_CType>', '\tstruct TCUniquePointer', '\t{',
      '\t\tt_CType *operator->() const;', '\t};', '\ttemplate <typename t_CFirst, typename t_CSecond>', '\tstruct TCPair',
      '\t{', '\t\tt_CSecond f_Second() const;', '\t};', '\ttemplate <typename t_CType>',
      '\tstruct TCPair<t_CType, t_CType>', '\t{', '\t\tt_CType f_Second() const;', '\t};', '}',
      'namespace NUse', '{', 'using CActorSubscription = NPointers::TCUniquePointer<int>;'] +
     sugarRows('fg_HandleFirst', 'CActorSubscription', 'NPointers::TCUniquePointer<int>', (None, None)) +
     sugarRows('fg_PointerFirst', 'NPointers::TCUniquePointer<int>', 'CActorSubscription', (None, None)) +
     sugarRows('fg_Handles', 'CActorSubscription', 'CActorSubscription', (X('pSecond', 'p'), None)) +
     sugarRows('fg_Pointers', 'NPointers::TCUniquePointer<int>', 'NPointers::TCUniquePointer<int>',
               (None, R('Second', 'p', False))) + ['}'])

# Free operators in the states the operand axis doesn't cross: brought into
# the class's namespace by a using declaration, and a hidden friend whose
# constraint depends on the class's parameters and that nothing uses. And
# `f` on a class known to declare no call operator.
hand('Nfree_using', ['namespace NData', '{', '\tstruct CUseIterator', '\t{', '\t};', '}', 'namespace NOps', '{',
                     '\tint &operator*(NData::CUseIterator const &_iIterator);', '}', 'namespace NData', '{',
                     '\tusing NOps::operator*;', '}', 'using NData::CUseIterator;',
                     ('CUseIterator g_Value;', R('g_Value', 'i'), R('g_Value', 'i'), None),
                     ('CUseIterator g_iValue;', None, None, None)])
hand('Nfree_friend_dependent', ['template <bool t_bEnabled>', 'struct TCGateIterator', '{',
                                '\tfriend int &operator*(TCGateIterator const &)', '\t\trequires t_bEnabled',
                                '\t{', '\t\tstatic int s_Value = 0;', '\t\treturn s_Value;', '\t}', '};',
                                ('TCGateIterator<true> g_Value;', None, None, None),
                                ('TCGateIterator<true> g_iValue;', None, None, None)])
hand('Nreject_unlisted', ['struct CPlain', '{', '\tint m_Value;', '};', ('CPlain g_Value;', None, None, None),
                          ('CPlain g_fValue;', X('g_fValue', 'f'), X('g_fValue', 'f'), None)])

if Mode == 'code':
    print('\n'.join(Lines))
else:
    print('\n'.join(Expected))
