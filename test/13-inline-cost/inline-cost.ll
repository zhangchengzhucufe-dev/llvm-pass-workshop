; RUN: opt -load-pass-plugin %plugin_inline -passes=inline-cost -disable-output %s | FileCheck %s

define internal i32 @small(i32 %x, i32 %unused) {
entry:
  %r = add i32 %x, 7
  ret i32 %r
}

define internal i32 @big(i32 %x) {
entry:
  %a = add i32 %x, 1
  %b = mul i32 %a, 2
  %c = add i32 %b, 3
  %d = mul i32 %c, 4
  %e = add i32 %d, 5
  %g = mul i32 %e, 6
  %h = add i32 %g, 7
  %i = mul i32 %h, 8
  %j = add i32 %i, 9
  %k = mul i32 %j, 10
  %l = add i32 %k, 11
  %m = mul i32 %l, 12
  %n = add i32 %m, 13
  %o = mul i32 %n, 14
  %p = add i32 %o, 15
  %q = mul i32 %p, 16
  %r = add i32 %q, 17
  %s = mul i32 %r, 18
  %t = add i32 %s, 19
  %u = mul i32 %t, 20
  ret i32 %u
}

define i32 @caller() {
entry:
  %r1 = call i32 @small(i32 3, i32 999)
  %r2 = call i32 @big(i32 1)
  %s = add i32 %r1, %r2
  ret i32 %s
}

; small: cost 2, the constant 3 kills the only use of %x -> bonus 2
; big: 21 instructions is way past the threshold even with the bonus
; CHECK: call 'small' at 'entry': cost=2, const-arg bonus=2 -> inline
; CHECK: call 'big' at 'entry': cost=21, const-arg bonus=2 -> keep
