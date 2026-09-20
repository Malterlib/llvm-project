# REQUIRES: x86
## A definition compiled with -fmalterlib-sized-destructors carries a marker
## symbol at its address, which the construction sites that depend on it refer
## to. A definition compiled without the flag has no marker, and the reference
## stays undefined.

# RUN: rm -rf %t; split-file %s %t
# RUN: llvm-mc -filetype=obj -triple=x86_64-apple-darwin %t/main.s -o %t/main.o
# RUN: llvm-mc -filetype=obj -triple=x86_64-apple-darwin %t/flagged.s -o %t/flagged.o
# RUN: llvm-mc -filetype=obj -triple=x86_64-apple-darwin %t/unflagged.s -o %t/unflagged.o

# RUN: %lld -lSystem %t/main.o %t/flagged.o -o %t/flagged.out
# RUN: not %lld -demangle -lSystem %t/main.o %t/unflagged.o -o /dev/null 2>&1 | FileCheck %s
# CHECK:      error: Foreign::~Foreign() was not compiled with -fmalterlib-sized-destructors
# CHECK-NEXT: >>> its marker __ZN7ForeignD0Ev.mib_sized is undefined
# CHECK-NEXT: >>> an object a construction site makes is destroyed with the size its deleting destructor returns

## The references are in data that dead code stripping removes, and an
## unresolved marker is an error whatever the treatment of the other symbols.
# RUN: not %lld -demangle -lSystem -dead_strip -undefined dynamic_lookup %t/main.o %t/unflagged.o -o /dev/null 2>&1 \
# RUN:   | FileCheck %s

## A weak definition is checked in the copy the link keeps. The marker of a
## copy that lost is left behind, and then belongs to a copy the link did not
## keep.
# RUN: llvm-mc -filetype=obj -triple=x86_64-apple-darwin %t/weak-flagged.s -o %t/weak-flagged.o
# RUN: llvm-mc -filetype=obj -triple=x86_64-apple-darwin %t/weak-unflagged.s -o %t/weak-unflagged.o
# RUN: %lld -lSystem %t/main.o %t/weak-flagged.o %t/weak-unflagged.o -o %t/weak.out
# RUN: not %lld -demangle -lSystem %t/main.o %t/weak-unflagged.o %t/weak-flagged.o -o /dev/null 2>&1 \
# RUN:   | FileCheck %s --check-prefix=MIXED
# MIXED:      error: Foreign::~Foreign() was compiled both with and without -fmalterlib-sized-destructors, and the link keeps a copy compiled without
# MIXED-NEXT: >>> defined in {{.*}}weak-unflagged.o
# MIXED-NEXT: >>> marked in {{.*}}weak-flagged.o
## A marker nothing refers to is not an error: the class is not constructed
## through the builtin. The marker the copy that lost left behind does not
## leave the image.
# RUN: %lld -dylib -lSystem %t/weak-unflagged.o %t/weak-flagged.o -o %t/libdisplaced.dylib
# RUN: llvm-nm -g %t/libdisplaced.dylib | FileCheck %s --check-prefix=DISPLACED
# DISPLACED: __ZN7ForeignD0Ev
# DISPLACED-NOT: __ZN7ForeignD0Ev.mib_sized

## A site and an inline definition in one object refer to the marker the
## assembler resolved within the object, and the link may keep another copy.
# RUN: llvm-mc -filetype=obj -triple=x86_64-apple-darwin %t/site-weak.s -o %t/site-weak.o
# RUN: %lld -lSystem -dead_strip %t/site-weak.o %t/weak-unflagged.o -o /dev/null
# RUN: not %lld -demangle -lSystem -dead_strip %t/weak-unflagged.o %t/site-weak.o -o /dev/null 2>&1 \
# RUN:   | FileCheck %s --check-prefix=MIXED-SITE
# MIXED-SITE:      error: Foreign::~Foreign() was compiled both with and without -fmalterlib-sized-destructors, and the link keeps a copy compiled without
# MIXED-SITE-NEXT: >>> defined in {{.*}}weak-unflagged.o
# MIXED-SITE-NEXT: >>> marked in {{.*}}site-weak.o

## A definition from a dylib is checked against the marker the dylib exports.
# RUN: %lld -dylib -lSystem %t/flagged.o -o %t/libflagged.dylib
# RUN: %lld -dylib -lSystem %t/unflagged.o -o %t/libunflagged.dylib
# RUN: %lld -lSystem %t/main.o %t/libflagged.dylib -o %t/dylib.out
# RUN: not %lld -demangle -lSystem %t/main.o %t/libunflagged.dylib -o /dev/null 2>&1 | FileCheck %s
## A marker leaves the image with its definition, whatever the list of
## exported symbols says of it.
# RUN: %lld -dylib -lSystem -exported_symbol __ZN7ForeignD0Ev %t/flagged.o -o %t/libexported.dylib
# RUN: llvm-nm -g %t/libexported.dylib | FileCheck %s --check-prefix=EXPORTED-MARKER
# EXPORTED-MARKER: __ZN7ForeignD0Ev
# EXPORTED-MARKER: __ZN7ForeignD0Ev.mib_sized
# RUN: %lld -dylib -lSystem -dead_strip -exported_symbol __ZN7ForeignD0Ev %t/flagged.o -o %t/libexported-ds.dylib
# RUN: llvm-nm -g %t/libexported-ds.dylib | FileCheck %s --check-prefix=EXPORTED-MARKER
## The dylib's marker does not stand in for an object's copy.
# RUN: not %lld -demangle -lSystem %t/main.o %t/unflagged.o %t/libflagged.dylib -o /dev/null 2>&1 \
# RUN:   | FileCheck %s --check-prefix=MIXED-DYLIB
# MIXED-DYLIB:      error: Foreign::~Foreign() was compiled both with and without -fmalterlib-sized-destructors, and the link keeps a copy compiled without
# MIXED-DYLIB-NEXT: >>> defined in {{.*}}unflagged.o
# MIXED-DYLIB-NEXT: >>> marked in {{.*}}libflagged.dylib

