; RUN: opt -load-pass-plugin %plugin_sccp -passes=sccp-lite -S %s | FileCheck %s

define i32 @f(i32 %x) {
entry:
  %a = add i32 5, 6
  br i1 true, label %then, label %else

then:
  %b = mul i32 %a, 2
  br label %join

else:
  %c = mul i32 %a, 100
  br label %join

join:
  %d = phi i32 [ %b, %then ], [ %c, %else ]
  %e = add i32 %d, 1
  ret i32 %e
}

; a -> 11, b -> 22, and through the phi e -> 23, all in one pass. %c is
; in a block the solver proved unreachable, so it stays put (this lite
; version doesn't clean up unreachable blocks)
; CHECK: folded 3 instruction(s) in 'f'
; CHECK: %c = mul i32 11, 100
; CHECK: %d = phi i32 [ 22, %then ], [ %c, %else ]
; CHECK: ret i32 23
; CHECK-NOT: add i32 5, 6
; CHECK-NOT: %b = mul
