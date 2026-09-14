; LICM demo: the loop computes invariant things twice
;   %k = %n * 3        (invariant, should be hoisted)
;   %j = 5 * 5         (constant-folding candidate, invariant)
define i32 @invariant(i32 %n, i32 %m) {
entry:
  br label %head

head:
  %i = phi i32 [ 0, %entry ], [ %inext, %body ]
  %acc = phi i32 [ 0, %entry ], [ %anext, %body ]
  %cont = icmp slt i32 %i, %n
  br i1 %cont, label %body, label %done

body:
  %k = mul i32 %n, 3        ; invariant: only depends on stuff from outside
  %j = mul i32 5, 5         ; invariant: a constant
  %t = mul i32 %k, %j       ; invariant: operands are invariants from this round
  %tmp = add i32 %t, %m
  %anext = add i32 %acc, %tmp
  %inext = add i32 %i, 1
  br label %head

done:
  ret i32 %acc
}
