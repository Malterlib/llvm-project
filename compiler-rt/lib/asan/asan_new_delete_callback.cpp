#include "asan_allocator.h"
#include "asan_internal.h"
#include "asan_report.h"

// Provide default implementation of __asan_on_delete that does nothing
// and may be overriden by user.
SANITIZER_INTERFACE_WEAK_DEF(int, __asan_on_delete, void *, uptr) { return 0; }
