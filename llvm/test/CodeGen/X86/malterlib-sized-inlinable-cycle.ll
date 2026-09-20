; RUN: llc -mtriple=x86_64-windows-msvc < %s | FileCheck %s

; A definition the module only had the body of to inline, and what that
; depends on, is referenced only while code emitted for the module uses it: not
; a local vftable and a deleting destructor that only use each other, which
; the anchor of dependency records keeps, where the site only calls the
; imported constructor. A function that may be discarded uses what it uses
; only while it is used in turn.

declare dllimport void @imported(ptr)

@local_vftable = linkonce_odr constant [1 x ptr] [ptr @deleting]
@used_vftable = linkonce_odr constant [1 x ptr] [ptr null]

define linkonce_odr void @deleting(ptr %this) #2 {
  store ptr @local_vftable, ptr %this, !malterlib.sized.destructor-vptr !{}
  ret void
}

define linkonce_odr void @installer(ptr %this) {
  store ptr @used_vftable, ptr %this
  ret void
}

define void @site(ptr %p) {
  call void @imported(ptr %p)
  call void @installer(ptr %p)
  ret void
}

; But a function `llvm.used` keeps is emitted, also one a table it keeps holds,
; like a factory registered in a section, `[[gnu::used]]`.
declare void @kept_ctor(ptr)
declare void @registered_ctor(ptr)

define linkonce_odr void @kept_site(ptr %p) {
  call void @kept_ctor(ptr %p)
  ret void
}

define internal void @registered_factory(ptr %p) {
  call void @registered_ctor(ptr %p)
  ret void
}

@registry = internal constant ptr @registered_factory, section ".mib$m"

; And one another object can call through an alias, `alias("_ZL4makePv")`.
declare void @aliased_ctor(ptr)

define internal void @aliased_factory(ptr %p) {
  call void @aliased_ctor(ptr %p)
  ret void
}

@make_alias = alias void (ptr), ptr @aliased_factory

; And one the references of construction sites keep, like an exported
; constructor its callers inlined, which is emitted with the sites in it.
declare void @nested_ctor(ptr)

define linkonce_odr void @referenced_ctor(ptr %p) {
  call void @nested_ctor(ptr %p)
  ret void
}

@__mib_sized_ref = private constant [1 x ptr] [ptr @referenced_ctor], section ".rdata$mibszf"

; And a construction site only the anchor keeps, which another object can call:
; its code is emitted with the sites in it.
declare void @anchored_ctor(ptr)

define linkonce_odr void @anchored_site(ptr %p) #1 {
  call void @anchored_ctor(ptr %p)
  ret void
}

; And other code only the anchor keeps, like a delegating constructor a site
; was inlined in, but not a destructor, whose installing of vtables constructs
; nothing.
declare void @inlined_site_ctor(ptr)

define linkonce_odr void @anchored_delegating(ptr %p) {
  call void @inlined_site_ctor(ptr %p)
  ret void
}

; And one another object can call through a weak alias, `make` of
; `weak, alias("wrapper")`, which can be the copy the link keeps.
declare void @weak_aliased_ctor(ptr)

define internal void @weak_wrapper(ptr %p) {
  call void @weak_aliased_ctor(ptr %p)
  ret void
}

@weak_make = weak alias void (ptr), ptr @weak_wrapper

; But not a cycle a weak alias of a destructor reaches, like the vector
; deleting destructor of the MS ABI, which is used only where it is used in
; turn.
@weak_vftable = linkonce_odr constant [1 x ptr] [ptr @weak_deleting_alias]

define linkonce_odr void @weak_deleting(ptr %this) #2 {
  store ptr @weak_vftable, ptr %this, !malterlib.sized.destructor-vptr !{}
  ret void
}

@weak_deleting_alias = weak alias void (ptr), ptr @weak_deleting

; And a destructor whose code is emitted, kept by the anchor or callable
; through a weak alias, with what a site inlined in it uses, like the
; constructors it calls and the vtables it installs, but not the vtables the
; destructor installs itself while it destroys.
declare void @destructor_site_ctor(ptr)
@destructor_site_vftable = linkonce_odr constant [1 x ptr] [ptr null]
@destroyed_vftable = linkonce_odr constant [1 x ptr] [ptr null]

define linkonce_odr void @anchored_destructor(ptr %this) #2 {
  store ptr @destroyed_vftable, ptr %this, !malterlib.sized.destructor-vptr !{}
  call void @destructor_site_ctor(ptr %this)
  store ptr @destructor_site_vftable, ptr %this
  ret void
}

declare void @aliased_destructor_site_ctor(ptr)

define linkonce_odr void @aliased_destructor(ptr %this) #2 {
  call void @aliased_destructor_site_ctor(ptr %this)
  ret void
}

