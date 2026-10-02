// A C library's header that a search path finds, not a system header, which
// defines the types its callers allocate. The type is checked here, where it
// is defined, which the library's code isn't.
extern "C" {
typedef struct DBusPendingCall DBusPendingCall;
struct DBusPendingCall // NOLINT(malterlib-type-naming): the library's name
{
	void *dummy1;
};
}
