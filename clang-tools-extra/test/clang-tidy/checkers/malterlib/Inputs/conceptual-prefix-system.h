#pragma clang system_header

// The types of a system header whose names start like function types.
typedef struct _FILETIME
{
	unsigned m_Low;
	unsigned m_High;
} FILETIME;

typedef struct FSRef
{
	unsigned char m_Hidden[80];
} FSRef;

template <typename t_CType>
using FSystemAlias = t_CType;