## dyld coalesces weak definitions across images: a dylib's sites bind at
## runtime to the copy of the first image that defines it, this image's or
## another dylib's.
# RUN: llvm-mc -filetype=obj -triple=x86_64-apple-darwin %t/start.s -o %t/start.o
# RUN: %lld -dylib -lSystem %t/main.o %t/weak-flagged.o -o %t/libsite.dylib
# RUN: %lld -dylib -lSystem %t/weak-unflagged.o -o %t/libweak-unflagged.dylib
# RUN: %lld -lSystem %t/start.o %t/libsite.dylib -o /dev/null
# RUN: %lld -lSystem %t/start.o %t/weak-flagged.o %t/libsite.dylib -o /dev/null
# RUN: %lld -lSystem %t/start.o %t/libsite.dylib %t/libweak-unflagged.dylib -o /dev/null
# RUN: not %lld -demangle -lSystem %t/start.o %t/weak-unflagged.o %t/libsite.dylib -o /dev/null 2>&1 \
# RUN:   | FileCheck %s --check-prefix=COALESCE
# COALESCE:      error: Foreign::~Foreign() was compiled both with and without -fmalterlib-sized-destructors, and the link keeps a copy compiled without
# COALESCE-NEXT: >>> defined in {{.*}}weak-unflagged.o
# COALESCE-NEXT: >>> marked in {{.*}}libsite.dylib
# COALESCE-NEXT: >>> the construction sites of {{.*}}libsite.dylib bind to the copy the link keeps at runtime
## Dead stripping keeps what the dylib names.
# RUN: %lld -dylib -lSystem -dead_strip %t/main.o %t/weak-flagged.o -o %t/libsite-ds.dylib
# RUN: not %lld -demangle -lSystem %t/start.o %t/weak-unflagged.o %t/libsite-ds.dylib -o /dev/null 2>&1 \
# RUN:   | FileCheck %s --check-prefix=COALESCE-DS
# COALESCE-DS: error: Foreign::~Foreign() was compiled both with and without -fmalterlib-sized-destructors, and the link keeps a copy compiled without
## A dylib another re-exports is loaded with it.
# RUN: %lld -dylib -lSystem %t/start.o -reexport_library %t/libsite.dylib -o %t/libwrapper.dylib
# RUN: %lld -lSystem %t/start.o %t/libwrapper.dylib -o /dev/null
# RUN: not %lld -demangle -lSystem %t/start.o %t/weak-unflagged.o %t/libwrapper.dylib -o /dev/null 2>&1 \
# RUN:   | FileCheck %s --check-prefix=COALESCE-REEXPORT
# COALESCE-REEXPORT: error: Foreign::~Foreign() was compiled both with and without -fmalterlib-sized-destructors, and the link keeps a copy compiled without
## dyld loads the dylibs an image loads before those they re-export, whatever
## order the link parses them in: here the unflagged dylib before the one the
## umbrella re-exports.
# RUN: not %lld -demangle -lSystem %t/start.o %t/libwrapper.dylib %t/libweak-unflagged.dylib -o /dev/null 2>&1 \
# RUN:   | FileCheck %s --check-prefix=COALESCE-REEXPORT-ORDER
# COALESCE-REEXPORT-ORDER:      error: Foreign::~Foreign() was compiled both with and without -fmalterlib-sized-destructors, and the link keeps a copy compiled without
# COALESCE-REEXPORT-ORDER-NEXT: >>> defined in {{.*}}libweak-unflagged.dylib
# COALESCE-REEXPORT-ORDER-NEXT: >>> marked in {{.*}}libsite.dylib
## dyld loads what a dylib loads the first time it goes through that dylib,
## also one the image loads itself that an umbrella re-exports before another:
## here what the image's own copy of the re-exported dylib re-exports comes
## before what the umbrella re-exports after it.
# RUN: %lld -dylib -lSystem %t/weak-unflagged.o -o %t/libreexport-d-unflagged.dylib
# RUN: %lld -dylib -lSystem %t/weak-flagged.o -o %t/libreexport-e-flagged.dylib
# RUN: %lld -dylib -lSystem %t/start.o -reexport_library %t/libreexport-d-unflagged.dylib \
# RUN:   -o %t/libreexport-b-unflagged.dylib
# RUN: %lld -dylib -lSystem %t/start.o -reexport_library %t/libreexport-e-flagged.dylib \
# RUN:   -o %t/libreexport-c-flagged.dylib
# RUN: %lld -dylib -lSystem %t/start.o -reexport_library %t/libreexport-b-unflagged.dylib \
# RUN:   -reexport_library %t/libreexport-c-flagged.dylib -o %t/libreexport-a-unflagged-first.dylib
# RUN: not %lld -demangle -lSystem %t/main.o %t/libreexport-a-unflagged-first.dylib \
# RUN:   %t/libreexport-b-unflagged.dylib -o /dev/null 2>&1 | FileCheck %s --check-prefix=REEXPORT-EXPANDED
# REEXPORT-EXPANDED:      error: Foreign::~Foreign() was compiled both with and without -fmalterlib-sized-destructors, and the link keeps a copy compiled without
# REEXPORT-EXPANDED-NEXT: >>> defined in {{.*}}libreexport-d-unflagged.dylib
# REEXPORT-EXPANDED-NEXT: >>> marked in {{.*}}libreexport-e-flagged.dylib
# RUN: %lld -dylib -lSystem %t/weak-flagged.o -o %t/libreexport-d-flagged.dylib
# RUN: %lld -dylib -lSystem %t/weak-unflagged.o -o %t/libreexport-e-unflagged.dylib
# RUN: %lld -dylib -lSystem %t/start.o -reexport_library %t/libreexport-d-flagged.dylib \
# RUN:   -o %t/libreexport-b-flagged.dylib
# RUN: %lld -dylib -lSystem %t/start.o -reexport_library %t/libreexport-e-unflagged.dylib \
# RUN:   -o %t/libreexport-c-unflagged.dylib
# RUN: %lld -dylib -lSystem %t/start.o -reexport_library %t/libreexport-b-flagged.dylib \
# RUN:   -reexport_library %t/libreexport-c-unflagged.dylib -o %t/libreexport-a-flagged-first.dylib
# RUN: %lld -lSystem %t/main.o %t/libreexport-a-flagged-first.dylib %t/libreexport-b-flagged.dylib -o /dev/null
## A dylib an umbrella re-exports that the link names too has a load command of
## its own, where the link names it, before the dylibs named after it.
# RUN: %lld -dylib -lSystem %t/start.o -reexport_library %t/libweak-unflagged.dylib -o %t/libwrapper-unflagged.dylib
# RUN: not %lld -demangle -lSystem %t/start.o %t/libwrapper-unflagged.dylib %t/libweak-unflagged.dylib \
# RUN:   %t/libsite.dylib -o /dev/null 2>&1 | FileCheck %s --check-prefix=COALESCE-REEXPORT-ORDER
# RUN: %lld -lSystem %t/start.o %t/libwrapper.dylib %t/libsite.dylib %t/libweak-unflagged.dylib -o /dev/null
## Also of an umbrella that re-exports another umbrella before a dylib: dyld
## loads that dylib before what the other umbrella re-exports, which the
## umbrella's own sites then bind to.
# RUN: %lld -dylib -lSystem %t/start.o -reexport_library %t/libsite.dylib -o %t/libmid.dylib
# RUN: not %lld -demangle -dylib -lSystem %t/start.o -reexport_library %t/libmid.dylib \
# RUN:   -reexport_library %t/libweak-unflagged.dylib -o /dev/null 2>&1 \
# RUN:   | FileCheck %s --check-prefix=COALESCE-REEXPORT-ORDER
## The copy of a dylib that an umbrella re-exports is checked against the
## markers of that dylib, not of the others it re-exports, and binds from the
## first that has it.
# RUN: %lld -dylib -lSystem %t/start.o -reexport_library %t/libunflagged.dylib \
# RUN:   -reexport_library %t/libflagged.dylib -o %t/libwrapper-mixed.dylib
# RUN: %lld -dylib -lSystem %t/start.o -reexport_library %t/libflagged.dylib \
# RUN:   -reexport_library %t/libunflagged.dylib -o %t/libwrapper-flagged.dylib
# RUN: %lld -lSystem %t/main.o %t/libwrapper-flagged.dylib -o /dev/null
# RUN: not %lld -demangle -lSystem %t/main.o %t/libwrapper-mixed.dylib -o /dev/null 2>&1 \
# RUN:   | FileCheck %s --check-prefix=MIXED-REEXPORT
# MIXED-REEXPORT:      error: Foreign::~Foreign() was compiled both with and without -fmalterlib-sized-destructors, and the link keeps a copy compiled without
# MIXED-REEXPORT-NEXT: >>> defined in {{.*}}libunflagged.dylib
# MIXED-REEXPORT-NEXT: >>> marked in {{.*}}libflagged.dylib
## Also where the umbrella's own exports forward them to the dylibs it
## re-exports, which have them.
# RUN: %lld -dylib -lSystem %t/start.o -reexport_library %t/libunflagged.dylib \
# RUN:   -reexport_library %t/libflagged.dylib -exported_symbol '__ZN7Foreign*' -o %t/libwrapper-forwarded.dylib
# RUN: not %lld -demangle -lSystem %t/main.o %t/libwrapper-forwarded.dylib -o /dev/null 2>&1 \
# RUN:   | FileCheck %s --check-prefix=MIXED-REEXPORT
## Through nested umbrellas dyld looks in each re-exported dylib with those it
## re-exports in turn before the next, so the copy of the inner one binds.
# RUN: %lld -dylib -lSystem %t/start.o -reexport_library %t/libunflagged.dylib -o %t/libinner-unflagged.dylib
# RUN: %lld -dylib -lSystem %t/start.o -reexport_library %t/libinner-unflagged.dylib \
# RUN:   -reexport_library %t/libflagged.dylib -o %t/libouter-unflagged.dylib
# RUN: not %lld -demangle -lSystem %t/main.o %t/libouter-unflagged.dylib -o /dev/null 2>&1 \
# RUN:   | FileCheck %s --check-prefix=MIXED-REEXPORT
# RUN: %lld -dylib -lSystem %t/start.o -reexport_library %t/libflagged.dylib -o %t/libinner-flagged.dylib
# RUN: %lld -dylib -lSystem %t/start.o -reexport_library %t/libinner-flagged.dylib \
# RUN:   -reexport_library %t/libunflagged.dylib -o %t/libouter-flagged.dylib
# RUN: %lld -lSystem %t/main.o %t/libouter-flagged.dylib -o /dev/null
## An umbrella whose export trie forwards a symbol to a dylib it re-exports,
## the one whose copy is strong, binds there, also when another it re-exports
## first has a weak copy.
# RUN: %lld -dylib -lSystem %t/weak-flagged.o -o %t/libweak-flagged-forward.dylib
# RUN: %lld -dylib -lSystem %t/start.o -reexport_library %t/libweak-flagged-forward.dylib \
# RUN:   -reexport_library %t/libunflagged.dylib -exported_symbol __ZN7ForeignD0Ev -o %t/libforward.dylib
# RUN: llvm-objdump --macho --exports-trie %t/libforward.dylib | FileCheck %s --check-prefix=FORWARD-TRIE
# FORWARD-TRIE: [re-export] __ZN7ForeignD0Ev (from libunflagged)
# RUN: not %lld -demangle -lSystem %t/main.o %t/libforward.dylib -o /dev/null 2>&1 \
# RUN:   | FileCheck %s --check-prefix=FORWARD
# FORWARD:      error: Foreign::~Foreign() was compiled both with and without -fmalterlib-sized-destructors, and the link keeps a copy compiled without
# FORWARD-NEXT: >>> defined in {{.*}}libunflagged.dylib
## So does a façade whose export trie forwards a symbol to a dylib it only
## loads, which the link loads too, in either order.
# RUN: %lld -dylib -lSystem %t/start.o %t/libflagged.dylib -exported_symbol __ZN7ForeignD0Ev \
# RUN:   -o %t/libfacade.dylib
# RUN: llvm-objdump --macho --exports-trie %t/libfacade.dylib | FileCheck %s --check-prefix=FACADE-TRIE
# FACADE-TRIE: [re-export] __ZN7ForeignD0Ev (from libflagged)
# RUN: %lld -lSystem %t/main.o %t/libfacade.dylib %t/libflagged.dylib -o /dev/null
# RUN: %lld -lSystem %t/main.o %t/libflagged.dylib %t/libfacade.dylib -o /dev/null
# RUN: %lld -dylib -lSystem %t/start.o %t/libunflagged.dylib -exported_symbol __ZN7ForeignD0Ev \
# RUN:   -o %t/libfacade-unflagged.dylib
# RUN: not %lld -demangle -lSystem %t/main.o %t/libfacade-unflagged.dylib %t/libunflagged.dylib \
# RUN:   %t/libflagged.dylib -o /dev/null 2>&1 | FileCheck %s --check-prefix=FORWARD
## Also when the façade forwards the marker too, and the image loads that
## dylib only through it, with -dead_strip_dylibs.
# RUN: %lld -dylib -lSystem %t/start.o %t/libflagged.dylib -exported_symbol '__ZN7ForeignD0Ev*' \
# RUN:   -o %t/libfacade-marker.dylib
# RUN: %lld -lSystem -dead_strip_dylibs %t/main.o %t/libfacade-marker.dylib %t/libflagged.dylib -o %t/facade-only.out
# RUN: llvm-objdump --macho --dylibs-used %t/facade-only.out | FileCheck %s --check-prefix=FACADE-ONLY
# FACADE-ONLY-NOT: libflagged
## A dylib without sites names nothing its sites depend on.
# RUN: %lld -dylib -lSystem %t/weak-flagged.o -o %t/libweak-flagged.dylib
# RUN: %lld -lSystem %t/start.o %t/weak-unflagged.o %t/libweak-flagged.dylib -o /dev/null
## dyld loads a dylib another loads without re-exporting it too, in the order
## of that one's load commands, before what it re-exports after it, also when
## the image doesn't load it itself.
# RUN: %lld -dylib -lSystem %t/start.o %t/libweak-unflagged.dylib -reexport_library %t/libweak-flagged.dylib \
# RUN:   -o %t/libloads-unflagged.dylib
# RUN: llvm-objdump --macho --dylibs-used %t/libloads-unflagged.dylib | FileCheck %s --check-prefix=LOADS-ORDER
# LOADS-ORDER:      libweak-unflagged
# LOADS-ORDER-NEXT: libweak-flagged
# RUN: not %lld -demangle -lSystem -dead_strip_dylibs %t/main.o %t/libloads-unflagged.dylib \
# RUN:   %t/libweak-unflagged.dylib -o /dev/null 2>&1 | FileCheck %s --check-prefix=COALESCE-LOADS
# COALESCE-LOADS:      error: Foreign::~Foreign() was compiled both with and without -fmalterlib-sized-destructors, and the link keeps a copy compiled without
# COALESCE-LOADS-NEXT: >>> defined in {{.*}}libweak-unflagged.dylib
# RUN: %lld -dylib -lSystem %t/start.o -reexport_library %t/libweak-flagged.dylib %t/libweak-unflagged.dylib \
# RUN:   -o %t/libloads-unflagged-after.dylib
# RUN: %lld -lSystem -dead_strip_dylibs %t/main.o %t/libloads-unflagged-after.dylib \
# RUN:   %t/libweak-unflagged.dylib -o /dev/null
## A strong import binds to the dylib its link found it in, whatever another
## dylib of the same names that a later link loads defines.
# RUN: %lld -dylib -lSystem %t/main.o %t/libflagged.dylib -o %t/libfactory-strong.dylib
# RUN: %lld -lSystem %t/start.o %t/libfactory-strong.dylib %t/libunflagged.dylib -o /dev/null
## A dylib the image does not load requires nothing.
# RUN: %lld -lSystem -dead_strip_dylibs %t/start.o %t/weak-unflagged.o %t/libsite.dylib -o /dev/null
# RUN: not %lld -demangle -lSystem %t/start.o %t/libweak-unflagged.dylib %t/libsite.dylib -o /dev/null 2>&1 \
# RUN:   | FileCheck %s --check-prefix=COALESCE-DYLIB
# COALESCE-DYLIB:      error: Foreign::~Foreign() was compiled both with and without -fmalterlib-sized-destructors, and the link keeps a copy compiled without
# COALESCE-DYLIB-NEXT: >>> defined in {{.*}}libweak-unflagged.dylib
# COALESCE-DYLIB-NEXT: >>> marked in {{.*}}libsite.dylib
## A copy the image does not export does not take the dylib's place.
# RUN: %lld -lSystem -exported_symbol _main %t/start.o %t/weak-unflagged.o %t/libsite.dylib -o /dev/null
## The executable a bundle is loaded into, which dyld looks in first, takes the
## place of a dylib's copy too, also when it is weak.
# RUN: %lld -lSystem %t/start.o %t/weak-unflagged.o -o %t/host-unflagged
# RUN: %lld -lSystem -exported_symbol _main %t/start.o %t/weak-unflagged.o -o %t/host-hidden
# RUN: not %lld -demangle -bundle -lSystem -bundle_loader %t/host-unflagged %t/start.o %t/libsite.dylib \
# RUN:   -o /dev/null 2>&1 | FileCheck %s --check-prefix=COALESCE-BUNDLE
# COALESCE-BUNDLE:      error: Foreign::~Foreign() was compiled both with and without -fmalterlib-sized-destructors, and the link keeps a copy compiled without
# COALESCE-BUNDLE-NEXT: >>> defined in {{.*}}host-unflagged
# COALESCE-BUNDLE-NEXT: >>> marked in {{.*}}libsite.dylib
# RUN: %lld -bundle -lSystem -bundle_loader %t/host-hidden %t/start.o %t/libsite.dylib -o /dev/null
## So does its weak copy of one the bundle defines too, which dyld finds before
## the bundle's.
# RUN: not %lld -demangle -bundle -lSystem -bundle_loader %t/host-unflagged %t/main.o %t/weak-flagged.o \
# RUN:   -o /dev/null 2>&1 | FileCheck %s --check-prefix=HOST-WEAK
# HOST-WEAK:      error: Foreign::~Foreign() was not compiled with -fmalterlib-sized-destructors
# HOST-WEAK-NEXT: >>> defined in {{.*}}host-unflagged
# RUN: %lld -bundle -lSystem -bundle_loader %t/host-hidden %t/main.o %t/weak-flagged.o -o /dev/null
## Of a strong one the bundle defines that a dylib binds by weak lookup, a
## strong copy of the executable flagged with MH_WEAK_DEFINES, which dyld finds
## first, takes the place.
# RUN: llvm-mc -filetype=obj -triple=x86_64-apple-darwin %t/weak-other.s -o %t/weak-other.o
# RUN: %lld -lSystem %t/start.o %t/unflagged.o %t/weak-other.o -o %t/host-strong-weakdefines
# RUN: not %lld -demangle -bundle -lSystem -bundle_loader %t/host-strong-weakdefines %t/start.o \
# RUN:   %t/flagged.o %t/libsite.dylib -o /dev/null 2>&1 | FileCheck %s --check-prefix=HOST-STRONG
# HOST-STRONG:      error: Foreign::~Foreign() was compiled both with and without -fmalterlib-sized-destructors, and the link keeps a copy compiled without
# HOST-STRONG-NEXT: >>> defined in {{.*}}host-strong-weakdefines
# RUN: %lld -lSystem %t/start.o %t/unflagged.o -o %t/host-strong
# RUN: %lld -bundle -lSystem -bundle_loader %t/host-strong %t/start.o %t/flagged.o %t/libsite.dylib -o /dev/null
## Of any one a bundle linked with -flat_namespace binds, the copy of the
## executable, which dyld looks in first.
# RUN: not %lld -demangle -bundle -lSystem -flat_namespace -bundle_loader %t/host-strong %t/main.o \
# RUN:   %t/flagged.o -o /dev/null 2>&1 | FileCheck %s --check-prefix=HOST-FLAT
# HOST-FLAT:      error: Foreign::~Foreign() was not compiled with -fmalterlib-sized-destructors
# HOST-FLAT-NEXT: >>> defined in {{.*}}host-strong
# RUN: %lld -bundle -lSystem -bundle_loader %t/host-strong %t/main.o %t/flagged.o -o /dev/null
## So of its own exported ones a bundle linked with -interposable binds, which
## it binds in a flat namespace.
# RUN: not %lld -demangle -bundle -lSystem -interposable -bundle_loader %t/host-strong %t/main.o \
# RUN:   %t/flagged.o -o /dev/null 2>&1 | FileCheck %s --check-prefix=HOST-FLAT
## So do the dylibs it loads, which are there before those of the bundle, also
## one the bundle names after another.
# RUN: %lld -lSystem %t/start.o %t/libweak-unflagged.dylib -o %t/host-loads-unflagged
# RUN: %lld -lSystem %t/start.o -o %t/host-plain
# RUN: not %lld -demangle -bundle -lSystem -bundle_loader %t/host-loads-unflagged %t/start.o %t/libsite.dylib \
# RUN:   %t/libweak-unflagged.dylib -o /dev/null 2>&1 | FileCheck %s --check-prefix=COALESCE-HOST-LOADS
# COALESCE-HOST-LOADS:      error: Foreign::~Foreign() was compiled both with and without -fmalterlib-sized-destructors, and the link keeps a copy compiled without
# COALESCE-HOST-LOADS-NEXT: >>> defined in {{.*}}libweak-unflagged.dylib
# COALESCE-HOST-LOADS-NEXT: >>> marked in {{.*}}libsite.dylib
## Also one the bundle doesn't load itself, with -dead_strip_dylibs.
# RUN: not %lld -demangle -bundle -lSystem -dead_strip_dylibs -bundle_loader %t/host-loads-unflagged \
# RUN:   %t/main.o %t/libweak-flagged.dylib %t/libweak-unflagged.dylib -o /dev/null 2>&1 \
# RUN:   | FileCheck %s --check-prefix=COALESCE-HOST-LOADS-DS
# COALESCE-HOST-LOADS-DS:      error: Foreign::~Foreign() was compiled both with and without -fmalterlib-sized-destructors, and the link keeps a copy compiled without
# COALESCE-HOST-LOADS-DS-NEXT: >>> defined in {{.*}}libweak-unflagged.dylib
# RUN: %lld -bundle -lSystem -dead_strip_dylibs -bundle_loader %t/host-plain %t/main.o %t/libweak-flagged.dylib \
# RUN:   %t/libweak-unflagged.dylib -o /dev/null
# RUN: %lld -bundle -lSystem -bundle_loader %t/host-plain %t/start.o %t/libsite.dylib \
# RUN:   %t/libweak-unflagged.dylib -o /dev/null
## So do the dylibs it re-exports, which it loads before the bundle.
# RUN: %lld -lSystem %t/start.o -reexport_library %t/libweak-unflagged.dylib -o %t/host-reexport
# RUN: not %lld -demangle -bundle -lSystem -bundle_loader %t/host-reexport %t/start.o %t/libsite.dylib \
# RUN:   -o /dev/null 2>&1 | FileCheck %s --check-prefix=COALESCE-DYLIB
## An executable names what its own sites depend on that a bundle loaded into
## it may take the place of, a weak definition it exports, which the link of the
## bundle checks against its copies; not one it does not export.
# RUN: llvm-mc -filetype=obj -triple=x86_64-apple-darwin %t/dtor-strong-weakdefines.s -o %t/dtor-strong-weakdefines.o
# RUN: %lld -lSystem %t/site-weak.o -o %t/host-site
# RUN: llvm-nm -g %t/host-site | FileCheck %s --check-prefix=HOST-REQUIRED
# HOST-REQUIRED: __ZN7ForeignD0Ev.mib_sized_required
# RUN: not %lld -demangle -bundle -lSystem -bundle_loader %t/host-site %t/dtor-strong-weakdefines.o \
# RUN:   -o /dev/null 2>&1 | FileCheck %s --check-prefix=HOST-SITE
# HOST-SITE:      error: Foreign::~Foreign() was compiled both with and without -fmalterlib-sized-destructors, and the link keeps a copy compiled without
# HOST-SITE-NEXT: >>> defined in {{.*}}dtor-strong-weakdefines.o
# HOST-SITE-NEXT: >>> marked in {{.*}}host-site
# HOST-SITE-NEXT: >>> the construction sites of {{.*}}host-site bind to the copy the link keeps at runtime
# RUN: %lld -bundle -lSystem -bundle_loader %t/host-site %t/weak-flagged.o -o /dev/null
# RUN: %lld -lSystem -exported_symbol _main %t/site-weak.o -o %t/host-site-hidden
# RUN: llvm-nm -g %t/host-site-hidden | FileCheck %s --check-prefix=HOST-HIDDEN
# HOST-HIDDEN-NOT: mib_sized_required
# RUN: %lld -bundle -lSystem -bundle_loader %t/host-site-hidden %t/dtor-strong-weakdefines.o -o /dev/null

## The marker of a constructor proves its claim only when the vtable it
## installs in its image was compiled with the flag. A dylib whose constructor
## installs its own vtable links, but does not export the marker it cannot
## prove; a site elsewhere then fails.
# RUN: llvm-mc -filetype=obj -triple=x86_64-apple-darwin %t/ctor.s -o %t/ctor.o
# RUN: llvm-mc -filetype=obj -triple=x86_64-apple-darwin %t/vtable-flagged.s -o %t/vtable-flagged.o
# RUN: llvm-mc -filetype=obj -triple=x86_64-apple-darwin %t/vtable-unflagged.s -o %t/vtable-unflagged.o
# RUN: llvm-mc -filetype=obj -triple=x86_64-apple-darwin %t/ctor-site.s -o %t/ctor-site.o
# RUN: %lld -dylib -lSystem %t/ctor.o %t/vtable-flagged.o %t/flagged.o -o %t/libctor-flagged.dylib
# RUN: %lld -dylib -lSystem %t/ctor.o %t/vtable-unflagged.o %t/flagged.o -o %t/libctor-unflagged.dylib
# RUN: %lld -lSystem %t/ctor-site.o %t/libctor-flagged.dylib -o /dev/null
# RUN: not %lld -demangle -lSystem %t/ctor-site.o %t/libctor-unflagged.dylib -o /dev/null 2>&1 | FileCheck %s --check-prefix=CTOR
# CTOR: error: Foreign::Foreign() was not compiled with -fmalterlib-sized-destructors
## A dylib whose sites import a strong constructor in its two-level namespace
## binds it to the dylib it loads that has it, by the ordinal its link
## recorded, which in the link of an image that loads it can be another file
## of that install name, like one rebuilt without the flag: that link checks
## the copy of the file it has, and the proof of it.
# RUN: %lld -dylib -lSystem %t/ctor.o %t/vtable-flagged.o %t/flagged.o -install_name @rpath/libctor-bound.dylib \
# RUN:   -o %t/libctor-bound-flagged.dylib
# RUN: %lld -dylib -lSystem %t/ctor.o %t/vtable-unflagged.o %t/flagged.o -install_name @rpath/libctor-bound.dylib \
# RUN:   -o %t/libctor-bound-unflagged.dylib
# RUN: %lld -dylib -lSystem %t/ctor-site.o %t/libctor-bound-flagged.dylib -o %t/libfactory-bound.dylib
# RUN: llvm-nm -g %t/libfactory-bound.dylib | FileCheck %s --check-prefix=BOUND-EXPORT
# BOUND-EXPORT: __ZN7ForeignC1Ev.mib_sized_required_bound
# RUN: %lld -lSystem %t/start.o %t/libfactory-bound.dylib %t/libctor-bound-flagged.dylib -o /dev/null
# RUN: not %lld -demangle -lSystem %t/start.o %t/libfactory-bound.dylib %t/libctor-bound-unflagged.dylib \
# RUN:   -o /dev/null 2>&1 | FileCheck %s --check-prefix=BOUND
# RUN: not %lld -demangle -lSystem %t/start.o %t/libfactory-bound.dylib %t/libctor-bound-unflagged.dylib \
# RUN:   %t/libctor-bound-flagged.dylib -o /dev/null 2>&1 | FileCheck %s --check-prefix=BOUND
# BOUND:      error: Foreign::Foreign() was not compiled with -fmalterlib-sized-destructors
# BOUND-NEXT: >>> defined in {{.*}}libctor-bound-unflagged.dylib
# BOUND-NEXT: >>> the construction sites of {{.*}}libfactory-bound.dylib bind to the copy the link keeps at runtime
# RUN: %lld -lSystem %t/start.o %t/libfactory-bound.dylib %t/libctor-bound-flagged.dylib \
# RUN:   %t/libctor-bound-unflagged.dylib -o /dev/null
## Nothing where the link doesn't have that dylib.
# RUN: %lld -lSystem %t/start.o %t/libfactory-bound.dylib -o /dev/null
## Also where the image's own copy is one LTO removed or internalized, like a
## hidden constructor, which takes no place of that dylib's.
# RUN: llvm-as %t/hidden-ctor.ll -o %t/hidden-ctor-bound.o
# RUN: %lld -dylib -lSystem %t/hidden-ctor-bound.o %t/libfactory-bound.dylib %t/libctor-bound-flagged.dylib \
# RUN:   -o /dev/null
# RUN: not %lld -dylib -demangle -lSystem %t/hidden-ctor-bound.o %t/libfactory-bound.dylib \
# RUN:   %t/libctor-bound-unflagged.dylib -o /dev/null 2>&1 | FileCheck %s --check-prefix=BOUND
# RUN: not %lld -demangle -lSystem %t/ctor-site.o %t/ctor.o %t/vtable-unflagged.o %t/flagged.o -o /dev/null 2>&1 \
# RUN:   | FileCheck %s --check-prefix=CHAIN
# CHAIN:      error: Foreign::Foreign() depends on vtable for Foreign
# CHAIN-NEXT: >>> vtable for Foreign was not compiled with -fmalterlib-sized-destructors
## The dependencies of a copy of an owner the link did not keep are not those
## of the copy it kept.
# RUN: llvm-mc -filetype=obj -triple=x86_64-apple-darwin %t/ctor-weak.s -o %t/ctor-weak.o
# RUN: llvm-mc -filetype=obj -triple=x86_64-apple-darwin %t/ctor-strong.s -o %t/ctor-strong.o
# RUN: not %lld -demangle -lSystem %t/ctor-site.o %t/ctor-weak.o %t/vtable-unflagged.o %t/flagged.o -o /dev/null 2>&1 \
# RUN:   | FileCheck %s --check-prefix=CHAIN
# RUN: %lld -lSystem %t/ctor-site.o %t/ctor-weak.o %t/ctor-strong.o %t/vtable-unflagged.o %t/flagged.o -o /dev/null

