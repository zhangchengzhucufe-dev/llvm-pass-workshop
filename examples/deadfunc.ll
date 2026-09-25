; Dead function removal: main calls used; helper1/helper2 call each other
; but nobody calls either of them
define internal i32 @helper1() {
entry:
  ret i32 1
}
define internal i32 @helper2() {
entry:
  %v = call i32 @helper1()
  ret i32 %v
}
define internal i32 @helper3() {
entry:
  ret i32 3
}
define internal i32 @used() {
entry:
  ret i32 4
}
define internal i32 @dangling() {
entry:
  ret i32 5
}
@fp = global ptr @dangling          ; address taken -> must NOT be removed
define i32 @main() {
entry:
  %v = call i32 @used()
  ret i32 %v
}
