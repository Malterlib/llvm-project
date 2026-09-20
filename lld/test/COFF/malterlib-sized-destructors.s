# REQUIRES: x86
## A definition compiled with -fmalterlib-sized-destructors carries a marker
## symbol at its address, which the construction sites that depend on it refer
## to. A definition compiled without the flag has no marker, and the reference
## stays undefined.

# RUN: rm -rf %t; split-file %s %t
# RUN: llvm-mc -filetype=obj -triple=x86_64-windows-msvc %t/main.s -o %t/main.obj
# RUN: llvm-mc -filetype=obj -triple=x86_64-windows-msvc %t/flagged.s -o %t/flagged.obj
# RUN: llvm-mc -filetype=obj -triple=x86_64-windows-msvc %t/unflagged.s -o %t/unflagged.obj

# RUN: lld-link %t/main.obj %t/flagged.obj /out:%t/flagged.exe /entry:main /subsystem:console
# RUN: env LLD_IN_TEST=1 not lld-link %t/main.obj %t/unflagged.obj /out:%t/unflagged.exe \
# RUN:   /entry:main /subsystem:console 2>&1 | FileCheck %s
# CHECK:      error: public: virtual void * __cdecl Foreign::`vector deleting dtor'(unsigned int) was not compiled with -fmalterlib-sized-destructors
# CHECK-NEXT: >>> its marker ??_EForeign@@UEAAPEAXI@Z.mib_sized is undefined
# CHECK-NEXT: >>> an object a construction site makes is destroyed with the size its deleting destructor returns

## The references are in data that dead code stripping removes, and an
## unresolved marker is an error whatever /force says.
# RUN: env LLD_IN_TEST=1 not lld-link /opt:ref /force:unresolved %t/main.obj %t/unflagged.obj \
# RUN:   /out:%t/forced.exe /entry:main /subsystem:console 2>&1 | FileCheck %s

## An inline definition is checked in the copy the link keeps. The marker is
## an external symbol of the definition's COMDAT section, which is dropped with
## a copy that loses, and a copy from code without the flag has none.
# RUN: llvm-mc -filetype=obj -triple=x86_64-windows-msvc %t/inline-flagged.s -o %t/inline-flagged.obj
# RUN: llvm-mc -filetype=obj -triple=x86_64-windows-msvc %t/inline-unflagged.s -o %t/inline-unflagged.obj
# RUN: lld-link %t/main.obj %t/inline-flagged.obj %t/inline-unflagged.obj /out:%t/inline.exe \
# RUN:   /entry:main /subsystem:console
# RUN: env LLD_IN_TEST=1 not lld-link %t/main.obj %t/inline-unflagged.obj %t/inline-flagged.obj \
# RUN:   /out:%t/inline.exe /entry:main /subsystem:console 2>&1 | FileCheck %s
## A marker nothing refers to is left alone: the class is not constructed
## through the builtin.
# RUN: llvm-mc -filetype=obj -triple=x86_64-windows-msvc %t/plain.s -o %t/plain.obj
# RUN: lld-link %t/plain.obj %t/inline-unflagged.obj %t/inline-flagged.obj /out:%t/plain.exe \
# RUN:   /entry:main /subsystem:console

## A site and an inline definition in one object refer to the marker the
## assembler resolved within the object, and the link may keep another copy.
# RUN: llvm-mc -filetype=obj -triple=x86_64-windows-msvc %t/site-inline.s -o %t/site-inline.obj
# RUN: lld-link /opt:ref %t/site-inline.obj %t/inline-unflagged.obj /out:%t/site.exe /entry:main /subsystem:console
# RUN: env LLD_IN_TEST=1 not lld-link /opt:ref %t/inline-unflagged.obj %t/site-inline.obj /out:%t/site.exe \
# RUN:   /entry:main /subsystem:console 2>&1 | FileCheck %s

