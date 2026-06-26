#include "PdbAstBuilder.h"

#include "llvm/DebugInfo/CodeView/CVTypeVisitor.h"
#include "llvm/DebugInfo/CodeView/LazyRandomTypeCollection.h"
#include "llvm/DebugInfo/CodeView/RecordName.h"
#include "llvm/DebugInfo/CodeView/SymbolDeserializer.h"
#include "llvm/DebugInfo/CodeView/SymbolRecord.h"
#include "llvm/DebugInfo/CodeView/SymbolRecordHelpers.h"
#include "llvm/DebugInfo/CodeView/TypeDeserializer.h"
#include "llvm/DebugInfo/CodeView/TypeVisitorCallbacks.h"
#include "llvm/DebugInfo/PDB/Native/DbiStream.h"
#include "llvm/DebugInfo/PDB/Native/PublicsStream.h"
#include "llvm/DebugInfo/PDB/Native/SymbolStream.h"
#include "llvm/DebugInfo/PDB/Native/TpiStream.h"
#include "llvm/Demangle/MicrosoftDemangle.h"
#include "llvm/Demangle/MicrosoftDemangleNodes.h"

#include "PdbUtil.h"
#include "Plugins/ExpressionParser/Clang/ClangASTMetadata.h"
#include "Plugins/ExpressionParser/Clang/ClangUtil.h"
#include "Plugins/Language/CPlusPlus/MSVCUndecoratedNameParser.h"
#include "Plugins/TypeSystem/Clang/TypeSystemClang.h"
#include "SymbolFileNativePDB.h"
#include "UdtRecordCompleter.h"
#include "lldb/Core/Module.h"
#include "lldb/Symbol/ObjectFile.h"
#include "lldb/Utility/LLDBAssert.h"
#include "lldb/Utility/FileSpec.h"
#include "clang/AST/ASTContext.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/DeclTemplate.h"
#include "clang/AST/QualTypeNames.h"
#include "clang/AST/TemplateBase.h"
#include "llvm/ADT/APSInt.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/raw_ostream.h"
#include <limits>
#include <optional>
#include <string_view>

using namespace lldb_private;
using namespace lldb_private::npdb;
using namespace llvm::codeview;
using namespace llvm::pdb;

namespace {
static void ApplyDeclarationMetadata(TypeSystemClang &clang_ast,
                                     clang::Decl *decl,
                                     const Declaration &declaration) {
  if (!decl || !declaration.IsValid())
    return;

  ClangASTMetadata metadata;
  if (std::optional<ClangASTMetadata> existing_metadata =
          clang_ast.GetMetadata(decl))
    metadata = *existing_metadata;
  metadata.SetDeclaration(declaration);
  clang_ast.SetMetadata(decl, metadata);
}

struct CreateMethodDecl : public TypeVisitorCallbacks {
  CreateMethodDecl(PdbIndex &m_index, TypeSystemClang &m_clang,
                   TypeIndex func_type_index,
                   clang::FunctionDecl *&function_decl,
                   lldb::opaque_compiler_type_t parent_ty,
                   llvm::StringRef proc_name, ConstString mangled_name,
                   CompilerType func_ct, const Declaration *declaration)
      : m_index(m_index), m_clang(m_clang), func_type_index(func_type_index),
        function_decl(function_decl), parent_ty(parent_ty),
        proc_name(proc_name), mangled_name(mangled_name), func_ct(func_ct),
        declaration(declaration) {}
  PdbIndex &m_index;
  TypeSystemClang &m_clang;
  TypeIndex func_type_index;
  clang::FunctionDecl *&function_decl;
  lldb::opaque_compiler_type_t parent_ty;
  llvm::StringRef proc_name;
  ConstString mangled_name;
  CompilerType func_ct;
  const Declaration *declaration;

  llvm::Error visitKnownMember(CVMemberRecord &cvr,
                               OverloadedMethodRecord &overloaded) override {
    TypeIndex method_list_idx = overloaded.MethodList;

    CVType method_list_type = m_index.tpi().getType(method_list_idx);
    assert(method_list_type.kind() == LF_METHODLIST);

    MethodOverloadListRecord method_list;
    llvm::cantFail(TypeDeserializer::deserializeAs<MethodOverloadListRecord>(
        method_list_type, method_list));

    for (const OneMethodRecord &method : method_list.Methods) {
      if (method.getType().getIndex() == func_type_index.getIndex())
        AddMethod(overloaded.Name, method.getAccess(), method.getOptions(),
                  method.Attrs);
    }

    return llvm::Error::success();
  }

  llvm::Error visitKnownMember(CVMemberRecord &cvr,
                               OneMethodRecord &record) override {
    AddMethod(record.getName(), record.getAccess(), record.getOptions(),
              record.Attrs);
    return llvm::Error::success();
  }

  void AddMethod(llvm::StringRef name, MemberAccess access,
                 MethodOptions options, MemberAttributes attrs) {
    if (name != proc_name || function_decl)
      return;
    lldb::AccessType access_type = TranslateMemberAccess(access);
    bool is_virtual = attrs.isVirtual();
    bool is_static = attrs.isStatic();
    bool is_artificial = (options & MethodOptions::CompilerGenerated) ==
                         MethodOptions::CompilerGenerated;
    function_decl = m_clang.AddMethodToCXXRecordType(
        parent_ty, proc_name, mangled_name, func_ct, /*access=*/access_type,
        /*is_virtual=*/is_virtual, /*is_static=*/is_static,
        /*is_inline=*/false, /*is_explicit=*/false,
        /*is_attr_used=*/false, /*is_artificial=*/is_artificial, declaration);
  }
};
} // namespace

static clang::TagTypeKind TranslateUdtKind(const TagRecord &cr) {
  switch (cr.Kind) {
  case TypeRecordKind::Class:
    return clang::TagTypeKind::Class;
  case TypeRecordKind::Struct:
    return clang::TagTypeKind::Struct;
  case TypeRecordKind::Union:
    return clang::TagTypeKind::Union;
  case TypeRecordKind::Interface:
    return clang::TagTypeKind::Interface;
  case TypeRecordKind::Enum:
    return clang::TagTypeKind::Enum;
  default:
    lldbassert(false && "Invalid tag record kind!");
    return clang::TagTypeKind::Struct;
  }
}

static bool IsCVarArgsFunction(llvm::ArrayRef<TypeIndex> args) {
  if (args.empty())
    return false;
  return args.back() == TypeIndex::None();
}

static bool
AnyScopesHaveTemplateParams(llvm::ArrayRef<llvm::ms_demangle::Node *> scopes) {
  for (llvm::ms_demangle::Node *n : scopes) {
    auto *idn = static_cast<llvm::ms_demangle::IdentifierNode *>(n);
    if (idn->TemplateParams)
      return true;
  }
  return false;
}

static std::optional<clang::CallingConv>
TranslateCallingConvention(llvm::codeview::CallingConvention conv) {
  using CC = llvm::codeview::CallingConvention;
  switch (conv) {

  case CC::NearC:
  case CC::FarC:
    return clang::CallingConv::CC_C;
  case CC::NearPascal:
  case CC::FarPascal:
    return clang::CallingConv::CC_X86Pascal;
  case CC::NearFast:
  case CC::FarFast:
    return clang::CallingConv::CC_X86FastCall;
  case CC::NearStdCall:
  case CC::FarStdCall:
    return clang::CallingConv::CC_X86StdCall;
  case CC::ThisCall:
    return clang::CallingConv::CC_X86ThisCall;
  case CC::NearVector:
    return clang::CallingConv::CC_X86VectorCall;
  default:
    return std::nullopt;
  }
}

static std::optional<clang::CallingConv>
TranslateCallingConvention(llvm::ms_demangle::CallingConv conv) {
  using CC = llvm::ms_demangle::CallingConv;
  switch (conv) {
  case CC::None:
  case CC::Cdecl:
    return clang::CallingConv::CC_C;
  case CC::Pascal:
    return clang::CallingConv::CC_X86Pascal;
  case CC::Thiscall:
    return clang::CallingConv::CC_X86ThisCall;
  case CC::Stdcall:
    return clang::CallingConv::CC_X86StdCall;
  case CC::Fastcall:
    return clang::CallingConv::CC_X86FastCall;
  case CC::Vectorcall:
    return clang::CallingConv::CC_X86VectorCall;
  case CC::Regcall:
    return clang::CallingConv::CC_X86RegCall;
  case CC::Swift:
    return clang::CallingConv::CC_Swift;
  case CC::SwiftAsync:
    return clang::CallingConv::CC_SwiftAsync;
  case CC::Clrcall:
  case CC::Eabi:
    return std::nullopt;
  }
  llvm_unreachable("Unhandled Microsoft demangle calling convention");
}

static std::optional<clang::RefQualifierKind>
TranslateFunctionRefQualifier(llvm::ms_demangle::FunctionRefQualifier ref) {
  using Ref = llvm::ms_demangle::FunctionRefQualifier;
  switch (ref) {
  case Ref::None:
    return clang::RefQualifierKind::RQ_None;
  case Ref::Reference:
    return clang::RefQualifierKind::RQ_LValue;
  case Ref::RValueReference:
    return clang::RefQualifierKind::RQ_RValue;
  }
  llvm_unreachable("Unhandled Microsoft demangle function ref qualifier");
}

static bool IsAnonymousNamespaceName(llvm::StringRef name) {
  return name == "`anonymous namespace'" || name == "`anonymous-namespace'";
}

static clang::QualType GetClangTypeForPrimitiveTemplateArgument(
    PdbAstBuilder &builder, llvm::ms_demangle::PrimitiveKind kind) {
  using PK = llvm::ms_demangle::PrimitiveKind;
  switch (kind) {
  case PK::Void:
    return builder.GetBasicType(lldb::eBasicTypeVoid);
  case PK::Bool:
    return builder.GetBasicType(lldb::eBasicTypeBool);
  case PK::Char:
    return builder.GetBasicType(lldb::eBasicTypeChar);
  case PK::Schar:
    return builder.GetBasicType(lldb::eBasicTypeSignedChar);
  case PK::Uchar:
    return builder.GetBasicType(lldb::eBasicTypeUnsignedChar);
  case PK::Char8:
    return builder.GetBasicType(lldb::eBasicTypeChar8);
  case PK::Char16:
    return builder.GetBasicType(lldb::eBasicTypeChar16);
  case PK::Char32:
    return builder.GetBasicType(lldb::eBasicTypeChar32);
  case PK::Short:
    return builder.GetBasicType(lldb::eBasicTypeShort);
  case PK::Ushort:
    return builder.GetBasicType(lldb::eBasicTypeUnsignedShort);
  case PK::Int:
    return builder.GetBasicType(lldb::eBasicTypeInt);
  case PK::Uint:
    return builder.GetBasicType(lldb::eBasicTypeUnsignedInt);
  case PK::Long:
    return builder.GetBasicType(lldb::eBasicTypeLong);
  case PK::Ulong:
    return builder.GetBasicType(lldb::eBasicTypeUnsignedLong);
  case PK::Int64:
    return builder.GetBasicType(lldb::eBasicTypeLongLong);
  case PK::Uint64:
    return builder.GetBasicType(lldb::eBasicTypeUnsignedLongLong);
  case PK::Wchar:
    return builder.GetBasicType(lldb::eBasicTypeWChar);
  case PK::Float:
    return builder.GetBasicType(lldb::eBasicTypeFloat);
  case PK::Double:
    return builder.GetBasicType(lldb::eBasicTypeDouble);
  case PK::Ldouble:
    return builder.GetBasicType(lldb::eBasicTypeLongDouble);
  case PK::Nullptr:
    return builder.GetBasicType(lldb::eBasicTypeNullPtr);
  case PK::Auto:
  case PK::DecltypeAuto:
    return {};
  }
  llvm_unreachable("Unhandled MS demangler primitive kind");
}

static clang::QualType
ApplyMSDemangleTypeQualifiers(clang::QualType qt,
                              llvm::ms_demangle::Qualifiers qualifiers) {
  if (qt.isNull())
    return {};

  if ((qualifiers & llvm::ms_demangle::Q_Const) != 0)
    qt = qt.withConst();
  if ((qualifiers & llvm::ms_demangle::Q_Volatile) != 0)
    qt = qt.withVolatile();
  if ((qualifiers & llvm::ms_demangle::Q_Restrict) != 0)
    qt = qt.withRestrict();

  return qt;
}

static std::string GetTemplateArgumentTypeName(llvm::ms_demangle::Node &node) {
  std::string name = node.toString(llvm::ms_demangle::OutputFlags(
      llvm::ms_demangle::OF_NoTagSpecifier |
      llvm::ms_demangle::OF_NoCallingConvention));
  name = llvm::StringRef(name).trim().str();
  return name;
}

static bool IsTemplateSeparator(char c) {
  return c == '<' || c == '>' || c == ',';
}

static std::optional<size_t>
FindLastTopLevelScopeSeparator(llvm::StringRef name) {
  unsigned template_depth = 0;
  std::optional<size_t> separator_pos;
  for (size_t i = 0; i + 1 < name.size(); ++i) {
    if (name[i] == '<') {
      ++template_depth;
      continue;
    }

    if (name[i] == '>' && template_depth > 0) {
      --template_depth;
      continue;
    }

    if (template_depth == 0 && name[i] == ':' && name[i + 1] == ':') {
      separator_pos = i;
      ++i;
    }
  }

  return separator_pos;
}

static llvm::StringRef GetParentTypeName(llvm::StringRef name) {
  std::optional<size_t> separator_pos = FindLastTopLevelScopeSeparator(name);
  if (!separator_pos)
    return {};

  return name.take_front(*separator_pos);
}

static llvm::StringRef GetUnqualifiedTypeName(llvm::StringRef name) {
  std::optional<size_t> separator_pos = FindLastTopLevelScopeSeparator(name);
  if (!separator_pos)
    return name;

  return name.drop_front(*separator_pos + 2);
}

static llvm::StringRef TrimTypeNameQuotes(llvm::StringRef name) {
  name = name.trim();
  while (!name.empty() && (name.front() == '`' || name.front() == '\''))
    name = name.drop_front().trim();
  while (!name.empty() && (name.back() == '`' || name.back() == '\''))
    name = name.drop_back().trim();
  return name;
}

static std::optional<Declaration>
GetLambdaDeclarationFromTypeName(llvm::StringRef name) {
  constexpr llvm::StringLiteral lambda_at("lambda at ");
  size_t pos = name.find(lambda_at);
  if (pos == llvm::StringRef::npos)
    return std::nullopt;

  llvm::StringRef source_name = name.drop_front(pos + lambda_at.size());
  source_name = TrimTypeNameQuotes(source_name);

  size_t column_separator = source_name.rfind(':');
  if (column_separator == llvm::StringRef::npos)
    return std::nullopt;

  size_t line_separator = source_name.rfind(':', column_separator - 1);
  if (line_separator == llvm::StringRef::npos)
    return std::nullopt;

  uint32_t line = 0;
  llvm::StringRef line_text =
      source_name.slice(line_separator + 1, column_separator);
  if (line_text.getAsInteger(10, line) || line == 0)
    return std::nullopt;

  uint32_t column = 0;
  llvm::StringRef column_text = source_name.drop_front(column_separator + 1);
  if (column_text.getAsInteger(10, column))
    column = 0;

  llvm::StringRef file_name = source_name.take_front(line_separator);
  return Declaration(FileSpec(file_name.str()), line, column);
}

static std::optional<Declaration>
GetDirectLambdaDeclarationFromTypeName(llvm::StringRef name) {
  constexpr llvm::StringLiteral lambda_at("lambda at ");
  name = TrimTypeNameQuotes(name);
  for (llvm::StringRef prefix : {llvm::StringRef("class "),
                                 llvm::StringRef("struct "),
                                 llvm::StringRef("union ")}) {
    if (name.starts_with(prefix)) {
      name = TrimTypeNameQuotes(name.drop_front(prefix.size()));
      break;
    }
  }
  if (!name.starts_with(lambda_at))
    return std::nullopt;
  return GetLambdaDeclarationFromTypeName(name);
}

