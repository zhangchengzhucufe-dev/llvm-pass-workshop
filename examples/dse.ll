; Dead store cases for the dse-lite pass, from easiest to meanest.
@g = global i32 0

define i32 @straightline() {
entry:
  %a = alloca i32
  %b = alloca i32
  store i32 1, ptr %a          ; dead: overwritten right away
  store i32 2, ptr %a          ; live: read below
  store i32 7, ptr %b          ; dead: overwritten before anyone reads b
  store i32 9, ptr %b
  %v1 = load i32, ptr %a
  %v2 = load i32, ptr %b
  store i32 3, ptr %a          ; dead: nobody reads a after this point
  %s = add i32 %v1, %v2
  ret i32 %s
}

define void @unrelated(i32 %n) {
entry:
  %p = alloca i32
  %q = alloca i32
  store i32 %n, ptr %p
  store i32 5, ptr %q          ; unrelated location: gets skipped, p still dies
  store i32 6, ptr %p
  ret void
}

define i32 @notdead(i32 %n) {
entry:
  %p = alloca i32
  store i32 %n, ptr %p
  %v = load i32, ptr %p        ; this read keeps the store alive
  ret i32 %v
}