## A construction site that imports what it depends on from a DLL through
## `__imp_` names the definition only in its record, which loads no member of
## an archive that defines it too, as the site without the flag loads none,
## whatever order they come in.
# RUN: llvm-mc -filetype=obj -triple=x86_64-windows-msvc %t/import-def.s -o %t/import-def.obj
# RUN: llvm-mc -filetype=obj -triple=x86_64-windows-msvc %t/import-site.s -o %t/import-site.obj
# RUN: lld-link /dll /noentry %t/import-def.obj /out:%t/import-flagged.dll /implib:%t/import-flagged.lib \
# RUN:   /export:foo
# RUN: llvm-lib /out:%t/import-static.lib %t/import-def.obj
# RUN: lld-link %t/import-site.obj %t/import-static.lib %t/import-flagged.lib /out:%t/import.exe \
# RUN:   /entry:main /subsystem:console
# RUN: lld-link %t/import-site.obj %t/import-flagged.lib %t/import-static.lib /out:%t/import.exe \
# RUN:   /entry:main /subsystem:console
## Also one whose construction site inlined what it depends on, which only its
## record names: the import of it that a later import library has binds it,
## not the member of the archive before it, which isn't loaded.
# RUN: llvm-mc -filetype=obj -triple=x86_64-windows-msvc %t/import-record-only.s -o %t/import-record-only.obj
# RUN: llvm-mc -filetype=obj -triple=x86_64-windows-msvc %t/import-def-unflagged.s -o %t/import-def-unflagged.obj
# RUN: lld-link /dll /noentry %t/import-def-unflagged.obj /out:%t/import-unflagged.dll \
# RUN:   /implib:%t/import-unflagged.lib /export:foo
# RUN: lld-link %t/import-record-only.obj %t/import-static.lib %t/import-flagged.lib /out:%t/import.exe \
# RUN:   /entry:main /subsystem:console
# RUN: lld-link %t/import-record-only.obj %t/import-flagged.lib %t/import-static.lib /out:%t/import.exe \
# RUN:   /entry:main /subsystem:console
# RUN: env LLD_IN_TEST=1 not lld-link %t/import-record-only.obj %t/import-static.lib %t/import-unflagged.lib \
# RUN:   /out:%t/import.exe /entry:main /subsystem:console 2>&1 | FileCheck %s --check-prefix=IMPORT-UNFLAGGED
# IMPORT-UNFLAGGED: error: foo was not compiled with -fmalterlib-sized-destructors
## Also when the archive comes before the site, and the import library after
## the site or before it.
# RUN: lld-link %t/import-static.lib %t/import-site.obj %t/import-flagged.lib /out:%t/import.exe \
# RUN:   /entry:main /subsystem:console
# RUN: lld-link %t/import-static.lib %t/import-flagged.lib %t/import-site.obj /out:%t/import.exe \
# RUN:   /entry:main /subsystem:console
# RUN: lld-link %t/import-static.lib %t/import-record-only.obj %t/import-flagged.lib /out:%t/import.exe \
# RUN:   /entry:main /subsystem:console
# RUN: lld-link %t/import-static.lib %t/import-flagged.lib %t/import-record-only.obj /out:%t/import.exe \
# RUN:   /entry:main /subsystem:console
# RUN: env LLD_IN_TEST=1 not lld-link %t/import-static.lib %t/import-unflagged.lib %t/import-record-only.obj \
# RUN:   /out:%t/import.exe /entry:main /subsystem:console 2>&1 | FileCheck %s --check-prefix=IMPORT-UNFLAGGED
## A site that calls the definition loads the member of the archive for it,
## which defines the marker with it, not the import of the marker that an
## import library after it has, also when the import library comes first.
# RUN: llvm-mc -filetype=obj -triple=x86_64-windows-msvc %t/import-direct.s -o %t/import-direct.obj
# RUN: lld-link %t/import-direct.obj %t/import-static.lib %t/import-flagged.lib /out:%t/direct.exe \
# RUN:   /entry:main /subsystem:console
# RUN: llvm-readobj --coff-imports %t/direct.exe | FileCheck %s --check-prefix=DIRECT
# RUN: lld-link %t/import-static.lib %t/import-flagged.lib %t/import-direct.obj /out:%t/direct.exe \
# RUN:   /entry:main /subsystem:console
# RUN: llvm-readobj --coff-imports %t/direct.exe | FileCheck %s --check-prefix=DIRECT
# DIRECT-NOT: Name: import-flagged.dll
## A DLL that exports again a definition it imports from another exports with
## it the marker the other exports, which proves it to a site that imports it
## from the DLL, also when it is exported by name.
# RUN: lld-link /dll /noentry %t/import-flagged.lib /export:foo /out:%t/facade.dll /implib:%t/facade.lib
# RUN: llvm-readobj --coff-exports %t/facade.dll | FileCheck %s --check-prefix=FACADE
# FACADE: Name: foo{{$}}
# FACADE: Name: foo.mib_sized{{$}}
# RUN: lld-link %t/import-site.obj %t/facade.lib /out:%t/facade.exe /entry:main /subsystem:console
# RUN: lld-link /dll /noentry %t/import-flagged.lib /export:foo /export:foo.mib_sized /out:%t/facade-named.dll \
# RUN:   /implib:%t/facade-named.lib
# RUN: llvm-readobj --coff-exports %t/facade-named.dll | FileCheck %s --check-prefix=FACADE
# RUN: lld-link %t/import-site.obj %t/facade-named.lib /out:%t/facade.exe /entry:main /subsystem:console
## Also of a DLL that MinGW links directly, whose imports it makes itself.
# RUN: lld-link -lldmingw /dll /noentry %t/import-flagged.dll /export:foo /out:%t/facade-dll.dll \
# RUN:   /implib:%t/facade-dll.lib
# RUN: llvm-readobj --coff-exports %t/facade-dll.dll | FileCheck %s --check-prefix=FACADE
# RUN: lld-link %t/import-site.obj %t/facade-dll.lib /out:%t/facade.exe /entry:main /subsystem:console
## But not of one compiled without the flag, which the other exports no marker
## of.
# RUN: lld-link /dll /noentry %t/import-unflagged.lib /export:foo /out:%t/facade-unflagged.dll \
# RUN:   /implib:%t/facade-unflagged.lib
# RUN: env LLD_IN_TEST=1 not lld-link %t/import-site.obj %t/facade-unflagged.lib /out:%t/facade.exe \
# RUN:   /entry:main /subsystem:console 2>&1 | FileCheck %s --check-prefix=IMPORT-UNFLAGGED

