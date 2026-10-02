# Writes a class whose flag of a phase is set to the value of a chain of the
# given number of helpers, each calling the next, the last of which starts the
# phase.
import sys

Count = int(sys.argv[1])
print("struct CApp {")
print('  [[clang::annotate("malterlib-actor-phase-flag", "Destroy")]] bool m_bDestroyed = false;')
print("  bool f_Helper0() { m_bDestroyed = true; return true; }")
for Index in range(1, Count + 1):
    print(f"  bool f_Helper{Index}() {{ return f_Helper{Index - 1}(); }}")
print(f"  void f_Run() {{ m_bDestroyed = m_bDestroyed || f_Helper{Count}(); }}")
print("};")