## A dylib whose sites construct a class another dylib defines names that
## class's constructor, and that dylib names what the proof of its constructor
## depends on that dyld may coalesce. An image whose weak copy takes the place
## of one of those fails, though neither dylib has anything to check in its
## own link.
# RUN: llvm-mc -filetype=obj -triple=x86_64-apple-darwin %t/vtable-weak-flagged.s -o %t/vtable-weak-flagged.o
# RUN: llvm-mc -filetype=obj -triple=x86_64-apple-darwin %t/vtable-weak-unflagged.s -o %t/vtable-weak-unflagged.o
# RUN: %lld -dylib -lSystem %t/ctor.o %t/vtable-weak-flagged.o %t/flagged.o -o %t/libclass.dylib
# RUN: %lld -dylib -lSystem %t/ctor-site.o %t/libclass.dylib -o %t/libfactory.dylib
# RUN: %lld -lSystem %t/start.o %t/libfactory.dylib %t/libclass.dylib -o /dev/null
# RUN: %lld -lSystem %t/start.o %t/vtable-weak-unflagged.o %t/libclass.dylib -o /dev/null
# RUN: not %lld -demangle -lSystem %t/start.o %t/vtable-weak-unflagged.o %t/libfactory.dylib %t/libclass.dylib \
# RUN:   -o /dev/null 2>&1 | FileCheck %s --check-prefix=CHAIN-DYLIB
# CHAIN-DYLIB:      error: vtable for Foreign was compiled both with and without -fmalterlib-sized-destructors, and the link keeps a copy compiled without
# CHAIN-DYLIB-NEXT: >>> defined in {{.*}}vtable-weak-unflagged.o
# CHAIN-DYLIB-NEXT: >>> marked in {{.*}}libclass.dylib
# CHAIN-DYLIB-NEXT: >>> the construction sites of {{.*}}libfactory.dylib bind to the copy the link keeps at runtime
## A dylib whose sites construct a class it defines with a weak constructor
## names only the constructor. A copy of the constructor that an image keeps
## private takes no place of the dylib's, and what the dylib's copy depends on
## is checked as if the image had none.
# RUN: llvm-mc -filetype=obj -triple=x86_64-apple-darwin %t/ctor-hidden.s -o %t/ctor-hidden.o
# RUN: %lld -dylib -lSystem %t/ctor-site.o %t/ctor-weak.o %t/vtable-weak-flagged.o %t/flagged.o -o %t/libsiteclass.dylib
# RUN: %lld -dylib -lSystem %t/ctor-hidden.o %t/libsiteclass.dylib -o /dev/null
# RUN: not %lld -dylib -demangle -lSystem %t/ctor-hidden.o %t/vtable-weak-unflagged.o %t/libsiteclass.dylib \
# RUN:   -o /dev/null 2>&1 | FileCheck %s --check-prefix=HIDDEN-CTOR
# HIDDEN-CTOR:      error: vtable for Foreign was compiled both with and without -fmalterlib-sized-destructors, and the link keeps a copy compiled without
# HIDDEN-CTOR-NEXT: >>> defined in {{.*}}vtable-weak-unflagged.o
# HIDDEN-CTOR-NEXT: >>> marked in {{.*}}libsiteclass.dylib
# HIDDEN-CTOR-NEXT: >>> the construction sites of {{.*}}libsiteclass.dylib bind to the copy the link keeps at runtime
## Behind a copy the image keeps private, the copy of the first dylib loaded
## that exports it is the one dyld coalesces to, which must carry the marker.
# RUN: llvm-mc -filetype=obj -triple=x86_64-apple-darwin %t/ctor-weak-unflagged.s -o %t/ctor-weak-unflagged.o
# RUN: %lld -dylib -lSystem %t/ctor-weak-unflagged.o -o %t/libctorunflagged.dylib
# RUN: %lld -dylib -lSystem %t/ctor-hidden.o %t/libsiteclass.dylib %t/libctorunflagged.dylib -o /dev/null
# RUN: not %lld -dylib -demangle -lSystem %t/ctor-hidden.o %t/libctorunflagged.dylib %t/libsiteclass.dylib \
# RUN:   -o /dev/null 2>&1 | FileCheck %s --check-prefix=HIDDEN-FIRST
# HIDDEN-FIRST:      error: Foreign::Foreign() was compiled both with and without -fmalterlib-sized-destructors, and the link keeps a copy compiled without
# HIDDEN-FIRST-NEXT: >>> defined in {{.*}}libctorunflagged.dylib
# HIDDEN-FIRST-NEXT: >>> marked in {{.*}}libsiteclass.dylib
# HIDDEN-FIRST-NEXT: >>> the construction sites of {{.*}}libsiteclass.dylib bind to the copy the link keeps at runtime
## dyld coalesces a weak definition to a strong one, also of a dylib loaded
## after the first that exports it weak.
# RUN: llvm-mc -filetype=obj -triple=x86_64-apple-darwin %t/ctor-strong-unflagged.s -o %t/ctor-strong-unflagged.o
# RUN: %lld -dylib -lSystem %t/ctor-strong-unflagged.o -o %t/libctorstrong.dylib
# RUN: not %lld -dylib -demangle -lSystem %t/ctor-hidden.o %t/libsiteclass.dylib %t/libctorstrong.dylib \
# RUN:   -o /dev/null 2>&1 | FileCheck %s --check-prefix=HIDDEN-STRONG
# HIDDEN-STRONG:      error: Foreign::Foreign() was compiled both with and without -fmalterlib-sized-destructors, and the link keeps a copy compiled without
# HIDDEN-STRONG-NEXT: >>> defined in {{.*}}libctorstrong.dylib
# HIDDEN-STRONG-NEXT: >>> marked in {{.*}}libsiteclass.dylib
## dyld coalesces only in the images flagged with MH_WEAK_DEFINES: the strong
## copy of a dylib without weak definitions takes no place of a weak one.
# RUN: llvm-mc -filetype=obj -triple=x86_64-apple-darwin %t/ctor-strong-only.s -o %t/ctor-strong-only.o
# RUN: %lld -dylib -lSystem %t/ctor-strong-only.o -o %t/libctorstrongonly.dylib
## What a dylib requires binds by name to the copy dyld coalesces to, not to
## the strong copy of a dylib without weak definitions that the link binds.
# RUN: not %lld -dylib -demangle -lSystem %t/start.o %t/libctorunflagged.dylib %t/libsiteclass.dylib \
# RUN:   %t/libctor-flagged.dylib -o /dev/null 2>&1 | FileCheck %s --check-prefix=COALESCE-NOT-STRONG
# COALESCE-NOT-STRONG:      error: Foreign::Foreign() was compiled both with and without -fmalterlib-sized-destructors, and the link keeps a copy compiled without
# COALESCE-NOT-STRONG-NEXT: >>> defined in {{.*}}libctorunflagged.dylib
# COALESCE-NOT-STRONG-NEXT: >>> marked in {{.*}}libsiteclass.dylib
# RUN: %lld -dylib -lSystem %t/start.o %t/libsiteclass.dylib %t/libctorstrongonly.dylib -o /dev/null
## But a stub's, which can't tell whether its dylib is flagged with
## MH_WEAK_DEFINES, as one that exports only strong definitions is where they
## take the place of weak ones of a dylib it links, is taken as flagged.
# RUN: not %lld -dylib -demangle -lSystem %t/start.o %t/libsiteclass.dylib %t/libctorstrongstub.tbd \
# RUN:   -o /dev/null 2>&1 | FileCheck %s --check-prefix=STUB-STRONG
# STUB-STRONG:      error: Foreign::Foreign() was compiled both with and without -fmalterlib-sized-destructors, and the link keeps a copy compiled without
# STUB-STRONG-NEXT: >>> defined in {{.*}}libctorstrongstub.tbd
# STUB-STRONG-NEXT: >>> marked in {{.*}}libsiteclass.dylib
# STUB-STRONG-NEXT: >>> {{.*}}libctorstrongstub.tbd{{.*}} is a text stub, which doesn't tell whether its dylib is flagged with MH_WEAK_DEFINES, where dyld coalesces weak definitions to its strong ones, so it is taken to be; link the dylib itself to check what it does
## A marker a dylib exports at another address than its definition's, which
## a linker that doesn't check them can leave of a copy it didn't keep,
## proves nothing.
# RUN: yaml2obj %t/misplaced-marker.yaml -o %t/libmisplaced.dylib
# RUN: not %lld -dylib -demangle -lSystem %t/ctor-site.o %t/libmisplaced.dylib -o /dev/null 2>&1 \
# RUN:   | FileCheck %s --check-prefix=MISPLACED
# MISPLACED:      error: Foreign::Foreign() was compiled both with and without -fmalterlib-sized-destructors, and the link keeps a copy compiled without
# MISPLACED-NEXT: >>> defined in {{.*}}libmisplaced.dylib
# MISPLACED-NEXT: >>> its marker __ZN7ForeignC1Ev.mib_sized in {{.*}}libmisplaced.dylib is at another address, of a copy that dylib's link didn't keep
## A weak definition this image imports binds to the copy dyld coalesces to,
## also another dylib's than the one an umbrella the link bound it to
## re-exports, like a flagged one this image loads before the umbrella's.
# RUN: %lld -dylib -lSystem %t/ctor-weak-unflagged.o -install_name %t/libbadweak.dylib -o %t/libbadweak.dylib
# RUN: %lld -dylib -lSystem %t/start.o -reexport_library %t/libbadweak.dylib -install_name %t/libumbbad.dylib \
# RUN:   -o %t/libumbbad.dylib
# RUN: %lld -dylib -lSystem %t/ctor-weak.o %t/vtable-weak-flagged.o %t/weak-flagged.o \
# RUN:   -install_name %t/libgoodweak.dylib -o %t/libgoodweak.dylib
# RUN: %lld -dylib -lSystem %t/ctor-site.o %t/libumbbad.dylib %t/libgoodweak.dylib -o /dev/null
# RUN: not %lld -dylib -lSystem %t/ctor-site.o %t/libbadweak.dylib %t/libgoodweak.dylib -o /dev/null
## What the proof of a dylib's definition depends on that the dylib binds by
## weak lookup, a weak definition of another dylib, which a strong one of its
## own doesn't take the place of where it isn't flagged with MH_WEAK_DEFINES,
## like after LTO, is followed by weak lookup by the links of the images that
## load it.
# RUN: llvm-as %t/lto-unflagged.ll -o %t/lto-unflagged-legacy.o
# RUN: %lld -dylib -lSystem %t/vtable-flagged.o %t/weak-flagged.o -install_name %t/libvtablew.dylib \
# RUN:   -o %t/libvtablew.dylib
# RUN: %lld -dylib -lSystem %t/ctor.o %t/lto-unflagged-legacy.o %t/libvtablew.dylib \
# RUN:   -install_name %t/libctorlegacy.dylib -o %t/libctorlegacy.dylib
# RUN: llvm-nm -g %t/libctorlegacy.dylib | FileCheck %s --check-prefix=WEAK-EDGE
# WEAK-EDGE: __ZN7ForeignD0Ev.mib_sized_edge_weak.
# RUN: %lld -dylib -lSystem %t/ctor-site.o %t/libctorlegacy.dylib %t/libvtablew.dylib -o /dev/null
## A weak definition -dead_strip removes, like a constructor a site inlined,
## takes no place, but what it depends on that the site binds, like the
## vtable it installs and that one's deleting destructor, which a dylib's
## strong copy can take the place of.
# RUN: llvm-mc -filetype=obj -triple=x86_64-apple-darwin %t/ctor-site-inlined.s -o %t/ctor-site-inlined.o
# RUN: %lld -dylib -lSystem %t/dtor-strong-weakdefines.o -o %t/libdtorstrongwd.dylib
# RUN: not %lld -demangle -lSystem -dead_strip %t/ctor-site-inlined.o %t/ctor-weak.o %t/vtable-weak-flagged.o \
# RUN:   %t/weak-flagged.o %t/libdtorstrongwd.dylib -o /dev/null 2>&1 | FileCheck %s --check-prefix=STRIPPED-CTOR
# STRIPPED-CTOR:      error: Foreign::~Foreign() was not compiled with -fmalterlib-sized-destructors
# STRIPPED-CTOR-NEXT: >>> defined in {{.*}}libdtorstrongwd.dylib
## A dylib's import of a dylib whose proof binds what it depends on by name,
## where it looks at runtime, linked with -flat_namespace, also when nothing
## here defines it, like a hidden constructor LTO removed, or linked with
## -interposable, whose strong definitions its own references bind so, binds
## to that dylib's copy, whose proof the images that load it follow.
# RUN: llvm-as %t/hidden-ctor.ll -o %t/hidden-ctor.o
# RUN: %lld -dylib -lSystem -flat_namespace %t/ctor.o %t/vtable-flagged.o %t/flagged.o \
# RUN:   -install_name %t/libflatclass.dylib -o %t/libflatclass.dylib
# RUN: %lld -dylib -lSystem %t/ctor-site.o %t/libflatclass.dylib -install_name %t/libflatfactory.dylib \
# RUN:   -o %t/libflatfactory.dylib
# RUN: not %lld -dylib -demangle -lSystem %t/hidden-ctor.o %t/vtable-unflagged.o %t/unflagged.o \
# RUN:   %t/libflatfactory.dylib %t/libflatclass.dylib -o /dev/null 2>&1 | FileCheck %s --check-prefix=BY-NAME-FLAT
# BY-NAME-FLAT:      error: vtable for Foreign was compiled both with and without -fmalterlib-sized-destructors, and the link keeps a copy compiled without
# BY-NAME-FLAT-NEXT: >>> defined in {{.*}}vtable-unflagged.o
# BY-NAME-FLAT-NEXT: >>> marked in {{.*}}libflatclass.dylib
# BY-NAME-FLAT-NEXT: >>> the construction sites of {{.*}}libflatfactory.dylib bind to the copy the link keeps at runtime
# RUN: %lld -dylib -lSystem -interposable %t/ctor.o %t/vtable-flagged.o %t/flagged.o \
# RUN:   -install_name %t/libipclass.dylib -o %t/libipclass.dylib
# RUN: %lld -dylib -lSystem %t/ctor-site.o %t/libipclass.dylib -install_name %t/libipfactory.dylib \
# RUN:   -o %t/libipfactory.dylib
# RUN: llvm-nm -g %t/libipfactory.dylib | FileCheck %s --check-prefix=BY-NAME-REQUIRED
# BY-NAME-REQUIRED: __ZN7ForeignC1Ev.mib_sized_required
# RUN: not %lld -dylib -demangle -lSystem %t/vtable-unflagged.o %t/unflagged.o %t/libipfactory.dylib \
# RUN:   %t/libipclass.dylib -o /dev/null 2>&1 | FileCheck %s --check-prefix=BY-NAME-INTERPOSABLE
# BY-NAME-INTERPOSABLE:      error: vtable for Foreign was compiled both with and without -fmalterlib-sized-destructors, and the link keeps a copy compiled without
# BY-NAME-INTERPOSABLE-NEXT: >>> defined in {{.*}}vtable-unflagged.o
# BY-NAME-INTERPOSABLE-NEXT: >>> marked in {{.*}}libipclass.dylib
# BY-NAME-INTERPOSABLE-NEXT: >>> the construction sites of {{.*}}libipfactory.dylib bind to the copy the link keeps at runtime
# RUN: %lld -dylib -lSystem %t/vtable-flagged.o %t/flagged.o %t/libipfactory.dylib %t/libipclass.dylib -o /dev/null
## A dylib's strong copy that takes the place of this image's leaves what
## this image's copy depends on to check, which a site that inlined it binds,
## like the deleting destructor of the vtable it installs.
# RUN: %lld -dylib -lSystem %t/ctor-strong.o %t/weak-other.o -install_name %t/libctorgoodstrong.dylib \
# RUN:   -o %t/libctorgoodstrong.dylib
# RUN: %lld -dylib -lSystem %t/dtor-strong-weakdefines.o -install_name %t/libdtorbadwd.dylib -o %t/libdtorbadwd.dylib
# RUN: not %lld -dylib -demangle -lSystem %t/ctor-site.o %t/ctor-weak.o %t/vtable-weak-flagged.o %t/weak-flagged.o \
# RUN:   %t/libctorgoodstrong.dylib %t/libdtorbadwd.dylib -o /dev/null 2>&1 | FileCheck %s --check-prefix=REPLACED-OWN
# REPLACED-OWN:      error: Foreign::~Foreign() was not compiled with -fmalterlib-sized-destructors
# REPLACED-OWN-NEXT: >>> defined in {{.*}}libdtorbadwd.dylib
## What a dylib requires of its own weak definitions binds by weak lookup, which
## a dylib that gives it on with -flat_namespace says, so a strong copy of a
## dylib without MH_WEAK_DEFINES takes no place of it.
# RUN: %lld -dylib -lSystem %t/ctor-site.o %t/ctor-weak.o %t/vtable-weak-flagged.o %t/weak-flagged.o \
# RUN:   -install_name %t/libfactoryw.dylib -o %t/libfactoryw.dylib
# RUN: llvm-nm -g %t/libfactoryw.dylib | FileCheck %s --check-prefix=OWN-WEAK
# OWN-WEAK: __ZTV7Foreign.mib_sized_required_weak
# RUN: %lld -dylib -lSystem -flat_namespace -undefined dynamic_lookup %t/start.o %t/libfactoryw.dylib \
# RUN:   -install_name %t/libmiddleflat.dylib -o %t/libmiddleflat.dylib
# RUN: %lld -dylib -lSystem %t/vtable-unflagged.o %t/unflagged.o -install_name %t/libordinary.dylib \
# RUN:   -o %t/libordinary.dylib
# RUN: %lld -dylib -lSystem %t/libordinary.dylib %t/libmiddleflat.dylib -o /dev/null
## Also when the image has a flat namespace: what a dylib requires binds in
## its own two-level namespace.
# RUN: %lld -dylib -flat_namespace -lSystem %t/start.o %t/libsiteclass.dylib %t/libctorstrongonly.dylib \
# RUN:   -o /dev/null
## So does what the proof of a dylib's owner that this image's sites import
## depends on: the strong copy of a dylib without MH_WEAK_DEFINES takes no
## place of the weak one dyld coalesces the owner's dylib's binding to.
# RUN: %lld -dylib -lSystem %t/vtable-weak-unflagged.o %t/weak-unflagged.o -o %t/libvtableweakbad.dylib
# RUN: %lld -dylib -lSystem %t/vtable-flagged.o %t/flagged.o -o %t/libvtablestrong.dylib
# RUN: not %lld -dylib -demangle -flat_namespace -lSystem %t/ctor-site.o %t/libvtableweakbad.dylib \
# RUN:   %t/libclass.dylib %t/libvtablestrong.dylib -o /dev/null 2>&1 | FileCheck %s --check-prefix=FLAT-EDGE
# FLAT-EDGE:      error: vtable for Foreign was compiled both with and without -fmalterlib-sized-destructors, and the link keeps a copy compiled without
# FLAT-EDGE-NEXT: >>> defined in {{.*}}libvtableweakbad.dylib
# FLAT-EDGE-NEXT: >>> marked in {{.*}}libclass.dylib
## Also when the image's own sites bind the strong copy of another dylib by
## name of the same definition, which is another copy.
# RUN: not %lld -dylib -demangle -lSystem %t/ctor-site.o %t/libctor-flagged.dylib %t/libctorunflagged.dylib \
# RUN:   %t/libsiteclass.dylib -o /dev/null 2>&1 | FileCheck %s --check-prefix=COALESCE-BESIDE-STRONG
# COALESCE-BESIDE-STRONG:      error: Foreign::Foreign() was compiled both with and without -fmalterlib-sized-destructors, and the link keeps a copy compiled without
# COALESCE-BESIDE-STRONG-NEXT: >>> defined in {{.*}}libctorunflagged.dylib
# COALESCE-BESIDE-STRONG-NEXT: >>> marked in {{.*}}libctor-flagged.dylib
# COALESCE-BESIDE-STRONG-NEXT: >>> the construction sites of {{.*}}libsiteclass.dylib bind to the copy the link keeps at runtime
## Of dylibs with one install name, dyld loads the first, whatever the link
## binds in the others.
# RUN: %lld -dylib -lSystem %t/ctor-weak-unflagged.o -install_name @rpath/libdup.dylib -o %t/libdupweak.dylib
# RUN: %lld -dylib -lSystem %t/ctor.o %t/vtable-flagged.o %t/flagged.o -install_name @rpath/libdup.dylib \
# RUN:   -o %t/libdupstrong.dylib
# RUN: not %lld -dylib -demangle -lSystem %t/ctor-site.o %t/libdupweak.dylib %t/libdupstrong.dylib \
# RUN:   -o /dev/null 2>&1 | FileCheck %s --check-prefix=SAME-INSTALL-NAME
# SAME-INSTALL-NAME:      error: Foreign::Foreign() was compiled both with and without -fmalterlib-sized-destructors, and the link keeps a copy compiled without
# SAME-INSTALL-NAME-NEXT: >>> defined in {{.*}}libdupweak.dylib
# SAME-INSTALL-NAME-NEXT: >>> marked in {{.*}}libdupstrong.dylib
# RUN: %lld -dylib -lSystem %t/ctor-site.o %t/libdupstrong.dylib %t/libdupweak.dylib -o /dev/null
## Not the first when the image doesn't load it, like an older copy without
## the definition with -dead_strip_dylibs.
# RUN: %lld -dylib -lSystem %t/start.o -install_name @rpath/libdup.dylib -o %t/libdupold.dylib
# RUN: %lld -dylib -lSystem -dead_strip_dylibs %t/ctor-site.o %t/libdupold.dylib %t/libdupstrong.dylib \
# RUN:   -o /dev/null
## Also one an umbrella re-exports: a copy of it by another path that the
## image loads, which fails as the copy, and a rebuilt dylib of its install
## name that the image loads first, which dyld finds instead.
# RUN: mkdir -p %t/copy
# RUN: %lld -dylib -lSystem %t/ctor-strong-unflagged.o -install_name %t/libumbchild.dylib -o %t/libumbchild.dylib
# RUN: cp %t/libumbchild.dylib %t/copy/libumbchild.dylib
# RUN: %lld -dylib -lSystem %t/start.o -reexport_library %t/libumbchild.dylib \
# RUN:   -reexport_library %t/libctor-flagged.dylib -o %t/libumbchildren.dylib
# RUN: not %lld -dylib -demangle -lSystem %t/ctor-site.o %t/libumbchildren.dylib %t/copy/libumbchild.dylib \
# RUN:   -o /dev/null 2>&1 | FileCheck %s --check-prefix=UMBRELLA-COPY
# UMBRELLA-COPY:      error: Foreign::Foreign() was compiled both with and without -fmalterlib-sized-destructors, and the link keeps a copy compiled without
# UMBRELLA-COPY-NEXT: >>> defined in {{.*}}copy{{/|\\}}libumbchild.dylib
# RUN: %lld -dylib -lSystem %t/ctor-weak.o %t/vtable-weak-flagged.o %t/weak-flagged.o \
# RUN:   -install_name %t/libumbchild.dylib -o %t/libumbchild-rebuilt.dylib
# RUN: %lld -dylib -lSystem %t/start.o -reexport_library %t/libumbchild.dylib -o %t/libumbold.dylib
# RUN: %lld -dylib -lSystem %t/ctor-site.o %t/libumbchild-rebuilt.dylib %t/libumbold.dylib -o /dev/null
## Nor what the dylib it took the place of re-exports, which dyld doesn't
## load, also when another umbrella re-exports that dylib.
# RUN: mkdir -p %t/oldmiddle %t/newmiddle
# RUN: %lld -dylib -lSystem %t/ctor-strong-unflagged.o %t/weak-other.o -install_name %t/libgrandchild.dylib \
# RUN:   -o %t/libgrandchild.dylib
# RUN: %lld -dylib -lSystem %t/start.o -reexport_library %t/libgrandchild.dylib \
# RUN:   -o %t/oldmiddle/libmiddle.dylib
# RUN: %lld -dylib -lSystem %t/start.o -reexport_library %t/oldmiddle/libmiddle.dylib -o %t/libouter.dylib
# RUN: %lld -dylib -lSystem %t/ctor-site.o %t/ctor-weak.o %t/vtable-weak-flagged.o %t/weak-flagged.o \
# RUN:   -install_name %t/oldmiddle/libmiddle.dylib -o %t/newmiddle/libmiddle.dylib
# RUN: %lld -dylib -lSystem %t/start.o %t/newmiddle/libmiddle.dylib %t/libouter.dylib -o /dev/null
## A dylib linked with -flat_namespace binds what it requires in every image
## dyld has loaded, also in a strong copy of a dylib without MH_WEAK_DEFINES
## that comes first.
# RUN: %lld -dylib -lSystem -flat_namespace %t/ctor-site.o %t/libctor-flagged.dylib -o %t/libsiteflat.dylib
# RUN: not %lld -dylib -demangle -lSystem %t/start.o %t/libctorstrongonly.dylib %t/libsiteflat.dylib \
# RUN:   %t/libctor-flagged.dylib -o /dev/null 2>&1 | FileCheck %s --check-prefix=FLAT-REQUIRER
# FLAT-REQUIRER:      error: Foreign::Foreign() was compiled both with and without -fmalterlib-sized-destructors, and the link keeps a copy compiled without
# FLAT-REQUIRER-NEXT: >>> defined in {{.*}}libctorstrongonly.dylib
# FLAT-REQUIRER-NEXT: >>> marked in {{.*}}libctor-flagged.dylib
# FLAT-REQUIRER-NEXT: >>> the construction sites of {{.*}}libsiteflat.dylib bind to the copy the link keeps at runtime
## There a lookup takes the first image that exports it, also a weak copy
## before a strong one.
# RUN: %lld -dylib -lSystem %t/ctor-weak-unflagged.o -o %t/libctorweak-unflagged.dylib
# RUN: not %lld -dylib -demangle -lSystem %t/start.o %t/libctorweak-unflagged.dylib %t/libsiteflat.dylib \
# RUN:   %t/libctor-flagged.dylib -o /dev/null 2>&1 | FileCheck %s --check-prefix=FLAT-FIRST
# FLAT-FIRST:      error: Foreign::Foreign() was compiled both with and without -fmalterlib-sized-destructors, and the link keeps a copy compiled without
# FLAT-FIRST-NEXT: >>> defined in {{.*}}libctorweak-unflagged.dylib
# FLAT-FIRST-NEXT: >>> marked in {{.*}}libctor-flagged.dylib
# RUN: %lld -dylib -lSystem %t/start.o %t/libctor-flagged.dylib %t/libsiteflat.dylib \
# RUN:   %t/libctorweak-unflagged.dylib -o /dev/null
## Also a weak copy this image exports, which the lookup isn't a weak lookup
## of, that a strong copy of a dylib flagged with MH_WEAK_DEFINES, which takes
## the place of it for a weak lookup, doesn't.
# RUN: %lld -dylib -lSystem %t/ctor.o %t/vtable-flagged.o %t/flagged.o %t/weak-other.o \
# RUN:   -install_name %t/libctor-flagged.dylib -o %t/libctor-flagged-weakdefines.dylib
# RUN: not %lld -dylib -demangle -lSystem %t/ctor-weak-unflagged.o %t/libsiteflat.dylib \
# RUN:   %t/libctor-flagged-weakdefines.dylib -o /dev/null 2>&1 | FileCheck %s --check-prefix=FLAT-FIRST-OWN
# FLAT-FIRST-OWN:      error: Foreign::Foreign() was compiled both with and without -fmalterlib-sized-destructors, and the link keeps a copy compiled without
# FLAT-FIRST-OWN-NEXT: >>> defined in {{.*}}ctor-weak-unflagged.o
# FLAT-FIRST-OWN-NEXT: >>> marked in {{.*}}libctor-flagged-weakdefines.dylib
## So does a strong import of an image linked with -flat_namespace, which the
## link binds to a later strong copy.
# RUN: not %lld -dylib -demangle -lSystem -flat_namespace %t/ctor-site.o %t/libctorweak-unflagged.dylib \
# RUN:   %t/libctor-flagged.dylib -o /dev/null 2>&1 | FileCheck %s --check-prefix=FLAT-IMPORT
# FLAT-IMPORT:      error: Foreign::Foreign() was compiled both with and without -fmalterlib-sized-destructors, and the link keeps a copy compiled without
# FLAT-IMPORT-NEXT: >>> defined in {{.*}}libctorweak-unflagged.dylib
# RUN: %lld -dylib -lSystem %t/ctor-site.o %t/libctorweak-unflagged.dylib %t/libctor-flagged.dylib -o /dev/null
## And when the first is a flagged weak copy, the image binds to that, not to
## the later strong copy compiled without that the link binds it to.
# RUN: %lld -dylib -lSystem %t/ctor-weak.o %t/vtable-weak-flagged.o %t/weak-flagged.o \
# RUN:   -o %t/libctorweak-flagged.dylib
# RUN: %lld -dylib -lSystem -flat_namespace %t/ctor-site.o %t/libctorweak-flagged.dylib \
# RUN:   %t/libctorstrongonly.dylib -o /dev/null
# RUN: not %lld -dylib -demangle -lSystem %t/ctor-site.o %t/libctorweak-flagged.dylib \
# RUN:   %t/libctorstrongonly.dylib -o /dev/null 2>&1 | FileCheck %s --check-prefix=TWO-LEVEL-STRONG
# TWO-LEVEL-STRONG:      error: Foreign::Foreign() was compiled both with and without -fmalterlib-sized-destructors, and the link keeps a copy compiled without
# TWO-LEVEL-STRONG-NEXT: >>> defined in {{.*}}libctorstrongonly.dylib
# RUN: not %lld -dylib -demangle -lSystem -flat_namespace %t/ctor-site.o %t/libctorstrongonly.dylib \
# RUN:   %t/libctorweak-flagged.dylib -o /dev/null 2>&1 | FileCheck %s --check-prefix=TWO-LEVEL-STRONG
## A dylib linked with -flat_namespace binds what it imports as a weak
## definition by weak lookup, which looks only in the images flagged with
## MH_WEAK_DEFINES, as its symbol table records: not in a strong copy of a
## dylib without it, whether that is compiled with the flag or not.
# RUN: %lld -dylib -lSystem -flat_namespace %t/ctor-site.o %t/libctorweak-flagged.dylib \
# RUN:   -o %t/libfactory-flat-weakimport.dylib
# RUN: llvm-nm -m %t/libfactory-flat-weakimport.dylib | FileCheck %s --check-prefix=WEAK-IMPORT
# WEAK-IMPORT: (undefined) weak external __ZN7ForeignC1Ev
# RUN: not %lld -dylib -demangle -lSystem %t/start.o %t/libctor-flagged.dylib %t/libctorweak-unflagged.dylib \
# RUN:   %t/libfactory-flat-weakimport.dylib %t/libctorweak-flagged.dylib -o /dev/null 2>&1 \
# RUN:   | FileCheck %s --check-prefix=WEAK-IMPORT-BAD
# WEAK-IMPORT-BAD:      error: Foreign::Foreign() was compiled both with and without -fmalterlib-sized-destructors, and the link keeps a copy compiled without
# WEAK-IMPORT-BAD-NEXT: >>> defined in {{.*}}libctorweak-unflagged.dylib
# RUN: %lld -dylib -lSystem %t/start.o %t/libctorstrongonly.dylib %t/libfactory-flat-weakimport.dylib \
# RUN:   %t/libctorweak-flagged.dylib -o /dev/null
## A dylib an umbrella re-exports is loaded where the umbrella loads it, also
## when the link has an unused copy of it by another path first, which dyld
## doesn't load: a weak copy of it that comes first takes the place of the
## flagged one of a factory that a later umbrella re-exports.
# RUN: %lld -dylib -lSystem %t/ctor-site.o %t/ctor-weak.o %t/vtable-weak-flagged.o %t/weak-flagged.o \
# RUN:   -o %t/libfactory-weak.dylib
# RUN: %lld -dylib -lSystem %t/start.o -reexport_library %t/libctorweak-unflagged.dylib -o %t/libfirst.dylib
# RUN: %lld -dylib -lSystem %t/start.o -reexport_library %t/libfactory-weak.dylib -o %t/libsecond.dylib
# RUN: mkdir -p %t/unused && cp %t/libctorweak-unflagged.dylib %t/unused/libctorweak-unflagged.dylib
# RUN: not %lld -dylib -demangle -lSystem %t/start.o -needed_library %t/libfirst.dylib \
# RUN:   -needed_library %t/libsecond.dylib -o /dev/null 2>&1 | FileCheck %s --check-prefix=UNUSED-COPY
# RUN: not %lld -dylib -demangle -lSystem %t/start.o -dead_strip_dylibs %t/unused/libctorweak-unflagged.dylib \
# RUN:   -needed_library %t/libfirst.dylib -needed_library %t/libsecond.dylib -o /dev/null 2>&1 \
# RUN:   | FileCheck %s --check-prefix=UNUSED-COPY
# UNUSED-COPY:      error: Foreign::Foreign() was compiled both with and without -fmalterlib-sized-destructors, and the link keeps a copy compiled without
# UNUSED-COPY-NEXT: >>> defined in {{.*}}libctorweak-unflagged.dylib
## What a dylib linked with -flat_namespace gives on of what a dylib it loads
## requires, for the images that may not load that one, isn't bound by it:
## where that one requires it too, it is followed as that one binds it, by
## weak lookup, which doesn't look in a strong copy of a dylib without
## MH_WEAK_DEFINES, not by flat lookup.
# RUN: %lld -dylib -lSystem -flat_namespace %t/start.o %t/libfactory-weak.dylib -o %t/libmiddle-flat.dylib
# RUN: llvm-nm -g %t/libmiddle-flat.dylib | FileCheck %s --check-prefix=MIDDLE-FLAT
# MIDDLE-FLAT: __ZN7ForeignC1Ev.mib_sized_required
# RUN: %lld -dylib -lSystem %t/start.o %t/libctorstrongonly.dylib %t/libmiddle-flat.dylib \
# RUN:   %t/libfactory-weak.dylib -o /dev/null
## And what a dylib linked with -flat_namespace gives on of what the proof of
## a dylib it imports from depends on, which that dylib binds by weak lookup,
## is required as bound by weak lookup, not by its own flat lookup, which
## would bind an earlier strong copy of a dylib without MH_WEAK_DEFINES.
# RUN: %lld -dylib -lSystem %t/ctor-weak.o %t/vtable-weak-flagged.o %t/weak-flagged.o \
# RUN:   -o %t/libclass-weakall.dylib
# RUN: %lld -dylib -lSystem %t/vtable-unflagged.o %t/unflagged.o -o %t/libvtable-strongbad.dylib
# RUN: %lld -dylib -lSystem -flat_namespace %t/ctor-site.o %t/libclass-weakall.dylib \
# RUN:   -o %t/libfactory-flat-weakdeps.dylib
# RUN: llvm-nm -g %t/libfactory-flat-weakdeps.dylib | FileCheck %s --check-prefix=WEAK-REQUIRED
# WEAK-REQUIRED: __ZTV7Foreign.mib_sized_required_weak
# RUN: %lld -dylib -lSystem %t/start.o %t/libvtable-strongbad.dylib %t/libfactory-flat-weakdeps.dylib \
# RUN:   %t/libclass-weakall.dylib -o /dev/null
## Where the weak copy that comes first isn't flagged, it binds.
# RUN: not %lld -dylib -demangle -lSystem %t/start.o %t/libweak-unflagged.dylib \
# RUN:   %t/libfactory-flat-weakdeps.dylib %t/libclass-weakall.dylib -o /dev/null 2>&1 \
# RUN:   | FileCheck %s --check-prefix=WEAK-REQUIRED-BAD
# WEAK-REQUIRED-BAD: >>> defined in {{.*}}libweak-unflagged.dylib
## What the proof of a dylib a site imports from depends on, which that dylib
## binds by weak lookup, is required by name, also when the link binds it to
## a strong copy of a dylib without MH_WEAK_DEFINES, which dyld doesn't look
## in: an image that loads a weak copy first binds that.
# RUN: %lld -dylib -lSystem %t/ctor.o %t/vtable-weak-flagged.o %t/weak-flagged.o -o %t/libclass-weak.dylib
# RUN: %lld -dylib -lSystem %t/vtable-flagged.o %t/flagged.o -o %t/libclass-strong.dylib
# RUN: %lld -dylib -lSystem %t/vtable-weak-unflagged.o %t/weak-unflagged.o -o %t/libclass-weak-unflagged.dylib
# RUN: %lld -dylib -lSystem %t/ctor-site.o %t/libclass-weak.dylib %t/libclass-strong.dylib \
# RUN:   -o %t/libsite-of-weak.dylib
# RUN: llvm-nm -g %t/libsite-of-weak.dylib | FileCheck %s --check-prefix=REQUIRES-WEAK-DEPS
# REQUIRES-WEAK-DEPS: __ZTV7Foreign.mib_sized_required
# RUN: not %lld -dylib -demangle -lSystem %t/start.o %t/libclass-weak-unflagged.dylib %t/libsite-of-weak.dylib \
# RUN:   %t/libclass-weak.dylib %t/libclass-strong.dylib -o /dev/null 2>&1 | FileCheck %s --check-prefix=WEAK-DEPS
# WEAK-DEPS:      error: vtable for Foreign was compiled both with and without -fmalterlib-sized-destructors, and the link keeps a copy compiled without
# WEAK-DEPS-NEXT: >>> defined in {{.*}}libclass-weak-unflagged.dylib
# RUN: %lld -dylib -lSystem %t/start.o %t/libsite-of-weak.dylib %t/libclass-weak.dylib %t/libclass-strong.dylib \
# RUN:   -o /dev/null
## What the proof of a dylib a site imports from depends on, which that dylib
## binds by weak lookup, is required by weak lookup also when the site's link
## binds it to a strong copy of a dylib without MH_WEAK_DEFINES that comes
## first, by the ordinal its link records: an image flagged with
## MH_WEAK_DEFINES whose own strong copies aren't flagged takes the place of
## the weak copies the proof was checked against.
# RUN: llvm-mc -filetype=obj -triple=x86_64-apple-darwin %t/weakdep-w.s -o %t/weakdep-w.o
# RUN: llvm-mc -filetype=obj -triple=x86_64-apple-darwin %t/weakdep-s.s -o %t/weakdep-s.o
# RUN: llvm-mc -filetype=obj -triple=x86_64-apple-darwin %t/weakdep-m.s -o %t/weakdep-m.o
# RUN: llvm-mc -filetype=obj -triple=x86_64-apple-darwin %t/weakdep-h.s -o %t/weakdep-h.o
# RUN: %lld -dylib -lSystem %t/weakdep-w.o -o %t/libweakdep-w.dylib -exported_symbol _otherw \
# RUN:   -exported_symbol __ZN1AC1Ei -exported_symbol __ZTV1A -exported_symbol __ZN1AD0Ev
# RUN: %lld -dylib -lSystem %t/weakdep-s.o -o %t/libweakdep-s.dylib
# RUN: %lld -dylib -lSystem %t/weakdep-m.o %t/libweakdep-s.dylib %t/libweakdep-w.dylib -o %t/libweakdep-m.dylib
# RUN: llvm-nm -g %t/libweakdep-m.dylib | FileCheck %s --check-prefix=WEAKDEP-REQUIRED
# WEAKDEP-REQUIRED: __ZTV1A.mib_sized_required_weak
# RUN: not %lld -dylib -demangle -lSystem %t/weakdep-h.o %t/libweakdep-m.dylib %t/libweakdep-s.dylib -o /dev/null 2>&1 \
# RUN:   | FileCheck %s --check-prefix=WEAKDEP
# WEAKDEP: error: {{.*}} was compiled both with and without -fmalterlib-sized-destructors, and the link keeps a copy compiled without
# WEAKDEP-NEXT: >>> defined in {{.*}}weakdep-h.o
## A requirement a dylib gives on that a dylib it loads binds by weak lookup
## is followed so also when another dylib it loads requires the same name by
## its own flat lookup, which binds another copy: weak lookup doesn't look in
## a dylib without MH_WEAK_DEFINES, and binds the weak copy of a dylib whose
## constructor isn't flagged.
# RUN: llvm-mc -filetype=obj -triple=x86_64-apple-darwin %t/flatweak-good.s -o %t/flatweak-good.o
# RUN: llvm-mc -filetype=obj -triple=x86_64-apple-darwin %t/flatweak-bad.s -o %t/flatweak-bad.o
# RUN: llvm-mc -filetype=obj -triple=x86_64-apple-darwin %t/flatweak-factory.s -o %t/flatweak-factory.o
# RUN: llvm-mc -filetype=obj -triple=x86_64-apple-darwin %t/flatweak-wrapper.s -o %t/flatweak-wrapper.o
# RUN: llvm-mc -filetype=obj -triple=x86_64-apple-darwin %t/flatweak-app.s -o %t/flatweak-app.o
# RUN: %lld -dylib -lSystem %t/flatweak-good.o -flat_namespace -o %t/libflatweak-good.dylib
# RUN: %lld -dylib -lSystem %t/flatweak-bad.o -exported_symbol __ZN1AC1Ev -o %t/libflatweak-bad.dylib
# RUN: %lld -dylib -lSystem %t/flatweak-factory.o -exported_symbol __ZN1AC1Ev -exported_symbol _weak_make \
# RUN:   -exported_symbol _destroy -o %t/libflatweak-factory.dylib
# RUN: %lld -dylib -lSystem %t/flatweak-wrapper.o %t/libflatweak-factory.dylib %t/libflatweak-good.dylib \
# RUN:   -o %t/libflatweak-wrapper.dylib
# RUN: not %lld -dylib -lSystem %t/flatweak-app.o %t/libflatweak-good.dylib %t/libflatweak-bad.dylib \
# RUN:   %t/libflatweak-wrapper.dylib -o /dev/null 2>&1 | FileCheck %s --check-prefix=FLATWEAK
# FLATWEAK: error: __ZN1AC1Ev was compiled both with and without -fmalterlib-sized-destructors, and the link keeps a copy compiled without
# FLATWEAK-NEXT: >>> defined in {{.*}}libflatweak-bad.dylib
## A dylib linked with -flat_namespace binds what its proof depends on by name,
## so a dylib whose sites import its strong constructor requires that, which
## the images that load them follow there, where they look.
# RUN: %lld -dylib -lSystem -flat_namespace %t/ctor.o %t/vtable-flagged.o %t/flagged.o -o %t/libctor-flat.dylib
# RUN: %lld -dylib -lSystem %t/ctor-site.o %t/libctor-flat.dylib -o %t/libfactory-of-flat.dylib
# RUN: llvm-nm -g %t/libfactory-of-flat.dylib | FileCheck %s --check-prefix=REQUIRES-FLAT-CTOR
# REQUIRES-FLAT-CTOR: __ZN7ForeignC1Ev.mib_sized_required
# RUN: not %lld -demangle -lSystem %t/start.o %t/unflagged.o %t/libfactory-of-flat.dylib %t/libctor-flat.dylib \
# RUN:   -o /dev/null 2>&1 | FileCheck %s --check-prefix=FACTORY-OF-FLAT
# FACTORY-OF-FLAT:      error: Foreign::~Foreign() was compiled both with and without -fmalterlib-sized-destructors, and the link keeps a copy compiled without
# FACTORY-OF-FLAT-NEXT: >>> defined in {{.*}}unflagged.o
# RUN: %lld -lSystem %t/start.o %t/libfactory-of-flat.dylib %t/libctor-flat.dylib -o /dev/null
## Also where another dylib binds it in its own two-level namespace to another
## dylib linked with -flat_namespace, whose proof depends on nothing by name,
## whichever is followed first: the constructor of the first binds the vtable
## of this image by name.
# RUN: %lld -dylib -lSystem -flat_namespace %t/ctor.o %t/vtable-flagged.o %t/flagged.o \
# RUN:   -exported_symbol __ZN7ForeignC1Ev -o %t/libctor-flat-hidden.dylib
# RUN: %lld -dylib -lSystem %t/ctor-site.o %t/libctor-flat-hidden.dylib -o %t/libfactory-of-flat-hidden.dylib
# RUN: not %lld -demangle -lSystem %t/start.o %t/unflagged.o %t/vtable-unflagged.o \
# RUN:   %t/libfactory-of-flat-hidden.dylib %t/libfactory-of-flat.dylib %t/libctor-flat-hidden.dylib \
# RUN:   %t/libctor-flat.dylib -o /dev/null 2>&1 | FileCheck %s --check-prefix=FLAT-PROVIDERS
# RUN: not %lld -demangle -lSystem %t/start.o %t/unflagged.o %t/vtable-unflagged.o \
# RUN:   %t/libfactory-of-flat.dylib %t/libfactory-of-flat-hidden.dylib %t/libctor-flat-hidden.dylib \
# RUN:   %t/libctor-flat.dylib -o /dev/null 2>&1 | FileCheck %s --check-prefix=FLAT-PROVIDERS
# FLAT-PROVIDERS:      error: vtable for Foreign was compiled both with and without -fmalterlib-sized-destructors, and the link keeps a copy compiled without
# FLAT-PROVIDERS-NEXT: >>> defined in {{.*}}vtable-unflagged.o
# FLAT-PROVIDERS-NEXT: >>> marked in {{.*}}libctor-flat.dylib
# FLAT-PROVIDERS-NEXT: >>> the construction sites of {{.*}}libfactory-of-flat.dylib bind to the copy the link keeps at runtime
## There the strong import binds to the dylib the requiring one loads that has
## it, not to another flat dylib's copy that this link binds it to.
# RUN: %lld -dylib -lSystem -flat_namespace %t/ctor-strong-unflagged.o %t/libctor-flat.dylib \
# RUN:   -o %t/libother-flat.dylib
# RUN: %lld -dylib -lSystem %t/start.o %t/libother-flat.dylib %t/libfactory-of-flat.dylib \
# RUN:   %t/libctor-flat.dylib -o /dev/null
## A dylib that gives on what a dylib it loads requires names it as given, and
## where that one requires it too, it is followed as that one binds it, also
## when the giving one imports it for a construction of its own without the
## builtin, from another dylib.
# RUN: llvm-mc -filetype=obj -triple=x86_64-apple-darwin %t/ctor-user.s -o %t/ctor-user.o
# RUN: %lld -dylib -lSystem %t/ctor-user.o %t/libother-flat.dylib %t/libfactory-of-flat.dylib \
# RUN:   %t/libctor-flat.dylib -o %t/libwrapper-user.dylib
# RUN: llvm-nm -g %t/libwrapper-user.dylib | FileCheck %s --check-prefix=GIVEN
# GIVEN: __ZN7ForeignC1Ev.mib_sized_required_given
# RUN: %lld -dylib -lSystem %t/start.o %t/libwrapper-user.dylib %t/libother-flat.dylib \
# RUN:   %t/libfactory-of-flat.dylib %t/libctor-flat.dylib -o /dev/null
## Nor to a strong copy this image exports, also where dyld coalesces in it.
# RUN: %lld -dylib -lSystem %t/start.o %t/ctor-strong-unflagged.o %t/libfactory-of-flat.dylib \
# RUN:   %t/libctor-flat.dylib -o /dev/null
## Also when it loads one with a weak copy first, which its link took the
## strong copy over, as its symbol table records.
# RUN: %lld -dylib -lSystem %t/ctor-site.o %t/libctorweak-unflagged.dylib %t/libctor-flat.dylib \
# RUN:   -o %t/libfactory-weak-first.dylib
# RUN: llvm-nm -m %t/libfactory-weak-first.dylib | FileCheck %s --check-prefix=WEAK-FIRST-ORDINAL
# WEAK-FIRST-ORDINAL: (undefined) external __ZN7ForeignC1Ev (from libctor-flat)
# RUN: %lld -lSystem %t/start.o %t/libfactory-weak-first.dylib %t/libctorweak-unflagged.dylib \
# RUN:   %t/libctor-flat.dylib -o /dev/null
## Also when strip removed its symbol table, from the bind of its dyld info, or
## the import of its chained fixups.
# RUN: llvm-mc -filetype=obj -triple=x86_64-apple-darwin %t/ctor-site-data.s -o %t/ctor-site-data.o
# RUN: %lld -dylib -lSystem %t/ctor-site-data.o %t/libctorweak-unflagged.dylib %t/libctor-flat.dylib \
# RUN:   -o %t/libfactory-weak-first-stripped.dylib
# RUN: llvm-strip %t/libfactory-weak-first-stripped.dylib
# RUN: %lld -lSystem %t/start.o %t/libfactory-weak-first-stripped.dylib %t/libctorweak-unflagged.dylib \
# RUN:   %t/libctor-flat.dylib -o /dev/null
# RUN: %lld -dylib -lSystem -fixup_chains %t/ctor-site-data.o %t/libctorweak-unflagged.dylib \
# RUN:   %t/libctor-flat.dylib -o %t/libfactory-weak-first-chained.dylib
# RUN: llvm-strip %t/libfactory-weak-first-chained.dylib
# RUN: %lld -lSystem %t/start.o %t/libfactory-weak-first-chained.dylib %t/libctorweak-unflagged.dylib \
# RUN:   %t/libctor-flat.dylib -o /dev/null
## A weak definition that such a dylib requires and exports a copy of binds by
## weak lookup, which looks only in the images flagged with MH_WEAK_DEFINES:
## not in the strong copy of a dylib without it that comes first.
# RUN: %lld -dylib -lSystem -flat_namespace %t/ctor-site.o %t/ctor-weak.o %t/vtable-weak-flagged.o \
# RUN:   %t/weak-flagged.o -o %t/libfactory-flat-weak.dylib
# RUN: not %lld -demangle -lSystem %t/start.o %t/libctor-flagged.dylib %t/libctorweak-unflagged.dylib \
# RUN:   %t/libfactory-flat-weak.dylib -o /dev/null 2>&1 | FileCheck %s --check-prefix=FLAT-WEAK-LOOKUP
# FLAT-WEAK-LOOKUP:      error: Foreign::Foreign() was compiled both with and without -fmalterlib-sized-destructors, and the link keeps a copy compiled without
# FLAT-WEAK-LOOKUP-NEXT: >>> defined in {{.*}}libctorweak-unflagged.dylib
# RUN: %lld -lSystem %t/start.o %t/libctor-flagged.dylib %t/libfactory-flat-weak.dylib -o /dev/null
## Also when another dylib linked with -flat_namespace requires it too, whose
## strong import binds by flat lookup to the first that exports it, whichever
## is checked first.
# RUN: not %lld -demangle -lSystem %t/start.o %t/libctorstrongonly.dylib %t/libfactory-flat-weak.dylib \
# RUN:   %t/libsiteflat.dylib %t/libctor-flagged.dylib -o /dev/null 2>&1 | FileCheck %s --check-prefix=FLAT-BOTH
# RUN: not %lld -demangle -lSystem %t/start.o %t/libctorstrongonly.dylib %t/libsiteflat.dylib \
# RUN:   %t/libfactory-flat-weak.dylib %t/libctor-flagged.dylib -o /dev/null 2>&1 | FileCheck %s --check-prefix=FLAT-BOTH
# FLAT-BOTH:      error: Foreign::Foreign() was compiled both with and without -fmalterlib-sized-destructors, and the link keeps a copy compiled without
# FLAT-BOTH-NEXT: >>> defined in {{.*}}libctorstrongonly.dylib
## But a weak definition such an image exports binds by weak lookup, which looks
## only in the images flagged with MH_WEAK_DEFINES: the strong copy of a dylib
## without it takes no place of it.
# RUN: %lld -dylib -lSystem -flat_namespace %t/ctor-site.o %t/ctor-weak.o %t/vtable-weak-flagged.o \
# RUN:   %t/flagged.o %t/libctorstrongonly.dylib -o /dev/null
# RUN: %lld -dylib -lSystem %t/ctor-site.o %t/libctor-flagged.dylib -o %t/libsitetwolevel.dylib
# RUN: %lld -dylib -lSystem %t/start.o %t/libctorstrongonly.dylib %t/libsitetwolevel.dylib \
# RUN:   %t/libctor-flagged.dylib -o /dev/null
## A strong copy the image exports takes the place of the dylibs' copies, also
## of a later strong one.
# RUN: %lld -dylib -lSystem %t/ctor.o %t/vtable-flagged.o %t/flagged.o %t/libsiteclass.dylib \
# RUN:   %t/libctorstrong.dylib -o /dev/null
# RUN: %lld -dylib -lSystem %t/ctor-hidden.o %t/libsiteclass.dylib %t/libctorstrongonly.dylib -o /dev/null
## Each dylib an umbrella re-exports by its own header.
# RUN: %lld -dylib -lSystem %t/start.o -reexport_library %t/libctorstrongonly.dylib \
# RUN:   -reexport_library %t/libweak-flagged.dylib -o %t/libwrapper-strong.dylib
# RUN: %lld -dylib -lSystem %t/ctor-hidden.o %t/libsiteclass.dylib %t/libwrapper-strong.dylib -o /dev/null
## The strong copy of a dylib flagged with it takes the place of a weak one
## the image exports, also in an executable, whose sites then bind to it.
# RUN: not %lld -dylib -demangle -lSystem %t/ctor-site.o %t/ctor-weak.o %t/vtable-weak-flagged.o %t/flagged.o \
# RUN:   %t/libctorstrong.dylib -o /dev/null 2>&1 | FileCheck %s --check-prefix=STRONG-OVER-WEAK
# RUN: not %lld -demangle -lSystem %t/ctor-site.o %t/ctor-weak.o %t/vtable-weak-flagged.o %t/flagged.o \
# RUN:   %t/libctorstrong.dylib -o /dev/null 2>&1 | FileCheck %s --check-prefix=STRONG-OVER-WEAK
# STRONG-OVER-WEAK:      error: Foreign::Foreign() was not compiled with -fmalterlib-sized-destructors
# STRONG-OVER-WEAK-NEXT: >>> defined in {{.*}}libctorstrong.dylib
# STRONG-OVER-WEAK-NEXT: >>> an object a construction site makes is destroyed with the size its deleting destructor returns
# RUN: %lld -dylib -lSystem %t/ctor-site.o %t/ctor-weak.o %t/vtable-weak-flagged.o %t/flagged.o \
# RUN:   %t/libctorstrongonly.dylib -o /dev/null
## A weak copy of the image that the strong copy of a flagged dylib compiled
## with it takes the place of must still hold: a site can have inlined its
## body, which installs the vtable of the image.
# RUN: llvm-mc -filetype=obj -triple=x86_64-apple-darwin %t/weak-other.s -o %t/weak-other.o
# RUN: %lld -dylib -lSystem %t/ctor.o %t/vtable-flagged.o %t/flagged.o %t/weak-other.o -o %t/libctorprovider.dylib
# RUN: not %lld -dylib -demangle -lSystem %t/ctor-site.o %t/ctor-weak-unflagged.o %t/libctorprovider.dylib \
# RUN:   -o /dev/null 2>&1 | FileCheck %s --check-prefix=INLINED
# RUN: not %lld -dylib -demangle -lSystem %t/ctor-site.o %t/vtable-weak-unflagged.o %t/ctor-weak.o \
# RUN:   %t/vtable-weak-flagged.o %t/flagged.o %t/libctorprovider.dylib -o /dev/null 2>&1 \
# RUN:   | FileCheck %s --check-prefix=INLINED-VTABLE
# INLINED:      error: Foreign::Foreign() was compiled both with and without -fmalterlib-sized-destructors, and the link keeps a copy compiled without
# INLINED-NEXT: >>> defined in {{.*}}ctor-weak-unflagged.o
# INLINED-NEXT: >>> marked in {{.*}}libctorprovider.dylib
# INLINED-VTABLE:      error: Foreign::Foreign() depends on vtable for Foreign
# INLINED-VTABLE-NEXT: >>> vtable for Foreign was compiled both with and without -fmalterlib-sized-destructors, and the link keeps a copy compiled without
# INLINED-VTABLE-NEXT: >>> defined in {{.*}}vtable-weak-unflagged.o
# RUN: %lld -dylib -lSystem %t/ctor-site.o %t/ctor-weak.o %t/vtable-weak-flagged.o %t/flagged.o \
# RUN:   %t/libctorprovider.dylib -o /dev/null
## Also of what the weak copies it exports depend on, deeper, like the
## deleting destructor the vtable a constructor installs names.
# RUN: llvm-mc -filetype=obj -triple=x86_64-apple-darwin %t/dtor-strong-weakdefines.s -o %t/dtor-strong-weakdefines.o
# RUN: %lld -dylib -lSystem %t/dtor-strong-weakdefines.o -o %t/libdtorstrong.dylib
# RUN: not %lld -dylib -demangle -lSystem %t/ctor-site.o %t/ctor-weak.o %t/vtable-weak-flagged.o \
# RUN:   %t/weak-flagged.o %t/libdtorstrong.dylib -o /dev/null 2>&1 | FileCheck %s --check-prefix=STRONG-DEEPER
# STRONG-DEEPER:      error: Foreign::~Foreign() was not compiled with -fmalterlib-sized-destructors
# STRONG-DEEPER-NEXT: >>> defined in {{.*}}libdtorstrong.dylib
## So does a strong one that a façade flagged with MH_WEAK_DEFINES forwards to
## a dylib without the flag, which dyld follows from the façade; one the façade
## isn't flagged for doesn't.
# RUN: %lld -dylib -lSystem %t/weak-other.o %t/libunflagged.dylib -exported_symbol __ZN7ForeignD0Ev \
# RUN:   -exported_symbol _otherWeakDefinition -o %t/libfacade-weakdefines.dylib
# RUN: not %lld -dylib -demangle -lSystem %t/site-weak.o %t/libfacade-weakdefines.dylib \
# RUN:   %t/libunflagged.dylib -o /dev/null 2>&1 | FileCheck %s --check-prefix=FACADE-STRONG
# FACADE-STRONG: error: Foreign::~Foreign() was {{.*}}
## And so does it for what another dylib requires.
# RUN: not %lld -demangle -lSystem %t/start.o %t/libfacade-weakdefines.dylib %t/libunflagged.dylib \
# RUN:   %t/libsite.dylib -o /dev/null 2>&1 | FileCheck %s --check-prefix=FACADE-REQUIRED
# FACADE-REQUIRED:      error: Foreign::~Foreign() was compiled both with and without -fmalterlib-sized-destructors, and the link keeps a copy compiled without
# FACADE-REQUIRED-NEXT: >>> defined in {{.*}}libunflagged.dylib
# FACADE-REQUIRED-NEXT: >>> marked in {{.*}}libsite.dylib
# RUN: %lld -dylib -lSystem %t/site-weak.o %t/libfacade-unflagged.dylib %t/libunflagged.dylib \
# RUN:   -o /dev/null
## But dyld's lookup of a weak definition looks only in what a façade forwards
## to, not into the dylibs that one re-exports otherwise, like an umbrella's:
## a strong copy there takes no place of the image's flagged weak one.
# RUN: %lld -dylib -lSystem %t/start.o -reexport_library %t/libunflagged.dylib -o %t/libumb-unflagged.dylib
# RUN: %lld -dylib -lSystem %t/weak-other.o %t/libumb-unflagged.dylib -exported_symbol __ZN7ForeignD0Ev \
# RUN:   -exported_symbol _otherWeakDefinition -o %t/libfacade-umb.dylib
# RUN: llvm-objdump --macho --exports-trie %t/libfacade-umb.dylib | FileCheck %s --check-prefix=FACADE-UMB-TRIE
# FACADE-UMB-TRIE: [re-export] __ZN7ForeignD0Ev (from libumb-unflagged)
# RUN: %lld -dylib -lSystem %t/site-weak.o %t/libfacade-umb.dylib %t/libumb-unflagged.dylib -o /dev/null
## A dylib linked with -interposable binds its own references to what it
## exports in a flat namespace, which another image loaded first takes the
## place of: what its sites depend on is required, and followed so.
# RUN: %lld -dylib -lSystem -interposable %t/ctor-site.o %t/ctor.o %t/vtable-flagged.o %t/flagged.o \
# RUN:   -o %t/libinterposable.dylib
# RUN: llvm-nm -g %t/libinterposable.dylib | FileCheck %s --check-prefix=INTERPOSABLE
# INTERPOSABLE: __ZN7ForeignC1Ev.mib_sized_required
# RUN: not %lld -dylib -demangle -lSystem %t/start.o %t/libctor-unflagged.dylib %t/libinterposable.dylib \
# RUN:   -o /dev/null 2>&1 | FileCheck %s --check-prefix=INTERPOSABLE-BAD
# INTERPOSABLE-BAD: >>> defined in {{.*}}libctor-unflagged.dylib
# RUN: %lld -dylib -lSystem %t/start.o %t/libinterposable.dylib %t/libctor-unflagged.dylib -o /dev/null
# RUN: %lld -dylib -lSystem %t/ctor-site.o %t/ctor-weak.o %t/vtable-weak-flagged.o %t/weak-flagged.o \
# RUN:   %t/libflagged.dylib -o /dev/null
# RUN: %lld -lSystem -exported_symbol _main %t/ctor-site.o %t/ctor-weak.o %t/vtable-weak-flagged.o %t/flagged.o \
# RUN:   %t/libctorstrong.dylib -o /dev/null
## Such a dylib linked into another that keeps its copies of the constructor
## and the vtable private names what the first one's copies depend on for the
## images linked against it only, which may not load the first.
# RUN: llvm-mc -filetype=obj -triple=x86_64-apple-darwin %t/vtable-hidden.s -o %t/vtable-hidden.o
# RUN: %lld -dylib -lSystem %t/ctor-hidden.o %t/vtable-hidden.o %t/libsiteclass.dylib -o %t/libmiddle.dylib
# RUN: llvm-nm %t/libmiddle.dylib | FileCheck %s --check-prefix=MIDDLE
# MIDDLE: __ZTV7Foreign.mib_sized_required
# RUN: not %lld -dylib -demangle -lSystem %t/vtable-weak-unflagged.o %t/libmiddle.dylib -o /dev/null 2>&1 \
# RUN:   | FileCheck %s --check-prefix=MIDDLE-VTABLE
# MIDDLE-VTABLE:      error: vtable for Foreign was not compiled with -fmalterlib-sized-destructors
# MIDDLE-VTABLE-NEXT: >>> the construction sites of {{.*}}libmiddle.dylib bind to the copy the link keeps at runtime
## A marker the optimizer folded into its definition still depends on what
## the definition binds.
# RUN: llvm-mc -filetype=obj -triple=x86_64-apple-darwin %t/ctor-folded.s -o %t/ctor-folded.o
# RUN: not %lld -demangle -lSystem %t/ctor-folded.o %t/ctor.o %t/vtable-unflagged.o %t/flagged.o -o /dev/null 2>&1 \
# RUN:   | FileCheck %s --check-prefix=CHAIN
## A strong definition of the image overrides a dylib's weak one at runtime.
# RUN: not %lld -demangle -lSystem %t/start.o %t/unflagged.o %t/libsite.dylib -o /dev/null 2>&1 \
# RUN:   | FileCheck %s --check-prefix=OVERRIDE
# OVERRIDE:      error: Foreign::~Foreign() was compiled both with and without -fmalterlib-sized-destructors, and the link keeps a copy compiled without
# OVERRIDE-NEXT: >>> defined in {{.*}}unflagged.o
## Also one that LTO compiles, which the link can't tell overrides a weak
## one, in an image that dyld looks in to coalesce, flagged with
## MH_WEAK_DEFINES for another weak definition it exports.
# RUN: llvm-as %t/lto-unflagged.ll -o %t/lto-unflagged-override.o
# RUN: not %lld -dylib -demangle -lSystem %t/weak-other.o %t/lto-unflagged-override.o %t/libsite.dylib \
# RUN:   -o /dev/null 2>&1 \
# RUN:   | FileCheck %s --check-prefix=OVERRIDE-LTO
# OVERRIDE-LTO: error: Foreign::~Foreign() was compiled both with and without -fmalterlib-sized-destructors
## Also flagged for a strong definition it hides that overrides a weak one of
## a dylib.
# RUN: llvm-mc -filetype=obj -triple=x86_64-apple-darwin %t/hidden-override.s -o %t/hidden-override.o
# RUN: %lld -dylib -lSystem %t/weak-other.o -o %t/libweakother.dylib
# RUN: not %lld -dylib -demangle -lSystem %t/hidden-override.o %t/lto-unflagged-override.o %t/libsite.dylib \
# RUN:   %t/libweakother.dylib -o /dev/null 2>&1 \
# RUN:   | FileCheck %s --check-prefix=OVERRIDE-LTO
## Not for the weak marker of a copy the link did not keep, which it hides.
# RUN: %lld -dylib -lSystem %t/weak-flagged.o %t/lto-unflagged-override.o %t/libsite.dylib -o %t/libdiscarded.dylib
# RUN: llvm-objdump --macho --private-header %t/libdiscarded.dylib | FileCheck %s --check-prefix=DISCARDED
# DISCARDED:     MH_MAGIC_64
# DISCARDED-NOT: WEAK_DEFINES
## Not where dead stripping removes the weak definition, which leaves the
## image unflagged.
# RUN: llvm-mc -filetype=obj -triple=x86_64-apple-darwin %t/start-uses.s -o %t/start-uses.o
# RUN: %lld -dead_strip -lSystem %t/start-uses.o %t/weak-other.o %t/lto-unflagged-override.o %t/libsite.dylib \
# RUN:   -o /dev/null

