; Instruction simplification demo: x = (a + 0) * 1, y = b - b, z = 3 + 4
define i64 @f(i64 %a, i64 %b) {
entry:
  %s1 = add i64 %a, 0        ; -> %a
  %s2 = mul i64 %s1, 1       ; -> %a
  %s3 = sub i64 %b, %b       ; -> 0
  %s4 = add i64 3, 4         ; -> 7
  %s5 = mul i64 %s4, %s4     ; constant fold -> 49
  %s6 = mul i64 %s2, 0       ; -> 0 (the whole chain below it disappears)
  %r = add i64 %s5, %s6      ; constant fold -> 49
  ret i64 %r
}