static void ApplyTypeDeclarationMetadata(PdbAstBuilder &builder,
                                         clang::QualType qt,
                                         const Declaration &declaration) {
  if (qt.isNull() || !declaration.IsValid())
    return;

  TypeSystemClang &clang_ast = builder.clang();
  ClangASTMetadata metadata;
  if (clang::TagDecl *tag = qt->getAsTagDecl()) {
    if (std::optional<ClangASTMetadata> existing_metadata =
            clang_ast.GetMetadata(tag))
      metadata = *existing_metadata;
    metadata.SetDeclaration(declaration);
    clang_ast.SetMetadata(tag, metadata);
  } else {
    metadata.SetDeclaration(declaration);
  }

  clang_ast.SetMetadata(qt.getTypePtr(), metadata);
}

static std::string RemoveTemplateSeparatorWhitespace(llvm::StringRef name) {
  std::string result;
  result.reserve(name.size());

  unsigned template_depth = 0;
  bool previous_was_space = false;
  for (size_t i = 0; i < name.size(); ++i) {
    char c = name[i];
    if (c == ' ' || c == '\t') {
      char previous = result.empty() ? '\0' : result.back();
      llvm::StringRef remaining = name.drop_front(i + 1).ltrim();
      char next = remaining.empty() ? '\0' : remaining.front();
      if (template_depth > 0 &&
          (IsTemplateSeparator(previous) || IsTemplateSeparator(next)))
        continue;

      if (previous_was_space)
        continue;

      previous_was_space = true;
      result.push_back(' ');
      continue;
    }

    if (c == '<') {
      ++template_depth;
      previous_was_space = false;
    } else if (c == '>' && template_depth > 0) {
      --template_depth;
      previous_was_space = false;
    } else if (c == ' ' || c == '\t') {
      if (previous_was_space)
        continue;
      previous_was_space = true;
    } else {
      previous_was_space = false;
    }

    result.push_back(c);
  }

  return result;
}

static std::string
AddCodeViewAdjacentClosingTemplateWhitespace(llvm::StringRef name) {
  std::string result;
  result.reserve(name.size());

  for (size_t i = 0; i < name.size(); ++i) {
    result.push_back(name[i]);
    if (name[i] == '>' && i + 1 < name.size() && name[i + 1] == '>')
      result.push_back(' ');
  }

  return result;
}

static void AddTypeNameCandidate(std::vector<std::string> &candidates,
                                 llvm::StringRef name) {
  if (name.empty())
    return;

  for (llvm::StringRef candidate : candidates) {
    if (candidate == name)
      return;
  }

  candidates.push_back(name.str());
}

static void AddTemplateTypeNameCandidates(
    std::vector<std::string> &candidates, llvm::StringRef name) {
  AddTypeNameCandidate(candidates, name);

  std::string compact = RemoveTemplateSeparatorWhitespace(name);
  AddTypeNameCandidate(candidates, compact);

  std::string codeview_compact =
      AddCodeViewAdjacentClosingTemplateWhitespace(compact);
  AddTypeNameCandidate(candidates, codeview_compact);
}

static std::vector<std::string>
ParseTemplateArgumentsFromName(llvm::StringRef name) {
  std::vector<std::string> args;
  std::optional<size_t> args_begin;
  unsigned template_depth = 0;
  size_t arg_begin = 0;

  for (size_t i = 0; i < name.size(); ++i) {
    if (name[i] == '<') {
      if (template_depth == 0) {
        args_begin = i + 1;
        arg_begin = i + 1;
      }
      ++template_depth;
      continue;
    }

    if (name[i] == '>' && template_depth > 0) {
      --template_depth;
      if (template_depth == 0) {
        args.push_back(name.slice(arg_begin, i).trim().str());
        break;
      }
      continue;
    }

    if (name[i] == ',' && template_depth == 1 && args_begin) {
      args.push_back(name.slice(arg_begin, i).trim().str());
      arg_begin = i + 1;
    }
  }

  return args;
}

static void ApplyTemplateArgumentDeclarationMetadataFromName(
    PdbAstBuilder &builder, clang::QualType qt, llvm::StringRef name) {
  if (qt.isNull())
    return;

  clang::TagDecl *tag = qt->getAsTagDecl();
  auto *specialization =
      llvm::dyn_cast_or_null<clang::ClassTemplateSpecializationDecl>(tag);
  if (!specialization)
    return;

  std::vector<std::string> spelled_args =
      ParseTemplateArgumentsFromName(GetUnqualifiedTypeName(name));
  if (spelled_args.empty())
    return;

  clang::ArrayRef<clang::TemplateArgument> template_args =
      specialization->getTemplateArgs().asArray();
  size_t count = std::min(spelled_args.size(), template_args.size());
  for (size_t i = 0; i < count; ++i) {
    clang::TemplateArgument const &arg = template_args[i];
    if (arg.getKind() != clang::TemplateArgument::Type)
      continue;

    clang::QualType arg_type = arg.getAsType();
    ApplyTemplateArgumentDeclarationMetadataFromName(builder, arg_type,
                                                    spelled_args[i]);

    std::optional<Declaration> declaration =
        GetDirectLambdaDeclarationFromTypeName(spelled_args[i]);
    if (declaration)
      ApplyTypeDeclarationMetadata(builder, arg_type, *declaration);
  }
}

struct TemplateArgumentTypeName {
  std::string name;
  bool is_const = false;
  bool is_volatile = false;
};

static TemplateArgumentTypeName
StripTopLevelQualifiers(llvm::StringRef name) {
  TemplateArgumentTypeName result;
  name = name.trim();

  bool changed = true;
  while (changed) {
    changed = false;
    if (name.consume_front("const ")) {
      result.is_const = true;
      name = name.ltrim();
      changed = true;
    }
    if (name.consume_front("volatile ")) {
      result.is_volatile = true;
      name = name.ltrim();
      changed = true;
    }
    if (name.consume_back(" const")) {
      result.is_const = true;
      name = name.rtrim();
      changed = true;
    }
    if (name.consume_back(" volatile")) {
      result.is_volatile = true;
      name = name.rtrim();
      changed = true;
    }
  }

  result.name = name.str();
  return result;
}

static llvm::ms_demangle::IdentifierNode *
FindIdentifierForUnqualifiedTypeName(llvm::ms_demangle::QualifiedNameNode &qn,
                                     llvm::StringRef unqualified_name) {
  if (unqualified_name.empty() || !qn.Components)
    return nullptr;

  std::string compact_unqualified_name =
      RemoveTemplateSeparatorWhitespace(unqualified_name);
  for (size_t i = 0; i < qn.Components->Count; ++i) {
    auto *idn = llvm::dyn_cast<llvm::ms_demangle::IdentifierNode>(
        qn.Components->Nodes[i]);
    if (!idn)
      continue;

    std::string component_name = GetTemplateArgumentTypeName(*idn);
    if (component_name == unqualified_name ||
        RemoveTemplateSeparatorWhitespace(component_name) ==
            compact_unqualified_name)
      return idn;
  }

  return nullptr;
}

static clang::QualType CreateFallbackTemplateSpecializationTypeFromName(
    PdbAstBuilder &builder, llvm::StringRef name,
    TypeIndex current_type_index, TypeIndex type_index,
    clang::QualType original_qt);

static clang::QualType CreateFallbackTemplateArgumentSpecializationType(
    PdbAstBuilder &builder, llvm::StringRef name,
    llvm::ms_demangle::TagKind tag_kind,
    llvm::ms_demangle::Qualifiers qualifiers, TypeIndex current_type_index);

static bool IsClassTemplateSpecializationType(clang::QualType qt) {
  clang::TagDecl *tag = qt->getAsTagDecl();
  return tag && llvm::isa<clang::ClassTemplateSpecializationDecl>(tag);
}

static clang::QualType
FindPdbTemplateArgumentType(PdbAstBuilder &builder, llvm::StringRef name,
                            TypeIndex current_type_index) {
  if (name.empty())
    return {};

  TemplateArgumentTypeName unqualified = StripTopLevelQualifiers(name);
  llvm::StringRef scope_unqualified =
      TrimTypeNameQuotes(GetUnqualifiedTypeName(unqualified.name));

  SymbolFileNativePDB *pdb = static_cast<SymbolFileNativePDB *>(
      builder.clang().GetSymbolFile()->GetBackingSymbolFile());
  PdbIndex &index = pdb->GetIndex();

  std::vector<std::string> candidates;
  AddTemplateTypeNameCandidates(candidates, name);

  if (unqualified.name != name.str()) {
    AddTemplateTypeNameCandidates(candidates, unqualified.name);
  }

  if (!scope_unqualified.empty() && scope_unqualified != unqualified.name) {
    AddTemplateTypeNameCandidates(candidates, scope_unqualified);
  }

  auto make_qualified_type = [&](TypeIndex ti) -> clang::QualType {
    if (ti == current_type_index)
      return {};

    clang::QualType qt = builder.GetOrCreateClangType(PdbTypeSymId(ti));
    if (qt.isNull())
      return {};

    if (name.contains('<') && !IsClassTemplateSpecializationType(qt)) {
      clang::QualType specialization_qt =
          CreateFallbackTemplateSpecializationTypeFromName(
              builder, name, current_type_index, ti, qt);
      if (!specialization_qt.isNull())
        qt = specialization_qt;
    }
    ApplyTemplateArgumentDeclarationMetadataFromName(builder, qt, name);
    if (unqualified.is_const)
      qt = qt.withConst();
    if (unqualified.is_volatile)
      qt = qt.withVolatile();
    return qt;
  };

  for (llvm::StringRef candidate : candidates) {
    std::vector<TypeIndex> matches = index.tpi().findRecordsByName(candidate);
    for (TypeIndex ti : matches) {
      clang::QualType qt = make_qualified_type(ti);
      if (!qt.isNull())
        return qt;
    }
  }

  for (llvm::StringRef candidate : candidates) {
    std::optional<PdbTypeSymId> match =
        pdb->FindCompleteTypeByName(candidate, current_type_index);
    if (!match)
      continue;

    clang::QualType qt = make_qualified_type(match->index);
    if (!qt.isNull())
      return qt;
  }

  auto find_by_source_location = [&]() -> clang::QualType {
    constexpr llvm::StringLiteral lambda_at("lambda at ");
    llvm::StringRef source_name = unqualified.name;
    size_t pos = source_name.find(lambda_at);
    if (pos == llvm::StringRef::npos)
      return {};

    source_name = source_name.drop_front(pos + lambda_at.size());
    source_name = TrimTypeNameQuotes(source_name);

    size_t column_separator = source_name.rfind(':');
    if (column_separator == llvm::StringRef::npos)
      return {};

    size_t line_separator = source_name.rfind(':', column_separator - 1);
    if (line_separator == llvm::StringRef::npos)
      return {};

    uint32_t line = 0;
    llvm::StringRef line_text =
        source_name.slice(line_separator + 1, column_separator);
    if (line_text.getAsInteger(10, line) || line == 0)
      return {};

    llvm::StringRef file_name = source_name.take_front(line_separator);
    llvm::StringRef file_basename = llvm::sys::path::filename(file_name);

    std::optional<PdbTypeSymId> match =
        pdb->FindUdtDeclarationBySourceLocation(file_basename, line,
                                                current_type_index);
    if (!match)
      return {};

    return make_qualified_type(match->index);
  };

  if (clang::QualType qt = find_by_source_location(); !qt.isNull())
    return qt;

  return {};
}

static clang::TagTypeKind
GetClangTagTypeKindForMSDemangleTag(llvm::ms_demangle::TagKind kind) {
  using TK = llvm::ms_demangle::TagKind;
  switch (kind) {
  case TK::Class:
    return clang::TagTypeKind::Class;
  case TK::Struct:
    return clang::TagTypeKind::Struct;
  case TK::Union:
    return clang::TagTypeKind::Union;
  case TK::Enum:
    return clang::TagTypeKind::Enum;
  }
  llvm_unreachable("Unhandled MS demangler tag kind");
}

static CompilerType CreateClassTemplateSpecializationType(
    PdbAstBuilder &builder, clang::DeclContext *context,
    lldb::AccessType access, llvm::StringRef uname, clang::TagTypeKind ttk,
    TypeSystemClang::TemplateParameterInfos const &template_param_infos,
    ClangASTMetadata const &metadata, bool require_new);

static clang::QualType
GetClangTypeForTemplateArgument(PdbAstBuilder &builder,
                                llvm::ms_demangle::Node &node,
                                TypeIndex current_type_index);

static clang::QualType
GetClangTypeForMSDemangleType(PdbAstBuilder &builder,
                              llvm::ms_demangle::Node &node,
                              TypeIndex current_type_index);

static clang::TemplateArgument CreateIntegralTemplateArgument(
    TypeSystemClang &clang_ast, llvm::ms_demangle::IntegerLiteralNode &literal,
    clang::QualType type = {});

static clang::QualType CreateFallbackTemplateArgumentType(
    PdbAstBuilder &builder, llvm::StringRef name,
    llvm::ms_demangle::TagTypeNode &tag_type, TypeIndex current_type_index);

static clang::QualType CreateFallbackTemplateArgumentRecordType(
    PdbAstBuilder &builder, llvm::StringRef name,
    llvm::ms_demangle::TagKind tag_kind,
    llvm::ms_demangle::Qualifiers qualifiers) {
  if (name.empty())
    return {};

  TypeSystemClang &clang_ast = builder.clang();
  clang::ASTContext &ast = clang_ast.getASTContext();
  clang::DeclContext *context = clang_ast.GetTranslationUnitDecl();
  clang::DeclarationName decl_name(&ast.Idents.get(name));
  for (clang::NamedDecl *decl : context->lookup(decl_name)) {
    if (auto *tag_decl = llvm::dyn_cast<clang::TagDecl>(decl)) {
      clang::QualType qt = ApplyMSDemangleTypeQualifiers(
          ast.getTypeDeclType(clang::ElaboratedTypeKeyword::None,
                              /*Qualifier=*/std::nullopt,
                              static_cast<clang::TypeDecl *>(tag_decl)),
          qualifiers);
      if (std::optional<Declaration> declaration =
              GetLambdaDeclarationFromTypeName(name))
        ApplyTypeDeclarationMetadata(builder, qt, *declaration);
      return qt;
    }
  }

  std::optional<Declaration> declaration =
      GetLambdaDeclarationFromTypeName(name);
  std::optional<ClangASTMetadata> metadata;
  if (declaration) {
    metadata.emplace();
    metadata->SetDeclaration(*declaration);
  }

  CompilerType ct = clang_ast.CreateRecordType(
      context, OptionalClangModuleID(), lldb::eAccessPublic, name,
      llvm::to_underlying(GetClangTagTypeKindForMSDemangleTag(tag_kind)),
      lldb::eLanguageTypeC_plus_plus, metadata);
  if (!ct.IsValid())
    return {};

  clang::QualType qt =
      clang::QualType::getFromOpaquePtr(ct.GetOpaqueQualType());
  qt = ApplyMSDemangleTypeQualifiers(qt, qualifiers);
  if (declaration)
    ApplyTypeDeclarationMetadata(builder, qt, *declaration);
  return qt;
}