## A marker belongs to the copy of a definition the link chose for the
## definition's own sake, so a reference to it loads no archive member, such
## as the record of a constructor the optimizer inlined everywhere.
# RUN: llvm-mc -filetype=obj -triple=x86_64-apple-darwin %t/owner-gone.s -o %t/owner-gone.o
# RUN: llvm-mc -filetype=obj -triple=x86_64-apple-darwin %t/ctor-member.s -o %t/ctor-member.o
# RUN: rm -f %t/ctor-member.a && llvm-ar rcs %t/ctor-member.a %t/ctor-member.o
# RUN: %lld -lSystem %t/owner-gone.o %t/ctor-member.a -o /dev/null

## A definition LTO compiles carries its marker into the object LTO produces,
## even when LTO makes it local.
# RUN: llvm-as %t/lto-main.ll -o %t/lto-main.o
# RUN: llvm-as %t/lto-flagged.ll -o %t/lto-flagged.o
# RUN: llvm-as %t/lto-unflagged.ll -o %t/lto-unflagged.o
# RUN: %lld -lSystem %t/main.o %t/lto-flagged.o -o %t/lto.out
# RUN: not %lld -demangle -lSystem %t/main.o %t/lto-unflagged.o -o /dev/null 2>&1 | FileCheck %s
# RUN: %lld -lSystem %t/lto-main.o %t/lto-flagged.o -o %t/lto-local.out
# RUN: not %lld -demangle -lSystem %t/lto-main.o %t/lto-unflagged.o -o /dev/null 2>&1 | FileCheck %s --check-prefix=LTO
# LTO: error: Foreign::~Foreign() was not compiled with -fmalterlib-sized-destructors

