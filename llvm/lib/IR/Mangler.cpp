//===-- Mangler.cpp - Self-contained c/asm llvm name mangler --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Unified name mangler for assembly backends.
//
//===----------------------------------------------------------------------===//

#include "llvm/IR/Mangler.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Demangle/Demangle.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/Module.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/TargetParser/Triple.h"

using namespace llvm;

namespace {
enum ManglerPrefixTy {
  Default,      ///< Emit default string before each symbol.
  Private,      ///< Emit "private" prefix before each symbol.
  LinkerPrivate ///< Emit "linker private" prefix before each symbol.
};
}

static void getNameWithPrefixImpl(raw_ostream &OS, const Twine &GVName,
                                  ManglerPrefixTy PrefixTy,
                                  const DataLayout &DL, char Prefix) {
  SmallString<256> TmpData;
  StringRef Name = GVName.toStringRef(TmpData);
  assert(!Name.empty() && "getNameWithPrefix requires non-empty name");

  // No need to do anything special if the global has the special "do not
  // mangle" flag in the name.
  if (Name[0] == '\1') {
    OS << Name.substr(1);
    return;
  }

  if (DL.doNotMangleLeadingQuestionMark() && Name[0] == '?')
    Prefix = '\0';

  if (PrefixTy == Private)
    OS << DL.getInternalSymbolPrefix();
  else if (PrefixTy == LinkerPrivate)
    OS << DL.getLinkerPrivateGlobalPrefix();

  if (Prefix != '\0')
    OS << Prefix;

  // If this is a simple string that doesn't need escaping, just append it.
  OS << Name;
}

static void getNameWithPrefixImpl(raw_ostream &OS, const Twine &GVName,
                                  const DataLayout &DL,
                                  ManglerPrefixTy PrefixTy) {
  char Prefix = DL.getGlobalPrefix();
  return getNameWithPrefixImpl(OS, GVName, PrefixTy, DL, Prefix);
}

void Mangler::getNameWithPrefix(raw_ostream &OS, const Twine &GVName,
                                const DataLayout &DL) {
  return getNameWithPrefixImpl(OS, GVName, DL, Default);
}

void Mangler::getNameWithPrefix(SmallVectorImpl<char> &OutName,
                                const Twine &GVName, const DataLayout &DL) {
  raw_svector_ostream OS(OutName);
  char Prefix = DL.getGlobalPrefix();
  return getNameWithPrefixImpl(OS, GVName, Default, DL, Prefix);
}

static bool hasByteCountSuffix(CallingConv::ID CC) {
  switch (CC) {
  case CallingConv::X86_FastCall:
  case CallingConv::X86_StdCall:
  case CallingConv::X86_VectorCall:
    return true;
  default:
    return false;
  }
}

/// Microsoft fastcall and stdcall functions require a suffix on their name
/// indicating the number of words of arguments they take.
static void addByteCountSuffix(raw_ostream &OS, const Function *F,
                               const DataLayout &DL) {
  // Calculate arguments size total.
  unsigned ArgWords = 0;

  const unsigned PtrSize = DL.getPointerSize();

  for (const Argument &A : F->args()) {
    // For the purposes of the byte count suffix, structs returned by pointer
    // do not count as function arguments.
    if (A.hasStructRetAttr())
      continue;

    // 'Dereference' type in case of byval or inalloca parameter attribute.
    uint64_t AllocSize = A.hasPassPointeeByValueCopyAttr() ?
      A.getPassPointeeByValueCopySize(DL) :
      DL.getTypeAllocSize(A.getType());

    // Size should be aligned to pointer size.
    ArgWords += alignTo(AllocSize, PtrSize);
  }

  OS << '@' << ArgWords;
}