static bool InsertTemplateArgumentFromMSDemangleNode(
    PdbAstBuilder &builder, llvm::ms_demangle::Node &node,
    TypeIndex current_type_index, llvm::StringRef fallback_type_name,
    TypeSystemClang::TemplateParameterInfos &template_param_infos) {
  if (auto *integer =
          llvm::dyn_cast<llvm::ms_demangle::IntegerLiteralNode>(&node)) {
    template_param_infos.InsertArg(
        nullptr, CreateIntegralTemplateArgument(builder.clang(), *integer));
    return true;
  }

  clang::QualType qt;
  if (!fallback_type_name.empty())
    qt = FindPdbTemplateArgumentType(builder, fallback_type_name,
                                     current_type_index);
  if (qt.isNull())
    qt = GetClangTypeForTemplateArgument(builder, node, current_type_index);
  if (qt.isNull()) {
    if (auto *tag_type =
            llvm::dyn_cast<llvm::ms_demangle::TagTypeNode>(&node)) {
      std::string fallback_name_storage;
      llvm::StringRef fallback_name = fallback_type_name;
      if (fallback_name.empty()) {
        fallback_name_storage = GetTemplateArgumentTypeName(node);
        fallback_name = fallback_name_storage;
      }
      qt = CreateFallbackTemplateArgumentType(builder, fallback_name, *tag_type,
                                              current_type_index);
    }
  }
  if (qt.isNull())
    return false;

  if (!fallback_type_name.empty()) {
    if (std::optional<Declaration> declaration =
            GetLambdaDeclarationFromTypeName(fallback_type_name))
      ApplyTypeDeclarationMetadata(builder, qt, *declaration);
  }

  template_param_infos.InsertArg(nullptr, clang::TemplateArgument(qt));
  return true;
}

static bool ParseTemplateParameterInfosFromNodes(
    PdbAstBuilder &builder, llvm::ms_demangle::NodeArrayNode &params,
    TypeIndex current_type_index, llvm::ArrayRef<std::string> fallback_args,
    TypeSystemClang::TemplateParameterInfos &template_param_infos) {
  for (size_t i = 0; i < params.Count; ++i) {
    llvm::ms_demangle::Node *node = params.Nodes[i];
    if (!node)
      return false;

    llvm::StringRef fallback_arg;
    if (i < fallback_args.size())
      fallback_arg = fallback_args[i];

    if (!InsertTemplateArgumentFromMSDemangleNode(
            builder, *node, current_type_index, fallback_arg,
            template_param_infos))
      return false;
  }

  return template_param_infos.IsValid() && !template_param_infos.IsEmpty();
}

static clang::DeclContext *CreateFallbackTemplateArgumentDeclContext(
    PdbAstBuilder &builder, llvm::ms_demangle::QualifiedNameNode &qn) {
  TypeSystemClang &clang_ast = builder.clang();
  clang::DeclContext *context = clang_ast.GetTranslationUnitDecl();
  llvm::ms_demangle::NodeArrayNode *components = qn.Components;
  if (!components || components->Count == 0)
    return context;

  for (size_t i = 0; i + 1 < components->Count; ++i) {
    auto *idn =
        llvm::dyn_cast<llvm::ms_demangle::IdentifierNode>(components->Nodes[i]);
    if (!idn || idn->TemplateParams)
      return clang_ast.GetTranslationUnitDecl();

    std::string namespace_name = GetTemplateArgumentTypeName(*idn);
    context = builder.GetOrCreateNamespaceDecl(namespace_name.c_str(), *context);
  }

  return context;
}

static clang::QualType CreateFallbackTemplateArgumentType(
    PdbAstBuilder &builder, llvm::StringRef name,
    llvm::ms_demangle::TagTypeNode &tag_type, TypeIndex current_type_index) {
  if (name.contains('<')) {
    clang::QualType qt = CreateFallbackTemplateArgumentSpecializationType(
        builder, name, tag_type.Tag, tag_type.Quals, current_type_index);
    if (!qt.isNull())
      return qt;
  }

  if (!tag_type.QualifiedName)
    return CreateFallbackTemplateArgumentRecordType(builder, name,
                                                   tag_type.Tag,
                                                   tag_type.Quals);

  llvm::ms_demangle::IdentifierNode *idn =
      tag_type.QualifiedName->getUnqualifiedIdentifier();
  if (!idn || !idn->TemplateParams || idn->TemplateParams->Count == 0)
    return CreateFallbackTemplateArgumentRecordType(builder, name,
                                                   tag_type.Tag,
                                                   tag_type.Quals);

  clang::DeclContext *context =
      CreateFallbackTemplateArgumentDeclContext(builder, *tag_type.QualifiedName);
  std::string template_name = GetTemplateArgumentTypeName(*idn);
  std::vector<std::string> fallback_args =
      ParseTemplateArgumentsFromName(GetUnqualifiedTypeName(name));

  TypeSystemClang::TemplateParameterInfos template_param_infos;
  if (!ParseTemplateParameterInfosFromNodes(builder, *idn->TemplateParams,
                                           current_type_index, fallback_args,
                                           template_param_infos)) {
    clang::QualType qt = CreateFallbackTemplateArgumentSpecializationType(
        builder, name, tag_type.Tag, tag_type.Quals, current_type_index);
    if (!qt.isNull())
      return qt;
    return {};
  }

  ClangASTMetadata metadata;
  if (std::optional<Declaration> declaration =
          GetLambdaDeclarationFromTypeName(name))
    metadata.SetDeclaration(*declaration);
  CompilerType ct = CreateClassTemplateSpecializationType(
      builder, context, lldb::eAccessPublic, template_name,
      GetClangTagTypeKindForMSDemangleTag(tag_type.Tag), template_param_infos,
      metadata, false);
  if (!ct.IsValid())
    return {};

  clang::QualType qt =
      clang::QualType::getFromOpaquePtr(ct.GetOpaqueQualType());
  ApplyTemplateArgumentDeclarationMetadataFromName(builder, qt, name);
  return ApplyMSDemangleTypeQualifiers(qt, tag_type.Quals);
}

static clang::QualType
GetClangTypeForTemplateArgument(PdbAstBuilder &builder,
                                llvm::ms_demangle::Node &node,
                                TypeIndex current_type_index) {
  clang::QualType qt =
      GetClangTypeForMSDemangleType(builder, node, current_type_index);
  if (!qt.isNull())
    return qt;

  if (auto *primitive =
          llvm::dyn_cast<llvm::ms_demangle::PrimitiveTypeNode>(&node))
    return ApplyMSDemangleTypeQualifiers(
        GetClangTypeForPrimitiveTemplateArgument(builder, primitive->PrimKind),
        primitive->Quals);

  if (!llvm::isa<llvm::ms_demangle::TypeNode>(&node))
    return {};

  std::string type_name = GetTemplateArgumentTypeName(node);
  return FindPdbTemplateArgumentType(builder, type_name, current_type_index);
}

static unsigned
GetClangTypeQualifiers(llvm::ms_demangle::Qualifiers qualifiers) {
  unsigned result = 0;
  if ((qualifiers & llvm::ms_demangle::Q_Const) != 0)
    result |= clang::Qualifiers::Const;
  if ((qualifiers & llvm::ms_demangle::Q_Volatile) != 0)
    result |= clang::Qualifiers::Volatile;
  if ((qualifiers & llvm::ms_demangle::Q_Restrict) != 0)
    result |= clang::Qualifiers::Restrict;
  return result;
}

static bool IsVoidTypeNode(llvm::ms_demangle::Node &node) {
  auto *primitive = llvm::dyn_cast<llvm::ms_demangle::PrimitiveTypeNode>(&node);
  return primitive &&
         primitive->PrimKind == llvm::ms_demangle::PrimitiveKind::Void;
}

static unsigned GetMemberFunctionTypeQualifiers(PdbIndex &index,
                                                TypeIndex this_type_idx) {
  if (this_type_idx.isNoneType() || this_type_idx.isSimple())
    return 0;

  CVType this_type = index.tpi().getType(this_type_idx);
  if (this_type.kind() != LF_POINTER)
    return 0;

  PointerRecord pointer;
  llvm::cantFail(TypeDeserializer::deserializeAs<PointerRecord>(this_type,
                                                                 pointer));

  unsigned qualifiers = 0;
  TypeIndex pointee_idx = pointer.ReferentType;
  while (!pointee_idx.isNoneType() && !pointee_idx.isSimple()) {
    CVType pointee_type = index.tpi().getType(pointee_idx);
    if (pointee_type.kind() != LF_MODIFIER)
      break;

    ModifierRecord modifier;
    llvm::cantFail(TypeDeserializer::deserializeAs<ModifierRecord>(
        pointee_type, modifier));
    if ((modifier.Modifiers & ModifierOptions::Const) != ModifierOptions::None)
      qualifiers |= clang::Qualifiers::Const;
    if ((modifier.Modifiers & ModifierOptions::Volatile) !=
        ModifierOptions::None)
      qualifiers |= clang::Qualifiers::Volatile;
    pointee_idx = modifier.ModifiedType;
  }

  return qualifiers;
}

static clang::QualType
GetClangTypeForMSDemangleFunctionSignature(
    PdbAstBuilder &builder, llvm::ms_demangle::FunctionSignatureNode &signature,
    TypeIndex current_type_index) {
  if (!signature.ReturnType)
    return {};

  clang::QualType return_type =
      GetClangTypeForMSDemangleType(builder, *signature.ReturnType,
                                    current_type_index);
  if (return_type.isNull())
    return {};

  std::vector<CompilerType> arg_types;
  if (signature.Params) {
    arg_types.reserve(signature.Params->Count);
    for (size_t i = 0; i < signature.Params->Count; ++i) {
      llvm::ms_demangle::Node *param = signature.Params->Nodes[i];
      if (!param)
        return {};
      if (signature.Params->Count == 1 && IsVoidTypeNode(*param))
        continue;

      clang::QualType arg_type =
          GetClangTypeForMSDemangleType(builder, *param, current_type_index);
      if (arg_type.isNull())
        return {};
      arg_types.push_back(builder.ToCompilerType(arg_type));
    }
  }

  std::optional<clang::CallingConv> cc =
      TranslateCallingConvention(signature.CallConvention);
  if (!cc)
    return {};
  std::optional<clang::RefQualifierKind> ref_qual =
      TranslateFunctionRefQualifier(signature.RefQualifier);
  if (!ref_qual)
    return {};

  CompilerType function_type = builder.clang().CreateFunctionType(
      builder.ToCompilerType(return_type), arg_types, signature.IsVariadic,
      GetClangTypeQualifiers(signature.Quals), *cc, *ref_qual);
  if (!function_type.IsValid())
    return {};

  return clang::QualType::getFromOpaquePtr(function_type.GetOpaqueQualType());
}

static clang::QualType
GetClangTypeForMSDemangleType(PdbAstBuilder &builder,
                              llvm::ms_demangle::Node &node,
                              TypeIndex current_type_index) {
  if (auto *primitive =
          llvm::dyn_cast<llvm::ms_demangle::PrimitiveTypeNode>(&node))
    return ApplyMSDemangleTypeQualifiers(
        GetClangTypeForPrimitiveTemplateArgument(builder, primitive->PrimKind),
        primitive->Quals);

  if (auto *function_signature =
          llvm::dyn_cast<llvm::ms_demangle::FunctionSignatureNode>(&node))
    return ApplyMSDemangleTypeQualifiers(
        GetClangTypeForMSDemangleFunctionSignature(
            builder, *function_signature, current_type_index),
        function_signature->Quals);

  if (auto *pointer = llvm::dyn_cast<llvm::ms_demangle::PointerTypeNode>(&node)) {
    if (!pointer->Pointee || pointer->ClassParent)
      return {};

    clang::QualType pointee_type =
        GetClangTypeForMSDemangleType(builder, *pointer->Pointee,
                                      current_type_index);
    if (pointee_type.isNull())
      return {};

    clang::ASTContext &ast = builder.clang().getASTContext();
    clang::QualType pointer_type;
    switch (pointer->Affinity) {
    case llvm::ms_demangle::PointerAffinity::Pointer:
      pointer_type = ast.getPointerType(pointee_type);
      break;
    case llvm::ms_demangle::PointerAffinity::Reference:
      pointer_type = ast.getLValueReferenceType(pointee_type);
      break;
    case llvm::ms_demangle::PointerAffinity::RValueReference:
      pointer_type = ast.getRValueReferenceType(pointee_type);
      break;
    case llvm::ms_demangle::PointerAffinity::None:
      return {};
    }

    return ApplyMSDemangleTypeQualifiers(pointer_type, pointer->Quals);
  }

  if (auto *tag_type = llvm::dyn_cast<llvm::ms_demangle::TagTypeNode>(&node)) {
    std::string type_name = GetTemplateArgumentTypeName(node);
    clang::QualType tag_qt =
        FindPdbTemplateArgumentType(builder, type_name, current_type_index);
    if (tag_qt.isNull()) {
      tag_qt = CreateFallbackTemplateArgumentType(builder, type_name, *tag_type,
                                                  current_type_index);
    }
    return ApplyMSDemangleTypeQualifiers(tag_qt, tag_type->Quals);
  }

  return {};
}

static clang::TemplateArgument CreateIntegralTemplateArgument(
    TypeSystemClang &clang_ast, llvm::ms_demangle::IntegerLiteralNode &literal,
    clang::QualType type) {
  clang::ASTContext &ast = clang_ast.getASTContext();
  if (type.isNull()) {
    constexpr uint64_t max_signed_int64 =
        static_cast<uint64_t>(std::numeric_limits<int64_t>::max());
    if (!literal.IsNegative &&
        literal.Value > max_signed_int64)
      type = ast.UnsignedLongLongTy;
    else
      type = ast.LongLongTy;
  }

  unsigned bit_width = ast.getIntWidth(type);
  llvm::APInt int_value(bit_width, literal.Value);
  if (literal.IsNegative)
    int_value = -int_value;

  llvm::APSInt value(int_value, !type->isSignedIntegerOrEnumerationType());
  return clang::TemplateArgument(ast, value, type);
}

static std::optional<clang::TemplateArgument>
CreateIntegralTemplateArgumentFromName(TypeSystemClang &clang_ast,
                                       llvm::StringRef name) {
  name = name.trim();
  int64_t int_value = 0;
  if (name.getAsInteger(0, int_value))
    return std::nullopt;

  clang::ASTContext &ast = clang_ast.getASTContext();
  llvm::APSInt value(llvm::APInt(64, int_value, true),
                     /*isUnsigned=*/false);
  return clang::TemplateArgument(ast, value, ast.LongLongTy);
}

static clang::QualType
GetClangTypeForTemplateArgumentName(PdbAstBuilder &builder,
                                    llvm::StringRef name) {
  TemplateArgumentTypeName unqualified = StripTopLevelQualifiers(name);

  std::optional<lldb::BasicType> basic_type;
  if (unqualified.name == "void")
    basic_type = lldb::eBasicTypeVoid;
  else if (unqualified.name == "bool")
    basic_type = lldb::eBasicTypeBool;
  else if (unqualified.name == "char")
    basic_type = lldb::eBasicTypeChar;
  else if (unqualified.name == "signed char")
    basic_type = lldb::eBasicTypeSignedChar;
  else if (unqualified.name == "unsigned char")
    basic_type = lldb::eBasicTypeUnsignedChar;
  else if (unqualified.name == "short")
    basic_type = lldb::eBasicTypeShort;
  else if (unqualified.name == "unsigned short")
    basic_type = lldb::eBasicTypeUnsignedShort;
  else if (unqualified.name == "int")
    basic_type = lldb::eBasicTypeInt;
  else if (unqualified.name == "unsigned int")
    basic_type = lldb::eBasicTypeUnsignedInt;
  else if (unqualified.name == "long")
    basic_type = lldb::eBasicTypeLong;
  else if (unqualified.name == "unsigned long")
    basic_type = lldb::eBasicTypeUnsignedLong;
  else if (unqualified.name == "long long" ||
           unqualified.name == "__int64")
    basic_type = lldb::eBasicTypeLongLong;
  else if (unqualified.name == "unsigned long long" ||
           unqualified.name == "unsigned __int64")
    basic_type = lldb::eBasicTypeUnsignedLongLong;
  else if (unqualified.name == "float")
    basic_type = lldb::eBasicTypeFloat;
  else if (unqualified.name == "double")
    basic_type = lldb::eBasicTypeDouble;

  if (!basic_type)
    return {};

  clang::QualType qt = builder.GetBasicType(*basic_type);
  if (unqualified.is_const)
    qt = qt.withConst();
  if (unqualified.is_volatile)
    qt = qt.withVolatile();
  return qt;
}

