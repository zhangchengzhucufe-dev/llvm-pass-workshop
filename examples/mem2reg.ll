; Two flavors of stack variables for the mem2reg pass:
;   f()     -- branches: needs a phi at the merge
;   count() -- a loop: needs phis at the loop header
; Watch the pass print every load it replaces, then compare with what the
; built-in `mem2reg` produces on the same file.
define i32 @f(i32 %n) {
entry:
  %x = alloca i32
  %y = alloca i32
  store i32 5, ptr %y
  %c = icmp sgt i32 %n, 0
  br i1 %c, label %then, label %else

then:
  store i32 10, ptr %x
  store i32 %n, ptr %y
  br label %merge

else:
  store i32 20, ptr %x
  br label %merge

merge:
  %v = load i32, ptr %x       ; both defs reach here -> phi (10 / 20)
  %w = load i32, ptr %y       ; def in entry + def in then -> phi (5 / %n)
  %s = add i32 %v, %w
  ret i32 %s
}

define i32 @count(i32 %n) {
entry:
  %i = alloca i32
  %acc = alloca i32
  store i32 0, ptr %i
  store i32 0, ptr %acc
  br label %loop

loop:
  %iv = load i32, ptr %i
  %sum = load i32, ptr %acc
  %done = icmp sge i32 %iv, %n
  br i1 %done, label %exit, label %body

body:
  %next = add i32 %iv, 1
  store i32 %next, ptr %i
  %acc2 = add i32 %sum, %iv
  store i32 %acc2, ptr %acc
  br label %loop

exit:
  %result = load i32, ptr %acc
  ret i32 %result
}