## A constructor that reaches the vtable it installs through its import,
## `__imp_vt`, as MinGW code of a class it sees as dllimport does, depends on
## the copy of the DLL, which its record names `vt`, not on another copy the
## image has, which its code doesn't use; and on the image's own copy when the
## import is a local import.
# RUN: llvm-mc -filetype=obj -triple=x86_64-windows-gnu %t/imp-ctor.s -o %t/imp-ctor.obj
# RUN: llvm-mc -filetype=obj -triple=x86_64-windows-gnu %t/vt-flagged.s -o %t/vt-flagged.obj
# RUN: llvm-mc -filetype=obj -triple=x86_64-windows-gnu %t/vt-unflagged.s -o %t/vt-unflagged.obj
# RUN: lld-link -lldmingw /dll /noentry %t/vt-flagged.obj /out:%t/vt-flagged.dll /implib:%t/vt-flagged.lib \
# RUN:   /export:vt,DATA
# RUN: lld-link -lldmingw /dll /noentry %t/vt-unflagged.obj /out:%t/vt-unflagged.dll /implib:%t/vt-unflagged.lib \
# RUN:   /export:vt,DATA
# RUN: lld-link -lldmingw %t/imp-ctor.obj %t/vt-flagged.lib /out:%t/imp.exe /entry:main /subsystem:console
# RUN: lld-link -lldmingw %t/imp-ctor.obj %t/vt-unflagged.obj %t/vt-flagged.lib /out:%t/imp.exe \
# RUN:   /entry:main /subsystem:console
# RUN: env LLD_IN_TEST=1 not lld-link -lldmingw %t/imp-ctor.obj %t/vt-unflagged.lib /out:%t/imp.exe \
# RUN:   /entry:main /subsystem:console 2>&1 | FileCheck %s --check-prefix=IMP-VTABLE
# RUN: env LLD_IN_TEST=1 not lld-link -lldmingw %t/imp-ctor.obj %t/vt-flagged.obj %t/vt-unflagged.lib \
# RUN:   /out:%t/imp.exe /entry:main /subsystem:console 2>&1 | FileCheck %s --check-prefix=IMP-VTABLE
# RUN: lld-link -lldmingw %t/imp-ctor.obj %t/vt-flagged.obj /out:%t/imp.exe /entry:main /subsystem:console
# RUN: env LLD_IN_TEST=1 not lld-link -lldmingw %t/imp-ctor.obj %t/vt-unflagged.obj /out:%t/imp.exe \
# RUN:   /entry:main /subsystem:console 2>&1 | FileCheck %s --check-prefix=IMP-VTABLE
# IMP-VTABLE: error: ctor depends on vt
# IMP-VTABLE-NEXT: >>> vt was not compiled with -fmalterlib-sized-destructors

## A larger copy of IMAGE_COMDAT_SELECT_LARGEST, like a vftable with RTTI,
## takes the place of a smaller one, like one without, and so does its marker,
## which the smaller one's leaves.
# RUN: llvm-mc -filetype=obj -triple=x86_64-windows-msvc %t/main-vftable.s -o %t/main-vftable.obj
# RUN: llvm-mc -filetype=obj -triple=x86_64-windows-msvc %t/vftable-small.s -o %t/vftable-small.obj
# RUN: llvm-mc -filetype=obj -triple=x86_64-windows-msvc %t/vftable-large.s -o %t/vftable-large.obj
# RUN: llvm-mc -filetype=obj -triple=x86_64-windows-msvc %t/vftable-large-unflagged.s -o %t/vftable-large-unflagged.obj
# RUN: lld-link %t/main-vftable.obj %t/vftable-small.obj %t/vftable-large.obj /out:%t/largest.exe \
# RUN:   /entry:main /subsystem:console
# RUN: lld-link %t/main-vftable.obj %t/vftable-large.obj %t/vftable-small.obj /out:%t/largest.exe \
# RUN:   /entry:main /subsystem:console
# RUN: env LLD_IN_TEST=1 not lld-link %t/main-vftable.obj %t/vftable-small.obj %t/vftable-large-unflagged.obj \
# RUN:   /out:%t/largest.exe /entry:main /subsystem:console 2>&1 | FileCheck %s --check-prefix=LARGEST-MIXED
# LARGEST-MIXED:      error: const Foreign::`vftable' was compiled both with and without -fmalterlib-sized-destructors, and the link keeps a copy compiled without
# LARGEST-MIXED-NEXT: >>> defined in {{.*}}vftable-large-unflagged.obj

## A vector deleting destructor is a weak alias of the scalar one, which the
## link resolves after the check.
# RUN: llvm-mc -filetype=obj -triple=x86_64-windows-msvc %t/weak-alias.s -o %t/weak-alias.obj
# RUN: lld-link %t/main.obj %t/weak-alias.obj /out:%t/weak-alias.exe /entry:main /subsystem:console
## The marker of the alias is a weak alias too: a copy that overrides the alias
## overrides the marker with its own, or leaves the marker elsewhere.
# RUN: lld-link %t/main.obj %t/weak-alias.obj %t/flagged.obj /out:%t/weak-alias.exe /entry:main /subsystem:console
# RUN: env LLD_IN_TEST=1 not lld-link %t/main.obj %t/weak-alias.obj %t/unflagged.obj /out:%t/weak-alias.exe \
# RUN:   /entry:main /subsystem:console 2>&1 | FileCheck %s --check-prefix=WEAK-MIXED
# WEAK-MIXED: error: public: virtual void * __cdecl Foreign::`vector deleting dtor'(unsigned int) was compiled both with and without -fmalterlib-sized-destructors, and the link keeps a copy compiled without
## The marker of the alias falls back on the marker of the definition, which
## the link drops with a copy it does not keep: the alias of a copy compiled
## without the flag has none.
# RUN: llvm-mc -filetype=obj -triple=x86_64-windows-msvc %t/weak-alias-unflagged.s -o %t/weak-alias-unflagged.obj
# RUN: lld-link %t/main.obj %t/weak-alias.obj %t/weak-alias-unflagged.obj /out:%t/weak-alias.exe /entry:main /subsystem:console
# RUN: env LLD_IN_TEST=1 not lld-link %t/main.obj %t/weak-alias-unflagged.obj %t/weak-alias.obj /out:%t/weak-alias.exe \
# RUN:   /entry:main /subsystem:console 2>&1 | FileCheck %s
## A DLL does not export the marker such a copy left behind.
# RUN: lld-link /dll /noentry %t/weak-alias.obj %t/unflagged.obj /out:%t/displaced.dll \
# RUN:   "/export:??_EForeign@@UEAAPEAXI@Z" "/export:??_EForeign@@UEAAPEAXI@Z.mib_sized"
# RUN: llvm-readobj --coff-exports %t/displaced.dll | FileCheck %s --check-prefix=DISPLACED
# DISPLACED: Name: ??_EForeign@@UEAAPEAXI@Z
# DISPLACED-NOT: Name: ??_EForeign@@UEAAPEAXI@Z.mib_sized