## Bitcode has no markers, so a weak definition that bitcode defines before an
## object leaves the marker to the object LTO produces, not to the object's
## copy, which the link does not keep.
# RUN: llvm-as %t/lto-weak-flagged.ll -o %t/lto-weak-flagged.o
# RUN: llvm-as %t/lto-weak-unflagged.ll -o %t/lto-weak-unflagged.o
# RUN: %lld -lSystem %t/main.o %t/lto-weak-flagged.o %t/weak-flagged.o -o %t/lto-weak.out
## A definition the proof of an owner depends on stays for the backend through
## a reference from a global that is never emitted, which lets LTO hide an
## inline one, like a native build does, which no image then exports.
# RUN: llvm-as %t/lto-inline-kept.ll -o %t/lto-inline-kept.o
# RUN: %lld -dylib -lSystem %t/lto-inline-kept.o -o %t/liblto-inline-kept.dylib
# RUN: llvm-nm -m %t/liblto-inline-kept.dylib | FileCheck %s --check-prefix=LTO-KEPT-HIDDEN
# LTO-KEPT-HIDDEN-NOT: weak external __ZTV7Foreign
# LTO-KEPT-HIDDEN-NOT: weak external __ZN7ForeignD0Ev
# LTO-KEPT-HIDDEN-DAG: non-external __ZN7ForeignD0Ev
# LTO-KEPT-HIDDEN-DAG: non-external __ZTV7Foreign
# RUN: %lld -lSystem %t/main.o %t/weak-flagged.o %t/lto-weak-unflagged.o -o %t/lto-weak.out
# RUN: not %lld -demangle -lSystem %t/main.o %t/lto-weak-unflagged.o %t/weak-flagged.o -o /dev/null 2>&1 \
# RUN:   | FileCheck %s