@aliased_destructor_alias = weak alias void (ptr), ptr @aliased_destructor

; But a destructor's store of a vtable pointer that is inlined in other code,
; whose construction of an object in the same storage the optimizer can make
; rely on it, `p->~A(); new (p) A;`, uses it.
@reset_vftable = linkonce_odr constant [1 x ptr] [ptr null]

define void @reset(ptr %p) {
  store ptr @reset_vftable, ptr %p, !malterlib.sized.destructor-vptr !{}
  ret void
}

; And one in a destructor a site was inlined in, which the inliner made a site
; too, whose construction of an object in the same storage the optimizer can
; make rely on it, `p->~A(); new (p) A;`.
@resetting_vftable = linkonce_odr constant [1 x ptr] [ptr null]

define linkonce_odr void @resetting_destructor(ptr %this) #3 {
  store ptr @resetting_vftable, ptr %this, !malterlib.sized.destructor-vptr !{}
  ret void
}

@__mib_sized_kept = internal constant [6 x ptr] [ptr @local_vftable, ptr @deleting, ptr @anchored_site, ptr @anchored_delegating, ptr @anchored_destructor, ptr @resetting_destructor], section "llvm.metadata"
@llvm.compiler.used = appending global [2 x ptr] [ptr @__mib_sized_kept, ptr @__mib_sized_ref], section "llvm.metadata"
@llvm.used = appending global [2 x ptr] [ptr @kept_site, ptr @registry], section "llvm.metadata"

!malterlib.sized.inlinable = !{!0, !1, !2, !3, !4, !5, !6, !7, !8, !9, !10, !11, !12, !13, !14, !15, !16, !17}
!0 = !{ptr @imported}
!1 = !{ptr @local_vftable}
!2 = !{ptr @deleting}
!3 = !{ptr @used_vftable}
!4 = !{ptr @kept_ctor}
!5 = !{ptr @registered_ctor}
!6 = !{ptr @aliased_ctor}
!7 = !{ptr @nested_ctor}
!8 = !{ptr @anchored_ctor}
!9 = !{ptr @weak_vftable}
!10 = !{ptr @inlined_site_ctor}
!11 = !{ptr @weak_aliased_ctor}
!12 = !{ptr @destructor_site_ctor}
!13 = !{ptr @aliased_destructor_site_ctor}
!14 = !{ptr @destructor_site_vftable}
!15 = !{ptr @destroyed_vftable}
!16 = !{ptr @reset_vftable}
!17 = !{ptr @resetting_vftable}

; CHECK: .section .rdata$mibszf,"dr"
; CHECK: .L__mib_sized_ref:
; CHECK-NEXT: .quad referenced_ctor
; CHECK: .quad imported
; CHECK-NEXT: .quad imported.mib_sized
; CHECK-NEXT: .quad used_vftable
; CHECK-NEXT: .quad used_vftable.mib_sized
; CHECK-NEXT: .quad kept_ctor
; CHECK-NEXT: .quad kept_ctor.mib_sized
; CHECK-NEXT: .quad registered_ctor
; CHECK-NEXT: .quad registered_ctor.mib_sized
; CHECK-NEXT: .quad aliased_ctor
; CHECK-NEXT: .quad aliased_ctor.mib_sized
; CHECK-NEXT: .quad nested_ctor
; CHECK-NEXT: .quad nested_ctor.mib_sized
; CHECK-NEXT: .quad anchored_ctor
; CHECK-NEXT: .quad anchored_ctor.mib_sized
; CHECK-NEXT: .quad inlined_site_ctor
; CHECK-NEXT: .quad inlined_site_ctor.mib_sized
; CHECK-NEXT: .quad weak_aliased_ctor
; CHECK-NEXT: .quad weak_aliased_ctor.mib_sized
; CHECK-NEXT: .quad destructor_site_ctor
; CHECK-NEXT: .quad destructor_site_ctor.mib_sized
; CHECK-NEXT: .quad aliased_destructor_site_ctor
; CHECK-NEXT: .quad aliased_destructor_site_ctor.mib_sized
; CHECK-NEXT: .quad destructor_site_vftable
; CHECK-NEXT: .quad destructor_site_vftable.mib_sized
; CHECK-NEXT: .quad reset_vftable
; CHECK-NEXT: .quad reset_vftable.mib_sized
; CHECK-NEXT: .quad resetting_vftable
; CHECK-NEXT: .quad resetting_vftable.mib_sized
; CHECK-NOT: .quad {{.*(local_vftable|deleting|weak_vftable|destroyed_vftable)}}

attributes #1 = { "malterlib-sized-site" }
attributes #2 = { "malterlib-sized-destructor" }
attributes #3 = { "malterlib-sized-destructor" "malterlib-sized-site" }
