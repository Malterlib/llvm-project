//===-- MalterlibClassifier.cpp -------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MalterlibClassifier.h"

#include "llvm/Support/FormatVariadic.h"

#include <cstdint>
#include <cstdlib>
#include <limits>
#include <map>
#include <memory>

using namespace lldb_private;

namespace {
    struct CDecodedColor
    {
      void f_Set(uint8_t _Red, uint8_t _Green, uint8_t _Blue);
      void f_SetAnsi16(uint8_t _Value);
      void f_SetAnsi256(uint8_t _Value);

      bool operator == (CDecodedColor const &_Right) const {
        return m_Red == _Right.m_Red
          && m_Green == _Right.m_Green
          && m_Blue == _Right.m_Blue
        ;
      }

      uint8_t m_Red = 0;
      uint8_t m_Green = 0;
      uint8_t m_Blue = 0;
      bool m_bEnabled = false;
    };

    CDecodedColor g_CommandLine_AnsiEncodingColor256Array[256] =
      {
        {0,0,0,true}
        , {201,27,0,true}
        , {0,194,0,true}
        , {199,196,0,true}
        , {2,37,199,true}
        , {201,48,199,true}
        , {0,197,199,true}
        , {199,199,199,true}
        , {103,103,103,true}
        , {255,109,103,true}
        , {95,249,103,true}
        , {254,251,103,true}
        , {104,113,255,true}
        , {255,118,255,true}
        , {95,253,255,true}
        , {254,255,255,true}
        , {0,0,0,true}
        , {0,0,95,true}
        , {0,0,135,true}
        , {0,0,175,true}
        , {0,0,215,true}
        , {0,0,255,true}
        , {0,95,0,true}
        , {0,95,95,true}
        , {0,95,135,true}
        , {0,95,175,true}
        , {0,95,215,true}
        , {0,95,255,true}
        , {0,135,0,true}
        , {0,135,95,true}
        , {0,135,135,true}
        , {0,135,175,true}
        , {0,135,215,true}
        , {0,135,255,true}
        , {0,175,0,true}
        , {0,175,95,true}
        , {0,175,135,true}
        , {0,175,175,true}
        , {0,175,215,true}
        , {0,175,255,true}
        , {0,215,0,true}
        , {0,215,95,true}
        , {0,215,135,true}
        , {0,215,175,true}
        , {0,215,215,true}
        , {0,215,255,true}
        , {0,255,0,true}
        , {0,255,95,true}
        , {0,255,135,true}
        , {0,255,175,true}
        , {0,255,215,true}
        , {0,255,255,true}
        , {95,0,0,true}
        , {95,0,95,true}
        , {95,0,135,true}
        , {95,0,175,true}
        , {95,0,215,true}
        , {95,0,255,true}
        , {95,95,0,true}
        , {95,95,95,true}
        , {95,95,135,true}
        , {95,95,175,true}
        , {95,95,215,true}
        , {95,95,255,true}
        , {95,135,0,true}
        , {95,135,95,true}
        , {95,135,135,true}
        , {95,135,175,true}
        , {95,135,215,true}
        , {95,135,255,true}
        , {95,175,0,true}
        , {95,175,95,true}
        , {95,175,135,true}
        , {95,175,175,true}
        , {95,175,215,true}
        , {95,175,255,true}
        , {95,215,0,true}
        , {95,215,95,true}
        , {95,215,135,true}
        , {95,215,175,true}
        , {95,215,215,true}
        , {95,215,255,true}
        , {95,255,0,true}
        , {95,255,95,true}
        , {95,255,135,true}
        , {95,255,175,true}
        , {95,255,215,true}
        , {95,255,255,true}
        , {135,0,0,true}
        , {135,0,95,true}
        , {135,0,135,true}
        , {135,0,175,true}
        , {135,0,215,true}
        , {135,0,255,true}
        , {135,95,0,true}
        , {135,95,95,true}
        , {135,95,135,true}
        , {135,95,175,true}
        , {135,95,215,true}
        , {135,95,255,true}
        , {135,135,0,true}
        , {135,135,95,true}
        , {135,135,135,true}
        , {135,135,175,true}
        , {135,135,215,true}
        , {135,135,255,true}
        , {135,175,0,true}
        , {135,175,95,true}
        , {135,175,135,true}
        , {135,175,175,true}
        , {135,175,215,true}
        , {135,175,255,true}
        , {135,215,0,true}
        , {135,215,95,true}
        , {135,215,135,true}
        , {135,215,175,true}
        , {135,215,215,true}
        , {135,215,255,true}
        , {135,255,0,true}
        , {135,255,95,true}
        , {135,255,135,true}
        , {135,255,175,true}
        , {135,255,215,true}
        , {135,255,255,true}
        , {175,0,0,true}
        , {175,0,95,true}
        , {175,0,135,true}
        , {175,0,175,true}
        , {175,0,215,true}
        , {175,0,255,true}
        , {175,95,0,true}
        , {175,95,95,true}
        , {175,95,135,true}
        , {175,95,175,true}
        , {175,95,215,true}
        , {175,95,255,true}
        , {175,135,0,true}
        , {175,135,95,true}
        , {175,135,135,true}
        , {175,135,175,true}
        , {175,135,215,true}
        , {175,135,255,true}
        , {175,175,0,true}
        , {175,175,95,true}
        , {175,175,135,true}
        , {175,175,175,true}
        , {175,175,215,true}
        , {175,175,255,true}
        , {175,215,0,true}
        , {175,215,95,true}
        , {175,215,135,true}
        , {175,215,175,true}
        , {175,215,215,true}
        , {175,215,255,true}
        , {175,255,0,true}
        , {175,255,95,true}
        , {175,255,135,true}
        , {175,255,175,true}
        , {175,255,215,true}
        , {175,255,255,true}
        , {215,0,0,true}
        , {215,0,95,true}
        , {215,0,135,true}
        , {215,0,175,true}
        , {215,0,215,true}
        , {215,0,255,true}
        , {215,95,0,true}
        , {215,95,95,true}
        , {215,95,135,true}
        , {215,95,175,true}
        , {215,95,215,true}
        , {215,95,255,true}
        , {215,135,0,true}
        , {215,135,95,true}
        , {215,135,135,true}
        , {215,135,175,true}
        , {215,135,215,true}
        , {215,135,255,true}
        , {215,175,0,true}
        , {215,175,95,true}
        , {215,175,135,true}
        , {215,175,175,true}
        , {215,175,215,true}
        , {215,175,255,true}
        , {215,215,0,true}
        , {215,215,95,true}
        , {215,215,135,true}
        , {215,215,175,true}
        , {215,215,215,true}
        , {215,215,255,true}
        , {215,255,0,true}
        , {215,255,95,true}
        , {215,255,135,true}
        , {215,255,175,true}
        , {215,255,215,true}
        , {215,255,255,true}
        , {255,0,0,true}
        , {255,0,95,true}
        , {255,0,135,true}
        , {255,0,175,true}
        , {255,0,215,true}
        , {255,0,255,true}
        , {255,95,0,true}
        , {255,95,95,true}
        , {255,95,135,true}
        , {255,95,175,true}
        , {255,95,215,true}
        , {255,95,255,true}
        , {255,135,0,true}
        , {255,135,95,true}
        , {255,135,135,true}
        , {255,135,175,true}
        , {255,135,215,true}
        , {255,135,255,true}
        , {255,175,0,true}
        , {255,175,95,true}
        , {255,175,135,true}
        , {255,175,175,true}
        , {255,175,215,true}
        , {255,175,255,true}
        , {255,215,0,true}
        , {255,215,95,true}
        , {255,215,135,true}
        , {255,215,175,true}
        , {255,215,215,true}
        , {255,215,255,true}
        , {255,255,0,true}
        , {255,255,95,true}
        , {255,255,135,true}
        , {255,255,175,true}
        , {255,255,215,true}
        , {255,255,255,true}
        , {8,8,8,true}
        , {18,18,18,true}
        , {28,28,28,true}
        , {38,38,38,true}
        , {48,48,48,true}
        , {58,58,58,true}
        , {68,68,68,true}
        , {78,78,78,true}
        , {88,88,88,true}
        , {98,98,98,true}
        , {108,108,108,true}
        , {118,118,118,true}
        , {128,128,128,true}
        , {138,138,138,true}
        , {148,148,148,true}
        , {158,158,158,true}
        , {168,168,168,true}
        , {178,178,178,true}
        , {188,188,188,true}
        , {198,198,198,true}
        , {208,208,208,true}
        , {218,218,218,true}
        , {228,228,228,true}
        , {238,238,238,true}
      }
    ;

    enum EClassification {
        EClassification_Unknown
        , EClassification_Type
        , EClassification_Namespace
        , EClassification_TemplateTypeParam_Class
        , EClassification_TemplateNonTypeParam
        , EClassification_TemplateTemplateParam
        , EClassification_FunctionTemplateTypeParam_Class
        , EClassification_FunctionTemplateNonTypeParam
        , EClassification_FunctionTemplateTemplateParam
        , EClassification_TemplateType
        , EClassification_Enumerator
        , EClassification_FunctionParameter
        , EClassification_FunctionParameter_Output
        , EClassification_FunctionParameter_Output_Pack
        , EClassification_FunctionParameter_Pack
        , EClassification_MacroParameter
        , EClassification_MemberFunctionPrivate
        , EClassification_MemberFunctionPublic
        , EClassification_MemberStaticFunctionPrivate
        , EClassification_MemberStaticFunctionPublic
        , EClassification_MemberVariablePrivate
        , EClassification_MemberVariablePublic
        , EClassification_MemberStaticVariablePrivate
        , EClassification_MemberStaticVariablePublic
        , EClassification_StaticFunction
        , EClassification_GlobalVariable
        , EClassification_GlobalStaticVariable
        , EClassification_MemberConstantPrivate
        , EClassification_MemberConstantPublic
        , EClassification_GlobalConstant
        , EClassification_Function
        , EClassification_Enum
        , EClassification_Macro
        , EClassification_Concept

        , EClassification_String
        , EClassification_Character
        , EClassification_Number
        , EClassification_PlainText

        , EClassification_LocalVariable

        , EClassification_Variable_Functor
        , EClassification_StaticVariable
        , EClassification_ConstantVariable

        , EClassification_KeywordBulitInConstants
        , EClassification_KeywordControlStatement
        , EClassification_KeywordStorageClass
        , EClassification_KeywordExceptionHandling
        , EClassification_KeywordIntrospection
        , EClassification_KeywordStaticAssert
        , EClassification_KeywordOptimization
        , EClassification_KeywordNewDelete
        , EClassification_KeywordCLR
        , EClassification_KeywordOther
        , EClassification_KeywordTypeSpecification
        , EClassification_KeywordNamespace
        , EClassification_KeywordTemplate
        , EClassification_KeywordFunction
        , EClassification_KeywordIn
        , EClassification_KeywordTypedef
        , EClassification_KeywordUsing
        , EClassification_KeywordThis
        , EClassification_KeywordOperator
        , EClassification_KeywordVirtual
        , EClassification_KeywordCasts
        , EClassification_KeywordPure

        , EClassification_KeywordBulitInTypes
        , EClassification_KeywordBulitInCharacterTypes
        , EClassification_KeywordBulitInIntegerTypes
        , EClassification_KeywordBulitInVectorTypes
        , EClassification_KeywordBulitInTypeModifiers
        , EClassification_KeywordBulitInFloatTypes

        , EClassification_KeywordPropertyModifiers
        , EClassification_KeywordAuto
        , EClassification_KeywordTypename
        , EClassification_KeywordAccess
        , EClassification_KeywordQualifier

        , EClassification_TemplateNonTypeParam_Pack
        , EClassification_FunctionTemplateNonTypeParam_Pack
        , EClassification_TemplateTypeParam_Class_Pack
        , EClassification_TemplateTemplateParam_Pack
        , EClassification_TemplateTypeParam_Function
        , EClassification_TemplateTypeParam_Function_Pack
        , EClassification_TemplateType_Interface
        , EClassification_Type_Interface
        , EClassification_Type_Function
        , EClassification_FunctionTemplateTypeParam_Class_Pack
        , EClassification_FunctionTemplateTypeParam_Function_Pack
        , EClassification_FunctionTemplateTemplateParam_Pack
        , EClassification_FunctionTemplateTypeParam_Function

        , EClassification_StaticFunction_Recursive
        , EClassification_Function_Recursive

        , EClassification_MemberStaticFunctionPrivate_Recursive
        , EClassification_MemberFunctionPrivate_Recursive
        , EClassification_MemberStaticFunctionPublic_Recursive
        , EClassification_MemberFunctionPublic_Recursive

        , EClassification_FunctionParameter_Functor
        , EClassification_FunctionParameter_Pack_Functor
        , EClassification_FunctionParameter_Output_Functor
        , EClassification_FunctionParameter_Output_Pack_Functor
        , EClassification_MemberVariablePublic_Functor
        , EClassification_MemberVariablePrivate_Functor
        , EClassification_MemberStaticVariablePublic_Functor
        , EClassification_MemberStaticVariablePrivate_Functor
        , EClassification_GlobalStaticVariable_Functor
        , EClassification_GlobalVariable_Functor
        , EClassification_StaticVariable_Functor

        , EClassification_PreprocessorDirective

        , EClassification_Comment
    };

