# Writes an input of the fix-it tests byte for byte, which a RUN line can't
# portably: the shells' printf and echo differ in their escapes, and on
# Windows a line break written to stdout becomes CRLF.
# Usage: fixit-inputs.py <case> <path>
import sys

Texts = {
    # struct-not-class-leading-label.cpp: the fragment other classes include.
    'leading-label-fragment': 'public:\n',
    # unused-parameter-cast.cpp: a cast written across two files.
    'span-header': '//xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx\n_Value;\n',
    'span-main': 'void fg_Work();\nvoid fg_Run(int _Value) {\n  (void)\n#include "param2.hpp"\n  fg_Work();\n}\n',
    # unused-parameter-cast-lines.cpp: a last line without a line break that
    # a backslash splices, and CRLF line endings.
    'last': 'void fg_Run(int _Value) {\n#if 1\n#endif\n}\nvoid fg_Last(int _Value) { int Local = 0; \\\n(void)_Value; }',
    'crlf': 'void fg_Run(int _Value)\r\n{\r\n\tint Local = 0; \\\r\n\t(void)_Value;\r\n#if 1\r\n\t++Local;\r\n#endif\r\n}\r\nvoid fg_Alone(int _Value)\r\n{\r\n\t(void)_Value;\r\n#if 1\r\n#endif\r\n}\r\n',
    # fixits-option.cpp: the plugin's configurations, with malterlib.FixIts
    # and without it.
    'plugin-config-on': "Checks: '-*,malterlib-struct-not-class,malterlib-unused-parameter-cast'\nCheckOptions:\n  malterlib.FixIts: true\n",
    'plugin-config-off': "Checks: '-*,malterlib-struct-not-class,malterlib-unused-parameter-cast'\n",
}

Case, Path = sys.argv[1:3]
with open(Path, 'wb') as File:
    File.write(Texts[Case].encode('utf-8'))