void Mangler::getNameWithPrefix(raw_ostream &OS, const GlobalValue *GV,
                                bool CannotUsePrivateLabel) const {
  ManglerPrefixTy PrefixTy = Default;
  assert(GV != nullptr && "Invalid Global Value");
  if (GV->hasPrivateLinkage()) {
    if (CannotUsePrivateLabel)
      PrefixTy = LinkerPrivate;
    else
      PrefixTy = Private;
  }

  const DataLayout &DL = GV->getDataLayout();
  if (!GV->hasName()) {
    // Get the ID for the global, assigning a new one if we haven't got one
    // already.
    unsigned &ID = AnonGlobalIDs[GV];
    if (ID == 0)
      ID = AnonGlobalIDs.size();

    // Must mangle the global into a unique ID.
    getNameWithPrefixImpl(OS, "__unnamed_" + Twine(ID), DL, PrefixTy);
    return;
  }

  StringRef Name = GV->getName();
  char Prefix = DL.getGlobalPrefix();

  // Mangle functions with Microsoft calling conventions specially.  Only do
  // this mangling for x86_64 vectorcall and 32-bit x86.
  const Function *MSFunc = dyn_cast_or_null<Function>(GV->getAliaseeObject());

  // Don't add byte count suffixes when '\01' or '?' are in the first
  // character.
  if (Name.starts_with("\01") ||
      (DL.doNotMangleLeadingQuestionMark() && Name.starts_with("?")))
    MSFunc = nullptr;

  CallingConv::ID CC =
      MSFunc ? MSFunc->getCallingConv() : (unsigned)CallingConv::C;
  if (!DL.hasMicrosoftFastStdCallMangling() &&
      CC != CallingConv::X86_VectorCall)
    MSFunc = nullptr;
  if (MSFunc) {
    if (CC == CallingConv::X86_FastCall)
      Prefix = '@'; // fastcall functions have an @ prefix instead of _.
    else if (CC == CallingConv::X86_VectorCall)
      Prefix = '\0'; // vectorcall functions have no prefix.
  }

  getNameWithPrefixImpl(OS, Name, PrefixTy, DL, Prefix);

  if (!MSFunc)
    return;

  // If we are supposed to add a microsoft-style suffix for stdcall, fastcall,
  // or vectorcall, add it.  These functions have a suffix of @N where N is the
  // cumulative byte size of all of the parameters to the function in decimal.
  if (CC == CallingConv::X86_VectorCall)
    OS << '@'; // vectorcall functions use a double @ suffix.
  FunctionType *FT = MSFunc->getFunctionType();
  if (hasByteCountSuffix(CC) &&
      // "Pure" variadic functions do not receive @0 suffix.
      (!FT->isVarArg() || FT->getNumParams() == 0 ||
       (FT->getNumParams() == 1 && MSFunc->hasStructRetAttr())))
    addByteCountSuffix(OS, MSFunc, DL);
}

void Mangler::getNameWithPrefix(SmallVectorImpl<char> &OutName,
                                const GlobalValue *GV,
                                bool CannotUsePrivateLabel) const {
  raw_svector_ostream OS(OutName);
  getNameWithPrefix(OS, GV, CannotUsePrivateLabel);
}

// Check if the name needs quotes to be safe for the linker to interpret.
static bool canBeUnquotedInDirective(char C) {
  return isAlnum(C) || C == '_' || C == '@' || C == '#';
}

static bool canBeUnquotedInDirective(StringRef Name) {
  if (Name.empty())
    return false;

  // If any of the characters in the string is an unacceptable character, force
  // quotes.
  for (char C : Name) {
    if (!canBeUnquotedInDirective(C))
      return false;
  }

  return true;
}

/// Whether the backend emits the marker of -fmalterlib-sized-destructors for
/// \p GV: a definition that carries the attribute, or an alias its aliasee
/// lists.
static bool hasMalterlibSizedMarker(const GlobalValue &GV) {
  auto Attr = [](const GlobalObject &GO, StringRef Kind) {
    if (const auto *F = dyn_cast<Function>(&GO))
      return F->getFnAttribute(Kind);
    if (const auto *V = dyn_cast<GlobalVariable>(&GO))
      return V->getAttribute(Kind);
    return Attribute();
  };
  if (const auto *GO = dyn_cast<GlobalObject>(&GV))
    return Attr(*GO, MalterlibSizedMarkerAttr).isValid();
  const auto *GA = dyn_cast<GlobalAlias>(&GV);
  const GlobalObject *GO = GA ? GA->getAliaseeObject() : nullptr;
  if (!GO)
    return false;
  Attribute A = Attr(*GO, MalterlibSizedAliasesAttr);
  if (!A.isStringAttribute())
    return false;
  SmallVector<StringRef, 2> Aliases;
  A.getValueAsString().split(Aliases, ',', /*MaxSplit=*/-1,
                             /*KeepEmpty=*/false);
  return llvm::any_of(Aliases, [&](StringRef Alias) {
    return Alias.rsplit('=').first == GA->getName();
  });
}