## A definition is exported with its marker, which needs no directive of its
## own.
# RUN: lld-link /dll /noentry %t/flagged.obj /out:%t/flagged-plain.dll "/export:??_EForeign@@UEAAPEAXI@Z"
# RUN: llvm-readobj --coff-exports %t/flagged-plain.dll | FileCheck %s --check-prefix=WITH-MARKER
# WITH-MARKER: Name: ??_EForeign@@UEAAPEAXI@Z
# WITH-MARKER: Name: ??_EForeign@@UEAAPEAXI@Z.mib_sized
# RUN: lld-link /dll /noentry %t/weak-alias.obj /out:%t/weak-alias.dll "/export:??_EForeign@@UEAAPEAXI@Z"
# RUN: llvm-readobj --coff-exports %t/weak-alias.dll | FileCheck %s --check-prefix=WITH-MARKER

## MinGW imports a variable another image exports when a reference names it
## directly, after the check.
# RUN: llvm-mc -filetype=obj -triple=x86_64-windows-gnu %t/mingw-export.s -o %t/mingw-export.obj
# RUN: llvm-mc -filetype=obj -triple=x86_64-windows-gnu %t/mingw-site.s -o %t/mingw-site.obj
# RUN: lld-link -lldmingw /dll /noentry %t/mingw-export.obj /out:%t/mingw.dll /implib:%t/mingw.lib
# RUN: lld-link -lldmingw %t/mingw-site.obj %t/mingw.lib /out:%t/mingw.exe /entry:main /subsystem:console

## The marker of a definition another image exports is exported with it, and
## a site refers to it through its import thunk.
# RUN: llvm-mc -filetype=obj -triple=x86_64-windows-msvc %t/main-import.s -o %t/main-import.obj
# RUN: llvm-mc -filetype=obj -triple=x86_64-windows-msvc %t/flagged-export.s -o %t/flagged-export.obj
# RUN: lld-link /dll /noentry %t/flagged-export.obj /out:%t/flagged.dll /implib:%t/flagged.lib
# RUN: lld-link /dll /noentry %t/unflagged.obj /out:%t/unflagged.dll /implib:%t/unflagged.lib "/export:??_EForeign@@UEAAPEAXI@Z"
# RUN: lld-link %t/main-import.obj %t/flagged.lib /out:%t/import.exe /entry:main /subsystem:console
# RUN: env LLD_IN_TEST=1 not lld-link %t/main-import.obj %t/unflagged.lib /out:%t/import.exe /entry:main /subsystem:console 2>&1 \
# RUN:   | FileCheck %s --check-prefix=IMPORT
# IMPORT: error: public: virtual void * __cdecl Foreign::`vector deleting dtor'(unsigned int) was not compiled with -fmalterlib-sized-destructors
# IMPORT-NEXT: >>> its marker ??_EForeign@@UEAAPEAXI@Z.mib_sized is undefined
## The image's marker does not stand in for an object's copy.
# RUN: env LLD_IN_TEST=1 not lld-link %t/main-import.obj %t/unflagged.obj %t/flagged.lib /out:%t/import.exe /entry:main /subsystem:console 2>&1 \
# RUN:   | FileCheck %s --check-prefix=MIXED-DLL
# MIXED-DLL: error: public: virtual void * __cdecl Foreign::`vector deleting dtor'(unsigned int) was compiled both with and without -fmalterlib-sized-destructors, and the link keeps a copy compiled without

## The marker of a constructor proves its claim only when the vftable it
## installs in its image was compiled with the flag. A DLL whose constructor
## installs its own vftable links, but does not export the marker it cannot
## prove; a site elsewhere then fails.
# RUN: llvm-mc -filetype=obj -triple=x86_64-windows-msvc %t/ctor.s -o %t/ctor.obj
# RUN: llvm-mc -filetype=obj -triple=x86_64-windows-msvc %t/vtable-flagged.s -o %t/vtable-flagged.obj
# RUN: llvm-mc -filetype=obj -triple=x86_64-windows-msvc %t/vtable-unflagged.s -o %t/vtable-unflagged.obj
# RUN: llvm-mc -filetype=obj -triple=x86_64-windows-msvc %t/ctor-site.s -o %t/ctor-site.obj
# RUN: lld-link /dll /noentry %t/ctor.obj %t/vtable-flagged.obj %t/flagged.obj /out:%t/ctor-flagged.dll /implib:%t/ctor-flagged.lib
# RUN: lld-link /dll /noentry %t/ctor.obj %t/vtable-unflagged.obj %t/flagged.obj /out:%t/ctor-unflagged.dll /implib:%t/ctor-unflagged.lib
# RUN: lld-link %t/ctor-site.obj %t/ctor-flagged.lib /out:%t/ctor.exe /entry:main /subsystem:console
## A definition exported under another name too, `/export:Alias=Name`, also
## has its marker exported under that name, besides the one of its own name.
# RUN: lld-link /dll /noentry %t/ctor.obj %t/vtable-flagged.obj %t/flagged.obj "/export:??0Foreign@@QEAA@XZ" \
# RUN:   "/export:PublicCtor=??0Foreign@@QEAA@XZ" /out:%t/ctor-alias.dll
# RUN: llvm-readobj --coff-exports %t/ctor-alias.dll | FileCheck %s --check-prefix=ALIAS-MARKER
# ALIAS-MARKER-DAG: Name: ??0Foreign@@QEAA@XZ.mib_sized
# ALIAS-MARKER-DAG: Name: PublicCtor.mib_sized
# RUN: env LLD_IN_TEST=1 not lld-link %t/ctor-site.obj %t/ctor-unflagged.lib /out:%t/ctor.exe /entry:main /subsystem:console 2>&1 \
# RUN:   | FileCheck %s --check-prefix=CTOR
# CTOR: error: public: __cdecl Foreign::Foreign(void) was not compiled with -fmalterlib-sized-destructors
## Of an import library that comes before one with the marker, which the
## loader binds the import to, as the import library's member of its DLL.
# RUN: env LLD_IN_TEST=1 not lld-link %t/ctor-site.obj %t/ctor-unflagged.lib %t/ctor-flagged.lib /out:%t/ctor.exe \
# RUN:   /entry:main /subsystem:console 2>&1 | FileCheck %s --check-prefix=CTOR-IMPORTS
# CTOR-IMPORTS:      error: public: __cdecl Foreign::Foreign(void) was compiled both with and without -fmalterlib-sized-destructors, and the link keeps a copy compiled without
# CTOR-IMPORTS-NEXT: >>> defined in {{.*}}ctor-unflagged.lib(ctor-unflagged.dll)
# CTOR-IMPORTS-NEXT: >>> marked in {{.*}}ctor-flagged.lib(ctor-flagged.dll)
# RUN: env LLD_IN_TEST=1 not lld-link %t/ctor-site.obj %t/ctor.obj %t/vtable-unflagged.obj %t/flagged.obj /out:%t/ctor.exe \
# RUN:   /entry:main /subsystem:console 2>&1 | FileCheck %s --check-prefix=CHAIN
# CHAIN:      error: public: __cdecl Foreign::Foreign(void) depends on const Foreign::`vftable'
# CHAIN-NEXT: >>> const Foreign::`vftable' was not compiled with -fmalterlib-sized-destructors