#--- main.s
.globl _main
_main:
  retq

.section __DATA_CONST,__mib_sized_ref
.quad __ZN7ForeignD0Ev
.quad __ZN7ForeignD0Ev.mib_sized

.subsections_via_symbols

#--- ctor-weak.s
.globl __ZN7ForeignC1Ev
.weak_definition __ZN7ForeignC1Ev
.globl __ZN7ForeignC1Ev.mib_sized
.weak_definition __ZN7ForeignC1Ev.mib_sized
__ZN7ForeignC1Ev:
__ZN7ForeignC1Ev.mib_sized:
  retq

.section __DATA_CONST,__mib_sized_dep
.quad __ZN7ForeignC1Ev.mib_sized
.quad __ZTV7Foreign
.quad __ZTV7Foreign.mib_sized

.subsections_via_symbols

#--- ctor-strong.s
.globl __ZN7ForeignC1Ev
.globl __ZN7ForeignC1Ev.mib_sized
__ZN7ForeignC1Ev:
__ZN7ForeignC1Ev.mib_sized:
  retq

.subsections_via_symbols

#--- ctor-folded.s
.globl _main
_main:
  retq

.section __DATA_CONST,__mib_sized_ref
.quad __ZN7ForeignC1Ev
.quad __ZN7ForeignC1Ev

.subsections_via_symbols

#--- owner-gone.s
.globl _main
_main:
  retq

.data
.globl __ZTV7Foreign
__ZTV7Foreign:
  .quad 0

.section __DATA_CONST,__mib_sized_dep
.quad __ZN7ForeignC1Ev.mib_sized
.quad __ZTV7Foreign
.quad __ZTV7Foreign.mib_sized

.subsections_via_symbols

#--- ctor-member.s
.globl __ZN7ForeignC1Ev
.globl __ZN7ForeignC1Ev.mib_sized
__ZN7ForeignC1Ev:
__ZN7ForeignC1Ev.mib_sized:
  callq _unrelated
  retq

.subsections_via_symbols

#--- start.s
.globl _main
_main:
  retq

.subsections_via_symbols

#--- ctor-strong-unflagged.s
.globl __ZN7ForeignC1Ev
__ZN7ForeignC1Ev:
  retq

.globl _otherWeakDefinition
.weak_definition _otherWeakDefinition
_otherWeakDefinition:
  retq

.subsections_via_symbols

#--- dtor-strong-weakdefines.s
.globl __ZN7ForeignD0Ev
__ZN7ForeignD0Ev:
  retq

.globl _otherWeakDefinition
.weak_definition _otherWeakDefinition
_otherWeakDefinition:
  retq

.subsections_via_symbols

#--- weak-other.s
.globl _otherWeakDefinition
.weak_definition _otherWeakDefinition
_otherWeakDefinition:
  retq

.subsections_via_symbols

#--- hidden-override.s
.globl _otherWeakDefinition
.private_extern _otherWeakDefinition
_otherWeakDefinition:
  retq

.subsections_via_symbols

#--- ctor-user.s
.globl _user
_user:
  callq __ZN7ForeignC1Ev
  retq

.subsections_via_symbols

#--- start-uses.s
.globl _main
_main:
  callq __ZN7ForeignD0Ev
  retq

.subsections_via_symbols

#--- ctor-strong-only.s
.globl __ZN7ForeignC1Ev
__ZN7ForeignC1Ev:
  retq

