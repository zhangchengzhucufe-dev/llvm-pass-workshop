; Purity inference demo: `pure` should get marked memory(none), `impure` not
define i64 @pure(i64 %a, i64 %b) {
entry:
  %s = add i64 %a, %b
  %t = mul i64 %s, 3
  ret i64 %t
}
define void @impure(ptr %p, i64 %v) {
entry:
  store i64 %v, ptr %p    ; writes global memory -> impure
  ret void
}
define i64 @caller(i64 %a, i64 %b) {
entry:
  %dead = call i64 @pure(i64 %a, i64 %b)  ; result unused -> dead call
  ret i64 %b
}
