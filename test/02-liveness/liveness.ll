; RUN: opt -load-pass-plugin %plugin_liveness -passes=liveness -disable-output %s | FileCheck %s

define i32 @f(i32 %n) {
entry:
  %s = alloca i32
  store i32 0, ptr %s
  br label %loop

loop:
  %v = load i32, ptr %s
  %next = add i32 %v, 1
  store i32 %next, ptr %s
  %c = icmp slt i32 %next, %n
  br i1 %c, label %loop, label %exit

exit:
  %r = load i32, ptr %s
  ret i32 %r
}

; %s is live around the back edge and read again in exit, but dead on
; entry: the first store in entry kills it before anything reads.
; CHECK: [entry]
; CHECK-NEXT: live-in : (none)
; CHECK-NEXT: live-out: %s
; CHECK: [loop]
; CHECK-NEXT: live-in : %s
; CHECK-NEXT: live-out: %s
; CHECK: [exit]
; CHECK-NEXT: live-in : %s
; CHECK-NEXT: live-out: (none)