void llvm::getMalterlibSizedMarkerName(SmallVectorImpl<char> &Out,
                                       const GlobalValue *GV, StringRef Name) {
  SmallString<128> Symbol;
  Mangler().getNameWithPrefix(Symbol, GV, /*CannotUsePrivateLabel=*/false);
  if (Name.empty() || Name == GV->getName()) {
    Out.append(Symbol.begin(), Symbol.end());
  } else {
    // An alias of a function is decorated as the function is, around its own
    // name; that of a variable, which may have no name, is not decorated.
    SmallString<128> Plain;
    if (isa<Function>(GV) && GV->hasName())
      Mangler::getNameWithPrefix(Plain, GV->getName(), GV->getDataLayout());
    size_t Core =
        Plain.empty() ? StringRef::npos : Symbol.str().find(GV->getName());
    if (Plain.empty() || Symbol == Plain ||
        GV->getName().starts_with("\01") || Core == StringRef::npos) {
      Mangler::getNameWithPrefix(Out, Name, GV->getDataLayout());
    } else {
      Out.append(Symbol.begin(), Symbol.begin() + Core);
      Out.append(Name.begin(), Name.end());
      Out.append(Symbol.begin() + Core + GV->getName().size(), Symbol.end());
    }
  }
  Out.append(MalterlibSizedMarkerSuffix.begin(),
             MalterlibSizedMarkerSuffix.end());
}

std::string llvm::getMalterlibSizedMarkerIRName(const GlobalValue *GV,
                                               StringRef Name) {
  if (Name.empty())
    Name = GV->getName();
  std::string IRName = (Name + MalterlibSizedMarkerSuffix).str();
  SmallString<128> Symbol, Plain;
  getMalterlibSizedMarkerName(Symbol, GV, Name);
  Mangler::getNameWithPrefix(Plain, IRName, GV->getDataLayout());
  if (Symbol != Plain)
    return ("\01" + Symbol).str();
  return IRName;
}

void llvm::emitLinkerFlagsForGlobalCOFF(raw_ostream &OS, const GlobalValue *GV,
                                        const Triple &TT, Mangler &Mangler) {
  if (GV->hasDLLExportStorageClass() && !GV->isDeclaration()) {

    if (TT.isWindowsMSVCEnvironment() || TT.isUEFI())
      OS << " /EXPORT:";
    else
      OS << " -export:";

    bool NeedQuotes = GV->hasName() && !canBeUnquotedInDirective(GV->getName());
    if (NeedQuotes)
      OS << "\"";
    if (TT.isWindowsGNUEnvironment() || TT.isWindowsCygwinEnvironment()) {
      std::string Flag;
      raw_string_ostream FlagOS(Flag);
      Mangler.getNameWithPrefix(FlagOS, GV, false);
      if (Flag[0] == GV->getDataLayout().getGlobalPrefix())
        OS << Flag.substr(1);
      else
        OS << Flag;
    } else {
      Mangler.getNameWithPrefix(OS, GV, false);
    }
    if (TT.isWindowsArm64EC()) {
      // Use EXPORTAS for mangled ARM64EC symbols.
      // FIXME: During LTO, we're invoked prior to the EC lowering pass,
      // so symbols are not yet mangled. Emitting the unmangled name
      // typically functions correctly; the linker can resolve the export
      // with the demangled alias.
      if (std::optional<std::string> demangledName =
              getArm64ECDemangledFunctionName(GV->getName()))
        OS << ",EXPORTAS," << *demangledName;
    }
    if (NeedQuotes)
      OS << "\"";

    if (!GV->getValueType()->isFunctionTy()) {
      if (TT.isWindowsMSVCEnvironment() || TT.isUEFI())
        OS << ",DATA";
      else
        OS << ",data";
    }

    // The marker of -fmalterlib-sized-destructors of an exported definition,
    // which exists only in the object file, is exported with it, as code, so
    // that a reference to it resolves to its import thunk whatever it marks.
    if (hasMalterlibSizedMarker(*GV)) {
      SmallString<128> Marker;
      getMalterlibSizedMarkerName(Marker, GV);
      StringRef Name = Marker;
      if ((TT.isWindowsGNUEnvironment() || TT.isWindowsCygwinEnvironment()) &&
          Name.front() == GV->getDataLayout().getGlobalPrefix())
        Name = Name.drop_front();
      OS << ((TT.isWindowsMSVCEnvironment() || TT.isUEFI()) ? " /EXPORT:\""
                                                             : " -export:\"")
         << Name << "\"";
    }
  }
  if (GV->hasHiddenVisibility() && !GV->isDeclaration() && TT.isOSCygMing()) {

    OS << " -exclude-symbols:";

    bool NeedQuotes = GV->hasName() && !canBeUnquotedInDirective(GV->getName());
    if (NeedQuotes)
      OS << "\"";

    std::string Flag;
    raw_string_ostream FlagOS(Flag);
    Mangler.getNameWithPrefix(FlagOS, GV, false);
    if (Flag[0] == GV->getDataLayout().getGlobalPrefix())
      OS << Flag.substr(1);
    else
      OS << Flag;

    if (NeedQuotes)
      OS << "\"";
  }
}

