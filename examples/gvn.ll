; Value numbering cases: same-block duplicates, commuted operands,
; cross-block duplicates that need the dominator tree, and a case where
; wrap flags must NOT be merged.
define i32 @redundant(i32 %x, i32 %y) {
entry:
  %a = add i32 %x, %y
  %b = add i32 %x, %y          ; same block -> collapses into %a
  %c = add i32 %y, %x          ; commuted twin of %a -> collapses too
  %d = mul i32 %a, %b          ; operands became (%a, %a)
  %e = mul i32 %a, %a          ; now matches %d -> collapses
  %cmp = icmp sgt i32 %x, 0
  br i1 %cmp, label %left, label %right

left:
  %f = mul i32 %a, %a          ; entry dominates left -> replaced by %d
  br label %join

right:
  %g = mul i32 %a, %a          ; also %d. NOT %f: left is a sibling, and
                               ; siblings don't dominate each other. This
                               ; is exactly what the scope stack is for.
  br label %join

join:
  %h = phi i32 [ %f, %left ], [ %g, %right ]
  ret i32 %h
}

define i32 @flagsmatter(i32 %x, i32 %y) {
entry:
  %a = add nsw i32 %x, %y
  %b = add i32 %x, %y          ; different wrap flags -> kept separate
  %c = add i32 %a, %b
  ret i32 %c
}
