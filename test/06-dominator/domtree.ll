; RUN: opt -load-pass-plugin %plugin_dom -passes=dom-demo -disable-output %s | FileCheck %s

define i32 @f(i32 %n) {
entry:
  br label %head

head:
  %c = icmp sgt i32 %n, 0
  br i1 %c, label %body, label %exit

body:
  br label %head

exit:
  ret i32 0
}

; CHECK: entry dominates head ? yes
; CHECK: entry dominates body ? yes
; CHECK: entry dominates exit ? yes
; the chain is the set of blocks that must have run when this one runs
; CHECK: head : entry -> head
; CHECK: body : entry -> head -> body
; CHECK: exit : entry -> head -> exit