    void CDecodedColor::f_Set(uint8_t _Red, uint8_t _Green, uint8_t _Blue)
    {
      m_Red = _Red;
      m_Green = _Green;
      m_Blue = _Blue;
      m_bEnabled = true;
    }

    void CDecodedColor::f_SetAnsi16(uint8_t _Value)
    {
      *this = g_CommandLine_AnsiEncodingColor256Array[_Value];
    }

    void CDecodedColor::f_SetAnsi256(uint8_t _Value)
    {
      *this = g_CommandLine_AnsiEncodingColor256Array[_Value];
    }

    struct CPrefixMap
    {
        char const *m_pPrefix;
        EClassification m_Classification;
        bool m_bVariable;
    };

    #define ignore(_Var)
    static struct CPrefixMap ms_PrefixMap[] =
        {
            {"t_", EClassification_TemplateNonTypeParam, true}                             ignore(t_Test)
            ,{"tp_", EClassification_TemplateNonTypeParam_Pack, true}                      ignore(tp_Test)
            ,{"E", EClassification_Enumerator, false}                                      ignore(ETest_Value) // pEnum if NodeType == NodeType_IdentifierType
            ,{"k", EClassification_Enumerator, false}                                      ignore(kCFCompareCaseInsensitive) // Compatibility with OSX system headers
            ,{"c_", EClassification_ConstantVariable, true}                                ignore(c_Test)
            ,{"gc_", EClassification_GlobalConstant, true}                                 ignore(gc_Test)
            ,{"mc_", EClassification_MemberConstantPublic, true}                           ignore(mc_Test)
            ,{"mcp_", EClassification_MemberConstantPrivate, true}                         ignore(mcp_Test)
            ,{"tf_", EClassification_FunctionTemplateNonTypeParam, true}                   ignore(tf_Test)
            ,{"tfp_", EClassification_FunctionTemplateNonTypeParam_Pack, true}             ignore(tfp_Test)

            ,{"N", EClassification_Namespace, false}                                       ignore(NTest)

            ,{"t_C", EClassification_TemplateTypeParam_Class, false}                       ignore(t_CTest)
            ,{"t_F", EClassification_TemplateTypeParam_Function, false}                    ignore(t_FTest)
            ,{"t_TC", EClassification_TemplateTemplateParam, false}                        ignore(t_TCTest)
            ,{"t_TF", EClassification_TemplateTemplateParam, false}                        ignore(t_TFTest)
            ,{"tp_C", EClassification_TemplateTypeParam_Class_Pack, false}                 ignore(tp_CTest)
            ,{"tp_F", EClassification_TemplateTypeParam_Function_Pack, false}              ignore(tp_FTest)
            ,{"tp_TC", EClassification_TemplateTemplateParam_Pack, false}                  ignore(tp_TCTest)
            ,{"tp_TF", EClassification_TemplateTemplateParam_Pack, false}                  ignore(tp_TFTest)
            ,{"C", EClassification_Type, false}                                            ignore(CTest)
            ,{"F", EClassification_Type_Function, false}                                   ignore(FTest)
            ,{"NS", EClassification_Type, false}                                           ignore(NSTest) // Compatibility with Cocoa
            ,{"UI", EClassification_Type, false}                                           ignore(UITest) // Compatibility with UIKit
            ,{"IC", EClassification_Type_Interface, false}                                 ignore(ICTest)
            ,{"TC", EClassification_TemplateType, false}                                   ignore(TCTest)
            ,{"TF", EClassification_TemplateType, false}                                   ignore(TFTest)
            ,{"TIC", EClassification_TemplateType_Interface, false}                        ignore(TICTest)
            ,{"CFStr", EClassification_Type, false}                                        ignore(CFStr256) // Compatibility with core foundation
            ,{"CFWStr", EClassification_Type, false}                                       ignore(CFWStr256) // Compatibility with core foundation
            ,{"CFUStr", EClassification_Type, false}                                       ignore(CFUStr256) // Compatibility with core foundation
            ,{"tf_C", EClassification_FunctionTemplateTypeParam_Class, false}              ignore(tf_CTest)
            ,{"tf_F", EClassification_FunctionTemplateTypeParam_Function, false}           ignore(tf_FTest)
            ,{"tf_TC", EClassification_FunctionTemplateTemplateParam, false}               ignore(tf_TCTest)
            ,{"tf_TF", EClassification_FunctionTemplateTemplateParam, false}               ignore(tf_TFTest)
            ,{"tfp_C", EClassification_FunctionTemplateTypeParam_Class_Pack, false}        ignore(tfp_CTest)
            ,{"tfp_F", EClassification_FunctionTemplateTypeParam_Function_Pack, false}     ignore(tfp_FTest)
            ,{"tfp_TC", EClassification_FunctionTemplateTemplateParam_Pack, false}         ignore(tfp_TCTest)
            ,{"tfp_TF", EClassification_FunctionTemplateTemplateParam_Pack, false}         ignore(tfp_TFTest)

            ,{"_f", EClassification_FunctionParameter_Functor, false}                      ignore(_fTest)
            ,{"p_f", EClassification_FunctionParameter_Pack_Functor, false}                ignore(p_fTest)
            ,{"o_f", EClassification_FunctionParameter_Output_Functor, false}              ignore(o_fTest)
            ,{"po_f", EClassification_FunctionParameter_Output_Pack_Functor, false}        ignore(po_fTest)

            ,{"_of", EClassification_FunctionParameter_Output_Functor, false}              ignore(_ofTest) // Deprecate?
            ,{"p_of", EClassification_FunctionParameter_Output_Pack_Functor, false}        ignore(p_ofTest) // Deprecate?

            ,{"f", EClassification_Variable_Functor, false}                                ignore(fTest)
            ,{"fl_", EClassification_Variable_Functor, false}                              ignore(fl_Test) // To be deprecated

            ,{"m_f", EClassification_MemberVariablePublic_Functor, false}                  ignore(m_fTest)
            ,{"mp_f", EClassification_MemberVariablePrivate_Functor, false}                ignore(mp_fTest)

            ,{"f_", EClassification_MemberFunctionPublic, false}                           ignore(f_Test)
            ,{"fr_", EClassification_MemberFunctionPublic_Recursive, false}                ignore(fr_Test)
            ,{"f_r", EClassification_MemberFunctionPublic_Recursive, false}                ignore(f_rTest)
            ,{"fs_", EClassification_MemberStaticFunctionPublic, false}                    ignore(fs_Test)
            ,{"fsr_", EClassification_MemberStaticFunctionPublic_Recursive, false}         ignore(fsr_Test)
            ,{"fs_r", EClassification_MemberStaticFunctionPublic_Recursive, false}         ignore(fs_rTest)
            ,{"fp_", EClassification_MemberFunctionPrivate, false}                         ignore(fp_Test)
            ,{"fpr_", EClassification_MemberFunctionPrivate_Recursive, false}              ignore(fpr_Test)
            ,{"fp_r", EClassification_MemberFunctionPrivate_Recursive, false}              ignore(fp_rTest)
            ,{"fsp_", EClassification_MemberStaticFunctionPrivate, false}                  ignore(fsp_Test)
            ,{"fspr_", EClassification_MemberStaticFunctionPrivate_Recursive, false}       ignore(fspr_Test)
            ,{"fsp_r", EClassification_MemberStaticFunctionPrivate_Recursive, false}       ignore(fsp_rTest)
            ,{"fg_", EClassification_Function, false}                                      ignore(fg_Test)
            ,{"CF", EClassification_Function, false}                                       ignore(CFRelease) // Compatibility with core foundation
            ,{"fgr_", EClassification_Function_Recursive, false}                           ignore(fgr_Test)
            ,{"fg_r", EClassification_Function_Recursive, false}                           ignore(fg_rTest)
            ,{"fsg_", EClassification_StaticFunction, false}                               ignore(fsg_Test)
            ,{"fsgr_", EClassification_StaticFunction_Recursive, false}                    ignore(fsgr_Test)
            ,{"fsg_r", EClassification_StaticFunction_Recursive, false}                    ignore(fsg_rTest)

            ,{"_", EClassification_FunctionParameter, true}                                ignore(_Test)
            ,{"p_", EClassification_FunctionParameter_Pack, true}                          ignore(p_Test)
            ,{"o_", EClassification_FunctionParameter_Output, true}                        ignore(o_Test)
            ,{"po_", EClassification_FunctionParameter_Output_Pack, true}                  ignore(po_Test)

            ,{"_o", EClassification_FunctionParameter_Output, true}                        ignore(_oTest) // Deprecate?
            ,{"p_o", EClassification_FunctionParameter_Output_Pack, true}                  ignore(p_oTest) // Deprecate?


            ,{"m_", EClassification_MemberVariablePublic, true}                            ignore(m_Test)
            ,{"mp_", EClassification_MemberVariablePrivate, true}                          ignore(mp_Test)

            ,{"D", EClassification_Macro, false}                                           ignore(DTest)
            ,{"d_", EClassification_MacroParameter, true}                                  ignore(d_Test)

            ,{"c", EClassification_Concept, false}                                         ignore(cTest)

            ,{"ms_", EClassification_MemberStaticVariablePublic, true}                     ignore(ms_Test)
            ,{"ms_f", EClassification_MemberStaticVariablePublic_Functor, false}           ignore(ms_fTest)
            ,{"msp_", EClassification_MemberStaticVariablePrivate, true}                   ignore(msp_Test)
            ,{"msp_f", EClassification_MemberStaticVariablePrivate_Functor, false}         ignore(msp_fTest)

            ,{"gs_", EClassification_GlobalStaticVariable, true}                           ignore(gs_Test)
            ,{"gs_f", EClassification_GlobalStaticVariable_Functor, false}                 ignore(gs_fTest)
            ,{"g_", EClassification_GlobalVariable, true}                                  ignore(g_Test)
            ,{"g_f", EClassification_GlobalVariable_Functor, false}                        ignore(g_fTest)
            ,{"s_", EClassification_StaticVariable, true}                                  ignore(s_Test)
            ,{"s_f", EClassification_StaticVariable_Functor, false}                        ignore(s_fTest)
        }
    ;
    const static int gc_MaxPrefixLen = 6;

    bool fg_StartsWith(std::string const &_String, char const *_pPrefix, size_t _PrefixLen)
    {
        if (_String.size() < _PrefixLen)
            return false;
        return std::equal(_pPrefix, _pPrefix + _PrefixLen, _String.begin());
    }

    bool fg_EndsWith(std::string const &_String, char const *_pPrefix, size_t _PrefixLen)
    {
        if (_String.size() < _PrefixLen)
            return false;
        return std::equal(_pPrefix, _pPrefix + _PrefixLen, _String.end() - _PrefixLen);
    }

    bool fg_IsUpperCase(char _Character)
    {
        unsigned char Char = (unsigned char)_Character;

        if (Char >= 'A' && Char <= 'Z')
            return true;

        if (Char >= '0' && Char <= '9')
            return true;

        if (Char >= 0xc0 && Char <= 0xdf)
            return true;

        return false;
    }

    struct CCharacterSet
    {
        uint32_t m_Set[256/4] = {0};

        CCharacterSet(char const *_pCharacters)
        {
            for (auto pChar = _pCharacters; *pChar; ++pChar)
            {
                unsigned char Char = *pChar;
                m_Set[Char / 32] |= Char & 31;
            }
        }

        bool f_IsMember(char _Charater)
        {
            unsigned char Char = _Charater;
            return (m_Set[Char / 32] & (Char & 31)) != 0;
        }
    };

    CCharacterSet g_ValidConcepCharacters{"binpfro"};

    bool fg_MatchVariablePrefix(std::string const &_Identifier, std::string const &_ToMatch, size_t _MatchLength, size_t _IdentLength)
    {
        if (!fg_StartsWith(_Identifier, _ToMatch.c_str(), _MatchLength))
            return false;

        size_t MatchLength = _MatchLength;
        size_t IdentLength = _IdentLength;
        if (IdentLength <= MatchLength)
            return false;

        char Character = _Identifier[MatchLength];

        if (fg_IsUpperCase(Character))
            return true;

        if (!g_ValidConcepCharacters.f_IsMember(Character))
            return false;

        if (IdentLength <= MatchLength + 1)
            return false;

        Character = _Identifier[MatchLength + 1];

        if (fg_IsUpperCase(Character))
            return true;

        return false;
    }

    bool fg_MatchOtherPrefix(std::string const &_Identifier, std::string const &_ToMatch, size_t _MatchLength, size_t _IdentLength)
    {
        if (_IdentLength <= _MatchLength)
            return false;
        if (fg_StartsWith(_Identifier, _ToMatch.c_str(), _MatchLength) && fg_IsUpperCase(_Identifier[_MatchLength]))
            return true;
        return false;
    }