.subsections_via_symbols

#--- ctor-weak-unflagged.s
.globl __ZN7ForeignC1Ev
.weak_definition __ZN7ForeignC1Ev
__ZN7ForeignC1Ev:
  retq

.subsections_via_symbols

#--- vtable-hidden.s
.data
.globl __ZTV7Foreign
.private_extern __ZTV7Foreign
__ZTV7Foreign:
  .quad 0

.subsections_via_symbols

#--- ctor-hidden.s
.globl __ZN7ForeignC1Ev
.private_extern __ZN7ForeignC1Ev
__ZN7ForeignC1Ev:
  retq

.subsections_via_symbols

#--- ctor.s
.globl __ZN7ForeignC1Ev
.globl __ZN7ForeignC1Ev.mib_sized
__ZN7ForeignC1Ev:
__ZN7ForeignC1Ev.mib_sized:
  retq

.section __DATA_CONST,__mib_sized_dep
.quad __ZN7ForeignC1Ev.mib_sized
.quad __ZTV7Foreign
.quad __ZTV7Foreign.mib_sized

.subsections_via_symbols

#--- vtable-flagged.s
.section __DATA,__data
.globl __ZTV7Foreign
.globl __ZTV7Foreign.mib_sized
__ZTV7Foreign:
__ZTV7Foreign.mib_sized:
  .quad __ZN7ForeignD0Ev

.section __DATA_CONST,__mib_sized_dep
.quad __ZTV7Foreign.mib_sized
.quad __ZN7ForeignD0Ev
.quad __ZN7ForeignD0Ev.mib_sized

.subsections_via_symbols

#--- vtable-weak-flagged.s
.section __DATA,__data
.globl __ZTV7Foreign
.weak_definition __ZTV7Foreign
.globl __ZTV7Foreign.mib_sized
.weak_definition __ZTV7Foreign.mib_sized
__ZTV7Foreign:
__ZTV7Foreign.mib_sized:
  .quad __ZN7ForeignD0Ev

.section __DATA_CONST,__mib_sized_dep
.quad __ZTV7Foreign.mib_sized
.quad __ZN7ForeignD0Ev
.quad __ZN7ForeignD0Ev.mib_sized

.subsections_via_symbols

#--- vtable-weak-unflagged.s
.section __DATA,__data
.globl __ZTV7Foreign
.weak_definition __ZTV7Foreign
__ZTV7Foreign:
  .quad __ZN7ForeignD0Ev

.subsections_via_symbols

#--- vtable-unflagged.s
.section __DATA,__data
.globl __ZTV7Foreign
__ZTV7Foreign:
  .quad __ZN7ForeignD0Ev

.subsections_via_symbols

#--- ctor-site-inlined.s
.globl _main
_main:
  leaq __ZTV7Foreign(%rip), %rax
  retq

.section __DATA_CONST,__mib_sized_ref
.quad __ZN7ForeignC1Ev
.quad __ZN7ForeignC1Ev.mib_sized

.subsections_via_symbols

#--- weakdep-w.s
	.section	__TEXT,__text,regular,pure_instructions
	.att_syntax
	.globl	__ZnwmPv
	.p2align	4
__ZnwmPv:
	pushq	%rbp
	movq	%rsp, %rbp
	movq	%rsi, %rax
	popq	%rbp
	retq
	.globl	__ZdlPv
	.p2align	4
__ZdlPv:
	pushq	%rbp
	movq	%rsp, %rbp
	popq	%rbp
	retq
	.globl	_otherw
	.p2align	4
_otherw:
	pushq	%rbp
	movq	%rsp, %rbp
	pushq	%rbx
	pushq	%rax
	movq	%rdi, %rbx
	movl	$1, %esi
	callq	__ZN1AC1Ei
	movq	%rbx, %rax
	addq	$8, %rsp
	popq	%rbx
	popq	%rbp
	retq
	.globl	__ZN1AC1Ei
	.weak_def_can_be_hidden	__ZN1AC1Ei
	.p2align	4
__ZN1AC1Ei:
	.globl	__ZN1AC1Ei.mib_sized
	.weak_def_can_be_hidden	__ZN1AC1Ei.mib_sized
__ZN1AC1Ei.mib_sized:
Lmib_sized_owner0:
	pushq	%rbp
	movq	%rsp, %rbp
	popq	%rbp
	jmp	__ZN1AC2Ei
	.globl	__ZN1AC2Ei
	.weak_def_can_be_hidden	__ZN1AC2Ei
	.p2align	4
__ZN1AC2Ei:
	.globl	__ZN1AC2Ei.mib_sized
	.weak_def_can_be_hidden	__ZN1AC2Ei.mib_sized
__ZN1AC2Ei.mib_sized:
Lmib_sized_owner1:
	pushq	%rbp
	movq	%rsp, %rbp
	movq	__ZTV1A@GOTPCREL(%rip), %rax
	addq	$16, %rax
	movq	%rax, (%rdi)
	popq	%rbp
	retq
	.globl	__ZN1AD1Ev
	.weak_def_can_be_hidden	__ZN1AD1Ev
	.p2align	4
__ZN1AD1Ev:
	pushq	%rbp
	movq	%rsp, %rbp
	popq	%rbp
	retq
	.globl	__ZN1AD0Ev
	.weak_def_can_be_hidden	__ZN1AD0Ev
	.p2align	4
__ZN1AD0Ev:
	.globl	__ZN1AD0Ev.mib_sized
	.weak_def_can_be_hidden	__ZN1AD0Ev.mib_sized
__ZN1AD0Ev.mib_sized:
	pushq	%rbp
	movq	%rsp, %rbp
	movq	%rdi, %rax
	andq	$-2, %rax
	movl	$16, %edx
	popq	%rbp
	retq
	.section	__DATA,__const
	.globl	__ZTV1A
	.weak_def_can_be_hidden	__ZTV1A
	.p2align	3, 0x0
__ZTV1A:
	.globl	__ZTV1A.mib_sized
	.weak_def_can_be_hidden	__ZTV1A.mib_sized
__ZTV1A.mib_sized:
Lmib_sized_owner2:
	.quad	0
	.quad	0
	.quad	__ZN1AD1Ev
	.quad	__ZN1AD0Ev
	.section	__DATA_CONST,__mib_sized_dep
	.p2align	3, 0x0
	.quad	Lmib_sized_owner0
	.quad	__ZN1AC2Ei
	.quad	__ZN1AC2Ei.mib_sized
	.quad	Lmib_sized_owner0
	.quad	__ZTV1A
	.quad	__ZTV1A.mib_sized
	.p2align	3, 0x0
	.quad	Lmib_sized_owner1
	.quad	__ZTV1A
	.quad	__ZTV1A.mib_sized
	.p2align	3, 0x0
	.quad	Lmib_sized_owner2
	.quad	__ZN1AD0Ev
	.quad	__ZN1AD0Ev.mib_sized
.subsections_via_symbols

#--- weakdep-s.s
	.section	__TEXT,__text,regular,pure_instructions
	.att_syntax
	.globl	__ZN1AD2Ev
	.p2align	4
__ZN1AD2Ev:
	pushq	%rbp
	movq	%rsp, %rbp
	popq	%rbp
	retq
	.globl	__ZN1AD1Ev
	.p2align	4
__ZN1AD1Ev:
	pushq	%rbp
	movq	%rsp, %rbp
	popq	%rbp
	retq
	.globl	__ZN1AD0Ev
	.p2align	4
__ZN1AD0Ev:
	.globl	__ZN1AD0Ev.mib_sized
__ZN1AD0Ev.mib_sized:
	pushq	%rbp
	movq	%rsp, %rbp
	movq	%rdi, %rax
	andq	$-2, %rax
	movl	$16, %edx
	popq	%rbp
	retq
	.globl	__ZdlPv
	.p2align	4
__ZdlPv:
	pushq	%rbp
	movq	%rsp, %rbp
	popq	%rbp
	retq
	.section	__DATA,__const
	.globl	__ZTV1A
	.p2align	3, 0x0
__ZTV1A:
	.globl	__ZTV1A.mib_sized
__ZTV1A.mib_sized:
Lmib_sized_owner0:
	.quad	0
	.quad	0
	.quad	__ZN1AD1Ev
	.quad	__ZN1AD0Ev
	.section	__DATA_CONST,__mib_sized_dep
	.p2align	3, 0x0
	.quad	Lmib_sized_owner0
	.quad	__ZN1AD0Ev
	.quad	__ZN1AD0Ev.mib_sized
.subsections_via_symbols

#--- weakdep-m.s
	.section	__TEXT,__text,regular,pure_instructions
	.att_syntax
	.globl	__ZnwmPv
	.p2align	4
__ZnwmPv:
	pushq	%rbp
	movq	%rsp, %rbp
	movq	%rsi, %rax
	popq	%rbp
	retq
	.globl	_mweak
	.p2align	4
_mweak:
	pushq	%rbp
	movq	%rsp, %rbp
	pushq	%rbx
	pushq	%rax
	movq	%rdi, %rbx
	movl	$1, %esi
	callq	__ZN1AC1Ei
	movq	%rbx, %rax
	addq	$8, %rsp
	popq	%rbx
	popq	%rbp
	retq
	.globl	_destroy
	.p2align	4
_destroy:
	pushq	%rbp
	movq	%rsp, %rbp
	pushq	%rbx
	pushq	%rax
	movq	%rsi, %rbx
	movq	(%rdi), %rax
	incq	%rdi
	callq	*8(%rax)
	movq	%rax, (%rbx)
	movq	%rdx, %rax
	addq	$8, %rsp
	popq	%rbx
	popq	%rbp
	retq
	.section	__DATA_CONST,__mib_sized_ref
	.p2align	3, 0x0
l___mib_sized_ref:
	.quad	__ZN1AC1Ei
	.quad	__ZN1AC1Ei.mib_sized
.subsections_via_symbols

#--- weakdep-h.s
	.section	__TEXT,__text,regular,pure_instructions
	.att_syntax
	.globl	__ZN1AD2Ev
	.p2align	4
__ZN1AD2Ev:
	pushq	%rbp
	movq	%rsp, %rbp
	popq	%rbp
	retq
	.globl	__ZN1AD1Ev
	.p2align	4
__ZN1AD1Ev:
	pushq	%rbp
	movq	%rsp, %rbp
	popq	%rbp
	retq
	.globl	__ZN1AD0Ev
	.p2align	4
__ZN1AD0Ev:
	pushq	%rbp
	movq	%rsp, %rbp
	popq	%rbp
	retq
	.globl	__ZdlPv
	.p2align	4
__ZdlPv:
	pushq	%rbp
	movq	%rsp, %rbp
	popq	%rbp
	retq
	.globl	__Z8identityIlET_S0_
	.weak_definition	__Z8identityIlET_S0_
	.p2align	4
__Z8identityIlET_S0_:
	pushq	%rbp
	movq	%rsp, %rbp
	movq	%rdi, %rax
	popq	%rbp
	retq
	.section	__DATA,__const
	.globl	__ZTV1A
	.p2align	3, 0x0
__ZTV1A:
	.quad	0
	.quad	0
	.quad	__ZN1AD1Ev
	.quad	__ZN1AD0Ev
.subsections_via_symbols

#--- flatweak-good.s
	.section	__TEXT,__text,regular,pure_instructions
	.att_syntax
	.globl	__Znwm
	.p2align	4
__Znwm:
	pushq	%rbp
	movq	%rsp, %rbp
	leaq	__ZL7storage(%rip), %rax
	popq	%rbp
	retq
	.globl	__ZdlPv
	.p2align	4
__ZdlPv:
	pushq	%rbp
	movq	%rsp, %rbp
	popq	%rbp
	retq
	.globl	__ZdlPvm
	.p2align	4
__ZdlPvm:
	pushq	%rbp
	movq	%rsp, %rbp
	popq	%rbp
	retq
	.globl	__ZN1AC2Ev
	.p2align	4
__ZN1AC2Ev:
	.globl	__ZN1AC2Ev.mib_sized
__ZN1AC2Ev.mib_sized:
Lmib_sized_owner0:
	pushq	%rbp
	movq	%rsp, %rbp
	leaq	__ZTV1A+16(%rip), %rax
	movq	%rax, (%rdi)
	movq	$123, 8(%rdi)
	popq	%rbp
	retq
	.globl	__ZN1AC1Ev
	.p2align	4
__ZN1AC1Ev:
	.globl	__ZN1AC1Ev.mib_sized
__ZN1AC1Ev.mib_sized:
Lmib_sized_owner1:
	pushq	%rbp
	movq	%rsp, %rbp
	popq	%rbp
	jmp	__ZN1AC2Ev
	.globl	__ZN1AD2Ev
	.p2align	4
__ZN1AD2Ev:
	pushq	%rbp
	movq	%rsp, %rbp
	popq	%rbp
	retq
	.globl	__ZN1AD1Ev
	.p2align	4
__ZN1AD1Ev:
	pushq	%rbp
	movq	%rsp, %rbp
	popq	%rbp
	retq
	.globl	__ZN1AD0Ev
	.p2align	4
__ZN1AD0Ev:
	.globl	__ZN1AD0Ev.mib_sized
__ZN1AD0Ev.mib_sized:
	pushq	%rbp
	movq	%rsp, %rbp
	movq	%rdi, %rax
	andq	$-2, %rax
	movl	$16, %edx
	popq	%rbp
	retq
	.globl	_flat_make
	.p2align	4
_flat_make:
	pushq	%rbp
	movq	%rsp, %rbp
	pushq	%rbx
	pushq	%rax
	leaq	__ZL7storage(%rip), %rbx
	movq	%rbx, %rdi
	callq	__ZN1AC1Ev
	movq	%rbx, %rax
	addq	$8, %rsp
	popq	%rbx
	popq	%rbp
	retq
	.globl	_destroy
	.p2align	4
_destroy:
	pushq	%rbp
	movq	%rsp, %rbp
	pushq	%rbx
	pushq	%rax
	movq	%rsi, %rbx
	movq	(%rdi), %rax
	incq	%rdi
	callq	*8(%rax)
	movq	%rax, (%rbx)
	movq	%rdx, %rax
	addq	$8, %rsp
	popq	%rbx
	popq	%rbp
	retq
	.globl	_empty
	.p2align	4
_empty:
	pushq	%rbp
	movq	%rsp, %rbp
	popq	%rbp
	retq
.zerofill __DATA,__bss,__ZL7storage,64,4
	.section	__DATA,__const
	.globl	__ZTV1A
	.p2align	3, 0x0
__ZTV1A:
	.globl	__ZTV1A.mib_sized
__ZTV1A.mib_sized:
Lmib_sized_owner2:
	.quad	0
	.quad	0
	.quad	__ZN1AD1Ev
	.quad	__ZN1AD0Ev
	.section	__DATA_CONST,__mib_sized_ref
	.p2align	3, 0x0
l___mib_sized_ref:
	.quad	__ZN1AC1Ev
	.quad	__ZN1AC1Ev.mib_sized
	.section	__DATA_CONST,__mib_sized_dep
	.p2align	3, 0x0
	.quad	Lmib_sized_owner0
	.quad	__ZTV1A
	.quad	__ZTV1A.mib_sized
	.p2align	3, 0x0
	.quad	Lmib_sized_owner1
	.quad	__ZN1AC2Ev
	.quad	__ZN1AC2Ev.mib_sized
	.quad	Lmib_sized_owner1
	.quad	__ZTV1A
	.quad	__ZTV1A.mib_sized
	.p2align	3, 0x0
	.quad	Lmib_sized_owner2
	.quad	__ZN1AD0Ev
	.quad	__ZN1AD0Ev.mib_sized
.subsections_via_symbols

#--- flatweak-bad.s
	.section	__TEXT,__text,regular,pure_instructions
	.att_syntax
	.globl	__Znwm
	.p2align	4
__Znwm:
	pushq	%rbp
	movq	%rsp, %rbp
	leaq	__ZL7storage(%rip), %rax
	popq	%rbp
	retq
	.globl	__ZdlPv
	.p2align	4
__ZdlPv:
	pushq	%rbp
	movq	%rsp, %rbp
	popq	%rbp
	retq
	.globl	__ZdlPvm
	.p2align	4
__ZdlPvm:
	pushq	%rbp
	movq	%rsp, %rbp
	popq	%rbp
	retq
	.globl	__ZN1AC2Ev
	.weak_def_can_be_hidden	__ZN1AC2Ev
	.p2align	4
__ZN1AC2Ev:
	pushq	%rbp
	movq	%rsp, %rbp
	movq	__ZTV1A@GOTPCREL(%rip), %rax
	addq	$16, %rax
	movq	%rax, (%rdi)
	movq	$123, 8(%rdi)
	popq	%rbp
	retq
	.globl	__ZN1AC1Ev
	.weak_def_can_be_hidden	__ZN1AC1Ev
	.p2align	4
__ZN1AC1Ev:
	pushq	%rbp
	movq	%rsp, %rbp
	popq	%rbp
	jmp	__ZN1AC2Ev
	.globl	__ZN1AD2Ev
	.weak_def_can_be_hidden	__ZN1AD2Ev
	.p2align	4
__ZN1AD2Ev:
	pushq	%rbp
	movq	%rsp, %rbp
	popq	%rbp
	retq
	.globl	__ZN1AD1Ev
	.weak_def_can_be_hidden	__ZN1AD1Ev
	.p2align	4
__ZN1AD1Ev:
	pushq	%rbp
	movq	%rsp, %rbp
	popq	%rbp
	jmp	__ZN1AD2Ev
	.globl	__ZN1AD0Ev
	.weak_def_can_be_hidden	__ZN1AD0Ev
	.p2align	4
__ZN1AD0Ev:
	pushq	%rbp
	movq	%rsp, %rbp
	popq	%rbp
	jmp	__ZN1AD1Ev
	.globl	_empty
	.p2align	4
_empty:
	pushq	%rbp
	movq	%rsp, %rbp
	popq	%rbp
	retq
.zerofill __DATA,__bss,__ZL7storage,64,4
	.section	__DATA,__const
	.globl	__ZTV1A
	.weak_def_can_be_hidden	__ZTV1A
	.p2align	3, 0x0
