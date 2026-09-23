; SCCP demo: all conditions are constants, so most instructions fold after
; the cross-block propagation
define i32 @f(i32 %x) {
entry:
  %a = add i32 5, 6            ; 11
  br i1 true, label %then, label %else

then:
  %b = mul i32 %a, 2           ; 22
  br label %join

else:
  %c = mul i32 %a, 100         ; in a non-executable block, must NOT fold
  br label %join

join:
  %d = phi i32 [ %b, %then ], [ %c, %else ]
  %e = add i32 %d, 1
  %f = add i32 %e, %x          ; x is an argument -> over, stops the spread
  ret i32 %f
}
