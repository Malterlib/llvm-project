#pragma once

namespace NTraits
{
	template <typename t_CType, typename t_CSignature>
	struct TCIsCallableWith
	{
		static constexpr bool mc_Value = true;
	};

	template <typename t_CType, typename t_CSignature>
	concept cIsCallableWith = TCIsCallableWith<t_CType, t_CSignature>::mc_Value;
}

template <typename t_CType>
struct TCSharedPointer
{
};

namespace NPrivate
{
	template <typename t_CFunction, typename ...tp_CParams>
	struct TCIsValidFunction
	{
		static constexpr bool mc_Value = NTraits::cIsCallableWith<t_CFunction, void (tp_CParams ...p_Params)>;
	};

	template <typename t_CType, typename ...tp_CParams>
	struct TCIsValidFunction<TCSharedPointer<t_CType>, tp_CParams...>
	{
		static constexpr bool mc_Value = false;
	};
}

template <typename tf_FFunction, typename ...tfp_CParams>
inline auto fg_CallChecked(tf_FFunction &&_fFunction, tfp_CParams && ...p_Params)
	requires (NPrivate::TCIsValidFunction<tf_FFunction, tfp_CParams...>::mc_Value)
{
	return 0;
}

inline int fg_UseInHeader()
{
	int Value = 0;
	return fg_CallChecked([](int _Value, long _Other) {}, Value, 3L);
}
