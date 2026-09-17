; RUN: opt -load-pass-plugin %plugin_instsimplify -passes=instsimplify-demo -S %s | FileCheck %s

define i64 @f(i64 %a, i64 %b) {
entry:
  %s1 = add i64 %a, 0
  %s2 = mul i64 %s1, 1
  %s3 = sub i64 %b, %b
  %s4 = add i64 3, 4
  %s5 = mul i64 %s4, %s4
  %s6 = mul i64 %s2, 0
  %r = add i64 %s5, %s6
  ret i64 %r
}

; every simplification feeds the next one, so the fixpoint loop has to
; chew through the whole chain before %r collapses to 49
; CHECK: simplified 7 instruction(s) in 'f'
; CHECK: define i64 @f
; CHECK-NOT: add i64
; CHECK-NOT: mul i64
; CHECK: ret i64 49