static bool ParseTemplateParameterInfosFromName(
    PdbAstBuilder &builder, llvm::StringRef name, TypeIndex current_type_index,
    TypeSystemClang::TemplateParameterInfos &template_param_infos) {
  std::vector<std::string> args = ParseTemplateArgumentsFromName(name);
  if (args.empty())
    return false;

  for (llvm::StringRef arg : args) {
    if (std::optional<clang::TemplateArgument> integral_arg =
            CreateIntegralTemplateArgumentFromName(builder.clang(), arg)) {
      template_param_infos.InsertArg(nullptr, *integral_arg);
      continue;
    }

    clang::QualType qt = GetClangTypeForTemplateArgumentName(builder, arg);
    if (qt.isNull())
      qt = FindPdbTemplateArgumentType(builder, arg, current_type_index);
    if (qt.isNull())
      return false;

    template_param_infos.InsertArg(nullptr, clang::TemplateArgument(qt));
  }

  return template_param_infos.IsValid() && !template_param_infos.IsEmpty();
}

static clang::DeclContext *
CreateNamespaceDeclContextForQualifiedName(PdbAstBuilder &builder,
                                           llvm::StringRef qualified_name) {
  TypeSystemClang &clang_ast = builder.clang();
  clang::DeclContext *context = clang_ast.GetTranslationUnitDecl();
  llvm::StringRef parent_name = GetParentTypeName(qualified_name);
  if (parent_name.empty())
    return context;

  size_t component_begin = 0;
  unsigned template_depth = 0;
  for (size_t i = 0; i <= parent_name.size(); ++i) {
    bool at_separator = i + 1 < parent_name.size() && parent_name[i] == ':' &&
                        parent_name[i + 1] == ':' && template_depth == 0;
    bool at_end = i == parent_name.size();
    if (!at_separator && !at_end) {
      if (parent_name[i] == '<')
        ++template_depth;
      else if (parent_name[i] == '>' && template_depth > 0)
        --template_depth;
      continue;
    }

    llvm::StringRef component =
        parent_name.slice(component_begin, i).trim();
    if (component.empty() || component.contains('<'))
      return nullptr;

    std::string component_storage = component.str();
    context =
        builder.GetOrCreateNamespaceDecl(component_storage.c_str(), *context);
    if (at_separator) {
      ++i;
      component_begin = i + 1;
    }
  }

  return context;
}

static clang::QualType CreateFallbackTemplateArgumentSpecializationType(
    PdbAstBuilder &builder, llvm::StringRef name,
    llvm::ms_demangle::TagKind tag_kind,
    llvm::ms_demangle::Qualifiers qualifiers, TypeIndex current_type_index) {
  if (!name.contains('<'))
    return {};

  clang::DeclContext *context =
      CreateNamespaceDeclContextForQualifiedName(builder, name);
  if (!context)
    return {};

  TypeSystemClang::TemplateParameterInfos template_param_infos;
  if (!ParseTemplateParameterInfosFromName(builder, name, current_type_index,
                                          template_param_infos))
    return {};

  clang::TagTypeKind ttk = GetClangTagTypeKindForMSDemangleTag(tag_kind);
  lldb::AccessType access = (ttk == clang::TagTypeKind::Class)
                                ? lldb::eAccessPrivate
                                : lldb::eAccessPublic;

  ClangASTMetadata metadata;
  if (std::optional<Declaration> declaration =
          GetLambdaDeclarationFromTypeName(name))
    metadata.SetDeclaration(*declaration);
  CompilerType ct = CreateClassTemplateSpecializationType(
      builder, context, access, GetUnqualifiedTypeName(name), ttk,
      template_param_infos, metadata, false);
  if (!ct.IsValid())
    return {};

  clang::QualType qt =
      clang::QualType::getFromOpaquePtr(ct.GetOpaqueQualType());
  ApplyTemplateArgumentDeclarationMetadataFromName(builder, qt, name);
  return ApplyMSDemangleTypeQualifiers(qt, qualifiers);
}

static clang::QualType CreateFallbackTemplateSpecializationTypeFromName(
    PdbAstBuilder &builder, llvm::StringRef name,
    TypeIndex current_type_index, TypeIndex type_index,
    clang::QualType original_qt) {
  TemplateArgumentTypeName unqualified = StripTopLevelQualifiers(name);
  llvm::StringRef type_name = unqualified.name;
  if (!type_name.contains('<'))
    return {};

  clang::DeclContext *context =
      CreateNamespaceDeclContextForQualifiedName(builder, type_name);
  if (!context)
    return {};

  TypeSystemClang::TemplateParameterInfos template_param_infos;
  if (!ParseTemplateParameterInfosFromName(builder, type_name, current_type_index,
                                          template_param_infos))
    return {};

  clang::TagDecl *tag = original_qt->getAsTagDecl();
  if (!tag)
    return {};

  clang::TagTypeKind ttk = tag->getTagKind();
  lldb::AccessType access = (ttk == clang::TagTypeKind::Class)
                                ? lldb::eAccessPrivate
                                : lldb::eAccessPublic;

  ClangASTMetadata metadata;
  metadata.SetUserID(toOpaqueUid(PdbTypeSymId(type_index)));
  metadata.SetIsDynamicCXXType(false);
  SymbolFileNativePDB *pdb = static_cast<SymbolFileNativePDB *>(
      builder.clang().GetSymbolFile()->GetBackingSymbolFile());
  llvm::Expected<Declaration> declaration =
      pdb->ResolveUdtDeclaration(PdbTypeSymId(type_index));
  if (declaration)
    metadata.SetDeclaration(*declaration);
  else
    llvm::consumeError(declaration.takeError());

  CompilerType ct = CreateClassTemplateSpecializationType(
      builder, context, access, GetUnqualifiedTypeName(type_name),
      ttk, template_param_infos, metadata, true);
  if (!ct.IsValid())
    return {};

  clang::QualType qt =
      clang::QualType::getFromOpaquePtr(ct.GetOpaqueQualType());
  ApplyTemplateArgumentDeclarationMetadataFromName(builder, qt, name);
  TypeSystemClang::StartTagDeclarationDefinition(ct);
  TypeSystemClang::SetHasExternalStorage(qt.getAsOpaquePtr(), true);
  builder.RegisterTagType(PdbTypeSymId(type_index), qt);
  if (unqualified.is_const)
    qt = qt.withConst();
  if (unqualified.is_volatile)
    qt = qt.withVolatile();
  return qt;
}

static std::optional<clang::TemplateArgument>
RetypeIntegralTemplateArgument(TypeSystemClang &clang_ast,
                               clang::TemplateArgument const &argument,
                               clang::QualType type) {
  if (argument.getKind() != clang::TemplateArgument::Integral || type.isNull() ||
      !type->isIntegralOrEnumerationType())
    return std::nullopt;

  clang::ASTContext &ast = clang_ast.getASTContext();
  unsigned bit_width = ast.getIntWidth(type);
  llvm::APSInt const &source = argument.getAsIntegral();
  llvm::APInt int_value = source.isSigned() ? source.sextOrTrunc(bit_width)
                                            : source.zextOrTrunc(bit_width);
  llvm::APSInt value(int_value, !type->isSignedIntegerOrEnumerationType());
  return clang::TemplateArgument(ast, value, type);
}

static bool ParseTemplateParameterInfos(
    PdbAstBuilder &builder, llvm::codeview::TagRecord const &record,
    TypeIndex current_type_index,
    TypeSystemClang::TemplateParameterInfos &template_param_infos) {
  if (!record.hasUniqueName())
    return false;

  llvm::ms_demangle::Demangler demangler;
  std::string_view sv(record.UniqueName.begin(), record.UniqueName.size());
  llvm::ms_demangle::TagTypeNode *ttn = demangler.parseTagUniqueName(sv);
  if (demangler.Error || !ttn || !ttn->QualifiedName)
    return false;

  llvm::ms_demangle::IdentifierNode *idn = FindIdentifierForUnqualifiedTypeName(
      *ttn->QualifiedName, GetUnqualifiedTypeName(record.Name));
  if (!idn)
    idn = ttn->QualifiedName->getUnqualifiedIdentifier();
  if (!idn || !idn->TemplateParams || idn->TemplateParams->Count == 0)
    return false;

  std::vector<std::string> record_template_args =
      ParseTemplateArgumentsFromName(GetUnqualifiedTypeName(record.Name));

  llvm::ms_demangle::NodeArrayNode &params = *idn->TemplateParams;
  for (size_t i = 0; i < params.Count; ++i) {
    llvm::ms_demangle::Node *node = params.Nodes[i];
    if (!node)
      return false;

    if (auto *integer =
            llvm::dyn_cast<llvm::ms_demangle::IntegerLiteralNode>(node)) {
      template_param_infos.InsertArg(
          nullptr, CreateIntegralTemplateArgument(builder.clang(), *integer));
      continue;
    }

    clang::QualType qt;
    if (i < record_template_args.size())
      qt = FindPdbTemplateArgumentType(builder, record_template_args[i],
                                       current_type_index);
    if (qt.isNull())
      qt = GetClangTypeForTemplateArgument(builder, *node, current_type_index);
    if (qt.isNull()) {
      if (auto *tag_type =
              llvm::dyn_cast<llvm::ms_demangle::TagTypeNode>(node)) {
        std::string fallback_name_storage;
        llvm::StringRef fallback_name;
        if (i < record_template_args.size())
          fallback_name = record_template_args[i];
        else {
          fallback_name_storage = GetTemplateArgumentTypeName(*node);
          fallback_name = fallback_name_storage;
        }
        qt = CreateFallbackTemplateArgumentType(builder, fallback_name,
                                                *tag_type,
                                                current_type_index);
      }
    }
    if (qt.isNull())
      return false;

    template_param_infos.InsertArg(nullptr, clang::TemplateArgument(qt));
  }

  return template_param_infos.IsValid() && !template_param_infos.IsEmpty();
}

static bool BuildTemplateParameterInfosForExistingTemplate(
    TypeSystemClang &clang_ast, clang::ClassTemplateDecl &class_template_decl,
    TypeSystemClang::TemplateParameterInfos const &source_infos,
    TypeSystemClang::TemplateParameterInfos &result_infos) {
  if (source_infos.hasParameterPack())
    return false;

  clang::TemplateParameterList &params =
      *class_template_decl.getTemplateParameters();

  llvm::ArrayRef<clang::TemplateArgument> args = source_infos.GetArgs();
  llvm::ArrayRef<const char *> names = source_infos.GetNames();
  size_t source_idx = 0;

  auto adjust_arg_for_param =
      [&](clang::NamedDecl *param, clang::TemplateArgument const &arg)
          -> std::optional<clang::TemplateArgument> {
    if (llvm::isa<clang::TemplateTypeParmDecl>(param)) {
      if (arg.getKind() != clang::TemplateArgument::Type)
        return std::nullopt;
      return arg;
    }

    if (auto *value_param =
            llvm::dyn_cast<clang::NonTypeTemplateParmDecl>(param))
      return RetypeIntegralTemplateArgument(clang_ast, arg,
                                            value_param->getType());

    return std::nullopt;
  };

  for (size_t i = 0; i < params.size(); ++i) {
    clang::NamedDecl *param = params.getParam(i);
    if (param->isParameterPack()) {
      if (i + 1 != params.size())
        return false;

      auto packed_infos =
          std::make_unique<TypeSystemClang::TemplateParameterInfos>();
      for (; source_idx < source_infos.Size(); ++source_idx) {
        std::optional<clang::TemplateArgument> adjusted_arg =
            adjust_arg_for_param(param, args[source_idx]);
        if (!adjusted_arg)
          return false;

        packed_infos->InsertArg(names[source_idx], *adjusted_arg);
      }

      if (!packed_infos->IsValid())
        return false;

      result_infos.SetParameterPack(std::move(packed_infos));
      break;
    }

    if (source_idx >= source_infos.Size())
      return false;

    std::optional<clang::TemplateArgument> adjusted_arg =
        adjust_arg_for_param(param, args[source_idx]);
    if (!adjusted_arg)
      return false;

    result_infos.InsertArg(names[source_idx], *adjusted_arg);
    ++source_idx;
  }

  if (source_idx != source_infos.Size())
    return false;

  return result_infos.IsValid();
}

static std::string GetClassTemplateBaseName(llvm::StringRef name) {
  std::string template_basename(name);
  if (auto i = template_basename.find('<'); i != std::string::npos)
    template_basename.erase(i);
  return template_basename;
}

static CompilerType CreateClassTemplateSpecializationTypeForDecl(
    TypeSystemClang &clang_ast, clang::DeclContext *context,
    clang::ClassTemplateDecl *class_template_decl, clang::TagTypeKind ttk,
    TypeSystemClang::TemplateParameterInfos const &template_param_infos,
    ClangASTMetadata const &metadata, bool require_new) {
  if (!class_template_decl)
    return {};

  clang::ASTContext &ast = clang_ast.getASTContext();
  llvm::SmallVector<clang::TemplateArgument, 2> args(
      template_param_infos.Size() +
      (template_param_infos.hasParameterPack() ? 1 : 0));
  llvm::ArrayRef<clang::TemplateArgument> orig_args =
      template_param_infos.GetArgs();
  std::copy(orig_args.begin(), orig_args.end(), args.begin());
  if (template_param_infos.hasParameterPack())
    args[args.size() - 1] = clang::TemplateArgument::CreatePackCopy(
        ast, template_param_infos.GetParameterPackArgs());

  void *insert_pos = nullptr;
  if (clang::ClassTemplateSpecializationDecl *existing_decl =
          class_template_decl->findSpecialization(args, insert_pos)) {
    if (require_new)
      return {};
    return clang_ast.CreateClassTemplateSpecializationType(existing_decl);
  }

  clang::ClassTemplateSpecializationDecl *class_specialization_decl =
      clang_ast.CreateClassTemplateSpecializationDecl(
          context, OptionalClangModuleID(), class_template_decl,
          llvm::to_underlying(ttk), template_param_infos);
  if (!class_specialization_decl)
    return {};

  CompilerType ct = clang_ast.CreateClassTemplateSpecializationType(
      class_specialization_decl);
  if (ct.IsValid()) {
    clang_ast.SetMetadata(class_template_decl, metadata);
    clang_ast.SetMetadata(class_specialization_decl, metadata);
    clang::QualType qt =
        clang::QualType::getFromOpaquePtr(ct.GetOpaqueQualType());
    clang_ast.SetMetadata(qt.getTypePtr(), metadata);
  }
  return ct;
}

static CompilerType CreateClassTemplateSpecializationType(
    PdbAstBuilder &builder, clang::DeclContext *context,
    lldb::AccessType access, llvm::StringRef uname, clang::TagTypeKind ttk,
    TypeSystemClang::TemplateParameterInfos const &template_param_infos,
    ClangASTMetadata const &metadata, bool require_new) {
  TypeSystemClang &clang_ast = builder.clang();
  std::string template_basename = GetClassTemplateBaseName(uname);
  clang::DeclarationName decl_name(
      &clang_ast.getASTContext().Idents.get(template_basename));
  for (clang::NamedDecl *decl : context->lookup(decl_name)) {
    auto *class_template_decl =
        llvm::dyn_cast<clang::ClassTemplateDecl>(decl);
    if (!class_template_decl)
      continue;

    TypeSystemClang::TemplateParameterInfos adjusted_infos;
    if (!BuildTemplateParameterInfosForExistingTemplate(
            clang_ast, *class_template_decl, template_param_infos,
            adjusted_infos))
      continue;

    CompilerType ct = CreateClassTemplateSpecializationTypeForDecl(
        clang_ast, context, class_template_decl, ttk, adjusted_infos, metadata,
        require_new);
    if (ct.IsValid())
      return ct;
  }

  // PDB records describe specializations, not necessarily the original template
  // declaration. If all known declarations have incompatible arity, synthesize
  // another declaration that matches this specialization.
  clang::ClassTemplateDecl *class_template_decl =
      clang_ast.ParseClassTemplateDecl(
          context, OptionalClangModuleID(), access, uname.str().c_str(),
          llvm::to_underlying(ttk), template_param_infos);
  return CreateClassTemplateSpecializationTypeForDecl(
      clang_ast, context, class_template_decl, ttk, template_param_infos,
      metadata, require_new);
}