__ZTV1A:
	.quad	0
	.quad	0
	.quad	__ZN1AD1Ev
	.quad	__ZN1AD0Ev
	.no_dead_strip	__ZN1AC1Ev
	.no_dead_strip	__ZN1AC2Ev
	.no_dead_strip	__ZN1AD0Ev
	.no_dead_strip	__ZN1AD1Ev
	.no_dead_strip	__ZN1AD2Ev
.subsections_via_symbols

#--- flatweak-factory.s
	.section	__TEXT,__text,regular,pure_instructions
	.att_syntax
	.globl	__Znwm
	.p2align	4
__Znwm:
	pushq	%rbp
	movq	%rsp, %rbp
	leaq	__ZL7storage(%rip), %rax
	popq	%rbp
	retq
	.globl	__ZdlPv
	.p2align	4
__ZdlPv:
	pushq	%rbp
	movq	%rsp, %rbp
	popq	%rbp
	retq
	.globl	__ZdlPvm
	.p2align	4
__ZdlPvm:
	pushq	%rbp
	movq	%rsp, %rbp
	popq	%rbp
	retq
	.globl	__ZN1AC2Ev
	.weak_def_can_be_hidden	__ZN1AC2Ev
	.p2align	4
__ZN1AC2Ev:
	.globl	__ZN1AC2Ev.mib_sized
	.weak_def_can_be_hidden	__ZN1AC2Ev.mib_sized
__ZN1AC2Ev.mib_sized:
Lmib_sized_owner0:
	pushq	%rbp
	movq	%rsp, %rbp
	movq	__ZTV1A@GOTPCREL(%rip), %rax
	addq	$16, %rax
	movq	%rax, (%rdi)
	movq	$123, 8(%rdi)
	popq	%rbp
	retq
	.globl	__ZN1AC1Ev
	.weak_def_can_be_hidden	__ZN1AC1Ev
	.p2align	4
__ZN1AC1Ev:
	.globl	__ZN1AC1Ev.mib_sized
	.weak_def_can_be_hidden	__ZN1AC1Ev.mib_sized
__ZN1AC1Ev.mib_sized:
Lmib_sized_owner1:
	pushq	%rbp
	movq	%rsp, %rbp
	popq	%rbp
	jmp	__ZN1AC2Ev
	.globl	__ZN1AD2Ev
	.weak_def_can_be_hidden	__ZN1AD2Ev
	.p2align	4
__ZN1AD2Ev:
	pushq	%rbp
	movq	%rsp, %rbp
	popq	%rbp
	retq
	.globl	__ZN1AD1Ev
	.weak_def_can_be_hidden	__ZN1AD1Ev
	.p2align	4
__ZN1AD1Ev:
	pushq	%rbp
	movq	%rsp, %rbp
	popq	%rbp
	jmp	__ZN1AD2Ev
	.globl	__ZN1AD0Ev
	.weak_def_can_be_hidden	__ZN1AD0Ev
	.p2align	4
__ZN1AD0Ev:
	.globl	__ZN1AD0Ev.mib_sized
	.weak_def_can_be_hidden	__ZN1AD0Ev.mib_sized
__ZN1AD0Ev.mib_sized:
	pushq	%rbp
	movq	%rsp, %rbp
	pushq	%rbx
	pushq	%rax
	movq	%rdi, %rbx
	andq	$-2, %rbx
	movq	%rbx, %rdi
	callq	__ZN1AD1Ev
	movl	$16, %edx
	movq	%rbx, %rax
	addq	$8, %rsp
	popq	%rbx
	popq	%rbp
	retq
	.globl	_weak_make
	.p2align	4
_weak_make:
	pushq	%rbp
	movq	%rsp, %rbp
	pushq	%rbx
	pushq	%rax
	leaq	__ZL7storage(%rip), %rbx
	movq	%rbx, %rdi
	callq	__ZN1AC1Ev
	movq	%rbx, %rax
	addq	$8, %rsp
	popq	%rbx
	popq	%rbp
	retq
	.globl	_destroy
	.p2align	4
_destroy:
	pushq	%rbp
	movq	%rsp, %rbp
	pushq	%rbx
	pushq	%rax
	movq	%rsi, %rbx
	movq	(%rdi), %rax
	incq	%rdi
	callq	*8(%rax)
	movq	%rax, (%rbx)
	movq	%rdx, %rax
	addq	$8, %rsp
	popq	%rbx
	popq	%rbp
	retq
	.globl	_empty
	.p2align	4
_empty:
	pushq	%rbp
	movq	%rsp, %rbp
	popq	%rbp
	retq
.zerofill __DATA,__bss,__ZL7storage,64,4
	.section	__DATA,__const
	.globl	__ZTV1A
	.weak_def_can_be_hidden	__ZTV1A
	.p2align	3, 0x0
__ZTV1A:
	.globl	__ZTV1A.mib_sized
	.weak_def_can_be_hidden	__ZTV1A.mib_sized
__ZTV1A.mib_sized:
Lmib_sized_owner2:
	.quad	0
	.quad	0
	.quad	__ZN1AD1Ev
	.quad	__ZN1AD0Ev
	.section	__DATA_CONST,__mib_sized_ref
	.p2align	3, 0x0
l___mib_sized_ref:
	.quad	__ZN1AC1Ev
	.quad	__ZN1AC1Ev.mib_sized
	.no_dead_strip	__ZN1AC1Ev
	.no_dead_strip	__ZN1AC2Ev
	.no_dead_strip	__ZN1AD0Ev
	.no_dead_strip	__ZN1AD1Ev
	.no_dead_strip	__ZN1AD2Ev
	.section	__DATA_CONST,__mib_sized_dep
	.p2align	3, 0x0
	.quad	Lmib_sized_owner0
	.quad	__ZTV1A
	.quad	__ZTV1A.mib_sized
	.p2align	3, 0x0
	.quad	Lmib_sized_owner1
	.quad	__ZN1AC2Ev
	.quad	__ZN1AC2Ev.mib_sized
	.quad	Lmib_sized_owner1
	.quad	__ZTV1A
	.quad	__ZTV1A.mib_sized
	.p2align	3, 0x0
	.quad	Lmib_sized_owner2
	.quad	__ZN1AD0Ev
	.quad	__ZN1AD0Ev.mib_sized
.subsections_via_symbols

#--- flatweak-wrapper.s
	.section	__TEXT,__text,regular,pure_instructions
	.att_syntax
	.globl	__Znwm
	.p2align	4
__Znwm:
	pushq	%rbp
	movq	%rsp, %rbp
	leaq	__ZL7storage(%rip), %rax
	popq	%rbp
	retq
	.globl	__ZdlPv
	.p2align	4
__ZdlPv:
	pushq	%rbp
	movq	%rsp, %rbp
	popq	%rbp
	retq
	.globl	__ZdlPvm
	.p2align	4
__ZdlPvm:
	pushq	%rbp
	movq	%rsp, %rbp
	popq	%rbp
	retq
	.globl	_wrapper_make
	.p2align	4
_wrapper_make:
	pushq	%rbp
	movq	%rsp, %rbp
	popq	%rbp
	jmp	_weak_make
	.globl	_empty
	.p2align	4
_empty:
	pushq	%rbp
	movq	%rsp, %rbp
	popq	%rbp
	retq
.zerofill __DATA,__bss,__ZL7storage,64,4
.subsections_via_symbols

#--- flatweak-app.s
	.section	__TEXT,__text,regular,pure_instructions
	.att_syntax
	.globl	__Znwm
	.p2align	4
__Znwm:
	pushq	%rbp
	movq	%rsp, %rbp
	leaq	__ZL7storage(%rip), %rax
	popq	%rbp
	retq
	.globl	__ZdlPv
	.p2align	4
__ZdlPv:
	pushq	%rbp
	movq	%rsp, %rbp
	popq	%rbp
	retq
	.globl	__ZdlPvm
	.p2align	4
__ZdlPvm:
	pushq	%rbp
	movq	%rsp, %rbp
	popq	%rbp
	retq
	.globl	_probe
	.p2align	4
_probe:
	pushq	%rbp
	movq	%rsp, %rbp
	pushq	%rbx
	pushq	%rax
	movq	%rdi, %rbx
	callq	_wrapper_make
	movq	(%rax), %rcx
	leaq	1(%rax), %rdi
	callq	*8(%rcx)
	movq	%rax, (%rbx)
	movq	%rdx, %rax
	addq	$8, %rsp
	popq	%rbx
	popq	%rbp
	retq
	.globl	_empty
	.p2align	4
_empty:
	pushq	%rbp
	movq	%rsp, %rbp
	popq	%rbp
	retq
.zerofill __DATA,__bss,__ZL7storage,64,4
.subsections_via_symbols

#--- ctor-site.s
.globl _main
_main:
  retq

.section __DATA_CONST,__mib_sized_ref
.quad __ZN7ForeignC1Ev
.quad __ZN7ForeignC1Ev.mib_sized

.subsections_via_symbols

#--- ctor-site-data.s
.globl _main
_main:
  retq

.data
.quad __ZN7ForeignC1Ev

.section __DATA_CONST,__mib_sized_ref
.quad __ZN7ForeignC1Ev
.quad __ZN7ForeignC1Ev.mib_sized

.subsections_via_symbols

#--- flagged.s
.globl __ZN7ForeignD0Ev
.globl __ZN7ForeignD0Ev.mib_sized
__ZN7ForeignD0Ev:
__ZN7ForeignD0Ev.mib_sized:
  retq

.subsections_via_symbols

#--- unflagged.s
.globl __ZN7ForeignD0Ev
__ZN7ForeignD0Ev:
  retq

.subsections_via_symbols

#--- site-weak.s
.globl _main
_main:
  retq

.section __DATA_CONST,__mib_sized_ref
.quad __ZN7ForeignD0Ev
.quad __ZN7ForeignD0Ev.mib_sized

.text
.globl __ZN7ForeignD0Ev
.weak_definition __ZN7ForeignD0Ev
.globl __ZN7ForeignD0Ev.mib_sized
.weak_definition __ZN7ForeignD0Ev.mib_sized
__ZN7ForeignD0Ev:
__ZN7ForeignD0Ev.mib_sized:
  retq

.subsections_via_symbols

#--- weak-flagged.s
.globl __ZN7ForeignD0Ev
.weak_definition __ZN7ForeignD0Ev
.globl __ZN7ForeignD0Ev.mib_sized
.weak_definition __ZN7ForeignD0Ev.mib_sized
__ZN7ForeignD0Ev:
__ZN7ForeignD0Ev.mib_sized:
  retq

.subsections_via_symbols

#--- weak-unflagged.s
.globl __ZN7ForeignD0Ev
.weak_definition __ZN7ForeignD0Ev
__ZN7ForeignD0Ev:
  retq

.subsections_via_symbols

#--- lto-main.ll
target triple = "x86_64-apple-darwin"
target datalayout = "e-m:o-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"

@_ZN7ForeignD0Ev.mib_sized = external constant i8
@__mib_sized_ref = private constant [2 x ptr] [ptr @_ZN7ForeignD0Ev, ptr @_ZN7ForeignD0Ev.mib_sized], section "__DATA_CONST,__mib_sized_ref"
@llvm.compiler.used = appending global [1 x ptr] [ptr @__mib_sized_ref], section "llvm.metadata"

declare void @_ZN7ForeignD0Ev()

define void @main() {
  ret void
}

#--- lto-flagged.ll
target triple = "x86_64-apple-darwin"
target datalayout = "e-m:o-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"

define void @_ZN7ForeignD0Ev() #0 {
  ret void
}

attributes #0 = { "malterlib-sized" }

#--- lto-unflagged.ll
target triple = "x86_64-apple-darwin"
target datalayout = "e-m:o-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"

define void @_ZN7ForeignD0Ev() {
  ret void
}

#--- lto-inline-kept.ll
target triple = "x86_64-apple-darwin"
target datalayout = "e-m:o-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"

@_ZTV7Foreign = linkonce_odr unnamed_addr constant { [3 x ptr] } { [3 x ptr] [ptr null, ptr null, ptr @_ZN7ForeignD0Ev] }, !malterlib.sized.deps !0 #0
@__mib_sized_kept = internal constant [1 x ptr] [ptr @_ZN7ForeignD0Ev], section "llvm.metadata"
@llvm.compiler.used = appending global [1 x ptr] [ptr @__mib_sized_kept], section "llvm.metadata"

define linkonce_odr void @_ZN7ForeignD0Ev() unnamed_addr #1 {
  ret void
}

define ptr @make() {
  ret ptr @_ZTV7Foreign
}

attributes #0 = { "malterlib-sized" }
attributes #1 = { "malterlib-sized" }

!0 = !{i64 0, ptr @_ZN7ForeignD0Ev, null}

#--- lto-weak-flagged.ll
target triple = "x86_64-apple-darwin"
target datalayout = "e-m:o-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"

@__mib_sized_kept = internal constant [1 x ptr] [ptr @_ZN7ForeignD0Ev], section "llvm.metadata"
@llvm.compiler.used = appending global [1 x ptr] [ptr @__mib_sized_kept], section "llvm.metadata"

define linkonce_odr void @_ZN7ForeignD0Ev() #0 {
  ret void
}

attributes #0 = { "malterlib-sized" }

#--- lto-weak-unflagged.ll
target triple = "x86_64-apple-darwin"
target datalayout = "e-m:o-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"

@__mib_sized_kept = internal constant [1 x ptr] [ptr @_ZN7ForeignD0Ev], section "llvm.metadata"
@llvm.compiler.used = appending global [1 x ptr] [ptr @__mib_sized_kept], section "llvm.metadata"

define linkonce_odr void @_ZN7ForeignD0Ev() {
  ret void
}

#--- libctorstrongstub.tbd
--- !tapi-tbd
tbd-version: 4
targets: [ x86_64-macos ]
install-name: '/usr/lib/libctorstrongstub.dylib'
exports:
  - targets: [ x86_64-macos ]
    symbols: [ __ZN7ForeignC1Ev ]
...

#--- hidden-ctor.ll
target triple = "x86_64-apple-darwin"
target datalayout = "e-m:o-p270:32:32-p271:32:32-p272:64:64-i64:64-i128:128-f80:128-n8:16:32:64-S128"

define hidden void @_ZN7ForeignC1Ev() {
  ret void
}

#--- misplaced-marker.yaml
--- !mach-o
FileHeader:
  magic:           0xFEEDFACF
  cputype:         0x1000007
  cpusubtype:      0x3
  filetype:        0x6
  ncmds:           10
  sizeofcmds:      520
  flags:           0x100085
  reserved:        0x0
LoadCommands:
  - cmd:             LC_SEGMENT_64
    cmdsize:         152
    segname:         __TEXT
    vmaddr:          0
    vmsize:          4096
    fileoff:         0
    filesize:        4096
    maxprot:         5
    initprot:        5
    nsects:          1
    flags:           0
    Sections:
      - sectname:        __text
        segname:         __TEXT
        addr:            0x248
        size:            2
        offset:          0x248
        align:           0
        reloff:          0x0
        nreloc:          0
        flags:           0x80000400
        reserved1:       0x0
        reserved2:       0x0
        reserved3:       0x0
        content:         C3C3
  - cmd:             LC_SEGMENT_64
    cmdsize:         72
    segname:         __LINKEDIT
    vmaddr:          4096
    vmsize:          136
    fileoff:         4096
    filesize:        136
    maxprot:         1
    initprot:        1
    nsects:          0
    flags:           0
  - cmd:             LC_DYLD_INFO_ONLY
    cmdsize:         48
    rebase_off:      0
    rebase_size:     0
    bind_off:        0
    bind_size:       0
    weak_bind_off:   0
    weak_bind_size:  0
    lazy_bind_off:   0
    lazy_bind_size:  0
    export_off:      4096
    export_size:     48
  - cmd:             LC_SYMTAB
    cmdsize:         24
    symoff:          4152
    nsyms:           2
    stroff:          4184
    strsize:         48
  - cmd:             LC_DYSYMTAB
    cmdsize:         80
    ilocalsym:       0
    nlocalsym:       0
    iextdefsym:      0
    nextdefsym:      2
    iundefsym:       2
    nundefsym:       0
    tocoff:          0
    ntoc:            0
    modtaboff:       0
    nmodtab:         0
    extrefsymoff:    0
    nextrefsyms:     0
    indirectsymoff:  0
    nindirectsyms:   0
    extreloff:       0
    nextrel:         0
    locreloff:       0
    nlocrel:         0
  - cmd:             LC_ID_DYLIB
    cmdsize:         56
    dylib:
      name:            24
      timestamp:       0
      current_version: 0
      compatibility_version: 0
    Content:         '/usr/lib/libmisplaced.dylib'
    ZeroPadBytes:    5
  - cmd:             LC_UUID
    cmdsize:         24
    uuid:            4C4C44D3-5555-3144-A1F5-DABBE9CD1774
  - cmd:             LC_BUILD_VERSION
    cmdsize:         32
    platform:        1
    minos:           720896
    sdk:             720896
    ntools:          1
    Tools:
      - tool:            4
        version:         1507586
  - cmd:             LC_FUNCTION_STARTS
    cmdsize:         16
    dataoff:         4144
    datasize:        8
  - cmd:             LC_DATA_IN_CODE
    cmdsize:         16
    dataoff:         4152
    datasize:        0
LinkEditData:
  ExportTrie:
    TerminalSize:    0
    NodeOffset:      0
    Name:            ''
    Flags:           0x0
    Address:         0x0
    Other:           0x0
    ImportName:      ''
    Children:
      - TerminalSize:    3
        NodeOffset:      20
        Name:            __ZN7ForeignC1Ev
        Flags:           0x0
        Address:         0x248
        Other:           0x0
        ImportName:      ''
        Children:
          - TerminalSize:    3
            NodeOffset:      37
            Name:            .mib_sized
            Flags:           0x0
            Address:         0x249
            Other:           0x0
            ImportName:      ''
  NameList:
    - n_strx:          2
      n_type:          0xF
      n_sect:          1
      n_desc:          0
      n_value:         584
    - n_strx:          19
      n_type:          0xF
      n_sect:          1
      n_desc:          0
      n_value:         585
  StringTable:
    - ' '
    - __ZN7ForeignC1Ev
    - __ZN7ForeignC1Ev.mib_sized
    - ''
    - ''
  FunctionStarts:  [ 0x248 ]
...