## The records of a COMDAT owner are associative with it, and garbage
## collection does not bring them back with the owner: an unmarked vtable
## nothing requires the proof of is not an error.
# RUN: llvm-mc -filetype=obj -triple=x86_64-windows-msvc %t/ctor-comdat.s -o %t/ctor-comdat.obj
# RUN: llvm-mc -filetype=obj -triple=x86_64-windows-msvc %t/ctor-comdat-user.s -o %t/ctor-comdat-user.obj
# RUN: lld-link /opt:ref %t/ctor-comdat-user.obj %t/ctor-comdat.obj %t/vtable-unflagged.obj %t/unflagged.obj \
# RUN:   /out:%t/ctor-comdat.exe /entry:main /subsystem:console

## A GNU import library holds an ordinary object for each import, which a
## marker may load, and whose library stands for the DLL.
# RUN: llvm-mc -triple=x86_64-windows-gnu %p/Inputs/gnu-implib-head.s -filetype=obj -o %t/gnu-dabcdh.o
# RUN: llvm-mc -triple=x86_64-windows-gnu %p/Inputs/gnu-implib-func.s -filetype=obj -o %t/gnu-dabcds00000.o
# RUN: llvm-mc -triple=x86_64-windows-gnu %t/gnu-func-marker.s -filetype=obj -o %t/gnu-dabcds00001.o
# RUN: llvm-mc -triple=x86_64-windows-gnu %p/Inputs/gnu-implib-tail.s -filetype=obj -o %t/gnu-dabcdt.o
# RUN: rm -f %t/gnu-implib.a
# RUN: llvm-ar rcs %t/gnu-implib.a %t/gnu-dabcdh.o %t/gnu-dabcds00000.o %t/gnu-dabcds00001.o %t/gnu-dabcdt.o
# RUN: llvm-mc -triple=x86_64-windows-gnu %t/gnu-site.s -filetype=obj -o %t/gnu-site.o
# RUN: lld-link %t/gnu-site.o %t/gnu-implib.a /out:%t/gnu.exe /entry:main /subsystem:console
## A DLL that exports again a definition it imports through a GNU import
## library exports with it the marker the library imports too.
# RUN: lld-link -lldmingw /dll /noentry %t/gnu-implib.a /export:func /out:%t/gnu-facade.dll \
# RUN:   /implib:%t/gnu-facade.lib
# RUN: llvm-readobj --coff-exports %t/gnu-facade.dll | FileCheck %s --check-prefix=GNU-FACADE
# GNU-FACADE: Name: func{{$}}
# GNU-FACADE: Name: func.mib_sized{{$}}
# RUN: lld-link %t/gnu-site.o %t/gnu-facade.lib /out:%t/gnu-facade.exe /entry:main /subsystem:console
## Also where the marker of a definition a GNU import library imports comes
## from an import library of Microsoft of the same DLL, which names it as the
## import descriptor of the GNU one does, in either case.
# RUN: rm -f %t/gnu-func-only.a
# RUN: llvm-ar rcs %t/gnu-func-only.a %t/gnu-dabcdh.o %t/gnu-dabcds00000.o %t/gnu-dabcdt.o
# RUN: printf "LIBRARY FOO.dll\nEXPORTS\nfunc.mib_sized\n" > %t/ms-marker.def
# RUN: llvm-lib /machine:x64 /def:%t/ms-marker.def /out:%t/ms-marker.lib
# RUN: lld-link %t/gnu-site.o %t/gnu-func-only.a %t/ms-marker.lib /out:%t/gnu-ms.exe /entry:main /subsystem:console
## But not an archive with the imports of several DLLs, whose import of the
## definition and of its marker come from different DLLs, as the heads of their
## imports tell.
# RUN: llvm-mc -triple=x86_64-windows-gnu %t/gnu-other-head.s -filetype=obj -o %t/gnu-otherh.o
# RUN: llvm-mc -triple=x86_64-windows-gnu %t/gnu-other-marker.s -filetype=obj -o %t/gnu-others00001.o
# RUN: llvm-mc -triple=x86_64-windows-gnu %t/gnu-other-tail.s -filetype=obj -o %t/gnu-othert.o
# RUN: rm -f %t/gnu-combined.a
# RUN: llvm-ar rcs %t/gnu-combined.a %t/gnu-dabcdh.o %t/gnu-dabcds00000.o %t/gnu-dabcdt.o \
# RUN:   %t/gnu-otherh.o %t/gnu-others00001.o %t/gnu-othert.o
# RUN: env LLD_IN_TEST=1 not lld-link %t/gnu-site.o %t/gnu-combined.a /out:%t/gnu-combined.exe /entry:main \
# RUN:   /subsystem:console 2>&1 | FileCheck %s --check-prefix=GNU-COMBINED
# GNU-COMBINED: error: func was compiled both with and without -fmalterlib-sized-destructors, and the link keeps a copy compiled without
## Also the import address table entries of a variable and its marker, which
## MinGW imports automatically, `_ZTV7Foreign DATA` of a GNU import library.
# RUN: llvm-mc -triple=x86_64-windows-gnu %t/gnu-data-vtable.s -filetype=obj -o %t/gnu-dabcds00002.o
# RUN: llvm-mc -triple=x86_64-windows-gnu %t/gnu-data-marker.s -filetype=obj -o %t/gnu-dabcds00003.o
# RUN: rm -f %t/gnu-data-implib.a
# RUN: llvm-ar rcs %t/gnu-data-implib.a %t/gnu-dabcdh.o %t/gnu-dabcds00002.o %t/gnu-dabcds00003.o %t/gnu-dabcdt.o
# RUN: lld-link -lldmingw %t/mingw-site.obj %t/gnu-data-implib.a /out:%t/gnu-data.exe /entry:main /subsystem:console

