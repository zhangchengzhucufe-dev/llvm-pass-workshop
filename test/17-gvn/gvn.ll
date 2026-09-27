; RUN: opt -load-pass-plugin %plugin_gvn -passes=gvn-lite -S %s | FileCheck %s

define i32 @f(i32 %x, i32 %y) {
entry:
  %a = add i32 %x, %y
  %b = add i32 %y, %x
  %cmp = icmp sgt i32 %x, 0
  br i1 %cmp, label %l, label %r

l:
  %c = add i32 %x, %y
  br label %join

r:
  %d = add i32 %y, %x
  br label %join

join:
  %e = add i32 %x, %y
  ret i32 %e
}

; b (commuted, same block), c and d (from entry, which dominates both
; sides), and e (entry again, after l and r's scopes popped) all collapse
; into %a. The l/r pair is the scope-stack test: l and r don't dominate
; each other, so c must never replace d or the other way around.
; CHECK: eliminated 4 redundant instruction(s) in 'f'

; nsw changes what the instruction can do on overflow, so these two are
; *different values* and both stay
; CHECK: eliminated 0 redundant instruction(s) in 'flags'
; CHECK: %a = add i32 %x, %y
; CHECK: ret i32 %a
; CHECK-NOT: %c = add i32 %x, %y
; CHECK-NOT: %d = add i32 %y, %x
; CHECK-NOT: %e = add i32 %x, %y
; CHECK: %b = add i32 %x, %y
; CHECK: %c = add i32 %a, %b
; CHECK: ret i32 %c

define i32 @flags(i32 %x, i32 %y) {
entry:
  %a = add nsw i32 %x, %y
  %b = add i32 %x, %y
  %c = add i32 %a, %b
  ret i32 %c
}