    uint8_t fg_FindColor256(uint8_t _Red, uint8_t _Green, uint8_t _Blue)
    {
      uint8_t BestColor = 0;
      uint32_t SmallestError = std::numeric_limits<uint32_t>::max();

      constexpr static uint8_t c_CubeColors[6] = {0, 95, 135, 175, 215, 255};

      auto fBestComponent = [](uint8_t _Value)
        {
          uint8_t BestColor = 0;
          uint32_t SmallestError = std::numeric_limits<uint32_t>::max();
          for (size_t i = 0; i < 6; ++i)
          {
            uint32_t Error = std::abs(int32_t(_Value) - int32_t(c_CubeColors[i]));
            Error = Error * Error;
            if (Error < SmallestError)
            {
              SmallestError = Error;
              BestColor = i;
            }
          }
          return BestColor;
        }
      ;

      auto fError = [&](CDecodedColor const &_Color)
        {
          uint32_t ErrorRed = std::abs(int32_t(_Red) - int32_t(_Color.m_Red));
          uint32_t ErrorGreen = std::abs(int32_t(_Green) - int32_t(_Color.m_Green));
          uint32_t ErrorBlue = std::abs(int32_t(_Blue) - int32_t(_Color.m_Blue));
          return ErrorRed * ErrorRed + ErrorGreen * ErrorGreen + ErrorBlue * ErrorBlue;
        }
      ;

      {
        auto BestRed = fBestComponent(_Red);
        auto BestGreen = fBestComponent(_Green);
        auto BestBlue = fBestComponent(_Blue);
        BestColor = 16 + BestRed * 36 + BestGreen * 6 + BestBlue;
        SmallestError = fError(g_CommandLine_AnsiEncodingColor256Array[BestColor]);
      }

      for (size_t i = 232; i < 256; ++i)
      {
        auto &Color = g_CommandLine_AnsiEncodingColor256Array[i];

        uint32_t Error = fError(Color);
        if (Error < SmallestError)
        {
          SmallestError = Error;
          BestColor = i;
        }
      }

      return BestColor;
    }
}

class MalterlibClassifier::Internal {
public:
    Internal() {
        f_AddDefaultKeywords();

        int nPrefixes = sizeof(ms_PrefixMap) / sizeof(ms_PrefixMap[0]);

        for (int i = 0; i < nPrefixes; ++i) {
            struct CPrefixMap *pPrefix = &ms_PrefixMap[i];

            size_t PrefixLen = strlen(pPrefix->m_pPrefix);

            std::map<std::string, EClassification> *pMapInfo = NULL;
            if (pPrefix->m_bVariable)
                pMapInfo = &m_PrefixMapInfoVar[PrefixLen];
            else
                pMapInfo = &m_PrefixMapInfo[PrefixLen];

            (*pMapInfo)[pPrefix->m_pPrefix] = pPrefix->m_Classification;
        }

        f_PopulateStyles();
    }

    void f_AddDefaultKeyword(std::string const &_String, EClassification _Classification) {
        m_DefaultKeywords[_String] = _Classification;
    }

    void f_AddDefaultKeyword_CLike(std::string const &_String, EClassification _Classification) {
        m_DefaultKeywords_CLike[_String] = _Classification;
    }

    void f_AddDefaultKeyword_C(std::string const &_String, EClassification _Classification) {
        m_DefaultKeywords_C[_String] = _Classification;
    }

    void f_AddDefaultKeyword_Cpp(std::string const &_String, EClassification _Classification) {
        m_DefaultKeywords_Cpp[_String] = _Classification;
    }

