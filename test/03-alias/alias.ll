; RUN: opt -load-pass-plugin %plugin_alias -passes=alias-demo -S %s | FileCheck %s

define i32 @f(i32 %x) {
entry:
  %a = alloca i32
  %b = alloca i32
  store i32 %x, ptr %a
  store i32 7, ptr %a
  %v = load i32, ptr %a
  store i32 %v, ptr %b
  %w = load i32, ptr %b
  ret i32 %w
}

define void @narrow(i64 %a, i32 %b) {
entry:
  %p = alloca i64
  store i64 %a, ptr %p
  store i32 %b, ptr %p
  ret void
}

; the two stores to %a must alias, everything across %a/%b must not
; CHECK: store#0 vs store#1: MustAlias
; CHECK: store#0 vs store#2: NoAlias
; CHECK: store#1 vs store#2: NoAlias
; CHECK: [dse-demo] removed dead store #0
; CHECK: pairs=3, NoAlias=2, MustAlias=1, dead stores removed=1

; same address -> MustAlias, but an i32 store only covers the low half
; of the i64 one, so nothing is dead here
; CHECK: store#0 vs store#1: MustAlias
; CHECK-NOT: [dse-demo]
; CHECK: pairs=1, NoAlias=0, MustAlias=1, dead stores removed=0

; CHECK: store i32 7, ptr %a
; CHECK-NOT: store i32 %x, ptr %a
; CHECK: define void @narrow
; CHECK: store i64 %a, ptr %p
; CHECK: store i32 %b, ptr %p