## A marker belongs to the copy of a definition the link chose for the
## definition's own sake, so a reference to it loads no archive member, such
## as the record of a constructor the optimizer inlined everywhere.
# RUN: llvm-mc -filetype=obj -triple=x86_64-windows-msvc %t/owner-gone.s -o %t/owner-gone.obj
# RUN: llvm-mc -filetype=obj -triple=x86_64-windows-msvc %t/ctor-member.s -o %t/ctor-member.obj
# RUN: rm -f %t/ctor-member.lib && llvm-lib /out:%t/ctor-member.lib %t/ctor-member.obj
# RUN: lld-link %t/owner-gone.obj %t/ctor-member.lib /out:%t/owner-gone.exe /entry:main /subsystem:console

## A definition LTO compiles carries its marker into the object LTO produces,
## even when LTO makes it local.
# RUN: llvm-as %t/lto-main.ll -o %t/lto-main.obj
# RUN: llvm-as %t/lto-flagged.ll -o %t/lto-flagged.obj
# RUN: llvm-as %t/lto-unflagged.ll -o %t/lto-unflagged.obj
# RUN: lld-link %t/main.obj %t/lto-flagged.obj /out:%t/lto.exe /entry:main /subsystem:console
# RUN: env LLD_IN_TEST=1 not lld-link %t/main.obj %t/lto-unflagged.obj /out:%t/lto.exe \
# RUN:   /entry:main /subsystem:console 2>&1 | FileCheck %s
# RUN: lld-link %t/lto-main.obj %t/lto-flagged.obj /out:%t/lto-local.exe /entry:main /subsystem:console
# RUN: env LLD_IN_TEST=1 not lld-link %t/lto-main.obj %t/lto-unflagged.obj /out:%t/lto-local.exe \
# RUN:   /entry:main /subsystem:console 2>&1 | FileCheck %s --check-prefix=LTO
# LTO: error: public: virtual void * __cdecl Foreign::`vector deleting dtor'(unsigned int) was not compiled with -fmalterlib-sized-destructors

#--- ctor-comdat.s
.section .text,"xr",discard,"??0Foreign@@QEAA@XZ"
.globl "??0Foreign@@QEAA@XZ"
"??0Foreign@@QEAA@XZ":
.Lowner:
  leaq "??_7Foreign@@6B@"(%rip), %rax
  retq

.section .rdata$mibszd,"dr",associative,"??0Foreign@@QEAA@XZ"
.quad .Lowner
.quad "??_7Foreign@@6B@"
.quad "??_7Foreign@@6B@.mib_sized"

#--- ctor-comdat-user.s
.text
.globl main
main:
  callq "??0Foreign@@QEAA@XZ"
  retq

#--- gnu-site.s
.text
.globl main
main:
  retq

.section .rdata$mibszf,"dr"
.quad func
.quad func.mib_sized

#--- gnu-func-marker.s
        .text
        .global         func.mib_sized
        .global         __imp_func.mib_sized
func.mib_sized:
        jmp             *__imp_func.mib_sized

        .section        .idata$7
        .rva            _head_test_lib

        .section        .idata$5
__imp_func.mib_sized:
        .rva            .Lhint_name
        .long           0

        .section        .idata$4
        .rva            .Lhint_name
        .long           0

        .section        .idata$6
.Lhint_name:
        .short          0
        .asciz          "func.mib_sized"

#--- gnu-data-vtable.s
        .global         __imp__ZTV7Foreign
        .section        .idata$7
        .rva            _head_test_lib

        .section        .idata$5
__imp__ZTV7Foreign:
        .rva            .Lhint_name
        .long           0

        .section        .idata$4
        .rva            .Lhint_name
        .long           0

        .section        .idata$6
.Lhint_name:
        .short          0
        .asciz          "_ZTV7Foreign"

#--- gnu-data-marker.s
        .global         __imp__ZTV7Foreign.mib_sized
        .section        .idata$7
        .rva            _head_test_lib

        .section        .idata$5
__imp__ZTV7Foreign.mib_sized:
        .rva            .Lhint_name
        .long           0

        .section        .idata$4
        .rva            .Lhint_name
        .long           0

        .section        .idata$6
.Lhint_name:
        .short          0
        .asciz          "_ZTV7Foreign.mib_sized"

#--- gnu-other-head.s
        .section        .idata$2
        .global         _head_other_lib
_head_other_lib:
        .rva            hname
        .long           0
        .long           0
        .rva            __other_lib_iname
        .rva            fthunk

        .section        .idata$5