    void f_AddDefaultKeywords() {
        // Qualifiers
        f_AddDefaultKeyword_C("const", EClassification_KeywordQualifier);
        f_AddDefaultKeyword_C("volatile", EClassification_KeywordQualifier);

        // Storage class
        f_AddDefaultKeyword_C("register", EClassification_KeywordStorageClass);
        f_AddDefaultKeyword_C("static", EClassification_KeywordStorageClass);
        f_AddDefaultKeyword_C("extern", EClassification_KeywordStorageClass);
        f_AddDefaultKeyword_C("mutable", EClassification_KeywordStorageClass);

        // built in types
        f_AddDefaultKeyword_C("bool", EClassification_KeywordBulitInTypes);
        f_AddDefaultKeyword_CLike("void", EClassification_KeywordBulitInTypes);
        f_AddDefaultKeyword_C("bint", EClassification_KeywordBulitInTypes);
        f_AddDefaultKeyword_C("zbint", EClassification_KeywordBulitInTypes);
        f_AddDefaultKeyword_C("zbool", EClassification_KeywordBulitInTypes);

        // built in character types
        f_AddDefaultKeyword_C("char", EClassification_KeywordBulitInCharacterTypes);
        f_AddDefaultKeyword_C("__wchar_t", EClassification_KeywordBulitInCharacterTypes);
        f_AddDefaultKeyword_C("wchar_t", EClassification_KeywordBulitInCharacterTypes);
        f_AddDefaultKeyword_C("ch8", EClassification_KeywordBulitInCharacterTypes);
        f_AddDefaultKeyword_C("ch16", EClassification_KeywordBulitInCharacterTypes);
        f_AddDefaultKeyword_C("ch32", EClassification_KeywordBulitInCharacterTypes);
        f_AddDefaultKeyword_C("uch8", EClassification_KeywordBulitInCharacterTypes);
        f_AddDefaultKeyword_C("uch16", EClassification_KeywordBulitInCharacterTypes);
        f_AddDefaultKeyword_C("uch32", EClassification_KeywordBulitInCharacterTypes);

        f_AddDefaultKeyword_C("zch8", EClassification_KeywordBulitInCharacterTypes);
        f_AddDefaultKeyword_C("zch16", EClassification_KeywordBulitInCharacterTypes);
        f_AddDefaultKeyword_C("zch32", EClassification_KeywordBulitInCharacterTypes);
        f_AddDefaultKeyword_C("zuch8", EClassification_KeywordBulitInCharacterTypes);
        f_AddDefaultKeyword_C("zuch16", EClassification_KeywordBulitInCharacterTypes);
        f_AddDefaultKeyword_C("zuch32", EClassification_KeywordBulitInCharacterTypes);
        f_AddDefaultKeyword_C("char16_t", EClassification_KeywordBulitInCharacterTypes);
        f_AddDefaultKeyword_C("char32_t", EClassification_KeywordBulitInCharacterTypes);
        f_AddDefaultKeyword_C("zuch32", EClassification_KeywordBulitInCharacterTypes);


        // built in integer types
        f_AddDefaultKeyword_C("int", EClassification_KeywordBulitInIntegerTypes);
        f_AddDefaultKeyword_C("size_t", EClassification_KeywordBulitInIntegerTypes);
        f_AddDefaultKeyword_C("__int16", EClassification_KeywordBulitInIntegerTypes);
        f_AddDefaultKeyword_C("__int32", EClassification_KeywordBulitInIntegerTypes);
        f_AddDefaultKeyword_C("__int64", EClassification_KeywordBulitInIntegerTypes);
        f_AddDefaultKeyword_C("__int8", EClassification_KeywordBulitInIntegerTypes);

        f_AddDefaultKeyword_C("int8", EClassification_KeywordBulitInIntegerTypes);
        f_AddDefaultKeyword_C("int16", EClassification_KeywordBulitInIntegerTypes);
        f_AddDefaultKeyword_C("int32", EClassification_KeywordBulitInIntegerTypes);
        f_AddDefaultKeyword_C("int64", EClassification_KeywordBulitInIntegerTypes);
        f_AddDefaultKeyword_C("int80", EClassification_KeywordBulitInIntegerTypes);
        f_AddDefaultKeyword_C("int128", EClassification_KeywordBulitInIntegerTypes);
        f_AddDefaultKeyword_C("int160", EClassification_KeywordBulitInIntegerTypes);
        f_AddDefaultKeyword_C("int256", EClassification_KeywordBulitInIntegerTypes);
        f_AddDefaultKeyword_C("int512", EClassification_KeywordBulitInIntegerTypes);
        f_AddDefaultKeyword_C("int1024", EClassification_KeywordBulitInIntegerTypes);
        f_AddDefaultKeyword_C("int2048", EClassification_KeywordBulitInIntegerTypes);
        f_AddDefaultKeyword_C("int4096", EClassification_KeywordBulitInIntegerTypes);
        f_AddDefaultKeyword_C("int8192", EClassification_KeywordBulitInIntegerTypes);


        f_AddDefaultKeyword_C("uint8", EClassification_KeywordBulitInIntegerTypes);
        f_AddDefaultKeyword_C("uint16", EClassification_KeywordBulitInIntegerTypes);
        f_AddDefaultKeyword_C("uint32", EClassification_KeywordBulitInIntegerTypes);
        f_AddDefaultKeyword_C("uint64", EClassification_KeywordBulitInIntegerTypes);
        f_AddDefaultKeyword_C("uint80", EClassification_KeywordBulitInIntegerTypes);
        f_AddDefaultKeyword_C("uint128", EClassification_KeywordBulitInIntegerTypes);
        f_AddDefaultKeyword_C("uint160", EClassification_KeywordBulitInIntegerTypes);
        f_AddDefaultKeyword_C("uint256", EClassification_KeywordBulitInIntegerTypes);
        f_AddDefaultKeyword_C("uint512", EClassification_KeywordBulitInIntegerTypes);
        f_AddDefaultKeyword_C("uint1024", EClassification_KeywordBulitInIntegerTypes);
        f_AddDefaultKeyword_C("uint2048", EClassification_KeywordBulitInIntegerTypes);
        f_AddDefaultKeyword_C("uint4096", EClassification_KeywordBulitInIntegerTypes);
        f_AddDefaultKeyword_C("uint8192", EClassification_KeywordBulitInIntegerTypes);

        f_AddDefaultKeyword_C("zint8", EClassification_KeywordBulitInIntegerTypes);
        f_AddDefaultKeyword_C("zuint8", EClassification_KeywordBulitInIntegerTypes);
        f_AddDefaultKeyword_C("zint16", EClassification_KeywordBulitInIntegerTypes);
        f_AddDefaultKeyword_C("zuint16", EClassification_KeywordBulitInIntegerTypes);
        f_AddDefaultKeyword_C("zint32", EClassification_KeywordBulitInIntegerTypes);
        f_AddDefaultKeyword_C("zuint32", EClassification_KeywordBulitInIntegerTypes);
        f_AddDefaultKeyword_C("zint64", EClassification_KeywordBulitInIntegerTypes);
        f_AddDefaultKeyword_C("zuint64", EClassification_KeywordBulitInIntegerTypes);
        f_AddDefaultKeyword_C("zint80", EClassification_KeywordBulitInIntegerTypes);
        f_AddDefaultKeyword_C("zuint80", EClassification_KeywordBulitInIntegerTypes);
        f_AddDefaultKeyword_C("zint128", EClassification_KeywordBulitInIntegerTypes);
        f_AddDefaultKeyword_C("zuint128", EClassification_KeywordBulitInIntegerTypes);
        f_AddDefaultKeyword_C("zint160", EClassification_KeywordBulitInIntegerTypes);
        f_AddDefaultKeyword_C("zuint160", EClassification_KeywordBulitInIntegerTypes);
        f_AddDefaultKeyword_C("zint256", EClassification_KeywordBulitInIntegerTypes);
        f_AddDefaultKeyword_C("zuint256", EClassification_KeywordBulitInIntegerTypes);
        f_AddDefaultKeyword_C("zint512", EClassification_KeywordBulitInIntegerTypes);
        f_AddDefaultKeyword_C("zuint512", EClassification_KeywordBulitInIntegerTypes);
        f_AddDefaultKeyword_C("zint1024", EClassification_KeywordBulitInIntegerTypes);
        f_AddDefaultKeyword_C("zuint1024", EClassification_KeywordBulitInIntegerTypes);
        f_AddDefaultKeyword_C("zint2048", EClassification_KeywordBulitInIntegerTypes);
        f_AddDefaultKeyword_C("zuint2048", EClassification_KeywordBulitInIntegerTypes);
        f_AddDefaultKeyword_C("zint4096", EClassification_KeywordBulitInIntegerTypes);
        f_AddDefaultKeyword_C("zuint4096", EClassification_KeywordBulitInIntegerTypes);
        f_AddDefaultKeyword_C("zint8192", EClassification_KeywordBulitInIntegerTypes);
        f_AddDefaultKeyword_C("zuint8192", EClassification_KeywordBulitInIntegerTypes);

        f_AddDefaultKeyword_C("mint", EClassification_KeywordBulitInIntegerTypes);
        f_AddDefaultKeyword_C("smint", EClassification_KeywordBulitInIntegerTypes);
        f_AddDefaultKeyword_C("umint", EClassification_KeywordBulitInIntegerTypes);
        f_AddDefaultKeyword_C("aint", EClassification_KeywordBulitInIntegerTypes);
        f_AddDefaultKeyword_C("uaint", EClassification_KeywordBulitInIntegerTypes);

        f_AddDefaultKeyword_C("zmint", EClassification_KeywordBulitInIntegerTypes);
        f_AddDefaultKeyword_C("zumint", EClassification_KeywordBulitInIntegerTypes);
        f_AddDefaultKeyword_C("zsmint", EClassification_KeywordBulitInIntegerTypes);
        f_AddDefaultKeyword_C("zamint", EClassification_KeywordBulitInIntegerTypes);
        f_AddDefaultKeyword_C("zuamint", EClassification_KeywordBulitInIntegerTypes);


        // builtin type modifiers
        f_AddDefaultKeyword_C("long", EClassification_KeywordBulitInTypeModifiers);
        f_AddDefaultKeyword_C("short", EClassification_KeywordBulitInTypeModifiers);
        f_AddDefaultKeyword_C("signed", EClassification_KeywordBulitInTypeModifiers);
        f_AddDefaultKeyword_C("unsigned", EClassification_KeywordBulitInTypeModifiers);

        // built in vector types
        f_AddDefaultKeyword_C("__m128", EClassification_KeywordBulitInVectorTypes);
        f_AddDefaultKeyword_C("__m64", EClassification_KeywordBulitInVectorTypes);
        f_AddDefaultKeyword_C("__w64", EClassification_KeywordBulitInVectorTypes);
        f_AddDefaultKeyword_C("__m128i", EClassification_KeywordBulitInVectorTypes);
        f_AddDefaultKeyword_C("__m128d", EClassification_KeywordBulitInVectorTypes);

        // built in floating point types
        f_AddDefaultKeyword_C("float", EClassification_KeywordBulitInFloatTypes);
        f_AddDefaultKeyword_C("double", EClassification_KeywordBulitInFloatTypes);

        f_AddDefaultKeyword_C("fp8", EClassification_KeywordBulitInFloatTypes);
        f_AddDefaultKeyword_C("fp16", EClassification_KeywordBulitInFloatTypes);
        f_AddDefaultKeyword_C("fp32", EClassification_KeywordBulitInFloatTypes);
        f_AddDefaultKeyword_C("fp64", EClassification_KeywordBulitInFloatTypes);
        f_AddDefaultKeyword_C("fp80", EClassification_KeywordBulitInFloatTypes);
        f_AddDefaultKeyword_C("fp128", EClassification_KeywordBulitInFloatTypes);
        f_AddDefaultKeyword_C("fp256", EClassification_KeywordBulitInFloatTypes);
        f_AddDefaultKeyword_C("fp512", EClassification_KeywordBulitInFloatTypes);
        f_AddDefaultKeyword_C("fp1024", EClassification_KeywordBulitInFloatTypes);
        f_AddDefaultKeyword_C("fp2048", EClassification_KeywordBulitInFloatTypes);
        f_AddDefaultKeyword_C("fp4096", EClassification_KeywordBulitInFloatTypes);
        f_AddDefaultKeyword_C("ufp8", EClassification_KeywordBulitInFloatTypes);
        f_AddDefaultKeyword_C("ufp16", EClassification_KeywordBulitInFloatTypes);
        f_AddDefaultKeyword_C("ufp32", EClassification_KeywordBulitInFloatTypes);
        f_AddDefaultKeyword_C("ufp64", EClassification_KeywordBulitInFloatTypes);
        f_AddDefaultKeyword_C("ufp80", EClassification_KeywordBulitInFloatTypes);
        f_AddDefaultKeyword_C("ufp128", EClassification_KeywordBulitInFloatTypes);
        f_AddDefaultKeyword_C("ufp256", EClassification_KeywordBulitInFloatTypes);
        f_AddDefaultKeyword_C("ufp512", EClassification_KeywordBulitInFloatTypes);
        f_AddDefaultKeyword_C("ufp1024", EClassification_KeywordBulitInFloatTypes);
        f_AddDefaultKeyword_C("ufp2048", EClassification_KeywordBulitInFloatTypes);
        f_AddDefaultKeyword_C("ufp4096", EClassification_KeywordBulitInFloatTypes);

        f_AddDefaultKeyword_C("zfp8", EClassification_KeywordBulitInFloatTypes);
        f_AddDefaultKeyword_C("zfp16", EClassification_KeywordBulitInFloatTypes);
        f_AddDefaultKeyword_C("zfp32", EClassification_KeywordBulitInFloatTypes);
        f_AddDefaultKeyword_C("zfp64", EClassification_KeywordBulitInFloatTypes);
        f_AddDefaultKeyword_C("zfp80", EClassification_KeywordBulitInFloatTypes);
        f_AddDefaultKeyword_C("zfp128", EClassification_KeywordBulitInFloatTypes);
        f_AddDefaultKeyword_C("zfp256", EClassification_KeywordBulitInFloatTypes);
        f_AddDefaultKeyword_C("zfp512", EClassification_KeywordBulitInFloatTypes);
        f_AddDefaultKeyword_C("zfp1024", EClassification_KeywordBulitInFloatTypes);
        f_AddDefaultKeyword_C("zfp2048", EClassification_KeywordBulitInFloatTypes);
        f_AddDefaultKeyword_C("zfp4096", EClassification_KeywordBulitInFloatTypes);
        f_AddDefaultKeyword_C("zufp8", EClassification_KeywordBulitInFloatTypes);
        f_AddDefaultKeyword_C("zufp16", EClassification_KeywordBulitInFloatTypes);
        f_AddDefaultKeyword_C("zufp32", EClassification_KeywordBulitInFloatTypes);
        f_AddDefaultKeyword_C("zufp64", EClassification_KeywordBulitInFloatTypes);
        f_AddDefaultKeyword_C("zufp80", EClassification_KeywordBulitInFloatTypes);
        f_AddDefaultKeyword_C("zufp128", EClassification_KeywordBulitInFloatTypes);
        f_AddDefaultKeyword_C("zufp256", EClassification_KeywordBulitInFloatTypes);
        f_AddDefaultKeyword_C("zufp512", EClassification_KeywordBulitInFloatTypes);
        f_AddDefaultKeyword_C("zufp1024", EClassification_KeywordBulitInFloatTypes);
        f_AddDefaultKeyword_C("zufp2048", EClassification_KeywordBulitInFloatTypes);
        f_AddDefaultKeyword_C("zufp4096", EClassification_KeywordBulitInFloatTypes);

        // bult in constants
        f_AddDefaultKeyword("false", EClassification_KeywordBulitInConstants);
        f_AddDefaultKeyword("true", EClassification_KeywordBulitInConstants);
        f_AddDefaultKeyword_C("nullptr", EClassification_KeywordBulitInConstants);
        f_AddDefaultKeyword_C("NULL", EClassification_KeywordBulitInConstants);


        // Exception handling
        f_AddDefaultKeyword_CLike("try", EClassification_KeywordExceptionHandling);
        f_AddDefaultKeyword_CLike("throw", EClassification_KeywordExceptionHandling);
        f_AddDefaultKeyword_CLike("catch", EClassification_KeywordExceptionHandling);
        f_AddDefaultKeyword_C("__try", EClassification_KeywordExceptionHandling);
        f_AddDefaultKeyword_C("__except", EClassification_KeywordExceptionHandling);
        f_AddDefaultKeyword_C("__finally", EClassification_KeywordExceptionHandling);
        f_AddDefaultKeyword_C("__leave", EClassification_KeywordExceptionHandling);
        f_AddDefaultKeyword_C("__raise", EClassification_KeywordExceptionHandling);
        f_AddDefaultKeyword_CLike("finally", EClassification_KeywordExceptionHandling);

        // Type introspection/type traits
        f_AddDefaultKeyword_C("__alignof", EClassification_KeywordIntrospection);
        f_AddDefaultKeyword_C("sizeof", EClassification_KeywordIntrospection);
        f_AddDefaultKeyword_C("decltype", EClassification_KeywordIntrospection);
        f_AddDefaultKeyword_C("__uuidof", EClassification_KeywordIntrospection);
        f_AddDefaultKeyword_C("typeid", EClassification_KeywordIntrospection);

        // Static assert
        f_AddDefaultKeyword_C("static_assert", EClassification_KeywordStaticAssert);

        // Control statements
        f_AddDefaultKeyword_CLike("while", EClassification_KeywordControlStatement);
        f_AddDefaultKeyword_CLike("for", EClassification_KeywordControlStatement);
        f_AddDefaultKeyword_CLike("goto", EClassification_KeywordControlStatement);
        f_AddDefaultKeyword_CLike("if", EClassification_KeywordControlStatement);
        f_AddDefaultKeyword_CLike("do", EClassification_KeywordControlStatement);
        f_AddDefaultKeyword_CLike("break", EClassification_KeywordControlStatement);
        f_AddDefaultKeyword_CLike("case", EClassification_KeywordControlStatement);
        f_AddDefaultKeyword_CLike("continue", EClassification_KeywordControlStatement);
        f_AddDefaultKeyword_CLike("default", EClassification_KeywordControlStatement);
        f_AddDefaultKeyword_CLike("else", EClassification_KeywordControlStatement);
        f_AddDefaultKeyword_CLike("return", EClassification_KeywordControlStatement);
        f_AddDefaultKeyword_CLike("switch", EClassification_KeywordControlStatement);

        f_AddDefaultKeyword_C("likely", EClassification_KeywordControlStatement);
        f_AddDefaultKeyword_C("unlikely", EClassification_KeywordControlStatement);
        f_AddDefaultKeyword_C("assume", EClassification_KeywordControlStatement);

        f_AddDefaultKeyword_C("yield_cpu", EClassification_KeywordControlStatement);


        f_AddDefaultKeyword_C("constant_int64", EClassification_KeywordControlStatement);
        f_AddDefaultKeyword_C("constant_uint64", EClassification_KeywordControlStatement);

        // Optimization
        f_AddDefaultKeyword_C("__asm", EClassification_KeywordOptimization);
        f_AddDefaultKeyword_C("__assume", EClassification_KeywordOptimization);

        // Property modifiers
        f_AddDefaultKeyword_C("__unaligned", EClassification_KeywordPropertyModifiers);
        f_AddDefaultKeyword_C("__declspec", EClassification_KeywordPropertyModifiers);
        f_AddDefaultKeyword_C("__based", EClassification_KeywordPropertyModifiers);
        f_AddDefaultKeyword_C("dllexport", EClassification_KeywordPropertyModifiers);
        f_AddDefaultKeyword_C("dllimport", EClassification_KeywordPropertyModifiers);
        f_AddDefaultKeyword_C("naked", EClassification_KeywordPropertyModifiers);
        f_AddDefaultKeyword_C("noinline", EClassification_KeywordPropertyModifiers);
        f_AddDefaultKeyword_C("nothrow", EClassification_KeywordPropertyModifiers);
        f_AddDefaultKeyword_C("noexcept", EClassification_KeywordPropertyModifiers);
        f_AddDefaultKeyword_C("novtable", EClassification_KeywordPropertyModifiers);
        f_AddDefaultKeyword_C("property", EClassification_KeywordPropertyModifiers);
        f_AddDefaultKeyword_C("selectany", EClassification_KeywordPropertyModifiers);
        f_AddDefaultKeyword_C("thread", EClassification_KeywordPropertyModifiers);
        f_AddDefaultKeyword_C("uuid", EClassification_KeywordPropertyModifiers);
        f_AddDefaultKeyword_C("explicit", EClassification_KeywordPropertyModifiers);
        f_AddDefaultKeyword_C("__forceinline", EClassification_KeywordPropertyModifiers);
        f_AddDefaultKeyword_C("__inline", EClassification_KeywordPropertyModifiers);
        f_AddDefaultKeyword_C("inline", EClassification_KeywordPropertyModifiers);
        f_AddDefaultKeyword_C("__cdecl", EClassification_KeywordPropertyModifiers);
        f_AddDefaultKeyword_C("__thiscall", EClassification_KeywordPropertyModifiers);
        f_AddDefaultKeyword_C("__fastcall", EClassification_KeywordPropertyModifiers);
        f_AddDefaultKeyword_C("__stdcall", EClassification_KeywordPropertyModifiers);
        f_AddDefaultKeyword_C("calling_convention_c", EClassification_KeywordPropertyModifiers);
        f_AddDefaultKeyword_C("cdecl", EClassification_KeywordPropertyModifiers);
        f_AddDefaultKeyword_C("stdcall", EClassification_KeywordPropertyModifiers);
        f_AddDefaultKeyword_C("fastcall", EClassification_KeywordPropertyModifiers);
        f_AddDefaultKeyword_C("inline_small", EClassification_KeywordPropertyModifiers);
        f_AddDefaultKeyword_C("inline_always", EClassification_KeywordPropertyModifiers);
        f_AddDefaultKeyword_C("inline_always_lto", EClassification_KeywordPropertyModifiers);
        f_AddDefaultKeyword_C("inline_never", EClassification_KeywordPropertyModifiers);
        f_AddDefaultKeyword_C("inline_never_debug", EClassification_KeywordPropertyModifiers);
        f_AddDefaultKeyword_C("inline_medium", EClassification_KeywordPropertyModifiers);
        f_AddDefaultKeyword_C("inline_large", EClassification_KeywordPropertyModifiers);
        f_AddDefaultKeyword_C("inline_extralarge", EClassification_KeywordPropertyModifiers);
        f_AddDefaultKeyword_C("inline_always_debug", EClassification_KeywordPropertyModifiers);
        f_AddDefaultKeyword_C("inline_always_lambda", EClassification_KeywordPropertyModifiers);
        f_AddDefaultKeyword_C("module_export", EClassification_KeywordPropertyModifiers);
        f_AddDefaultKeyword_C("module_import", EClassification_KeywordPropertyModifiers);
        f_AddDefaultKeyword_C("only_parameters_aliased", EClassification_KeywordPropertyModifiers);
        f_AddDefaultKeyword_C("return_not_aliased", EClassification_KeywordPropertyModifiers);
        f_AddDefaultKeyword_C("function_does_not_return", EClassification_KeywordPropertyModifiers);
        f_AddDefaultKeyword_C("variable_not_aliased", EClassification_KeywordPropertyModifiers);
        f_AddDefaultKeyword_C("constexpr", EClassification_KeywordPropertyModifiers);
        f_AddDefaultKeyword_C("__pragma", EClassification_KeywordPropertyModifiers);
        f_AddDefaultKeyword_C("__attribute__", EClassification_KeywordPropertyModifiers);
        f_AddDefaultKeyword_C("__restrict__", EClassification_KeywordPropertyModifiers);
        f_AddDefaultKeyword_C("assure_used", EClassification_KeywordPropertyModifiers);
        f_AddDefaultKeyword_C("align_cacheline", EClassification_KeywordPropertyModifiers);
        f_AddDefaultKeyword_C("intrinsic", EClassification_KeywordPropertyModifiers);
        f_AddDefaultKeyword_C("mark_nodebug", EClassification_KeywordPropertyModifiers);
        f_AddDefaultKeyword_C("mark_no_coroutine_debug", EClassification_KeywordPropertyModifiers);
        f_AddDefaultKeyword_C("mark_artificial", EClassification_KeywordPropertyModifiers);

        f_AddDefaultKeyword_C("noreturn", EClassification_KeywordPropertyModifiers);
        f_AddDefaultKeyword_C("carries_dependency", EClassification_KeywordPropertyModifiers);
        f_AddDefaultKeyword_C("deprecated", EClassification_KeywordPropertyModifiers);
        f_AddDefaultKeyword_C("fallthrough", EClassification_KeywordPropertyModifiers);
        f_AddDefaultKeyword_C("nodiscard", EClassification_KeywordPropertyModifiers);
        f_AddDefaultKeyword_C("maybe_unused", EClassification_KeywordPropertyModifiers);
        f_AddDefaultKeyword_C("no_unique_address", EClassification_KeywordPropertyModifiers);
        f_AddDefaultKeyword_C("optimize_for_synchronized", EClassification_KeywordPropertyModifiers);

        // new/delete operators
        f_AddDefaultKeyword_CLike("delete", EClassification_KeywordNewDelete);
        f_AddDefaultKeyword_CLike("new", EClassification_KeywordNewDelete);

        // CLR
        f_AddDefaultKeyword_C("__abstract", EClassification_KeywordCLR);
        f_AddDefaultKeyword_C("abstract", EClassification_KeywordCLR);
        f_AddDefaultKeyword_C("__box", EClassification_KeywordCLR);
        f_AddDefaultKeyword_C("__delegate", EClassification_KeywordCLR);
        f_AddDefaultKeyword_C("__gc", EClassification_KeywordCLR);
        f_AddDefaultKeyword_C("__hook", EClassification_KeywordCLR);
        f_AddDefaultKeyword_C("__nogc", EClassification_KeywordCLR);
        f_AddDefaultKeyword_C("__pin", EClassification_KeywordCLR);
        f_AddDefaultKeyword_C("__property", EClassification_KeywordCLR);
        f_AddDefaultKeyword_C("__sealed", EClassification_KeywordCLR);
        f_AddDefaultKeyword_C("__try_cast", EClassification_KeywordCLR);
        f_AddDefaultKeyword_C("__unhook", EClassification_KeywordCLR);
        f_AddDefaultKeyword_C("__value", EClassification_KeywordCLR);
        // f_AddDefaultKeyword_C("array", EClassification_KeywordCLR); // used in stdlib
        // f_AddDefaultKeyword_C("delegate", EClassification_KeywordCLR); // too generic
        f_AddDefaultKeyword_C("event", EClassification_KeywordCLR);
        f_AddDefaultKeyword_C("__identifier", EClassification_KeywordCLR);
        f_AddDefaultKeyword_C("friend_as", EClassification_KeywordCLR);
        f_AddDefaultKeyword_C("interface", EClassification_KeywordCLR);
        f_AddDefaultKeyword_C("interior_ptr", EClassification_KeywordCLR);
        f_AddDefaultKeyword_C("gcnew", EClassification_KeywordCLR);
        f_AddDefaultKeyword_C("generic", EClassification_KeywordCLR);
        f_AddDefaultKeyword_C("initonly", EClassification_KeywordCLR);
        f_AddDefaultKeyword_C("literal", EClassification_KeywordCLR);
        f_AddDefaultKeyword_C("ref", EClassification_KeywordCLR);
        f_AddDefaultKeyword_C("safecast", EClassification_KeywordCLR);
        //  f_AddDefaultKeyword_C("value", EClassification_KeywordCLR); // used in stdlib

        // Other keywords
        f_AddDefaultKeyword_C("__event", EClassification_KeywordOther);
        f_AddDefaultKeyword_C("__if_exists", EClassification_KeywordOther);
        f_AddDefaultKeyword_C("__if_not_exists", EClassification_KeywordOther);
        f_AddDefaultKeyword_C("__interface", EClassification_KeywordOther);
        f_AddDefaultKeyword_C("__multiple_inheritance", EClassification_KeywordOther);
        f_AddDefaultKeyword_C("__single_inheritance", EClassification_KeywordOther);
        f_AddDefaultKeyword_C("__virtual_inheritance", EClassification_KeywordOther);
        f_AddDefaultKeyword_C("__super", EClassification_KeywordOther);
        f_AddDefaultKeyword_C("__noop", EClassification_KeywordOther);

        // Type specification keywords
        f_AddDefaultKeyword_C("union", EClassification_KeywordTypeSpecification);
        f_AddDefaultKeyword_C("class", EClassification_KeywordTypeSpecification);
        f_AddDefaultKeyword_C("enum", EClassification_KeywordTypeSpecification);
        f_AddDefaultKeyword_C("struct", EClassification_KeywordTypeSpecification);

        // namespace
        f_AddDefaultKeyword_C("namespace", EClassification_KeywordNamespace);

        // typename
        f_AddDefaultKeyword_C("typename", EClassification_KeywordTypename);

        // template
        f_AddDefaultKeyword_C("template", EClassification_KeywordTemplate);

        // typedef
        f_AddDefaultKeyword_C("typedef", EClassification_KeywordTypedef);

        // using
        f_AddDefaultKeyword_C("using", EClassification_KeywordUsing);

        // auto
        f_AddDefaultKeyword_Cpp("auto", EClassification_KeywordAuto);

        // this
        f_AddDefaultKeyword_CLike("this", EClassification_KeywordThis);

        // operator
        f_AddDefaultKeyword_CLike("operator", EClassification_KeywordOperator);

        // Access keywords
        f_AddDefaultKeyword_C("friend", EClassification_KeywordAccess);
        f_AddDefaultKeyword_C("private", EClassification_KeywordAccess);
        f_AddDefaultKeyword_C("public", EClassification_KeywordAccess);
        f_AddDefaultKeyword_C("protected", EClassification_KeywordAccess);

        // Virtual keywords
        f_AddDefaultKeyword_C("final", EClassification_KeywordVirtual);
        f_AddDefaultKeyword_C("sealed", EClassification_KeywordVirtual);
        f_AddDefaultKeyword_C("override", EClassification_KeywordVirtual);
        f_AddDefaultKeyword_C("virtual", EClassification_KeywordVirtual);
        f_AddDefaultKeyword_C("pure", EClassification_KeywordPure);

        // casts
        f_AddDefaultKeyword_C("const_cast", EClassification_KeywordCasts);
        f_AddDefaultKeyword_C("dynamic_cast", EClassification_KeywordCasts);
        f_AddDefaultKeyword_C("reinterpret_cast", EClassification_KeywordCasts);
        f_AddDefaultKeyword_C("static_cast", EClassification_KeywordCasts);

        // ignore
        f_AddDefaultKeyword_C("ignore", EClassification_KeywordPropertyModifiers);

        // Preprocessor directive
        f_AddDefaultKeyword_C("#define", EClassification_PreprocessorDirective);
        f_AddDefaultKeyword_C("#error", EClassification_PreprocessorDirective);
        f_AddDefaultKeyword_C("#import", EClassification_PreprocessorDirective);
        f_AddDefaultKeyword_C("#undef", EClassification_PreprocessorDirective);
        f_AddDefaultKeyword_C("#elif", EClassification_PreprocessorDirective);
        f_AddDefaultKeyword_C("#if", EClassification_PreprocessorDirective);
        f_AddDefaultKeyword_C("#include", EClassification_PreprocessorDirective);
        f_AddDefaultKeyword_C("#using", EClassification_PreprocessorDirective);
        f_AddDefaultKeyword_C("#else", EClassification_PreprocessorDirective);
        f_AddDefaultKeyword_C("#ifdef", EClassification_PreprocessorDirective);
        f_AddDefaultKeyword_C("#line", EClassification_PreprocessorDirective);
        f_AddDefaultKeyword_C("#endif", EClassification_PreprocessorDirective);
        f_AddDefaultKeyword_C("#ifndef", EClassification_PreprocessorDirective);
        f_AddDefaultKeyword_C("#pragma", EClassification_PreprocessorDirective);

        f_AddDefaultKeyword_C("define", EClassification_PreprocessorDirective);
        f_AddDefaultKeyword_C("error", EClassification_PreprocessorDirective);
        f_AddDefaultKeyword_C("import", EClassification_PreprocessorDirective);
        f_AddDefaultKeyword_C("undef", EClassification_PreprocessorDirective);
        f_AddDefaultKeyword_C("elif", EClassification_PreprocessorDirective);
        //f_AddDefaultKeyword("if", EClassification_PreprocessorDirective);
        f_AddDefaultKeyword_C("include", EClassification_PreprocessorDirective);
        f_AddDefaultKeyword_C("once", EClassification_PreprocessorDirective);
        f_AddDefaultKeyword_C("defined", EClassification_PreprocessorDirective);
        f_AddDefaultKeyword_C("using", EClassification_PreprocessorDirective);
        //f_AddDefaultKeyword("else", EClassification_PreprocessorDirective);
        f_AddDefaultKeyword_C("ifdef", EClassification_PreprocessorDirective);
        f_AddDefaultKeyword_C("line", EClassification_PreprocessorDirective);
        f_AddDefaultKeyword_C("endif", EClassification_PreprocessorDirective);
        f_AddDefaultKeyword_C("ifndef", EClassification_PreprocessorDirective);
        f_AddDefaultKeyword_C("pragma", EClassification_PreprocessorDirective);

        // std lib
        f_AddDefaultKeyword_Cpp("std", EClassification_Namespace);
        f_AddDefaultKeyword_Cpp("atomic", EClassification_TemplateType);
        f_AddDefaultKeyword_Cpp("set", EClassification_TemplateType);
        f_AddDefaultKeyword_Cpp("multiset", EClassification_TemplateType);
        f_AddDefaultKeyword_Cpp("map", EClassification_TemplateType);
        f_AddDefaultKeyword_Cpp("multimap", EClassification_TemplateType);
        f_AddDefaultKeyword_Cpp("unordered_set", EClassification_TemplateType);
        f_AddDefaultKeyword_Cpp("unordered_multiset", EClassification_TemplateType);
        f_AddDefaultKeyword_Cpp("unordered_map", EClassification_TemplateType);
        f_AddDefaultKeyword_Cpp("unordered_multimap", EClassification_TemplateType);
        f_AddDefaultKeyword_Cpp("list", EClassification_TemplateType);
        f_AddDefaultKeyword_Cpp("vector", EClassification_TemplateType);
        f_AddDefaultKeyword_Cpp("queue", EClassification_TemplateType);
        f_AddDefaultKeyword_Cpp("priority_queue", EClassification_TemplateType);
        f_AddDefaultKeyword_Cpp("forward_list", EClassification_TemplateType);
        f_AddDefaultKeyword_Cpp("deque", EClassification_TemplateType);
        f_AddDefaultKeyword_Cpp("array", EClassification_TemplateType);
        f_AddDefaultKeyword_Cpp("stack", EClassification_TemplateType);
        f_AddDefaultKeyword_Cpp("basic_ifstream", EClassification_TemplateType);
        f_AddDefaultKeyword_Cpp("basic_ofstream", EClassification_TemplateType);
        f_AddDefaultKeyword_Cpp("basic_fstream", EClassification_TemplateType);
        f_AddDefaultKeyword_Cpp("basic_filebuf", EClassification_TemplateType);
        f_AddDefaultKeyword_Cpp("basic_string", EClassification_TemplateType);
        f_AddDefaultKeyword_Cpp("char_traits", EClassification_TemplateType);
        f_AddDefaultKeyword_Cpp("tuple", EClassification_TemplateType);
        f_AddDefaultKeyword_Cpp("pair", EClassification_TemplateType);
        f_AddDefaultKeyword_Cpp("coroutine_handle", EClassification_TemplateType);
        f_AddDefaultKeyword_Cpp("coroutine_traits", EClassification_TemplateType);

        f_AddDefaultKeyword_Cpp("string", EClassification_Type);
        f_AddDefaultKeyword_Cpp("u16string", EClassification_Type);
        f_AddDefaultKeyword_Cpp("u32string", EClassification_Type);
        f_AddDefaultKeyword_Cpp("wstring", EClassification_Type);
        f_AddDefaultKeyword_Cpp("ifstream", EClassification_Type);
        f_AddDefaultKeyword_Cpp("ofstream", EClassification_Type);
        f_AddDefaultKeyword_Cpp("fstream", EClassification_Type);
        f_AddDefaultKeyword_Cpp("filebuf", EClassification_Type);
        f_AddDefaultKeyword_Cpp("wifstream", EClassification_Type);
        f_AddDefaultKeyword_Cpp("wofstream", EClassification_Type);
        f_AddDefaultKeyword_Cpp("wfstream", EClassification_Type);
        f_AddDefaultKeyword_Cpp("wfilebuf", EClassification_Type);
        f_AddDefaultKeyword_Cpp("atomic_flag", EClassification_Type);
        f_AddDefaultKeyword_Cpp("iterator", EClassification_Type);
        f_AddDefaultKeyword_Cpp("const_iterator", EClassification_Type);
        f_AddDefaultKeyword_Cpp("value_type", EClassification_Type);
        f_AddDefaultKeyword_Cpp("allocator_type", EClassification_Type);
        f_AddDefaultKeyword_Cpp("type", EClassification_Type);
        f_AddDefaultKeyword_Cpp("reference", EClassification_Type);
        f_AddDefaultKeyword_Cpp("const_reference", EClassification_Type);
        f_AddDefaultKeyword_Cpp("pointer", EClassification_Type);
        f_AddDefaultKeyword_Cpp("const_pointer", EClassification_Type);
        f_AddDefaultKeyword_Cpp("reverse_iterator", EClassification_Type);
        f_AddDefaultKeyword_Cpp("const_reverse_iterator", EClassification_Type);
        f_AddDefaultKeyword_Cpp("difference_type", EClassification_Type);
        f_AddDefaultKeyword_Cpp("size_type", EClassification_Type);
        f_AddDefaultKeyword_Cpp("key_compare", EClassification_Type);
        f_AddDefaultKeyword_Cpp("value_compare", EClassification_Type);
        f_AddDefaultKeyword_Cpp("key_type", EClassification_Type);
        f_AddDefaultKeyword_Cpp("mapped_type", EClassification_Type);
        f_AddDefaultKeyword_Cpp("hasher", EClassification_Type);
        f_AddDefaultKeyword_Cpp("key_equal", EClassification_Type);
        f_AddDefaultKeyword_Cpp("local_iterator", EClassification_Type);
        f_AddDefaultKeyword_Cpp("const_local_iterator", EClassification_Type);
        f_AddDefaultKeyword_Cpp("char_type", EClassification_Type);
        f_AddDefaultKeyword_Cpp("traits_type", EClassification_Type);
        f_AddDefaultKeyword_Cpp("int_type", EClassification_Type);
        f_AddDefaultKeyword_Cpp("pos_type", EClassification_Type);
        f_AddDefaultKeyword_Cpp("off_type", EClassification_Type);
        f_AddDefaultKeyword_Cpp("state_type", EClassification_Type);
        f_AddDefaultKeyword_Cpp("suspend_never", EClassification_Type);
        f_AddDefaultKeyword_Cpp("suspend_always", EClassification_Type);

        f_AddDefaultKeyword_Cpp("length", EClassification_MemberFunctionPublic);
        f_AddDefaultKeyword_Cpp("fill", EClassification_MemberFunctionPublic);
        f_AddDefaultKeyword_Cpp("data", EClassification_MemberFunctionPublic);
        f_AddDefaultKeyword_Cpp("size", EClassification_MemberFunctionPublic);
        f_AddDefaultKeyword_Cpp("empty", EClassification_MemberFunctionPublic);
        f_AddDefaultKeyword_Cpp("max_size", EClassification_MemberFunctionPublic);
        f_AddDefaultKeyword_Cpp("at", EClassification_MemberFunctionPublic);
        f_AddDefaultKeyword_Cpp("insert", EClassification_MemberFunctionPublic);
        f_AddDefaultKeyword_Cpp("erase", EClassification_MemberFunctionPublic);
        f_AddDefaultKeyword_Cpp("clear", EClassification_MemberFunctionPublic);
        f_AddDefaultKeyword_Cpp("emplace", EClassification_MemberFunctionPublic);
        f_AddDefaultKeyword_Cpp("emplace_hint", EClassification_MemberFunctionPublic);
        f_AddDefaultKeyword_Cpp("key_comp", EClassification_MemberFunctionPublic);
        f_AddDefaultKeyword_Cpp("value_comp", EClassification_MemberFunctionPublic);
        f_AddDefaultKeyword_Cpp("find", EClassification_MemberFunctionPublic);
        f_AddDefaultKeyword_Cpp("count", EClassification_MemberFunctionPublic);
        f_AddDefaultKeyword_Cpp("lower_bound", EClassification_MemberFunctionPublic);
        f_AddDefaultKeyword_Cpp("upper_bound", EClassification_MemberFunctionPublic);
        f_AddDefaultKeyword_Cpp("equal_range", EClassification_MemberFunctionPublic);
        f_AddDefaultKeyword_Cpp("get_allocator", EClassification_MemberFunctionPublic);
        f_AddDefaultKeyword_Cpp("front", EClassification_MemberFunctionPublic);
        f_AddDefaultKeyword_Cpp("back", EClassification_MemberFunctionPublic);
        f_AddDefaultKeyword_Cpp("push", EClassification_MemberFunctionPublic);
        f_AddDefaultKeyword_Cpp("pop", EClassification_MemberFunctionPublic);
        f_AddDefaultKeyword_Cpp("top", EClassification_MemberFunctionPublic);
        f_AddDefaultKeyword_Cpp("assign", EClassification_MemberFunctionPublic);
        f_AddDefaultKeyword_Cpp("emplace_front", EClassification_MemberFunctionPublic);
        f_AddDefaultKeyword_Cpp("emplace_back", EClassification_MemberFunctionPublic);
        f_AddDefaultKeyword_Cpp("push_front", EClassification_MemberFunctionPublic);
        f_AddDefaultKeyword_Cpp("push_back", EClassification_MemberFunctionPublic);
        f_AddDefaultKeyword_Cpp("pop_front", EClassification_MemberFunctionPublic);
        f_AddDefaultKeyword_Cpp("pop_back", EClassification_MemberFunctionPublic);
        f_AddDefaultKeyword_Cpp("resize", EClassification_MemberFunctionPublic);
        f_AddDefaultKeyword_Cpp("splice", EClassification_MemberFunctionPublic);
        f_AddDefaultKeyword_Cpp("remove", EClassification_MemberFunctionPublic);
        f_AddDefaultKeyword_Cpp("remove_if", EClassification_MemberFunctionPublic);
        f_AddDefaultKeyword_Cpp("unique", EClassification_MemberFunctionPublic);
        f_AddDefaultKeyword_Cpp("merge", EClassification_MemberFunctionPublic);
        f_AddDefaultKeyword_Cpp("sort", EClassification_MemberFunctionPublic);
        f_AddDefaultKeyword_Cpp("reverse", EClassification_MemberFunctionPublic);
        f_AddDefaultKeyword_Cpp("before_begin", EClassification_MemberFunctionPublic);
        f_AddDefaultKeyword_Cpp("cbefore_begin", EClassification_MemberFunctionPublic);
        f_AddDefaultKeyword_Cpp("emplace_after", EClassification_MemberFunctionPublic);
        f_AddDefaultKeyword_Cpp("insert_after", EClassification_MemberFunctionPublic);
        f_AddDefaultKeyword_Cpp("erase_after", EClassification_MemberFunctionPublic);
        f_AddDefaultKeyword_Cpp("splice_after", EClassification_MemberFunctionPublic);
        f_AddDefaultKeyword_Cpp("shrink_to_fit", EClassification_MemberFunctionPublic);
        f_AddDefaultKeyword_Cpp("bucket_count", EClassification_MemberFunctionPublic);
        f_AddDefaultKeyword_Cpp("bucket_size", EClassification_MemberFunctionPublic);
        f_AddDefaultKeyword_Cpp("bucket", EClassification_MemberFunctionPublic);
        f_AddDefaultKeyword_Cpp("max_bucket_count", EClassification_MemberFunctionPublic);
        f_AddDefaultKeyword_Cpp("load_factor", EClassification_MemberFunctionPublic);
        f_AddDefaultKeyword_Cpp("max_load_factor", EClassification_MemberFunctionPublic);
        f_AddDefaultKeyword_Cpp("rehash", EClassification_MemberFunctionPublic);
        f_AddDefaultKeyword_Cpp("reserve", EClassification_MemberFunctionPublic);
        f_AddDefaultKeyword_Cpp("hash_fuction", EClassification_MemberFunctionPublic);
        f_AddDefaultKeyword_Cpp("key_eq", EClassification_MemberFunctionPublic);
        f_AddDefaultKeyword_Cpp("capacity", EClassification_MemberFunctionPublic);
        f_AddDefaultKeyword_Cpp("c_str", EClassification_MemberFunctionPublic);
        f_AddDefaultKeyword_Cpp("find", EClassification_MemberFunctionPublic);
        f_AddDefaultKeyword_Cpp("rfind", EClassification_MemberFunctionPublic);
        f_AddDefaultKeyword_Cpp("copy", EClassification_MemberFunctionPublic);
        f_AddDefaultKeyword_Cpp("find_first_of", EClassification_MemberFunctionPublic);
        f_AddDefaultKeyword_Cpp("find_last_of", EClassification_MemberFunctionPublic);
        f_AddDefaultKeyword_Cpp("find_first_not_of", EClassification_MemberFunctionPublic);
        f_AddDefaultKeyword_Cpp("fint_last_not_of", EClassification_MemberFunctionPublic);
        f_AddDefaultKeyword_Cpp("substr", EClassification_MemberFunctionPublic);
        f_AddDefaultKeyword_Cpp("compare", EClassification_MemberFunctionPublic);
        f_AddDefaultKeyword_Cpp("resume", EClassification_MemberFunctionPublic);
        f_AddDefaultKeyword_Cpp("address", EClassification_MemberFunctionPublic);
        f_AddDefaultKeyword_Cpp("from_address", EClassification_MemberFunctionPublic);
        f_AddDefaultKeyword_Cpp("destroy", EClassification_MemberFunctionPublic);
        f_AddDefaultKeyword_Cpp("promise", EClassification_MemberFunctionPublic);


        f_AddDefaultKeyword_Cpp("memory_order", EClassification_Enum);
        f_AddDefaultKeyword_Cpp("memory_order_relaxed", EClassification_Enumerator);
        f_AddDefaultKeyword_Cpp("memory_order_consume", EClassification_Enumerator);
        f_AddDefaultKeyword_Cpp("memory_order_acquire", EClassification_Enumerator);
        f_AddDefaultKeyword_Cpp("memory_order_release", EClassification_Enumerator);
        f_AddDefaultKeyword_Cpp("memory_order_acq_rel", EClassification_Enumerator);
        f_AddDefaultKeyword_Cpp("memory_order_seq_cst", EClassification_Enumerator);

        f_AddDefaultKeyword_Cpp("is_lock_free", EClassification_MemberFunctionPublic);
        f_AddDefaultKeyword_Cpp("store", EClassification_MemberFunctionPublic);
        f_AddDefaultKeyword_Cpp("load", EClassification_MemberFunctionPublic);
        f_AddDefaultKeyword_Cpp("exchange", EClassification_MemberFunctionPublic);
        f_AddDefaultKeyword_Cpp("compare_exchange_weak", EClassification_MemberFunctionPublic);
        f_AddDefaultKeyword_Cpp("compare_exchange_strong", EClassification_MemberFunctionPublic);
        f_AddDefaultKeyword_Cpp("fetch_add", EClassification_MemberFunctionPublic);
        f_AddDefaultKeyword_Cpp("fetch_sub", EClassification_MemberFunctionPublic);
        f_AddDefaultKeyword_Cpp("fetch_and", EClassification_MemberFunctionPublic);
        f_AddDefaultKeyword_Cpp("fetch_or", EClassification_MemberFunctionPublic);
        f_AddDefaultKeyword_Cpp("fetch_xor", EClassification_MemberFunctionPublic);

        f_AddDefaultKeyword_Cpp("tie", EClassification_Function);
        f_AddDefaultKeyword_Cpp("make_tuple", EClassification_Function);
        f_AddDefaultKeyword_Cpp("forward_as_tuple", EClassification_Function);
        f_AddDefaultKeyword_Cpp("tuple_cat", EClassification_Function);

        f_AddDefaultKeyword_Cpp("npos", EClassification_MemberConstantPublic);
        f_AddDefaultKeyword_Cpp("value", EClassification_MemberConstantPublic);
        f_AddDefaultKeyword_Cpp("getline", EClassification_Function);
        f_AddDefaultKeyword_Cpp("min", EClassification_Function);
        f_AddDefaultKeyword_Cpp("max", EClassification_Function);
        f_AddDefaultKeyword_Cpp("assert", EClassification_Macro);

        f_AddDefaultKeyword_Cpp("integral_constant", EClassification_TemplateType);
        f_AddDefaultKeyword_Cpp("true_type", EClassification_Type);
        f_AddDefaultKeyword_Cpp("false_type", EClassification_Type);

        f_AddDefaultKeyword_Cpp("is_array", EClassification_TemplateType);
        f_AddDefaultKeyword_Cpp("is_class", EClassification_TemplateType);
        f_AddDefaultKeyword_Cpp("is_enum", EClassification_TemplateType);
        f_AddDefaultKeyword_Cpp("is_floating_point", EClassification_TemplateType);
        f_AddDefaultKeyword_Cpp("is_function", EClassification_TemplateType);
        f_AddDefaultKeyword_Cpp("is_integral", EClassification_TemplateType);
        f_AddDefaultKeyword_Cpp("is_lvalue_reference", EClassification_TemplateType);
        f_AddDefaultKeyword_Cpp("is_member_function_pointer", EClassification_TemplateType);
        f_AddDefaultKeyword_Cpp("is_member_object_pointer", EClassification_TemplateType);
        f_AddDefaultKeyword_Cpp("is_pointer", EClassification_TemplateType);
        f_AddDefaultKeyword_Cpp("is_rvalue_reference", EClassification_TemplateType);
        f_AddDefaultKeyword_Cpp("is_union", EClassification_TemplateType);
        f_AddDefaultKeyword_Cpp("is_void", EClassification_TemplateType);

        f_AddDefaultKeyword_Cpp("is_arithmetic", EClassification_TemplateType);
        f_AddDefaultKeyword_Cpp("is_compound", EClassification_TemplateType);
        f_AddDefaultKeyword_Cpp("is_fundamental", EClassification_TemplateType);
        f_AddDefaultKeyword_Cpp("is_member_pointer", EClassification_TemplateType);
        f_AddDefaultKeyword_Cpp("is_object", EClassification_TemplateType);
        f_AddDefaultKeyword_Cpp("is_reference", EClassification_TemplateType);
        f_AddDefaultKeyword_Cpp("is_scalar", EClassification_TemplateType);

        f_AddDefaultKeyword_Cpp("is_abstract", EClassification_TemplateType);
        f_AddDefaultKeyword_Cpp("is_const", EClassification_TemplateType);
        f_AddDefaultKeyword_Cpp("is_empty", EClassification_TemplateType);
        f_AddDefaultKeyword_Cpp("is_literal_type", EClassification_TemplateType);
        f_AddDefaultKeyword_Cpp("is_pod", EClassification_TemplateType);
        f_AddDefaultKeyword_Cpp("is_polymorphic", EClassification_TemplateType);
        f_AddDefaultKeyword_Cpp("is_signed", EClassification_TemplateType);
        f_AddDefaultKeyword_Cpp("is_standard_layout", EClassification_TemplateType);
        f_AddDefaultKeyword_Cpp("is_trivial", EClassification_TemplateType);
        f_AddDefaultKeyword_Cpp("is_trivially_copyable", EClassification_TemplateType);
        f_AddDefaultKeyword_Cpp("is_unsigned", EClassification_TemplateType);
        f_AddDefaultKeyword_Cpp("is_volatile", EClassification_TemplateType);

        f_AddDefaultKeyword_Cpp("has_virtual_destructor", EClassification_TemplateType);
        f_AddDefaultKeyword_Cpp("is_assignable", EClassification_TemplateType);
        f_AddDefaultKeyword_Cpp("is_constructible", EClassification_TemplateType);
        f_AddDefaultKeyword_Cpp("is_copy_assignable", EClassification_TemplateType);
        f_AddDefaultKeyword_Cpp("is_copy_constructible", EClassification_TemplateType);
        f_AddDefaultKeyword_Cpp("is_destructible", EClassification_TemplateType);
        f_AddDefaultKeyword_Cpp("is_default_constructible", EClassification_TemplateType);
        f_AddDefaultKeyword_Cpp("is_move_assignable", EClassification_TemplateType);
        f_AddDefaultKeyword_Cpp("is_move_constructible", EClassification_TemplateType);
        f_AddDefaultKeyword_Cpp("is_trivially_assignable", EClassification_TemplateType);
        f_AddDefaultKeyword_Cpp("is_trivially_constructible", EClassification_TemplateType);
        f_AddDefaultKeyword_Cpp("is_trivially_copy_assignable", EClassification_TemplateType);
        f_AddDefaultKeyword_Cpp("is_trivially_copy_constructible", EClassification_TemplateType);
        f_AddDefaultKeyword_Cpp("is_trivially_destructible", EClassification_TemplateType);
        f_AddDefaultKeyword_Cpp("is_trivially_default_constructible", EClassification_TemplateType);
        f_AddDefaultKeyword_Cpp("is_trivially_move_assignable", EClassification_TemplateType);
        f_AddDefaultKeyword_Cpp("is_trivially_move_constructible", EClassification_TemplateType);
        f_AddDefaultKeyword_Cpp("is_nothrow_assignable", EClassification_TemplateType);
        f_AddDefaultKeyword_Cpp("is_nothrow_constructible", EClassification_TemplateType);
        f_AddDefaultKeyword_Cpp("is_nothrow_copy_assignable", EClassification_TemplateType);
        f_AddDefaultKeyword_Cpp("is_nothrow_copy_constructible", EClassification_TemplateType);
        f_AddDefaultKeyword_Cpp("is_nothrow_destructible", EClassification_TemplateType);
        f_AddDefaultKeyword_Cpp("is_nothrow_default_constructible", EClassification_TemplateType);
        f_AddDefaultKeyword_Cpp("is_nothrow_move_assignable", EClassification_TemplateType);
        f_AddDefaultKeyword_Cpp("is_nothrow_move_constructible", EClassification_TemplateType);

        f_AddDefaultKeyword_Cpp("is_base_of", EClassification_TemplateType);
        f_AddDefaultKeyword_Cpp("is_convertible", EClassification_TemplateType);
        f_AddDefaultKeyword_Cpp("is_same", EClassification_TemplateType);

        f_AddDefaultKeyword_Cpp("alignment_of", EClassification_TemplateType);
        f_AddDefaultKeyword_Cpp("extent", EClassification_TemplateType);
        f_AddDefaultKeyword_Cpp("rank", EClassification_TemplateType);


        f_AddDefaultKeyword_Cpp("add_const", EClassification_TemplateType);
        f_AddDefaultKeyword_Cpp("add_cv", EClassification_TemplateType);
        f_AddDefaultKeyword_Cpp("add_volatile", EClassification_TemplateType);
        f_AddDefaultKeyword_Cpp("remove_const", EClassification_TemplateType);
        f_AddDefaultKeyword_Cpp("remove_cv", EClassification_TemplateType);
        f_AddDefaultKeyword_Cpp("remove_volatile", EClassification_TemplateType);

        f_AddDefaultKeyword_Cpp("add_pointer", EClassification_TemplateType);
        f_AddDefaultKeyword_Cpp("add_lvalue_reference", EClassification_TemplateType);
        f_AddDefaultKeyword_Cpp("add_rvalue_reference", EClassification_TemplateType);
        f_AddDefaultKeyword_Cpp("decay", EClassification_TemplateType);
        f_AddDefaultKeyword_Cpp("make_signed", EClassification_TemplateType);
        f_AddDefaultKeyword_Cpp("make_unsigned", EClassification_TemplateType);
        f_AddDefaultKeyword_Cpp("remove_all_extents", EClassification_TemplateType);
        f_AddDefaultKeyword_Cpp("remove_extent", EClassification_TemplateType);
        f_AddDefaultKeyword_Cpp("remove_pointer", EClassification_TemplateType);
        f_AddDefaultKeyword_Cpp("remove_reference", EClassification_TemplateType);
        f_AddDefaultKeyword_Cpp("underlying_type", EClassification_TemplateType);

        f_AddDefaultKeyword_Cpp("aligned_storage", EClassification_TemplateType);
        f_AddDefaultKeyword_Cpp("aligned_union", EClassification_TemplateType);
        f_AddDefaultKeyword_Cpp("common_type", EClassification_TemplateType);
        f_AddDefaultKeyword_Cpp("conditional", EClassification_TemplateType);
        f_AddDefaultKeyword_Cpp("enable_if", EClassification_TemplateType);
        f_AddDefaultKeyword_Cpp("result_of", EClassification_TemplateType);


        f_AddDefaultKeyword_Cpp("make_pair", EClassification_Function);
        f_AddDefaultKeyword_Cpp("forward", EClassification_Function);
        f_AddDefaultKeyword_Cpp("move", EClassification_Function);
        f_AddDefaultKeyword_Cpp("move_if_noexcept", EClassification_Function);
        f_AddDefaultKeyword_Cpp("declval", EClassification_Function);

        f_AddDefaultKeyword_Cpp("bind", EClassification_Function);
        f_AddDefaultKeyword_Cpp("function", EClassification_TemplateType);

        f_AddDefaultKeyword_Cpp("allocator", EClassification_TemplateType);
        f_AddDefaultKeyword_Cpp("auto_ptr_ref", EClassification_TemplateType);
        f_AddDefaultKeyword_Cpp("shared_ptr", EClassification_TemplateType);
        f_AddDefaultKeyword_Cpp("weak_ptr", EClassification_TemplateType);
        f_AddDefaultKeyword_Cpp("unique_ptr", EClassification_TemplateType);
        f_AddDefaultKeyword_Cpp("default_delete", EClassification_TemplateType);

        f_AddDefaultKeyword_Cpp("make_shared", EClassification_Function);
        f_AddDefaultKeyword_Cpp("allocate_shared", EClassification_Function);
        f_AddDefaultKeyword_Cpp("static_pointer_cast", EClassification_Function);
        f_AddDefaultKeyword_Cpp("dynamic_pointer_cast", EClassification_Function);
        f_AddDefaultKeyword_Cpp("const_pointer_cast", EClassification_Function);
        f_AddDefaultKeyword_Cpp("get_deleter", EClassification_Function);

        f_AddDefaultKeyword_Cpp("owner_less", EClassification_TemplateType);
        f_AddDefaultKeyword_Cpp("enable_shared_from_this", EClassification_TemplateType);

        f_AddDefaultKeyword_Cpp("CFStr", EClassification_Type);
        f_AddDefaultKeyword_Cpp("CFWStr", EClassification_Type);
        f_AddDefaultKeyword_Cpp("CFUStr", EClassification_Type);

        f_AddDefaultKeyword_Cpp("str_utf8", EClassification_KeywordPropertyModifiers);
        f_AddDefaultKeyword_Cpp("str_utf16", EClassification_KeywordPropertyModifiers);
        f_AddDefaultKeyword_Cpp("str_utf32", EClassification_KeywordPropertyModifiers);
    }

