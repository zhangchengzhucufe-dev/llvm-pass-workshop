; Loop + branch + stack variables, for watching liveness and LoopInfo.
define i32 @sum_odd(i32 %n) {
entry:
  %sum = alloca i32
  %i = alloca i32
  store i32 0, ptr %sum
  store i32 0, ptr %i
  br label %head

head:
  %iv = load i32, ptr %i
  %cont = icmp slt i32 %iv, %n
  br i1 %cont, label %body, label %done

body:
  %acc = load i32, ptr %sum
  %next = add i32 %iv, 1
  store i32 %next, ptr %i
  br i1 true, label %add, label %skip

add:
  %newsum = add i32 %acc, %iv
  store i32 %newsum, ptr %sum
  br label %skip

skip:
  br label %head

done:
  %res = load i32, ptr %sum
  ret i32 %res
}
