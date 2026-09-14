; RUN: opt -load-pass-plugin %plugin_licm -passes=licm-demo -S %s | FileCheck %s

define i32 @invariant(i32 %n, i32 %m) {
entry:
  br label %head

head:
  %i = phi i32 [ 0, %entry ], [ %inext, %body ]
  %acc = phi i32 [ 0, %entry ], [ %anext, %body ]
  %cont = icmp slt i32 %i, %n
  br i1 %cont, label %body, label %done

body:
  %k = mul i32 %n, 3
  %j = mul i32 5, 5
  %t = mul i32 %k, %j
  %tmp = add i32 %t, %m
  %anext = add i32 %acc, %tmp
  %inext = add i32 %i, 1
  br label %head

done:
  ret i32 %acc
}

; k/j/t depend only on values from outside, tmp only on t and an argument
; CHECK: licm-demo: hoisted 4 instruction(s) out of loops in 'invariant'
; CHECK: entry:
; CHECK-NEXT: %k = mul i32 %n, 3
; CHECK-NEXT: %j = mul i32 5, 5
; CHECK-NEXT: %t = mul i32 %k, %j
; CHECK-NEXT: %tmp = add i32 %t, %m
; CHECK-NEXT: br label %head