    void f_PopulateStyle(EClassification _Classification, uint32_t _Color) {
      auto Color256 = fg_FindColor256((_Color >> 16) & 0xff, (_Color >> 8) & 0xff, _Color & 0xff);

      m_ColorStyles[_Classification] = HighlightStyle::ColorStyle(
          true, llvm::formatv("\x1B[38;5;{0}m", Color256).str(), "\x1B[0m");
    }

    void f_PopulateStyles() {

      // Language

      f_PopulateStyle(EClassification_PlainText, 0xffffff); ignore(= * + - % {} [] () <>)

      f_PopulateStyle(EClassification_PreprocessorDirective, 0xffffff); ignore(define)

      f_PopulateStyle(EClassification_KeywordControlStatement, 0xffffff); ignore(for if while do)
      f_PopulateStyle(EClassification_KeywordStorageClass, 0xffffff);
      f_PopulateStyle(EClassification_KeywordExceptionHandling, 0xffffff);
      f_PopulateStyle(EClassification_KeywordIntrospection, 0xffffff);
      f_PopulateStyle(EClassification_KeywordStaticAssert, 0xffffff);
      f_PopulateStyle(EClassification_KeywordOptimization, 0xffffff);
      f_PopulateStyle(EClassification_KeywordNewDelete, 0xffffff);
      f_PopulateStyle(EClassification_KeywordCLR, 0xffffff);
      f_PopulateStyle(EClassification_KeywordOther, 0xffffff);
      f_PopulateStyle(EClassification_KeywordTypeSpecification, 0xffffff);
      f_PopulateStyle(EClassification_KeywordNamespace, 0xffffff);
      f_PopulateStyle(EClassification_KeywordTemplate, 0xffffff);
      f_PopulateStyle(EClassification_KeywordFunction, 0xffffff);
      f_PopulateStyle(EClassification_KeywordIn, 0xffffff);
      f_PopulateStyle(EClassification_KeywordTypedef, 0xffffff);
      f_PopulateStyle(EClassification_KeywordUsing, 0xffffff);
      f_PopulateStyle(EClassification_KeywordThis, 0xffffff);
      f_PopulateStyle(EClassification_KeywordOperator, 0xffffff);
      f_PopulateStyle(EClassification_KeywordVirtual, 0xffffff);
      f_PopulateStyle(EClassification_KeywordCasts, 0xffffff);
      f_PopulateStyle(EClassification_KeywordPure, 0xffffff);

      f_PopulateStyle(EClassification_Comment, 0x898989); // Comment

      f_PopulateStyle(EClassification_KeywordTypename, 0xc0c0c0); ignore(typename)
      f_PopulateStyle(EClassification_KeywordPropertyModifiers, 0xc0c0c0); ignore(inline)
      f_PopulateStyle(EClassification_KeywordAccess, 0xffc8ca); ignore(public private protected friend)
      f_PopulateStyle(EClassification_KeywordQualifier, 0xffb680); ignore(const volatile)

      // Builtin types
      f_PopulateStyle(EClassification_KeywordBulitInTypes, 0xff5966); ignore(bool void)
      f_PopulateStyle(EClassification_KeywordBulitInCharacterTypes, 0xff5966); ignore(ch8 ch16)
      f_PopulateStyle(EClassification_KeywordBulitInIntegerTypes, 0xff5966); ignore(int8 int32)
      f_PopulateStyle(EClassification_KeywordBulitInVectorTypes, 0xff5966); ignore(__m128)
      f_PopulateStyle(EClassification_KeywordBulitInTypeModifiers, 0xff5966); ignore(long unsigned)
      f_PopulateStyle(EClassification_KeywordBulitInFloatTypes, 0xff5966); ignore(float double)

      // Constant values
      f_PopulateStyle(EClassification_Number, 0xff0080); ignore(15 60.6)

      f_PopulateStyle(EClassification_TemplateNonTypeParam, 0xff5bad); ignore(t_Test)
      f_PopulateStyle(EClassification_TemplateNonTypeParam_Pack, 0xff5bad); ignore(tp_Test)

      f_PopulateStyle(EClassification_KeywordBulitInConstants, 0xff8ac5); ignore(true false nullptr)
      f_PopulateStyle(EClassification_MemberConstantPublic, 0xff8ac5); ignore(mc_Test)
      f_PopulateStyle(EClassification_GlobalConstant, 0xff8ac5); ignore(gc_Test)
      f_PopulateStyle(EClassification_ConstantVariable, 0xff8ac5); ignore(c_Test)
      f_PopulateStyle(EClassification_Enumerator, 0xff8ac5); ignore(ETest_Value)

      f_PopulateStyle(EClassification_MemberConstantPrivate, 0xca97b1); ignore(mcp_Test)

      f_PopulateStyle(EClassification_FunctionTemplateNonTypeParam, 0xffb7db); ignore(tf_Test)
      f_PopulateStyle(EClassification_FunctionTemplateNonTypeParam_Pack, 0xffb7db); ignore(tfp_Test)

      // Character
      f_PopulateStyle(EClassification_Character, 0xff48f0); ignore('T')

      // Namespace
      f_PopulateStyle(EClassification_Namespace, 0xd785ff); ignore(NTest)

      // Types
      f_PopulateStyle(EClassification_TemplateTypeParam_Class, 0x8269ff); ignore(t_CTest)
      f_PopulateStyle(EClassification_TemplateTypeParam_Function, 0x8269ff); ignore(t_FTest)
      f_PopulateStyle(EClassification_TemplateTemplateParam, 0x8269ff); ignore(t_TCTest)
      f_PopulateStyle(EClassification_TemplateTypeParam_Class_Pack, 0x8269ff); ignore(tp_CTest)
      f_PopulateStyle(EClassification_TemplateTypeParam_Function_Pack, 0x8269ff); ignore(tp_FTest)
      f_PopulateStyle(EClassification_TemplateTemplateParam_Pack, 0x8269ff); ignore(tp_TCTest)

      f_PopulateStyle(EClassification_FunctionTemplateTypeParam_Class, 0xcdc3ff); ignore(tf_CTest)
      f_PopulateStyle(EClassification_FunctionTemplateTypeParam_Function, 0xcdc3ff); ignore(tf_FTest)
      f_PopulateStyle(EClassification_FunctionTemplateTemplateParam, 0xcdc3ff); ignore(tf_TCTest)
      f_PopulateStyle(EClassification_FunctionTemplateTypeParam_Class_Pack, 0xcdc3ff); ignore(tfp_CTest)
      f_PopulateStyle(EClassification_FunctionTemplateTypeParam_Function_Pack, 0xcdc3ff); ignore(tfp_FTest)
      f_PopulateStyle(EClassification_FunctionTemplateTemplateParam_Pack, 0xcdc3ff); ignore(tfp_TCTest)

      f_PopulateStyle(EClassification_Type, 0xb8aaff); ignore(CTest)
      f_PopulateStyle(EClassification_Type_Function, 0xb8aaff); ignore(FTest)
      f_PopulateStyle(EClassification_Type_Interface, 0xb8aaff); ignore(ICTest)
      f_PopulateStyle(EClassification_TemplateType, 0xb8aaff); ignore(TCTest)
      f_PopulateStyle(EClassification_TemplateType_Interface, 0xb8aaff); ignore(TICTest)
      f_PopulateStyle(EClassification_Enum, 0xb8aaff); ignore(ETest)

      f_PopulateStyle(EClassification_KeywordAuto, 0xdbd3ff); ignore(auto)

      // String
      f_PopulateStyle(EClassification_String, 0x009eff); ignore("String")

      // Functors
      f_PopulateStyle(EClassification_FunctionParameter_Functor, 0x00e4e6); ignore(_fTest)
      f_PopulateStyle(EClassification_FunctionParameter_Pack_Functor, 0x00e4e6); ignore(p_fTest)

      f_PopulateStyle(EClassification_FunctionParameter_Output_Functor, 0x36e8cd); ignore(o_fTest)
      f_PopulateStyle(EClassification_FunctionParameter_Output_Pack_Functor, 0x36e8cd); ignore(po_fTest)

      f_PopulateStyle(EClassification_Variable_Functor, 0x00edae); ignore(fTest)

      f_PopulateStyle(EClassification_MemberVariablePublic_Functor, 0x00f265); ignore(m_fTest)

      f_PopulateStyle(EClassification_MemberVariablePrivate_Functor, 0x4fc17e); ignore(mp_fTest)

      // Functions
      f_PopulateStyle(EClassification_MemberFunctionPublic, 0x26ff00); ignore(f_Test)
      f_PopulateStyle(EClassification_MemberFunctionPublic_Recursive, 0x26ff00); ignore(f_rTest)
      f_PopulateStyle(EClassification_MemberStaticFunctionPublic, 0x26ff00); ignore(fs_Test)
      f_PopulateStyle(EClassification_MemberStaticFunctionPublic_Recursive, 0x26ff00); ignore(fs_rTest)

      f_PopulateStyle(EClassification_Function, 0x1cb900); ignore(fg_Test)
      f_PopulateStyle(EClassification_Function_Recursive, 0x1cb900); ignore(fg_rTest)
      f_PopulateStyle(EClassification_StaticFunction, 0x1cb900); ignore(fsg_Test)
      f_PopulateStyle(EClassification_StaticFunction_Recursive, 0x1cb900); ignore(fsg_rTest)

      f_PopulateStyle(EClassification_MemberFunctionPrivate, 0x8dd580); ignore(fp_Test)
      f_PopulateStyle(EClassification_MemberFunctionPrivate_Recursive, 0x8dd580); ignore(fp_rTest)
      f_PopulateStyle(EClassification_MemberStaticFunctionPrivate, 0x8dd580); ignore(fsp_Test)
      f_PopulateStyle(EClassification_MemberStaticFunctionPrivate_Recursive, 0x8dd580); ignore(fsp_rTest)

      // Parameters
      f_PopulateStyle(EClassification_FunctionParameter, 0xe6ff00); ignore(_Test)
      f_PopulateStyle(EClassification_FunctionParameter_Pack, 0xe6ff00); ignore(p_Test)

      f_PopulateStyle(EClassification_FunctionParameter_Output, 0xfff54b); ignore(o_Test)
      f_PopulateStyle(EClassification_FunctionParameter_Output_Pack, 0xfff54b); ignore(po_Test)

      // Variables
      f_PopulateStyle(EClassification_LocalVariable, 0xffd700); ignore(Var)

      // Concepts
      f_PopulateStyle(EClassification_Concept, 0xffb680); ignore(cTest)

      // Member variables
      f_PopulateStyle(EClassification_MemberVariablePublic, 0xffa600); ignore(m_Test)

      f_PopulateStyle(EClassification_MemberVariablePrivate, 0xc59d53); ignore(mp_Test)

      // Macros
      f_PopulateStyle(EClassification_Macro, 0xff7700); ignore(DTest)

      f_PopulateStyle(EClassification_MacroParameter, 0xffbc81); ignore(d_Test)

      // Globals
      f_PopulateStyle(EClassification_MemberStaticVariablePublic, 0xff3f1c); ignore(ms_Test)
      f_PopulateStyle(EClassification_MemberStaticVariablePublic_Functor, 0xff3f1c); ignore(ms_fTest)

      f_PopulateStyle(EClassification_GlobalStaticVariable, 0xe13819); ignore(gs_Test)
      f_PopulateStyle(EClassification_GlobalStaticVariable_Functor, 0xe13819); ignore(gs_fTest)
      f_PopulateStyle(EClassification_GlobalVariable, 0xe13819); ignore(g_Test)
      f_PopulateStyle(EClassification_GlobalVariable_Functor, 0xe13819); ignore(g_fTest)
      f_PopulateStyle(EClassification_StaticVariable, 0xe13819); ignore(s_Test)
      f_PopulateStyle(EClassification_StaticVariable_Functor, 0xe13819); ignore(s_fTest)

      f_PopulateStyle(EClassification_MemberStaticVariablePrivate, 0xd56955); ignore(msp_Test)
      f_PopulateStyle(EClassification_MemberStaticVariablePrivate_Functor, 0xd56955); ignore(msp_fTest)
    }