PdbAstBuilder::PdbAstBuilder(TypeSystemClang &clang) : m_clang(clang) {}

void PdbAstBuilder::RegisterTagType(PdbTypeSymId type, clang::QualType qt,
                                    bool resolved) {
  if (qt.isNull())
    return;

  lldb::user_id_t uid = toOpaqueUid(type);
  m_uid_to_type[uid] = qt;

  clang::TagDecl *tag = qt->getAsTagDecl();
  if (!tag)
    return;

  m_uid_to_decl[uid] = tag;

  DeclStatus &status = m_decl_to_status[tag];
  if (status.uid == 0)
    status.uid = uid;
  if (resolved)
    status.resolved = true;
}

lldb_private::CompilerDeclContext PdbAstBuilder::GetTranslationUnitDecl() {
  return ToCompilerDeclContext(m_clang.GetTranslationUnitDecl());
}

std::pair<clang::DeclContext *, std::string>
PdbAstBuilder::CreateDeclInfoForType(const TagRecord &record, TypeIndex ti) {
  SymbolFileNativePDB *pdb = static_cast<SymbolFileNativePDB *>(
      m_clang.GetSymbolFile()->GetBackingSymbolFile());
  PdbIndex &index = pdb->GetIndex();
  // FIXME: Move this to GetDeclContextContainingUID.
  if (!record.hasUniqueName())
    return CreateDeclInfoForUndecoratedName(record.Name);

  llvm::ms_demangle::Demangler demangler;
  std::string_view sv(record.UniqueName.begin(), record.UniqueName.size());
  llvm::ms_demangle::TagTypeNode *ttn = demangler.parseTagUniqueName(sv);
  if (demangler.Error)
    return CreateDeclInfoForUndecoratedName(record.Name);

  llvm::ms_demangle::IdentifierNode *idn = FindIdentifierForUnqualifiedTypeName(
      *ttn->QualifiedName, GetUnqualifiedTypeName(record.Name));
  if (!idn)
    idn = ttn->QualifiedName->getUnqualifiedIdentifier();
  std::string uname = idn->toString(llvm::ms_demangle::OF_NoTagSpecifier);

  llvm::ms_demangle::NodeArrayNode *name_components =
      ttn->QualifiedName->Components;
  llvm::ArrayRef<llvm::ms_demangle::Node *> scopes(name_components->Nodes,
                                                   name_components->Count - 1);

  clang::DeclContext *context = m_clang.GetTranslationUnitDecl();

  // If this type doesn't have a parent type in the debug info, then the best we
  // can do is to say that it's either a series of namespaces (if the scope is
  // non-empty), or the translation unit (if the scope is empty).
  std::optional<TypeIndex> parent_index = pdb->GetParentType(ti);
  if (!parent_index) {
    if (scopes.empty())
      return {context, uname};

    // If there is no parent in the debug info, but some of the scopes have
    // template params, then this is a case of bad debug info.  See, for
    // example, llvm.org/pr39607.  We don't want to create an ambiguity between
    // a NamespaceDecl and a CXXRecordDecl.  If the undecorated type name still
    // gives us a complete parent type name, use it to recover the missing
    // parent context; otherwise create a class at global scope with the fully
    // qualified name.
    if (AnyScopesHaveTemplateParams(scopes)) {
      llvm::StringRef parent_name = GetParentTypeName(record.Name);
      std::vector<std::string> parent_candidates;
      if (!parent_name.empty()) {
        parent_candidates.push_back(parent_name.str());
        std::string compact_parent_name =
            RemoveTemplateSeparatorWhitespace(parent_name);
        if (compact_parent_name != parent_candidates.front())
          parent_candidates.push_back(compact_parent_name);
      }

      for (llvm::StringRef parent_candidate : parent_candidates) {
        std::vector<TypeIndex> matches =
            index.tpi().findRecordsByName(parent_candidate);
        for (TypeIndex parent_ti : matches) {
          if (parent_ti == ti)
            continue;

          clang::QualType parent_qt = GetOrCreateClangType(parent_ti);
          if (parent_qt.isNull())
            continue;

          if (clang::TagDecl *parent_tag = parent_qt->getAsTagDecl())
            return {clang::TagDecl::castToDeclContext(parent_tag),
                    GetUnqualifiedTypeName(record.Name).str()};
        }
      }

      if (!parent_name.empty() && !parent_name.contains('<'))
        return CreateDeclInfoForUndecoratedName(record.Name);

      return {context, std::string(record.Name)};
    }

    for (llvm::ms_demangle::Node *scope : scopes) {
      auto *nii = static_cast<llvm::ms_demangle::NamedIdentifierNode *>(scope);
      std::string str = nii->toString();
      context = GetOrCreateNamespaceDecl(str.c_str(), *context);
    }
    return {context, uname};
  }

  // Otherwise, all we need to do is get the parent type of this type and
  // recurse into our lazy type creation / AST reconstruction logic to get an
  // LLDB TypeSP for the parent.  This will cause the AST to automatically get
  // the right DeclContext created for any parent.
  clang::QualType parent_qt = GetOrCreateClangType(*parent_index);
  if (parent_qt.isNull())
    return {nullptr, ""};

  context = clang::TagDecl::castToDeclContext(parent_qt->getAsTagDecl());
  return {context, uname};
}

static bool isLocalVariableType(SymbolKind K) {
  switch (K) {
  case S_REGISTER:
  case S_REGREL32:
  case S_LOCAL:
    return true;
  default:
    break;
  }
  return false;
}

clang::Decl *PdbAstBuilder::GetOrCreateSymbolForId(PdbCompilandSymId id) {
  SymbolFileNativePDB *pdb = static_cast<SymbolFileNativePDB *>(
      m_clang.GetSymbolFile()->GetBackingSymbolFile());
  PdbIndex &index = pdb->GetIndex();
  CVSymbol cvs = index.ReadSymbolRecord(id);

  if (isLocalVariableType(cvs.kind())) {
    clang::DeclContext *scope = GetParentClangDeclContext(id);
    if (!scope)
      return nullptr;
    clang::Decl *scope_decl = clang::Decl::castFromDeclContext(scope);
    PdbCompilandSymId scope_id =
        PdbSymUid(m_decl_to_status[scope_decl].uid).asCompilandSym();
    return GetOrCreateVariableDecl(scope_id, id);
  }

  switch (cvs.kind()) {
  case S_GPROC32:
  case S_LPROC32:
    return GetOrCreateFunctionDecl(id);
  case S_GDATA32:
  case S_LDATA32:
  case S_GTHREAD32: {
    VariableInfo var_info = GetVariableNameInfo(cvs);
    clang::DeclContext *context = nullptr;
    std::string name;
    std::tie(context, name) = CreateDeclInfoForUndecoratedName(var_info.name);
    if (!context)
      context = FromCompilerDeclContext(GetTranslationUnitDecl());
    return CreateVariableDecl(PdbSymUid(id), cvs, *context, name);
  }
  case S_CONSTANT:
    // global variable
    return nullptr;
  case S_BLOCK32:
    return GetOrCreateBlockDecl(id);
  case S_INLINESITE:
    return GetOrCreateInlinedFunctionDecl(id);
  default:
    return nullptr;
  }
}

std::optional<CompilerDecl>
PdbAstBuilder::GetOrCreateDeclForUid(PdbSymUid uid) {
  if (clang::Decl *result = TryGetDecl(uid))
    return ToCompilerDecl(result);

  clang::Decl *result = nullptr;
  switch (uid.kind()) {
  case PdbSymUidKind::CompilandSym:
    result = GetOrCreateSymbolForId(uid.asCompilandSym());
    break;
  case PdbSymUidKind::Type: {
    clang::QualType qt = GetOrCreateClangType(uid.asTypeSym());
    if (qt.isNull())
      return std::nullopt;
    if (auto *tag = qt->getAsTagDecl()) {
      result = tag;
      break;
    }
    return std::nullopt;
  }
  default:
    return std::nullopt;
  }

  if (!result)
    return std::nullopt;
  m_uid_to_decl[toOpaqueUid(uid)] = result;
  return ToCompilerDecl(result);
}

clang::DeclContext *
PdbAstBuilder::GetOrCreateClangDeclContextForUid(PdbSymUid uid) {
  if (uid.kind() == PdbSymUidKind::CompilandSym) {
    if (uid.asCompilandSym().offset == 0)
      return FromCompilerDeclContext(GetTranslationUnitDecl());
  }
  auto option = GetOrCreateDeclForUid(uid);
  if (!option)
    return nullptr;
  clang::Decl *decl = FromCompilerDecl(*option);
  if (!decl)
    return nullptr;

  return clang::Decl::castToDeclContext(decl);
}

CompilerDeclContext PdbAstBuilder::GetOrCreateDeclContextForUid(PdbSymUid uid) {
  return ToCompilerDeclContext(GetOrCreateClangDeclContextForUid(uid));
}

std::pair<clang::DeclContext *, std::string>
PdbAstBuilder::CreateDeclInfoForUndecoratedName(llvm::StringRef name) {
  SymbolFileNativePDB *pdb = static_cast<SymbolFileNativePDB *>(
      m_clang.GetSymbolFile()->GetBackingSymbolFile());
  PdbIndex &index = pdb->GetIndex();
  MSVCUndecoratedNameParser parser(name);
  llvm::ArrayRef<MSVCUndecoratedNameSpecifier> specs = parser.GetSpecifiers();

  auto *context = FromCompilerDeclContext(GetTranslationUnitDecl());

  llvm::StringRef uname = specs.back().GetBaseName();
  specs = specs.drop_back();
  if (specs.empty())
    return {context, std::string(name)};

  llvm::StringRef scope_name = specs.back().GetFullName();

  // It might be a class name, try that first.
  auto get_type_decl_context = [&](llvm::StringRef type_name)
      -> clang::DeclContext * {
    std::vector<TypeIndex> types = index.tpi().findRecordsByName(type_name);
    while (!types.empty()) {
      clang::QualType qt = GetOrCreateClangType(types.back());
      types.pop_back();
      if (qt.isNull())
        continue;

      ApplyTemplateArgumentDeclarationMetadataFromName(*this, qt, type_name);
      clang::TagDecl *tag = qt->getAsTagDecl();
      if (tag)
        return clang::TagDecl::castToDeclContext(tag);
    }

    return nullptr;
  };

  std::vector<std::string> scope_candidates;
  scope_candidates.push_back(scope_name.str());
  std::string compact_scope_name =
      RemoveTemplateSeparatorWhitespace(scope_name);
  if (compact_scope_name != scope_name)
    scope_candidates.push_back(compact_scope_name);

  for (llvm::StringRef scope_candidate : scope_candidates) {
    if (clang::DeclContext *tag_context =
            get_type_decl_context(scope_candidate))
      return {tag_context, std::string(uname)};
  }

  if (scope_name.contains('<')) {
    clang::QualType qt = FindPdbTemplateArgumentType(*this, scope_name,
                                                     TypeIndex());
    if (!qt.isNull()) {
      if (clang::TagDecl *tag = qt->getAsTagDecl())
        return {clang::TagDecl::castToDeclContext(tag), std::string(uname)};
    }
  }

  // If that fails, treat it as a series of namespaces.
  for (const MSVCUndecoratedNameSpecifier &spec : specs) {
    std::string ns_name = spec.GetBaseName().str();
    context = GetOrCreateNamespaceDecl(ns_name.c_str(), *context);
  }
  return {context, std::string(uname)};
}

clang::DeclContext *PdbAstBuilder::GetParentClangDeclContext(PdbSymUid uid) {
  // We must do this *without* calling GetOrCreate on the current uid, as
  // that would be an infinite recursion.
  SymbolFileNativePDB *pdb = static_cast<SymbolFileNativePDB *>(
      m_clang.GetSymbolFile()->GetBackingSymbolFile());
  PdbIndex &index = pdb->GetIndex();
  switch (uid.kind()) {
  case PdbSymUidKind::CompilandSym: {
    std::optional<PdbCompilandSymId> scope =
        pdb->FindSymbolScope(uid.asCompilandSym());
    if (scope)
      return GetOrCreateClangDeclContextForUid(*scope);

    CVSymbol sym = index.ReadSymbolRecord(uid.asCompilandSym());
    if (sym.kind() == S_GPROC32 || sym.kind() == S_LPROC32) {
      ProcSym proc(static_cast<SymbolRecordKind>(sym.kind()));
      llvm::cantFail(SymbolDeserializer::deserializeAs<ProcSym>(sym, proc));
      if (!proc.FunctionType.isSimple()) {
        CVType func_type = index.tpi().getType(proc.FunctionType);
        if (func_type.kind() == LF_MFUNCTION) {
          MemberFunctionRecord mfr;
          llvm::cantFail(
              TypeDeserializer::deserializeAs<MemberFunctionRecord>(func_type,
                                                                     mfr));
          if (!mfr.getClassType().isNoneType())
            return GetOrCreateClangDeclContextForUid(
                PdbTypeSymId(mfr.getClassType(), false));
        }
      }
    }

    return CreateDeclInfoForUndecoratedName(getSymbolName(sym)).first;
  }
  case PdbSymUidKind::Type: {
    // It could be a namespace, class, or global.  We don't support nested
    // functions yet.  Anyway, we just need to consult the parent type map.
    PdbTypeSymId type_id = uid.asTypeSym();
    std::optional<TypeIndex> parent_index = pdb->GetParentType(type_id.index);
    if (!parent_index)
      return FromCompilerDeclContext(GetTranslationUnitDecl());
    return GetOrCreateClangDeclContextForUid(PdbTypeSymId(*parent_index));
  }
  case PdbSymUidKind::FieldListMember:
    // In this case the parent DeclContext is the one for the class that this
    // member is inside of.
    break;
  case PdbSymUidKind::GlobalSym: {
    // If this refers to a compiland symbol, just recurse in with that symbol.
    // The only other possibilities are S_CONSTANT and S_UDT, in which case we
    // need to parse the undecorated name to figure out the scope, then look
    // that up in the TPI stream.  If it's found, it's a type, othewrise it's
    // a series of namespaces.
    // FIXME: do this.
    CVSymbol global = index.ReadSymbolRecord(uid.asGlobalSym());
    switch (global.kind()) {
    case SymbolKind::S_GDATA32:
    case SymbolKind::S_LDATA32:
      return CreateDeclInfoForUndecoratedName(getSymbolName(global)).first;;
    case SymbolKind::S_PROCREF:
    case SymbolKind::S_LPROCREF: {
      ProcRefSym ref{global.kind()};
      llvm::cantFail(
          SymbolDeserializer::deserializeAs<ProcRefSym>(global, ref));
      PdbCompilandSymId cu_sym_id{ref.modi(), ref.SymOffset};
      return GetParentClangDeclContext(cu_sym_id);
    }
    case SymbolKind::S_CONSTANT:
    case SymbolKind::S_UDT:
      return CreateDeclInfoForUndecoratedName(getSymbolName(global)).first;
    default:
      break;
    }
    break;
  }
  default:
    break;
  }
  return FromCompilerDeclContext(GetTranslationUnitDecl());
}

CompilerDeclContext PdbAstBuilder::GetParentDeclContext(PdbSymUid uid) {
  return ToCompilerDeclContext(GetParentClangDeclContext(uid));
}

bool PdbAstBuilder::CompleteType(CompilerType ct) {
  if (GetClangASTImporter().CanImport(ct))
    return GetClangASTImporter().CompleteType(ct);

  clang::QualType qt = FromCompilerType(ct);
  if (qt.isNull())
    return false;
  clang::TagDecl *tag = qt->getAsTagDecl();
  if (qt->isArrayType()) {
    const clang::Type *element_type = qt->getArrayElementTypeNoTypeQual();
    tag = element_type->getAsTagDecl();
  }
  if (!tag)
    return false;

  return CompleteTagDecl(*tag);
}