fthunk:
        .section        .idata$4
hname:

#--- gnu-other-tail.s
        .section        .idata$4
        .long           0
        .long           0
        .section        .idata$5
        .long           0
        .long           0

        .section        .idata$7
        .global         __other_lib_iname
__other_lib_iname:
       .asciz           "bar.dll"

#--- gnu-other-marker.s
        .text
        .global         func.mib_sized
        .global         __imp_func.mib_sized
func.mib_sized:
        jmp             *__imp_func.mib_sized

        .section        .idata$7
        .rva            _head_other_lib

        .section        .idata$5
__imp_func.mib_sized:
        .rva            .Lhint_name
        .long           0

        .section        .idata$4
        .rva            .Lhint_name
        .long           0

        .section        .idata$6
.Lhint_name:
        .short          0
        .asciz          "func.mib_sized"

#--- owner-gone.s
.text
.globl main
main:
  retq

.section .rdata,"dr"
.globl "??_7Foreign@@6B@"
"??_7Foreign@@6B@":
  .quad 0

.section .rdata$mibszd,"dr"
.quad "??0Foreign@@QEAA@XZ.mib_sized"
.quad "??_7Foreign@@6B@"
.quad "??_7Foreign@@6B@.mib_sized"

#--- ctor-member.s
.text
.globl "??0Foreign@@QEAA@XZ"
.globl "??0Foreign@@QEAA@XZ.mib_sized"
"??0Foreign@@QEAA@XZ":
"??0Foreign@@QEAA@XZ.mib_sized":
  callq unrelated
  retq

#--- main.s
.text
.globl main
main:
  retq

.section .rdata$mibszf,"dr"
.quad "??_EForeign@@UEAAPEAXI@Z"
.quad "??_EForeign@@UEAAPEAXI@Z.mib_sized"

#--- main-import.s
.text
.globl main
main:
  retq

.section .rdata$mibszf,"dr"
.quad "??_EForeign@@UEAAPEAXI@Z"
.quad "??_EForeign@@UEAAPEAXI@Z.mib_sized"

#--- flagged-export.s
.text
.globl "??_EForeign@@UEAAPEAXI@Z"
.globl "??_EForeign@@UEAAPEAXI@Z.mib_sized"
"??_EForeign@@UEAAPEAXI@Z":
"??_EForeign@@UEAAPEAXI@Z.mib_sized":
  retq

.section .drectve,"yni"
.ascii " /EXPORT:\"??_EForeign@@UEAAPEAXI@Z\" /EXPORT:\"??_EForeign@@UEAAPEAXI@Z.mib_sized\""

#--- ctor.s
.text
.globl "??0Foreign@@QEAA@XZ"
.globl "??0Foreign@@QEAA@XZ.mib_sized"
"??0Foreign@@QEAA@XZ":
"??0Foreign@@QEAA@XZ.mib_sized":
  retq

.section .rdata$mibszd,"dr"
.quad "??0Foreign@@QEAA@XZ.mib_sized"
.quad "??_7Foreign@@6B@"
.quad "??_7Foreign@@6B@.mib_sized"

.section .drectve,"yni"
.ascii " /EXPORT:\"??0Foreign@@QEAA@XZ\" /EXPORT:\"??0Foreign@@QEAA@XZ.mib_sized\""

#--- vtable-flagged.s
.section .rdata,"dr"
.globl "??_7Foreign@@6B@"
.globl "??_7Foreign@@6B@.mib_sized"
"??_7Foreign@@6B@":
"??_7Foreign@@6B@.mib_sized":
  .quad "??_EForeign@@UEAAPEAXI@Z"

.section .rdata$mibszd,"dr"
.quad "??_7Foreign@@6B@.mib_sized"
.quad "??_EForeign@@UEAAPEAXI@Z"
.quad "??_EForeign@@UEAAPEAXI@Z.mib_sized"

#--- vtable-unflagged.s
.section .rdata,"dr"
.globl "??_7Foreign@@6B@"
"??_7Foreign@@6B@":
  .quad "??_EForeign@@UEAAPEAXI@Z"

#--- ctor-site.s
.text
.globl main
main:
  retq

.section .rdata$mibszf,"dr"
.quad "??0Foreign@@QEAA@XZ"
.quad "??0Foreign@@QEAA@XZ.mib_sized"

#--- imp-ctor.s
.text
.globl ctor
.globl ctor.mib_sized
ctor:
ctor.mib_sized:
  movq __imp_vt(%rip), %rax
  retq

.globl main
main:
  retq

.section .rdata$mibszd,"dr"
.quad ctor.mib_sized
.quad vt
.quad vt.mib_sized

.section .rdata$mibszf,"dr"
.quad ctor
.quad ctor.mib_sized

#--- vt-flagged.s
.section .rdata,"dr"
.globl vt
.globl vt.mib_sized
vt:
vt.mib_sized:
.quad 0

#--- vt-unflagged.s
.section .rdata,"dr"
.globl vt
vt:
.quad 0

#--- import-def.s
.text
.globl foo
foo:
.globl foo.mib_sized
foo.mib_sized:
  retq

#--- import-def-unflagged.s
.text
.globl foo
foo:
  retq

#--- import-direct.s
.text
.globl main
main:
  callq foo
  retq

.section .rdata$mibszf,"dr"
.quad foo
.quad foo.mib_sized

#--- import-record-only.s
.text
.globl main
main:
  retq

.section .rdata$mibszf,"dr"
.quad foo
.quad foo.mib_sized

#--- import-site.s
.text
.globl main
main:
  callq *__imp_foo(%rip)
  retq

.section .rdata$mibszf,"dr"
.quad foo
.quad foo.mib_sized

#--- main-vftable.s
.text
.globl main
main:
  retq