    EClassification f_Classify(std::string _Identifier) {
        {
            auto Found = m_DefaultKeywords_Cpp.find(_Identifier);
            if (Found != m_DefaultKeywords_Cpp.end())
                return Found->second;
        }
        {
            auto Found = m_DefaultKeywords_C.find(_Identifier);
            if (Found != m_DefaultKeywords_C.end())
                return Found->second;
        }
        {
            auto Found = m_DefaultKeywords_CLike.find(_Identifier);
            if (Found != m_DefaultKeywords_CLike.end())
                return Found->second;
        }
        {
            auto Found = m_DefaultKeywords.find(_Identifier);
            if (Found != m_DefaultKeywords.end())
                return Found->second;
        }

        int Length = static_cast<int>(_Identifier.length());
        if (Length < 3)
            return EClassification::EClassification_Unknown;
        for (int i = static_cast<int>(gc_MaxPrefixLen); i >= 0; --i)
        {
            if (i > Length)
                continue;

            std::string ToFind = _Identifier.substr(0, i);

            {
                auto Found = m_PrefixMapInfo[i].find(ToFind);

                if (Found != m_PrefixMapInfo[i].end())
                {
                    if (fg_MatchOtherPrefix(_Identifier, ToFind, i, Length))
                    {
                        if (i == 1 && ToFind == "E")
                        {
                            if (_Identifier.find('_') == std::string::npos)
                                return EClassification::EClassification_Enum;
                        }

                        if (i == 2 && ToFind == "CF")
                        {
                            if (fg_EndsWith(_Identifier, "Ref", 3))
                                return EClassification::EClassification_Type;
                        }

                        return Found->second;
                    }
                }
            }
            {
                auto Found = m_PrefixMapInfoVar[i].find(ToFind);
                if (Found != m_PrefixMapInfoVar[i].end())
                {
                    if (fg_MatchVariablePrefix(_Identifier, ToFind, i, Length))
                        return Found->second;
                }
            }
        }

        return EClassification_Unknown;
    }