void llvm::emitLinkerFlagsForUsedCOFF(raw_ostream &OS, const GlobalValue *GV,
                                      const Triple &T, Mangler &M) {
  if (!T.isWindowsMSVCEnvironment())
    return;

  OS << " /INCLUDE:";
  bool NeedQuotes = GV->hasName() && !canBeUnquotedInDirective(GV->getName());
  if (NeedQuotes)
    OS << "\"";
  M.getNameWithPrefix(OS, GV, false);
  if (NeedQuotes)
    OS << "\"";
}

std::optional<std::string> llvm::getArm64ECMangledFunctionName(StringRef Name) {
  assert(!Name.empty() &&
         "getArm64ECMangledFunctionName requires non-empty name");

  if (Name[0] != '?') {
    // For non-C++ symbols, prefix the name with "#" unless it's already
    // mangled.
    if (Name[0] == '#')
      return std::nullopt;
    return std::optional<std::string>(("#" + Name).str());
  }

  // If the name contains $$h, then it is already mangled.
  if (Name.contains("$$h"))
    return std::nullopt;

  // Handle MD5 mangled names, which use a slightly different rule from
  // other C++ manglings.
  //
  // A non-Arm64EC function:
  //
  // ??@aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa@
  //
  // An Arm64EC function:
  //
  // ??@aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa@$$h@
  if (Name.starts_with("??@") && Name.ends_with("@"))
    return (Name + "$$h@").str();

  // Ask the demangler where we should insert "$$h".
  auto InsertIdx = getArm64ECInsertionPointInMangledName(Name);
  if (!InsertIdx)
    return std::nullopt;

  return std::optional<std::string>(
      (Name.substr(0, *InsertIdx) + "$$h" + Name.substr(*InsertIdx)).str());
}

std::optional<std::string>
llvm::getArm64ECDemangledFunctionName(StringRef Name) {
  // For non-C++ names, drop the "#" prefix.
  if (Name[0] == '#')
    return std::optional<std::string>(Name.substr(1));
  if (Name[0] != '?')
    return std::nullopt;

  // MD5 mangled name; see comment in getArm64ECMangledFunctionName.
  if (Name.starts_with("??@") && Name.ends_with("@$$h@"))
    return Name.drop_back(4).str();

  // Drop the ARM64EC "$$h" tag.
  std::pair<StringRef, StringRef> Pair = Name.split("$$h");
  if (Pair.second.empty())
    return std::nullopt;
  return std::optional<std::string>((Pair.first + Pair.second).str());
}
