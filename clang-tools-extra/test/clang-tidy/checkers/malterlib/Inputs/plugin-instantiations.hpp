template <typename tf_CType>
void fg_Write(tf_CType o_Value);

template <typename t_CType>
struct TCPluginBox
{
	void f_Run(t_CType const &o_Value);
};

template <typename t_CType>
struct TCPluginPartial;

template <typename t_CType>
struct TCPluginPartial<t_CType *>
{
	void f_Run(t_CType const &o_Value);
};