    std::map<std::string, EClassification> m_PrefixMapInfoVar[gc_MaxPrefixLen + 1];
    std::map<std::string, EClassification> m_PrefixMapInfo[gc_MaxPrefixLen + 1];

    std::map<std::string, EClassification> m_DefaultKeywords;
    std::map<std::string, EClassification> m_DefaultKeywords_CLike;
    std::map<std::string, EClassification> m_DefaultKeywords_C;
    std::map<std::string, EClassification> m_DefaultKeywords_Cpp;
    std::map<EClassification, HighlightStyle::ColorStyle> m_ColorStyles;
};

MalterlibClassifier::MalterlibClassifier()
    : internal(std::make_unique<Internal>()) {
}

MalterlibClassifier::~MalterlibClassifier() = default;

HighlightStyle::ColorStyle MalterlibClassifier::highlightIdentifier(llvm::StringRef identifier, HighlightStyle::ColorStyle default_style) const {
    auto &Internal = *internal;

    auto Classification = Internal.f_Classify(identifier.str());
    if (Classification != EClassification_Unknown) {
      auto Found = Internal.m_ColorStyles.find(Classification);
      if (Found != Internal.m_ColorStyles.end())
          return Found->second;
    }

    auto Found = Internal.m_ColorStyles.find(EClassification_LocalVariable);
    if (Found != Internal.m_ColorStyles.end())
        return Found->second;

    return default_style;
}