.section .rdata$mibszf,"dr"
.quad "??_7Foreign@@6B@"
.quad "??_7Foreign@@6B@.mib_sized"

#--- vftable-small.s
.section .rdata,"dr",largest,"??_7Foreign@@6B@"
.globl "??_7Foreign@@6B@"
.globl "??_7Foreign@@6B@.mib_sized"
"??_7Foreign@@6B@":
"??_7Foreign@@6B@.mib_sized":
  .quad 0

#--- vftable-large.s
.section .rdata,"dr",largest,"??_7Foreign@@6B@"
  .quad 0
.globl "??_7Foreign@@6B@"
.globl "??_7Foreign@@6B@.mib_sized"
"??_7Foreign@@6B@":
"??_7Foreign@@6B@.mib_sized":
  .quad 0

#--- vftable-large-unflagged.s
.section .rdata,"dr",largest,"??_7Foreign@@6B@"
  .quad 0
.globl "??_7Foreign@@6B@"
"??_7Foreign@@6B@":
  .quad 0

#--- weak-alias.s
.section .text,"xr",discard,"??_GForeign@@UEAAPEAXI@Z"
.globl "??_GForeign@@UEAAPEAXI@Z"
.globl "??_GForeign@@UEAAPEAXI@Z.mib_sized"
"??_GForeign@@UEAAPEAXI@Z":
"??_GForeign@@UEAAPEAXI@Z.mib_sized":
  retq
.weak "??_EForeign@@UEAAPEAXI@Z"
"??_EForeign@@UEAAPEAXI@Z" = "??_GForeign@@UEAAPEAXI@Z"
.weak "??_EForeign@@UEAAPEAXI@Z.mib_sized"
"??_EForeign@@UEAAPEAXI@Z.mib_sized" = "??_GForeign@@UEAAPEAXI@Z.mib_sized"

#--- weak-alias-unflagged.s
.section .text,"xr",discard,"??_GForeign@@UEAAPEAXI@Z"
.globl "??_GForeign@@UEAAPEAXI@Z"
"??_GForeign@@UEAAPEAXI@Z":
  retq
.weak "??_EForeign@@UEAAPEAXI@Z"
"??_EForeign@@UEAAPEAXI@Z" = "??_GForeign@@UEAAPEAXI@Z"

#--- mingw-export.s
.section .rdata,"dr"
.globl _ZTV7Foreign
.globl _ZTV7Foreign.mib_sized
_ZTV7Foreign:
_ZTV7Foreign.mib_sized:
  .quad 0

.section .drectve,"yni"
.ascii " -export:_ZTV7Foreign,data -export:_ZTV7Foreign.mib_sized,data"

#--- mingw-site.s
.text
.globl main
main:
  retq

.section .rdata$mibszf,"dr"
.quad _ZTV7Foreign
.quad _ZTV7Foreign.mib_sized

#--- site-inline.s
.text
.globl main
main:
  retq

.section .rdata$mibszf,"dr"
.quad "??_EForeign@@UEAAPEAXI@Z"
.quad "??_EForeign@@UEAAPEAXI@Z.mib_sized"

.section .text,"xr",discard,"??_EForeign@@UEAAPEAXI@Z"
.globl "??_EForeign@@UEAAPEAXI@Z"
.globl "??_EForeign@@UEAAPEAXI@Z.mib_sized"
"??_EForeign@@UEAAPEAXI@Z":
"??_EForeign@@UEAAPEAXI@Z.mib_sized":
  retq

#--- plain.s
.text
.globl main
main:
  callq "??_EForeign@@UEAAPEAXI@Z"
  retq

#--- flagged.s
.text
.globl "??_EForeign@@UEAAPEAXI@Z"
.globl "??_EForeign@@UEAAPEAXI@Z.mib_sized"
"??_EForeign@@UEAAPEAXI@Z":
"??_EForeign@@UEAAPEAXI@Z.mib_sized":
  retq

#--- unflagged.s
.text
.globl "??_EForeign@@UEAAPEAXI@Z"
"??_EForeign@@UEAAPEAXI@Z":
  retq

#--- inline-flagged.s
.section .text,"xr",discard,"??_EForeign@@UEAAPEAXI@Z"
.globl "??_EForeign@@UEAAPEAXI@Z"
.globl "??_EForeign@@UEAAPEAXI@Z.mib_sized"
"??_EForeign@@UEAAPEAXI@Z":
"??_EForeign@@UEAAPEAXI@Z.mib_sized":
  retq

#--- inline-unflagged.s
.section .text,"xr",discard,"??_EForeign@@UEAAPEAXI@Z"
.globl "??_EForeign@@UEAAPEAXI@Z"
"??_EForeign@@UEAAPEAXI@Z":
  retq

#--- lto-main.ll
target triple = "x86_64-pc-windows-msvc"
target datalayout = "e-m:w-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"

@"??_EForeign@@UEAAPEAXI@Z.mib_sized" = external constant i8
@__mib_sized_ref = private constant [2 x ptr] [ptr @"??_EForeign@@UEAAPEAXI@Z", ptr @"??_EForeign@@UEAAPEAXI@Z.mib_sized"], section ".rdata$mibszf"
@llvm.compiler.used = appending global [1 x ptr] [ptr @__mib_sized_ref], section "llvm.metadata"

declare void @"??_EForeign@@UEAAPEAXI@Z"()

define void @main() {
  ret void
}

#--- lto-flagged.ll
target triple = "x86_64-pc-windows-msvc"
target datalayout = "e-m:w-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"

define void @"??_EForeign@@UEAAPEAXI@Z"() #0 {
  ret void
}

attributes #0 = { "malterlib-sized" }

#--- lto-unflagged.ll
target triple = "x86_64-pc-windows-msvc"
target datalayout = "e-m:w-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"

define void @"??_EForeign@@UEAAPEAXI@Z"() {
  ret void
}
