# Writes the place matrix of malterlib-parameter-ownership: where a write a
# destructor makes lands, so whether a copy of the object shares it. `code`
# writes the code, `expect` the lines that are reported, a line and the check
# each.
#
# Each row is a class whose destructor writes one place one way, and a
# function that takes the class by value. By the rule, the function is
# reported where the place is storage the copies share: what a pointer member
# points to, also of an element of an array of pointers, a member of that,
# what a reference member refers to, static storage, and bytes outside the
# object. It isn't where the place is the object's own: a member, an element
# of an array member, a member of such an element, a base's member, the object
# reached through a cast, bytes inside a member, or storage the destructor
# makes itself.
#
# The dimensions: the places above, the ways to write (assignment, compound
# assignment, increment, a member function that writes its object, a helper
# given the place's address that writes `*_p`, a helper given the object by
# reference that writes the place through it, `memset`), of a scalar and of a
# record with such a member function.
#
# A row that should report and doesn't yet is pending: written with a
# `// RESTORE-MESSAGES: <check>` comment that the gate counts, and not
# expected. A row reported on own storage that isn't fixed yet is written
# with `// KNOWN-FALSE-POSITIVE: <reason>` and is expected
# (lbt/po-matrix-check.sh counts both; the counts only go down).
import sys

Mode = sys.argv[1]
if Mode not in ('code', 'expect'):
    sys.exit('Unknown mode: ' + Mode)
Check = 'malterlib-parameter-ownership'
Lines = []
Expected = []

# Each place: its name, whether copies share it, the members and bases of the
# class, and how it is reached from an object, `{o}` the object as written
# (nothing in the destructor, `_Holder.` in a helper given it).
Places = [
    ('member', False, '', '{T} m_Place;', '{o}m_Place'),
    ('element', False, '', '{T} m_Places[2];', '{o}m_Places[1]'),
    ('element_member', False, '', 'struct CPart { {T} m_Place; }; CPart m_Parts[2];', '{o}m_Parts[1].m_Place'),
    ('base', False, ' : CBase', '', '{o}m_Place'),
    ('cast', False, ' : CEmpty', '{T} m_Place;',
     'static_cast<CHolder &>(static_cast<CEmpty &>({O})).m_Place'),
    ('bytes_in', False, '', 'unsigned long long m_Value;',
     'reinterpret_cast<unsigned char *>(&{o}m_Value)[4]'),
    ('fresh', False, '', '', '*pFresh'),
    ('pointee', True, '', '{T} *m_pPlace;', '*{o}m_pPlace'),
    ('element_pointee', True, '', '{T} *m_pPlaces[2];', '*{o}m_pPlaces[1]'),
    ('pointee_member', True, '', 'struct CPart { {T} m_Place; }; CPart *m_pPart;', '{o}m_pPart->m_Place'),
    ('reference_member', True, '', '{T} &m_rPlace;', '{o}m_rPlace'),
    ('static', True, '', '', 'g_Place'),
    ('bytes_out', True, '', 'unsigned long long m_Value;',
     'reinterpret_cast<unsigned char *>(&{o}m_Value)[8]'),
]

# Each way: its name, whether it writes a record (CCell) rather than a
# scalar, and the statement, `{P}` the place in the destructor.
Ways = [
    ('assign', False, '{P} = 0;'),
    ('compound', False, '{P} += 1;'),
    ('increment', False, '++{P};'),
    ('member_call', True, '({P}).f_Clear();'),
    ('pointer_helper', False, 'fg_Clear(&({P}));'),
    ('object_helper', False, 'fg_ClearIn(*this);'),
    ('memset', False, '__builtin_memset(&({P}), 0, sizeof({P}));'),
]


def pending(Name):
    # What a row the check doesn't report yet waits for.
    if Name in ('Nbytes_out_assign', 'Nbytes_out_increment', 'Nbytes_out_object_helper'):
        return ('a place outside the object to count as shared where its places can\'t be told, not by the shape '
                'of the pointer it is written through (C2b)')
    if Name in ('Nstatic_memset', 'Nstatic_pointer_helper'):
        return 'the address of static storage given to what writes through it to count as changing it (C5)'
    return None


def knownFalsePositive(Name):
    # Why a row the check reports on the object's own storage does.
    return None


def emit(Text, Reported=False, Name=None):
    if Reported:
        Waits = pending(Name)
        if Waits:
            Text += ' // RESTORE-MESSAGES: %s (waits for %s)' % (Check, Waits)
        else:
            Lines.append(Text)
            Expected.append('%d %s' % (len(Lines), Check))
            return
    elif Name and knownFalsePositive(Name):
        Text += ' // KNOWN-FALSE-POSITIVE: %s' % knownFalsePositive(Name)
        Lines.append(Text)
        Expected.append('%d %s' % (len(Lines), Check))
        return
    Lines.append(Text)


emit('// The place matrix of malterlib-parameter-ownership (parameter-ownership-place-matrix.py); generated.')
emit('struct CCell { int m_n; void f_Clear() { m_n = 0; } };')
for Place, Shared, Bases, Members, Access in Places:
    for Way, Record, Statement in Ways:
        T = 'CCell' if Record else ('unsigned char' if Place.startswith('bytes') else 'int')
        if Record and Place.startswith('bytes'):
            continue
        if Way == 'compound' and Place.startswith('bytes'):
            continue
        if Way == 'object_helper' and Place in ('fresh', 'static'):
            continue
        Name = 'N%s_%s' % (Place, Way)
        Scalar = 'unsigned char' if Place.startswith('bytes') else T
        def At(Object, Prefix):
            return Access.replace('{O}', Object).replace('{o}', Prefix)
        Here = At('*this', '')
        emit('namespace %s {' % Name)
        if Place == 'base':
            emit('struct CBase { %s m_Place; };' % T)
        if Place == 'cast':
            emit('struct CEmpty {};')
        if Place == 'static':
            emit('inline %s g_Place{};' % T)
        if Way == 'pointer_helper':
            emit('inline void fg_Clear(%s *_p) { *_p = {}; }' % Scalar)
        if Way == 'object_helper':
            emit('struct CHolder;')
            emit('inline void fg_ClearIn(CHolder &_Holder);')
        emit('struct CHolder%s {' % Bases)
        Body = Statement.replace('{P}', Here)
        if Way == 'object_helper':
            Body = 'fg_ClearIn(*this);'
        if Place == 'fresh':
            Body = '%s *pFresh = new %s(); %s delete pFresh;' % (T, T, Body)
        emit('  ~CHolder() { %s }' % Body)
        if Members:
            emit('  ' + Members.replace('{T}', T))
        emit('};')
        if Way == 'object_helper':
            Through = At('_Holder', '_Holder.')
            emit('inline void fg_ClearIn(CHolder &_Holder) { %s = 0; }' % Through)
        emit('void fg_Use(CHolder _Value) {}', Reported=Shared, Name=Name)
        emit('}')

print('\n'.join(Expected if Mode == 'expect' else Lines))
