; RUN: opt -passes=globalopt -S < %s | FileCheck %s

; An aliasee that takes the name of its alias keeps the marker of its own
; name, as the marker of an alias of that name, which the references to it
; resolve to.

; CHECK: define dso_local void @_ZN1AC1Ev() {{.*}}#[[CTOR:[0-9]+]]
; CHECK: attributes #[[CTOR]] = { "malterlib-sized" "malterlib-sized-aliases"="_ZN1AC1Ev=0,_ZN1AC2Ev=0" }

@_ZN1AC1Ev = dso_local unnamed_addr alias void (), ptr @_ZN1AC2Ev

define internal void @_ZN1AC2Ev() #0 {
  ret void
}

define void @use() {
  call void @_ZN1AC1Ev()
  ret void
}

attributes #0 = { "malterlib-sized" "malterlib-sized-aliases"="_ZN1AC1Ev=0" }