bool PdbAstBuilder::CompleteTagDecl(clang::TagDecl &tag) {
  // If this is not in our map, it's an error.
  auto status_iter = m_decl_to_status.find(&tag);
  lldbassert(status_iter != m_decl_to_status.end());

  // If it's already complete, just return.
  DeclStatus &status = status_iter->second;
  if (status.resolved)
    return true;

  PdbTypeSymId type_id = PdbSymUid(status.uid).asTypeSym();
  PdbIndex &index = static_cast<SymbolFileNativePDB *>(
                        m_clang.GetSymbolFile()->GetBackingSymbolFile())
                        ->GetIndex();
  lldbassert(IsTagRecord(type_id, index.tpi()));

  clang::QualType tag_qt = m_clang.getASTContext().getCanonicalTagType(&tag);
  TypeSystemClang::SetHasExternalStorage(tag_qt.getAsOpaquePtr(), false);

  TypeIndex tag_ti = type_id.index;
  CVType cvt = index.tpi().getType(tag_ti);
  if (cvt.kind() == LF_MODIFIER)
    tag_ti = LookThroughModifierRecord(cvt);

  PdbTypeSymId best_ti = GetBestPossibleDecl(tag_ti, index.tpi());
  cvt = index.tpi().getType(best_ti.index);
  lldbassert(IsTagRecord(cvt));

  if (IsForwardRefUdt(cvt)) {
    // If we can't find a full decl for this forward ref anywhere in the debug
    // info, then we have no way to complete it.
    return false;
  }

  TypeIndex field_list_ti = GetFieldListIndex(cvt);
  CVType field_list_cvt = index.tpi().getType(field_list_ti);
  if (field_list_cvt.kind() != LF_FIELDLIST)
    return false;
  FieldListRecord field_list;
  if (llvm::Error error = TypeDeserializer::deserializeAs<FieldListRecord>(
          field_list_cvt, field_list))
    llvm::consumeError(std::move(error));

  // Visit all members of this class, then perform any finalization necessary
  // to complete the class.
  CompilerType ct = ToCompilerType(tag_qt);
  UdtRecordCompleter completer(best_ti, ct, tag, *this, index, m_decl_to_status,
                               m_cxx_record_map);
  llvm::Error error =
      llvm::codeview::visitMemberRecordStream(field_list.Data, completer);
  completer.complete();

  m_decl_to_status[&tag].resolved = true;
  if (error) {
    llvm::consumeError(std::move(error));
    return false;
  }
  return true;
}

clang::QualType PdbAstBuilder::CreateSimpleType(TypeIndex ti) {
  if (ti == TypeIndex::NullptrT())
    return GetBasicType(lldb::eBasicTypeNullPtr);

  if (ti.getSimpleMode() != SimpleTypeMode::Direct) {
    clang::QualType direct_type = GetOrCreateClangType(ti.makeDirect());
    if (direct_type.isNull())
      return {};
    return m_clang.getASTContext().getPointerType(direct_type);
  }

  if (ti.getSimpleKind() == SimpleTypeKind::NotTranslated)
    return {};

  lldb::BasicType bt = GetCompilerTypeForSimpleKind(ti.getSimpleKind());
  if (bt == lldb::eBasicTypeInvalid)
    return {};

  return GetBasicType(bt);
}

clang::QualType PdbAstBuilder::CreatePointerType(const PointerRecord &pointer) {
  clang::QualType pointee_type = GetOrCreateClangType(pointer.ReferentType);

  // This can happen for pointers to LF_VTSHAPE records, which we shouldn't
  // create in the AST.
  if (pointee_type.isNull())
    return {};

  if (pointer.isPointerToMember()) {
    MemberPointerInfo mpi = pointer.getMemberInfo();
    clang::QualType class_type = GetOrCreateClangType(mpi.ContainingType);
    if (class_type.isNull())
      return {};
    if (clang::TagDecl *tag = class_type->getAsTagDecl()) {
      clang::MSInheritanceAttr::Spelling spelling;
      switch (mpi.Representation) {
      case llvm::codeview::PointerToMemberRepresentation::SingleInheritanceData:
      case llvm::codeview::PointerToMemberRepresentation::
          SingleInheritanceFunction:
        spelling =
            clang::MSInheritanceAttr::Spelling::Keyword_single_inheritance;
        break;
      case llvm::codeview::PointerToMemberRepresentation::
          MultipleInheritanceData:
      case llvm::codeview::PointerToMemberRepresentation::
          MultipleInheritanceFunction:
        spelling =
            clang::MSInheritanceAttr::Spelling::Keyword_multiple_inheritance;
        break;
      case llvm::codeview::PointerToMemberRepresentation::
          VirtualInheritanceData:
      case llvm::codeview::PointerToMemberRepresentation::
          VirtualInheritanceFunction:
        spelling =
            clang::MSInheritanceAttr::Spelling::Keyword_virtual_inheritance;
        break;
      case llvm::codeview::PointerToMemberRepresentation::Unknown:
        spelling =
            clang::MSInheritanceAttr::Spelling::Keyword_unspecified_inheritance;
        break;
      default:
        spelling = clang::MSInheritanceAttr::Spelling::SpellingNotCalculated;
        break;
      }
      tag->addAttr(clang::MSInheritanceAttr::CreateImplicit(
          m_clang.getASTContext(), spelling));
    }
    return m_clang.getASTContext().getMemberPointerType(
        pointee_type, /*Qualifier=*/std::nullopt,
        class_type->getAsCXXRecordDecl());
  }

  clang::QualType pointer_type;
  if (pointer.getMode() == PointerMode::LValueReference)
    pointer_type = m_clang.getASTContext().getLValueReferenceType(pointee_type);
  else if (pointer.getMode() == PointerMode::RValueReference)
    pointer_type = m_clang.getASTContext().getRValueReferenceType(pointee_type);
  else
    pointer_type = m_clang.getASTContext().getPointerType(pointee_type);

  if ((pointer.getOptions() & PointerOptions::Const) != PointerOptions::None)
    pointer_type.addConst();

  if ((pointer.getOptions() & PointerOptions::Volatile) != PointerOptions::None)
    pointer_type.addVolatile();

  if ((pointer.getOptions() & PointerOptions::Restrict) != PointerOptions::None)
    pointer_type.addRestrict();

  return pointer_type;
}

clang::QualType
PdbAstBuilder::CreateModifierType(const ModifierRecord &modifier) {
  clang::QualType unmodified_type = GetOrCreateClangType(modifier.ModifiedType);
  if (unmodified_type.isNull())
    return {};

  if ((modifier.Modifiers & ModifierOptions::Const) != ModifierOptions::None)
    unmodified_type.addConst();
  if ((modifier.Modifiers & ModifierOptions::Volatile) != ModifierOptions::None)
    unmodified_type.addVolatile();

  return unmodified_type;
}

clang::QualType PdbAstBuilder::CreateRecordType(PdbTypeSymId id,
                                                const TagRecord &record) {
  clang::DeclContext *context = nullptr;
  std::string uname;
  std::tie(context, uname) = CreateDeclInfoForType(record, id.index);
  if (!context)
    return {};

  clang::TagTypeKind ttk = TranslateUdtKind(record);
  lldb::AccessType access = (ttk == clang::TagTypeKind::Class)
                                ? lldb::eAccessPrivate
                                : lldb::eAccessPublic;

  ClangASTMetadata metadata;
  metadata.SetUserID(toOpaqueUid(id));
  metadata.SetIsDynamicCXXType(false);
  SymbolFileNativePDB *pdb = static_cast<SymbolFileNativePDB *>(
      m_clang.GetSymbolFile()->GetBackingSymbolFile());
  llvm::Expected<Declaration> declaration = pdb->ResolveUdtDeclaration(id);
  if (declaration)
    metadata.SetDeclaration(*declaration);
  else
    llvm::consumeError(declaration.takeError());

  TypeSystemClang::TemplateParameterInfos template_param_infos;
  CompilerType ct;
  clang::DeclContext *template_context = context;
  std::string template_uname = uname;
  if (FindLastTopLevelScopeSeparator(uname)) {
    if (clang::DeclContext *recovered_context =
            CreateNamespaceDeclContextForQualifiedName(*this, uname)) {
      template_context = recovered_context;
      template_uname = GetUnqualifiedTypeName(uname).str();
    }
  }

  if (!FindLastTopLevelScopeSeparator(template_uname) &&
      ParseTemplateParameterInfos(*this, record, id.index,
                                  template_param_infos)) {
    ct = CreateClassTemplateSpecializationType(
        *this, template_context, access, template_uname, ttk,
        template_param_infos, metadata, false);
  }

  if (!ct.IsValid() && !FindLastTopLevelScopeSeparator(template_uname)) {
    TypeSystemClang::TemplateParameterInfos name_template_param_infos;
    llvm::StringRef unqualified_record_name = GetUnqualifiedTypeName(record.Name);
    if (ParseTemplateParameterInfosFromName(*this, unqualified_record_name,
                                            id.index,
                                            name_template_param_infos))
      ct = CreateClassTemplateSpecializationType(
          *this, template_context, access, template_uname, ttk,
          name_template_param_infos, metadata, false);
  }

  if (!ct.IsValid()) {
    ct = m_clang.CreateRecordType(context, OptionalClangModuleID(), access,
                                  uname, llvm::to_underlying(ttk),
                                  lldb::eLanguageTypeC_plus_plus, metadata);
  }

  lldbassert(ct.IsValid());

  TypeSystemClang::StartTagDeclarationDefinition(ct);

  // Even if it's possible, don't complete it at this point. Just mark it
  // forward resolved, and if/when LLDB needs the full definition, it can
  // ask us.
  clang::QualType result =
      clang::QualType::getFromOpaquePtr(ct.GetOpaqueQualType());
  m_clang.SetMetadata(result.getTypePtr(), metadata);

  TypeSystemClang::SetHasExternalStorage(result.getAsOpaquePtr(), true);
  return result;
}

clang::Decl *PdbAstBuilder::TryGetDecl(PdbSymUid uid) const {
  auto iter = m_uid_to_decl.find(toOpaqueUid(uid));
  if (iter != m_uid_to_decl.end())
    return iter->second;
  return nullptr;
}

clang::NamespaceDecl *
PdbAstBuilder::GetOrCreateNamespaceDecl(const char *name,
                                        clang::DeclContext &context) {
  clang::NamespaceDecl *ns = m_clang.GetUniqueNamespaceDeclaration(
      IsAnonymousNamespaceName(name) ? nullptr : name, &context,
      OptionalClangModuleID());
  m_known_namespaces.insert(ns);
  m_parent_to_namespaces[&context].insert(ns);
  return ns;
}

clang::BlockDecl *
PdbAstBuilder::GetOrCreateBlockDecl(PdbCompilandSymId block_id) {
  if (clang::Decl *decl = TryGetDecl(block_id))
    return llvm::dyn_cast<clang::BlockDecl>(decl);

  clang::DeclContext *scope = GetParentClangDeclContext(block_id);

  clang::BlockDecl *block_decl =
      m_clang.CreateBlockDeclaration(scope, OptionalClangModuleID());
  m_uid_to_decl.insert({toOpaqueUid(block_id), block_decl});

  DeclStatus status;
  status.resolved = true;
  status.uid = toOpaqueUid(block_id);
  m_decl_to_status.insert({block_decl, status});

  return block_decl;
}

clang::VarDecl *PdbAstBuilder::CreateVariableDecl(PdbSymUid uid, CVSymbol sym,
                                                  clang::DeclContext &scope,
                                                  llvm::StringRef name) {
  VariableInfo var_info = GetVariableNameInfo(sym);
  clang::QualType qt = GetOrCreateClangType(var_info.type);
  if (qt.isNull())
    return nullptr;

  if (name.empty())
    name = var_info.name;

  clang::VarDecl *var_decl = m_clang.CreateVariableDeclaration(
      &scope, OptionalClangModuleID(), name.str().c_str(), qt);

  m_uid_to_decl[toOpaqueUid(uid)] = var_decl;
  DeclStatus status;
  status.resolved = true;
  status.uid = toOpaqueUid(uid);
  m_decl_to_status.insert({var_decl, status});
  return var_decl;
}

clang::VarDecl *
PdbAstBuilder::GetOrCreateVariableDecl(PdbCompilandSymId scope_id,
                                       PdbCompilandSymId var_id) {
  if (clang::Decl *decl = TryGetDecl(var_id))
    return llvm::dyn_cast<clang::VarDecl>(decl);

  clang::DeclContext *scope = GetOrCreateClangDeclContextForUid(scope_id);
  if (!scope)
    return nullptr;

  SymbolFileNativePDB *pdb = static_cast<SymbolFileNativePDB *>(
      m_clang.GetSymbolFile()->GetBackingSymbolFile());
  PdbIndex &index = pdb->GetIndex();
  CVSymbol sym = index.ReadSymbolRecord(var_id);
  return CreateVariableDecl(PdbSymUid(var_id), sym, *scope);
}

clang::VarDecl *PdbAstBuilder::GetOrCreateVariableDecl(PdbGlobalSymId var_id) {
  if (clang::Decl *decl = TryGetDecl(var_id))
    return llvm::dyn_cast<clang::VarDecl>(decl);

  SymbolFileNativePDB *pdb = static_cast<SymbolFileNativePDB *>(
      m_clang.GetSymbolFile()->GetBackingSymbolFile());
  PdbIndex &index = pdb->GetIndex();
  CVSymbol sym = index.ReadSymbolRecord(var_id);
  VariableInfo var_info = GetVariableNameInfo(sym);
  clang::DeclContext *context = nullptr;
  std::string name;
  std::tie(context, name) = CreateDeclInfoForUndecoratedName(var_info.name);
  if (!context)
    context = FromCompilerDeclContext(GetTranslationUnitDecl());
  return CreateVariableDecl(PdbSymUid(var_id), sym, *context, name);
}

CompilerType PdbAstBuilder::GetOrCreateTypedefType(PdbGlobalSymId id) {
  if (clang::Decl *decl = TryGetDecl(id)) {
    if (auto *tnd = llvm::dyn_cast<clang::TypedefNameDecl>(decl))
      return ToCompilerType(m_clang.getASTContext().getTypeDeclType(tnd));
    return CompilerType();
  }

  SymbolFileNativePDB *pdb = static_cast<SymbolFileNativePDB *>(
      m_clang.GetSymbolFile()->GetBackingSymbolFile());
  PdbIndex &index = pdb->GetIndex();
  CVSymbol sym = index.ReadSymbolRecord(id);
  lldbassert(sym.kind() == S_UDT);
  UDTSym udt = llvm::cantFail(SymbolDeserializer::deserializeAs<UDTSym>(sym));

  clang::DeclContext *scope = GetParentClangDeclContext(id);

  PdbTypeSymId real_type_id{udt.Type, false};
  clang::QualType qt = GetOrCreateClangType(real_type_id);
  if (qt.isNull() || !scope)
    return CompilerType();

  std::string uname = std::string(DropNameScope(udt.Name));

  CompilerType ct = ToCompilerType(qt).CreateTypedef(
      uname.c_str(), ToCompilerDeclContext(scope), 0);
  DeclStatus status;
  status.resolved = true;
  status.uid = toOpaqueUid(id);
  m_decl_to_status.insert({m_clang.GetAsTypedefDecl(ct), status});
  return ct;
}

clang::QualType PdbAstBuilder::GetBasicType(lldb::BasicType type) {
  CompilerType ct = m_clang.GetBasicType(type);
  return clang::QualType::getFromOpaquePtr(ct.GetOpaqueQualType());
}

