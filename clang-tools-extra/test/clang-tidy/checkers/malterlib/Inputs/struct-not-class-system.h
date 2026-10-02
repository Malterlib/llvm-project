#pragma clang system_header

// A template of a system header, which code here can specialize.
template <typename t_CType> struct TCSystemExternal;
template <typename t_CType> class TCSystemExternalClass;

// A template a system header defines, which code here can redeclare before
// specializing it (round 206).
namespace NRound206Library
{
	template <typename t_CType>
	class TCSerializer
	{
	public:
		void f_Write(t_CType const &_Value);
	};
}
