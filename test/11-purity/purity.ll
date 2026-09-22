; RUN: opt -load-pass-plugin %plugin_purity -passes=purity-demo -S %s | FileCheck %s

define i64 @pure(i64 %a, i64 %b) {
entry:
  %s = add i64 %a, %b
  %t = mul i64 %s, 3
  ret i64 %t
}

define void @impure(ptr %p, i64 %v) {
entry:
  store i64 %v, ptr %p
  ret void
}

define i64 @caller(i64 %a, i64 %b) {
entry:
  %dead = call i64 @pure(i64 %a, i64 %b)
  ret i64 %b
}

; CHECK: [purity] 'pure' is memory(none)
; CHECK: [purity] removing dead call to 'pure'
; caller only calls a memory(none) function, so the fixpoint marks it too
; CHECK: marked 2 function(s) memory(none), removed 1 dead call(s)
; CHECK: memory(none)
; CHECK-NOT: call i64 @pure