clang::QualType PdbAstBuilder::CreateType(PdbTypeSymId type) {
  if (type.index.isSimple())
    return CreateSimpleType(type.index);

  SymbolFileNativePDB *pdb = static_cast<SymbolFileNativePDB *>(
      m_clang.GetSymbolFile()->GetBackingSymbolFile());
  PdbIndex &index = pdb->GetIndex();
  CVType cvt = index.tpi().getType(type.index);

  if (cvt.kind() == LF_MODIFIER) {
    ModifierRecord modifier;
    llvm::cantFail(
        TypeDeserializer::deserializeAs<ModifierRecord>(cvt, modifier));
    return CreateModifierType(modifier);
  }

  if (cvt.kind() == LF_POINTER) {
    PointerRecord pointer;
    llvm::cantFail(
        TypeDeserializer::deserializeAs<PointerRecord>(cvt, pointer));
    return CreatePointerType(pointer);
  }

  if (IsTagRecord(cvt)) {
    CVTagRecord tag = CVTagRecord::create(cvt);
    if (tag.kind() == CVTagRecord::Union)
      return CreateRecordType(type.index, tag.asUnion());
    if (tag.kind() == CVTagRecord::Enum)
      return CreateEnumType(type.index, tag.asEnum());
    return CreateRecordType(type.index, tag.asClass());
  }

  if (cvt.kind() == LF_ARRAY) {
    ArrayRecord ar;
    llvm::cantFail(TypeDeserializer::deserializeAs<ArrayRecord>(cvt, ar));
    return CreateArrayType(ar);
  }

  if (cvt.kind() == LF_PROCEDURE) {
    ProcedureRecord pr;
    llvm::cantFail(TypeDeserializer::deserializeAs<ProcedureRecord>(cvt, pr));
    return CreateFunctionType(pr.ArgumentList, pr.ReturnType, pr.CallConv);
  }

  if (cvt.kind() == LF_MFUNCTION) {
    MemberFunctionRecord mfr;
    llvm::cantFail(
        TypeDeserializer::deserializeAs<MemberFunctionRecord>(cvt, mfr));
    return CreateFunctionType(
        mfr.ArgumentList, mfr.ReturnType, mfr.CallConv,
        GetMemberFunctionTypeQualifiers(index, mfr.ThisType));
  }

  return {};
}

clang::QualType PdbAstBuilder::GetOrCreateClangType(PdbTypeSymId type) {
  if (type.index.isNoneType())
    return {};

  lldb::user_id_t uid = toOpaqueUid(type);
  auto iter = m_uid_to_type.find(uid);
  if (iter != m_uid_to_type.end())
    return iter->second;

  SymbolFileNativePDB *pdb = static_cast<SymbolFileNativePDB *>(
      m_clang.GetSymbolFile()->GetBackingSymbolFile());
  PdbIndex &index = pdb->GetIndex();
  PdbTypeSymId best_type = GetBestPossibleDecl(type, index.tpi());
  if (best_type.index == type.index && IsForwardRefUdt(type, index.tpi())) {
    CVTagRecord tag = CVTagRecord::create(index.tpi().getType(type.index));
    if (std::optional<PdbTypeSymId> complete_type =
            pdb->FindCompleteTypeByName(tag.name(), type.index))
      best_type = *complete_type;
  }

  clang::QualType qt;
  if (best_type.index != type.index) {
    // This is a forward decl.  Call GetOrCreate on the full decl, then map the
    // forward decl id to the full decl QualType.
    clang::QualType qt = GetOrCreateClangType(best_type);
    if (qt.isNull())
      return {};
    m_uid_to_type[toOpaqueUid(type)] = qt;
    return qt;
  }

  // This is either a full decl, or a forward decl with no matching full decl
  // in the debug info.
  qt = CreateType(type);
  if (qt.isNull())
    return {};

  m_uid_to_type[toOpaqueUid(type)] = qt;
  if (IsTagRecord(type, index.tpi())) {
    clang::TagDecl *tag = qt->getAsTagDecl();
    DeclStatus &status = m_decl_to_status[tag];
    if (status.uid == 0)
      status.uid = uid;
  }
  return qt;
}

CompilerType PdbAstBuilder::GetOrCreateType(PdbTypeSymId type) {
  clang::QualType qt = GetOrCreateClangType(type);
  if (qt.isNull())
    return {};
  return ToCompilerType(qt);
}

clang::FunctionDecl *
PdbAstBuilder::CreateFunctionDecl(PdbCompilandSymId func_id,
                                  llvm::StringRef func_name, TypeIndex func_ti,
                                  CompilerType func_ct, uint32_t param_count,
                                  clang::StorageClass func_storage,
                                  bool is_inline, clang::DeclContext *parent) {
  clang::FunctionDecl *function_decl = nullptr;
  if (parent->isRecord()) {
    SymbolFileNativePDB *pdb = static_cast<SymbolFileNativePDB *>(
        m_clang.GetSymbolFile()->GetBackingSymbolFile());
    PdbIndex &index = pdb->GetIndex();
    clang::CanQualType parent_qt =
        m_clang.getASTContext().getCanonicalTypeDeclType(
            llvm::cast<clang::TypeDecl>(parent));
    lldb::opaque_compiler_type_t parent_opaque_ty =
        ToCompilerType(parent_qt).GetOpaqueQualType();

    Declaration declaration;
    llvm::Expected<Declaration> declaration_or_err =
        pdb->ResolveFunctionDeclaration(func_id);
    if (declaration_or_err)
      declaration = *declaration_or_err;
    else
      llvm::consumeError(declaration_or_err.takeError());

    auto &methods = m_cxx_record_map[parent_opaque_ty];
    auto method_iter = methods.find({func_name, func_ct});
    if (method_iter != methods.end()) {
      ApplyDeclarationMetadata(m_clang, method_iter->second, declaration);
      return method_iter->second;
    }

    CVType cvt = index.tpi().getType(func_ti);
    MemberFunctionRecord func_record(static_cast<TypeRecordKind>(cvt.kind()));
    llvm::cantFail(TypeDeserializer::deserializeAs<MemberFunctionRecord>(
        cvt, func_record));
    TypeIndex class_index = func_record.getClassType();

    CVType parent_cvt = index.tpi().getType(class_index);
    TagRecord tag_record = CVTagRecord::create(parent_cvt).asTag();
    // If it's a forward reference, try to get the real TypeIndex.
    if (tag_record.isForwardRef()) {
      llvm::Expected<TypeIndex> eti =
          index.tpi().findFullDeclForForwardRef(class_index);
      if (eti) {
        tag_record = CVTagRecord::create(index.tpi().getType(*eti)).asTag();
      }
    }

    ConstString mangled_name(
        pdb->FindMangledFunctionName(func_id).value_or(llvm::StringRef()));

    if (!tag_record.FieldList.isSimple()) {
      CVType field_list_cvt = index.tpi().getType(tag_record.FieldList);
      FieldListRecord field_list;
      if (llvm::Error error = TypeDeserializer::deserializeAs<FieldListRecord>(
              field_list_cvt, field_list))
        llvm::consumeError(std::move(error));
      CreateMethodDecl process(index, m_clang, func_ti, function_decl,
                               parent_opaque_ty, func_name, mangled_name,
                               func_ct,
                               declaration.IsValid() ? &declaration : nullptr);
      if (llvm::Error err = visitMemberRecordStream(field_list.Data, process))
        llvm::consumeError(std::move(err));
    }

    if (!function_decl) {
      function_decl = m_clang.AddMethodToCXXRecordType(
          parent_opaque_ty, func_name, mangled_name, func_ct,
          /*access=*/lldb::AccessType::eAccessPublic,
          /*is_virtual=*/false, /*is_static=*/false,
          /*is_inline=*/false, /*is_explicit=*/false,
          /*is_attr_used=*/false, /*is_artificial=*/false,
          declaration.IsValid() ? &declaration : nullptr);
    }
    if (clang::CXXMethodDecl *method =
            llvm::dyn_cast_or_null<clang::CXXMethodDecl>(function_decl))
      methods.try_emplace({func_name, func_ct}, method);
  } else {
    function_decl = m_clang.CreateFunctionDeclaration(
        parent, OptionalClangModuleID(), func_name, func_ct, func_storage,
        is_inline, /*asm_label=*/{});
    CreateFunctionParameters(func_id, *function_decl, param_count);
  }
  return function_decl;
}

clang::FunctionDecl *
PdbAstBuilder::GetOrCreateInlinedFunctionDecl(PdbCompilandSymId inlinesite_id) {
  SymbolFileNativePDB *pdb = static_cast<SymbolFileNativePDB *>(
      m_clang.GetSymbolFile()->GetBackingSymbolFile());
  PdbIndex &index = pdb->GetIndex();
  CompilandIndexItem *cii =
      index.compilands().GetCompiland(inlinesite_id.modi);
  CVSymbol sym = cii->m_debug_stream.readSymbolAtOffset(inlinesite_id.offset);
  InlineSiteSym inline_site(static_cast<SymbolRecordKind>(sym.kind()));
  cantFail(SymbolDeserializer::deserializeAs<InlineSiteSym>(sym, inline_site));

  // Inlinee is the id index to the function id record that is inlined.
  PdbTypeSymId func_id(inline_site.Inlinee, true);
  // Look up the function decl by the id index to see if we have created a
  // function decl for a different inlinesite that refers the same function.
  if (clang::Decl *decl = TryGetDecl(func_id))
    return llvm::dyn_cast<clang::FunctionDecl>(decl);
  clang::FunctionDecl *function_decl =
      CreateFunctionDeclFromId(func_id, inlinesite_id);
  if (function_decl == nullptr)
    return nullptr;

  // Use inline site id in m_decl_to_status because it's expected to be a
  // PdbCompilandSymId so that we can parse local variables info after it.
  uint64_t inlinesite_uid = toOpaqueUid(inlinesite_id);
  DeclStatus status;
  status.resolved = true;
  status.uid = inlinesite_uid;
  m_decl_to_status.insert({function_decl, status});
  // Use the index in IPI stream as uid in m_uid_to_decl, because index in IPI
  // stream are unique and there could be multiple inline sites (different ids)
  // referring the same inline function. This avoid creating multiple same
  // inline function delcs.
  uint64_t func_uid = toOpaqueUid(func_id);
  lldbassert(m_uid_to_decl.count(func_uid) == 0);
  m_uid_to_decl[func_uid] = function_decl;
  return function_decl;
}

clang::FunctionDecl *
PdbAstBuilder::CreateFunctionDeclFromId(PdbTypeSymId func_tid,
                                        PdbCompilandSymId func_sid) {
  lldbassert(func_tid.is_ipi);
  SymbolFileNativePDB *pdb = static_cast<SymbolFileNativePDB *>(
      m_clang.GetSymbolFile()->GetBackingSymbolFile());
  PdbIndex &index = pdb->GetIndex();
  CVType func_cvt = index.ipi().getType(func_tid.index);
  llvm::StringRef func_name;
  TypeIndex func_ti;
  clang::DeclContext *parent = nullptr;
  switch (func_cvt.kind()) {
  case LF_MFUNC_ID: {
    MemberFuncIdRecord mfr;
    cantFail(
        TypeDeserializer::deserializeAs<MemberFuncIdRecord>(func_cvt, mfr));
    func_name = mfr.getName();
    func_ti = mfr.getFunctionType();
    PdbTypeSymId class_type_id(mfr.ClassType, false);
    parent = GetOrCreateClangDeclContextForUid(class_type_id);
    break;
  }
  case LF_FUNC_ID: {
    FuncIdRecord fir;
    cantFail(TypeDeserializer::deserializeAs<FuncIdRecord>(func_cvt, fir));
    func_name = fir.getName();
    func_ti = fir.getFunctionType();
    parent = FromCompilerDeclContext(GetTranslationUnitDecl());
    if (!fir.ParentScope.isNoneType()) {
      CVType parent_cvt = index.ipi().getType(fir.ParentScope);
      if (parent_cvt.kind() == LF_STRING_ID) {
        StringIdRecord sir;
        cantFail(
            TypeDeserializer::deserializeAs<StringIdRecord>(parent_cvt, sir));
        parent = GetOrCreateNamespaceDecl(sir.String.data(), *parent);
      }
    }
    break;
  }
  default:
    lldbassert(false && "Invalid function id type!");
  }
  clang::QualType func_qt = GetOrCreateClangType(func_ti);
  if (func_qt.isNull() || !parent)
    return nullptr;
  CompilerType func_ct = ToCompilerType(func_qt);
  uint32_t param_count =
      llvm::cast<clang::FunctionProtoType>(func_qt)->getNumParams();
  return CreateFunctionDecl(func_sid, func_name, func_ti, func_ct, param_count,
                            clang::SC_None, true, parent);
}

clang::FunctionDecl *
PdbAstBuilder::GetOrCreateFunctionDecl(PdbCompilandSymId func_id) {
  if (clang::Decl *decl = TryGetDecl(func_id))
    return llvm::dyn_cast<clang::FunctionDecl>(decl);

  clang::DeclContext *parent = GetParentClangDeclContext(PdbSymUid(func_id));
  if (!parent)
    return nullptr;
  std::string context_name;
  if (clang::NamespaceDecl *ns = llvm::dyn_cast<clang::NamespaceDecl>(parent)) {
    context_name = ns->getQualifiedNameAsString();
  } else if (clang::TagDecl *tag = llvm::dyn_cast<clang::TagDecl>(parent)) {
    clang::PrintingPolicy policy = m_clang.getASTContext().getPrintingPolicy();
    policy.MSVCFormatting = true;
    policy.SuppressTagKeyword = true;
    clang::QualType tag_type = m_clang.getASTContext().getTypeDeclType(
        clang::ElaboratedTypeKeyword::None, /*Qualifier=*/std::nullopt,
        static_cast<clang::TypeDecl *>(tag));
    context_name = clang::TypeName::getFullyQualifiedName(
        tag_type, m_clang.getASTContext(), policy);
  }

  SymbolFileNativePDB *pdb = static_cast<SymbolFileNativePDB *>(
      m_clang.GetSymbolFile()->GetBackingSymbolFile());
  PdbIndex &index = pdb->GetIndex();
  CVSymbol cvs = index.ReadSymbolRecord(func_id);
  ProcSym proc(static_cast<SymbolRecordKind>(cvs.kind()));
  llvm::cantFail(SymbolDeserializer::deserializeAs<ProcSym>(cvs, proc));

  PdbTypeSymId type_id(proc.FunctionType);
  clang::QualType qt = GetOrCreateClangType(type_id);
  if (qt.isNull())
    return nullptr;

  clang::StorageClass storage = clang::SC_None;
  if (proc.Kind == SymbolRecordKind::ProcSym)
    storage = clang::SC_Static;

  const clang::FunctionProtoType *func_type =
      llvm::dyn_cast<clang::FunctionProtoType>(qt);

  CompilerType func_ct = ToCompilerType(qt);

  llvm::StringRef proc_name = proc.Name;
  proc_name.consume_front(context_name);
  proc_name.consume_front("::");
  if (parent->isRecord())
    proc_name = MSVCUndecoratedNameParser::DropScope(proc.Name);
  clang::FunctionDecl *function_decl =
      CreateFunctionDecl(func_id, proc_name, proc.FunctionType, func_ct,
                         func_type->getNumParams(), storage, false, parent);
  if (function_decl == nullptr)
    return nullptr;

  lldbassert(m_uid_to_decl.count(toOpaqueUid(func_id)) == 0);
  m_uid_to_decl[toOpaqueUid(func_id)] = function_decl;
  DeclStatus status;
  status.resolved = true;
  status.uid = toOpaqueUid(func_id);
  m_decl_to_status.insert({function_decl, status});

  return function_decl;
}

void PdbAstBuilder::EnsureFunction(PdbCompilandSymId func_id) {
  GetOrCreateFunctionDecl(func_id);
}

void PdbAstBuilder::EnsureInlinedFunction(PdbCompilandSymId inlinesite_id) {
  GetOrCreateInlinedFunctionDecl(inlinesite_id);
}

void PdbAstBuilder::EnsureBlock(PdbCompilandSymId block_id) {
  GetOrCreateBlockDecl(block_id);
}

void PdbAstBuilder::EnsureVariable(PdbCompilandSymId scope_id,
                                   PdbCompilandSymId var_id) {
  GetOrCreateVariableDecl(scope_id, var_id);
}

void PdbAstBuilder::EnsureVariable(PdbGlobalSymId var_id) {
  GetOrCreateVariableDecl(var_id);
}

