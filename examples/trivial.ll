; The simplest example: one function, two blocks, no loop.
define i32 @trivial(i32 %x) {
entry:
  %a = alloca i32
  %b = alloca i32
  store i32 %x, ptr %a
  store i32 7, ptr %a          ; overwrites the previous store -> dead store
  %v1 = load i32, ptr %a
  store i32 %v1, ptr %b
  br label %exit

exit:
  %v2 = load i32, ptr %b
  ret i32 %v2
}
