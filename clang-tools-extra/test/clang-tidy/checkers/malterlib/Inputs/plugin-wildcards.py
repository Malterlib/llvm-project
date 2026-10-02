# Writes, from the table of plugin-wildcards.tsv, a repository for each row
# whose IncludeFiles is the row's wildcard, with a header at the row's path
# that declares `BadRow<n>`, a source that includes them all in a repository
# around them that checks it (a source that isn't checked has no headers
# checked either), the rules, and
# the checks of what is reported: matched.txt for the rows that match,
# unmatched.txt for the others.
import json, os, sys

Table, Out = sys.argv[1], sys.argv[2]
Rules = [{'Location': Out.replace('\\', '/'), 'Tidy': {'Enabled': True}}]
Includes, Matched, Unmatched = [], [], []
Rows = [Line.rstrip('\n').split('\t') for Line in open(Table)
        if Line.strip() and not Line.startswith('#')]
for Index, (Pattern, Path, Match) in enumerate(Rows, 1):
    Repository = os.path.join(Out, 'Row%d' % Index)
    os.makedirs(os.path.dirname(os.path.join(Repository, Path)), exist_ok=True)
    with open(os.path.join(Repository, Path), 'w') as File:
        File.write('void BadRow%d();\n' % Index)
    Rules.append({'Location': Repository.replace('\\', '/'),
                  'Tidy': {'Enabled': True, 'IncludeFiles': [Pattern]}})
    Includes.append('#include "Row%d/%s"' % (Index, Path))
    if Match == 'match':
        Matched.append("CHECK-DAG: warning: global function 'BadRow%d' should" % Index)
    else:
        Unmatched.append("CHECK-NOT: 'BadRow%d'" % Index)
with open(os.path.join(Out, 'main.cpp'), 'w') as File:
    File.write('\n'.join(Includes) + '\n')
with open(os.path.join(Out, 'rules.json'), 'w') as File:
    json.dump(Rules, File)
with open(os.path.join(Out, 'matched.txt'), 'w') as File:
    File.write('\n'.join(Matched) + '\n')
with open(os.path.join(Out, 'unmatched.txt'), 'w') as File:
    File.write('\n'.join(Unmatched) + '\n')