void PdbAstBuilder::CreateFunctionParameters(PdbCompilandSymId func_id,
                                             clang::FunctionDecl &function_decl,
                                             uint32_t param_count) {
  SymbolFileNativePDB *pdb = static_cast<SymbolFileNativePDB *>(
      m_clang.GetSymbolFile()->GetBackingSymbolFile());
  PdbIndex &index = pdb->GetIndex();
  CompilandIndexItem *cii = index.compilands().GetCompiland(func_id.modi);
  CVSymbolArray scope =
      cii->m_debug_stream.getSymbolArrayForScope(func_id.offset);

  scope.drop_front();
  auto begin = scope.begin();
  auto end = scope.end();
  std::vector<clang::ParmVarDecl *> params;
  for (uint32_t i = 0; i < param_count && begin != end;) {
    uint32_t record_offset = begin.offset();
    CVSymbol sym = *begin++;

    TypeIndex param_type;
    llvm::StringRef param_name;
    switch (sym.kind()) {
    case S_REGREL32: {
      RegRelativeSym reg(SymbolRecordKind::RegRelativeSym);
      cantFail(SymbolDeserializer::deserializeAs<RegRelativeSym>(sym, reg));
      param_type = reg.Type;
      param_name = reg.Name;
      break;
    }
    case S_REGISTER: {
      RegisterSym reg(SymbolRecordKind::RegisterSym);
      cantFail(SymbolDeserializer::deserializeAs<RegisterSym>(sym, reg));
      param_type = reg.Index;
      param_name = reg.Name;
      break;
    }
    case S_LOCAL: {
      LocalSym local(SymbolRecordKind::LocalSym);
      cantFail(SymbolDeserializer::deserializeAs<LocalSym>(sym, local));
      if ((local.Flags & LocalSymFlags::IsParameter) == LocalSymFlags::None)
        continue;
      param_type = local.Type;
      param_name = local.Name;
      break;
    }
    case S_BLOCK32:
    case S_INLINESITE:
    case S_INLINESITE2:
      // All parameters should come before the first block/inlinesite.  If that
      // isn't the case, then perhaps this is bad debug info that doesn't
      // contain information about all parameters.
      return;
    default:
      continue;
    }

    PdbCompilandSymId param_uid(func_id.modi, record_offset);
    clang::QualType qt = GetOrCreateClangType(param_type);
    if (qt.isNull())
      return;

    CompilerType param_type_ct = m_clang.GetType(qt);
    clang::ParmVarDecl *param = m_clang.CreateParameterDeclaration(
        &function_decl, OptionalClangModuleID(), param_name.str().c_str(),
        param_type_ct, clang::SC_None, true);
    lldbassert(m_uid_to_decl.count(toOpaqueUid(param_uid)) == 0);

    m_uid_to_decl[toOpaqueUid(param_uid)] = param;
    params.push_back(param);
    ++i;
  }

  if (!params.empty() && params.size() == param_count)
    function_decl.setParams(params);
}

clang::QualType PdbAstBuilder::CreateEnumType(PdbTypeSymId id,
                                              const EnumRecord &er) {
  clang::DeclContext *decl_context = nullptr;
  std::string uname;
  std::tie(decl_context, uname) = CreateDeclInfoForType(er, id.index);
  if (!decl_context)
    return {};

  clang::QualType underlying_type = GetOrCreateClangType(er.UnderlyingType);
  if (underlying_type.isNull())
    return {};

  Declaration declaration;
  CompilerType enum_ct = m_clang.CreateEnumerationType(
      uname, decl_context, OptionalClangModuleID(), declaration,
      ToCompilerType(underlying_type), er.isScoped());

  TypeSystemClang::StartTagDeclarationDefinition(enum_ct);
  TypeSystemClang::SetHasExternalStorage(enum_ct.GetOpaqueQualType(), true);

  return clang::QualType::getFromOpaquePtr(enum_ct.GetOpaqueQualType());
}

clang::QualType PdbAstBuilder::CreateArrayType(const ArrayRecord &ar) {
  clang::QualType element_type = GetOrCreateClangType(ar.ElementType);
  TypeSystemClang::RequireCompleteType(ToCompilerType(element_type));

  SymbolFileNativePDB *pdb = static_cast<SymbolFileNativePDB *>(
      m_clang.GetSymbolFile()->GetBackingSymbolFile());
  PdbIndex &index = pdb->GetIndex();
  uint64_t element_size = GetSizeOfType({ar.ElementType}, index.tpi());
  if (element_type.isNull() || element_size == 0)
    return {};
  uint64_t element_count = ar.Size / element_size;

  CompilerType array_ct = m_clang.CreateArrayType(ToCompilerType(element_type),
                                                  element_count, false);
  return clang::QualType::getFromOpaquePtr(array_ct.GetOpaqueQualType());
}

clang::QualType PdbAstBuilder::CreateFunctionType(
    TypeIndex args_type_idx, TypeIndex return_type_idx,
    llvm::codeview::CallingConvention calling_convention, unsigned type_quals) {
  SymbolFileNativePDB *pdb = static_cast<SymbolFileNativePDB *>(
      m_clang.GetSymbolFile()->GetBackingSymbolFile());
  PdbIndex &index = pdb->GetIndex();
  TpiStream &stream = index.tpi();
  CVType args_cvt = stream.getType(args_type_idx);
  ArgListRecord args;
  llvm::cantFail(
      TypeDeserializer::deserializeAs<ArgListRecord>(args_cvt, args));

  llvm::ArrayRef<TypeIndex> arg_indices = llvm::ArrayRef(args.ArgIndices);
  bool is_variadic = IsCVarArgsFunction(arg_indices);
  if (is_variadic)
    arg_indices = arg_indices.drop_back();

  std::vector<CompilerType> arg_types;
  arg_types.reserve(arg_indices.size());

  for (TypeIndex arg_index : arg_indices) {
    clang::QualType arg_type = GetOrCreateClangType(arg_index);
    if (arg_type.isNull())
      continue;
    arg_types.push_back(ToCompilerType(arg_type));
  }

  clang::QualType return_type = GetOrCreateClangType(return_type_idx);
  if (return_type.isNull())
    return {};

  std::optional<clang::CallingConv> cc =
      TranslateCallingConvention(calling_convention);
  if (!cc)
    return {};

  CompilerType return_ct = ToCompilerType(return_type);
  CompilerType func_sig_ast_type =
      m_clang.CreateFunctionType(return_ct, arg_types, is_variadic, type_quals,
                                 *cc);

  return clang::QualType::getFromOpaquePtr(
      func_sig_ast_type.GetOpaqueQualType());
}

static bool isTagDecl(clang::DeclContext &context) {
  return llvm::isa<clang::TagDecl>(&context);
}

static bool isFunctionDecl(clang::DeclContext &context) {
  return llvm::isa<clang::FunctionDecl>(&context);
}

static bool isBlockDecl(clang::DeclContext &context) {
  return llvm::isa<clang::BlockDecl>(&context);
}

void PdbAstBuilder::ParseNamespace(clang::DeclContext &context) {
  clang::NamespaceDecl *ns = llvm::dyn_cast<clang::NamespaceDecl>(&context);
  if (m_parsed_namespaces.contains(ns))
    return;
  std::string qname = ns->getQualifiedNameAsString();
  SymbolFileNativePDB *pdb = static_cast<SymbolFileNativePDB *>(
      m_clang.GetSymbolFile()->GetBackingSymbolFile());
  PdbIndex &index = pdb->GetIndex();
  TypeIndex ti{index.tpi().TypeIndexBegin()};
  for (const CVType &cvt : index.tpi().typeArray()) {
    PdbTypeSymId tid{ti};
    ++ti;

    if (!IsTagRecord(cvt))
      continue;

    CVTagRecord tag = CVTagRecord::create(cvt);

    // Call CreateDeclInfoForType unconditionally so that the namespace info
    // gets created.  But only call CreateRecordType if the namespace name
    // matches.
    clang::DeclContext *context = nullptr;
    std::string uname;
    std::tie(context, uname) = CreateDeclInfoForType(tag.asTag(), tid.index);
    if (!context || !context->isNamespace())
      continue;

    clang::NamespaceDecl *ns = llvm::cast<clang::NamespaceDecl>(context);
    llvm::StringRef ns_name = ns->getName();
    if (ns_name.starts_with(qname)) {
      ns_name = ns_name.drop_front(qname.size());
      if (ns_name.starts_with("::"))
        GetOrCreateClangType(tid);
    }
  }
  ParseAllFunctionsAndNonLocalVars();
  m_parsed_namespaces.insert(ns);
}

void PdbAstBuilder::ParseAllTypes() {
  llvm::call_once(m_parse_all_types, [this]() {
    SymbolFileNativePDB *pdb = static_cast<SymbolFileNativePDB *>(
        m_clang.GetSymbolFile()->GetBackingSymbolFile());
    PdbIndex &index = pdb->GetIndex();
    TypeIndex ti{index.tpi().TypeIndexBegin()};
    for (const CVType &cvt : index.tpi().typeArray()) {
      PdbTypeSymId tid{ti};
      ++ti;

      if (!IsTagRecord(cvt))
        continue;

      GetOrCreateClangType(tid);
    }
  });
}

void PdbAstBuilder::ParseAllFunctionsAndNonLocalVars() {
  llvm::call_once(m_parse_functions_and_non_local_vars, [this]() {
    SymbolFileNativePDB *pdb = static_cast<SymbolFileNativePDB *>(
        m_clang.GetSymbolFile()->GetBackingSymbolFile());
    PdbIndex &index = pdb->GetIndex();
    uint32_t module_count = index.dbi().modules().getModuleCount();
    for (uint16_t modi = 0; modi < module_count; ++modi) {
      CompilandIndexItem &cii = index.compilands().GetOrCreateCompiland(modi);
      const CVSymbolArray &symbols = cii.m_debug_stream.getSymbolArray();
      auto iter = symbols.begin();
      while (iter != symbols.end()) {
        PdbCompilandSymId sym_id{modi, iter.offset()};

        switch (iter->kind()) {
        case S_GPROC32:
        case S_LPROC32:
          GetOrCreateFunctionDecl(sym_id);
          iter = symbols.at(getScopeEndOffset(*iter));
          break;
        case S_GDATA32:
        case S_GTHREAD32:
        case S_LDATA32:
        case S_LTHREAD32:
          GetOrCreateVariableDecl(PdbCompilandSymId(modi, 0), sym_id);
          ++iter;
          break;
        default:
          ++iter;
          continue;
        }
      }
    }
  });
}

static CVSymbolArray skipFunctionParameters(clang::Decl &decl,
                                            const CVSymbolArray &symbols) {
  clang::FunctionDecl *func_decl = llvm::dyn_cast<clang::FunctionDecl>(&decl);
  if (!func_decl)
    return symbols;
  unsigned int params = func_decl->getNumParams();
  if (params == 0)
    return symbols;

  CVSymbolArray result = symbols;

  while (!result.empty()) {
    if (params == 0)
      return result;

    CVSymbol sym = *result.begin();
    result.drop_front();

    if (!isLocalVariableType(sym.kind()))
      continue;

    --params;
  }
  return result;
}

void PdbAstBuilder::ParseBlockChildren(PdbCompilandSymId block_id) {
  SymbolFileNativePDB *pdb = static_cast<SymbolFileNativePDB *>(
      m_clang.GetSymbolFile()->GetBackingSymbolFile());
  PdbIndex &index = pdb->GetIndex();
  CVSymbol sym = index.ReadSymbolRecord(block_id);
  lldbassert(sym.kind() == S_GPROC32 || sym.kind() == S_LPROC32 ||
             sym.kind() == S_BLOCK32 || sym.kind() == S_INLINESITE);
  CompilandIndexItem &cii =
      index.compilands().GetOrCreateCompiland(block_id.modi);
  CVSymbolArray symbols =
      cii.m_debug_stream.getSymbolArrayForScope(block_id.offset);

  // Function parameters should already have been created when the function was
  // parsed.
  if (sym.kind() == S_GPROC32 || sym.kind() == S_LPROC32)
    symbols =
        skipFunctionParameters(*m_uid_to_decl[toOpaqueUid(block_id)], symbols);

  symbols.drop_front();
  auto begin = symbols.begin();
  while (begin != symbols.end()) {
    PdbCompilandSymId child_sym_id(block_id.modi, begin.offset());
    GetOrCreateSymbolForId(child_sym_id);
    if (begin->kind() == S_BLOCK32 || begin->kind() == S_INLINESITE) {
      ParseBlockChildren(child_sym_id);
      begin = symbols.at(getScopeEndOffset(*begin));
    }
    ++begin;
  }
}

void PdbAstBuilder::ParseDeclsForSimpleContext(clang::DeclContext &context) {

  clang::Decl *decl = clang::Decl::castFromDeclContext(&context);
  lldbassert(decl);

  auto iter = m_decl_to_status.find(decl);
  lldbassert(iter != m_decl_to_status.end());

  if (auto *tag = llvm::dyn_cast<clang::TagDecl>(&context)) {
    CompleteTagDecl(*tag);
    return;
  }

  if (isFunctionDecl(context) || isBlockDecl(context)) {
    PdbCompilandSymId block_id = PdbSymUid(iter->second.uid).asCompilandSym();
    ParseBlockChildren(block_id);
  }
}

void PdbAstBuilder::ParseDeclsForContext(CompilerDeclContext context) {
  clang::DeclContext *dc = FromCompilerDeclContext(context);
  if (!dc)
    return;

  // Namespaces aren't explicitly represented in the debug info, and the only
  // way to parse them is to parse all type info, demangling every single type
  // and trying to reconstruct the DeclContext hierarchy this way.  Since this
  // is an expensive operation, we have to special case it so that we do other
  // work (such as parsing the items that appear within the namespaces) at the
  // same time.
  if (dc->isTranslationUnit()) {
    ParseAllTypes();
    ParseAllFunctionsAndNonLocalVars();
    return;
  }

  if (dc->isNamespace()) {
    ParseNamespace(*dc);
    return;
  }

  if (isTagDecl(*dc) || isFunctionDecl(*dc) || isBlockDecl(*dc)) {
    ParseDeclsForSimpleContext(*dc);
    return;
  }
}

CompilerDecl PdbAstBuilder::ToCompilerDecl(clang::Decl *decl) {
  return m_clang.GetCompilerDecl(decl);
}

CompilerType PdbAstBuilder::ToCompilerType(clang::QualType qt) {
  return m_clang.GetType(qt);
}

clang::QualType PdbAstBuilder::FromCompilerType(CompilerType ct) {
  return ClangUtil::GetQualType(ct);
}

CompilerDeclContext
PdbAstBuilder::ToCompilerDeclContext(clang::DeclContext *context) {
  return m_clang.CreateDeclContext(context);
}

clang::Decl * PdbAstBuilder::FromCompilerDecl(CompilerDecl decl) {
  return ClangUtil::GetDecl(decl);
}

clang::DeclContext *
PdbAstBuilder::FromCompilerDeclContext(CompilerDeclContext context) {
  return static_cast<clang::DeclContext *>(context.GetOpaqueDeclContext());
}

void PdbAstBuilder::Dump(Stream &stream, llvm::StringRef filter,
                         bool show_color) {
  m_clang.Dump(stream.AsRawOstream(), filter, show_color);
}

CompilerDeclContext
PdbAstBuilder::FindNamespaceDecl(CompilerDeclContext parent_ctx,
                                 llvm::StringRef name) {
  clang::DeclContext *parent = FromCompilerDeclContext(parent_ctx);
  NamespaceSet *set;

  if (parent) {
    auto it = m_parent_to_namespaces.find(parent);
    if (it == m_parent_to_namespaces.end())
      return {};

    set = &it->second;
  } else {
    // In this case, search through all known namespaces
    set = &m_known_namespaces;
  }
  assert(set);

  for (clang::NamespaceDecl *namespace_decl : *set)
    if (namespace_decl->getName() == name)
      return ToCompilerDeclContext(namespace_decl);

  for (clang::NamespaceDecl *namespace_decl : *set)
    if (namespace_decl->isAnonymousNamespace())
      return FindNamespaceDecl(ToCompilerDeclContext(namespace_decl), name);

  return {};
}