HighlightStyle::ColorStyle MalterlibClassifier::highlightNumber(llvm::StringRef identifier, HighlightStyle::ColorStyle default_style) const {
    auto &Internal = *internal;

    auto Found = Internal.m_ColorStyles.find(EClassification_Number);
    if (Found != Internal.m_ColorStyles.end())
        return Found->second;

    return default_style;
}

HighlightStyle::ColorStyle MalterlibClassifier::highlightString(llvm::StringRef identifier, HighlightStyle::ColorStyle default_style) const {
    auto &Internal = *internal;

    auto Found = Internal.m_ColorStyles.find(EClassification_String);
    if (Found != Internal.m_ColorStyles.end())
        return Found->second;

    return default_style;
}

HighlightStyle::ColorStyle MalterlibClassifier::highlightPunctuation(llvm::StringRef identifier, HighlightStyle::ColorStyle default_style) const {
    auto &Internal = *internal;

    auto Found = Internal.m_ColorStyles.find(EClassification_PlainText);
    if (Found != Internal.m_ColorStyles.end())
        return Found->second;

    return default_style;
}

HighlightStyle::ColorStyle MalterlibClassifier::highlightKeyword(llvm::StringRef identifier, HighlightStyle::ColorStyle default_style) const {
    auto &Internal = *internal;

    auto Classification = Internal.f_Classify(identifier.str());

    if (Classification != EClassification_Unknown) {
      auto Found = Internal.m_ColorStyles.find(Classification);
      if (Found != Internal.m_ColorStyles.end())
          return Found->second;
    }

    auto Found = Internal.m_ColorStyles.find(EClassification_KeywordOther);
    if (Found != Internal.m_ColorStyles.end())
        return Found->second;

    return default_style;
}

HighlightStyle::ColorStyle MalterlibClassifier::highlightComment(llvm::StringRef identifier, HighlightStyle::ColorStyle default_style) const {
    auto &Internal = *internal;

    auto Found = Internal.m_ColorStyles.find(EClassification_Comment);
    if (Found != Internal.m_ColorStyles.end())
        return Found->second;

    return default_style;
}
