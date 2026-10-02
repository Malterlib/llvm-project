#include "malterlib-concurrency.h"
TCFuture<int> fg_Wait();
struct CCommandRegistry
{
	template <typename t_CFunctor> void f_RegisterCommand(t_CFunctor _fFunctor)
	{
	}
};
void fg_Test(CCommandRegistry &_Registry, int _Value)
{
	_Registry.f_RegisterCommand(
		[_Value]() -> TCFuture<int>
		{
			co_await fg_Wait();
			co_return _Value;
		}
	);
}
