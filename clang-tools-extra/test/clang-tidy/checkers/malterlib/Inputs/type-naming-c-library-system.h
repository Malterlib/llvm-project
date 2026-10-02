#pragma clang system_header

// A C library's header that defines the types its callers allocate.
struct DBusMessageIter
{
	void *dummy1;
};

struct DBusError
{
	const char *name;
};
