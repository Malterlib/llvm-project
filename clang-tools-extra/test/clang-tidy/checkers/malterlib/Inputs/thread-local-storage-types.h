// Classes a header defines, which a translation unit can hold for each
// thread.
struct CHeaderData {
  int m_Value;
};
struct CNestedHeaderData {
  int m_Value;
};
struct CListedData {
  int m_Value;
};
template <typename t_CType> struct TCHeaderTemplate {
  t_CType m_Value;
};
template <typename t_CType> struct TCWrapper {
  t_CType m_Wrapped;
};
