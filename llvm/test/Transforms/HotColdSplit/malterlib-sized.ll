; RUN: opt -passes=hotcoldsplit -hotcoldsplit-threshold=-1 -S < %s | FileCheck %s

; The marker of a definition, and those of the aliases it lists, belong to the
; definition alone, not to the code split out of it.

; CHECK: define void @_ZN1AC2Ev(i1 %c) #[[ORIG:[0-9]+]]
; CHECK: define internal void @_ZN1AC2Ev.cold.1() #[[COLD:[0-9]+]]
; CHECK: attributes #[[ORIG]] = { "malterlib-sized" "malterlib-sized-aliases"="_ZN1AC1Ev=0" }
; CHECK-NOT: malterlib-sized

define void @_ZN1AC2Ev(i1 %c) #0 {
entry:
  br i1 %c, label %cold, label %exit

cold:
  call void @sink()
  call void @sink()
  unreachable

exit:
  ret void
}

declare void @sink() cold

attributes #0 = { "malterlib-sized" "malterlib-sized-aliases"="_ZN1AC1Ev=0" }
