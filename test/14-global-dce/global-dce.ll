; RUN: opt -load-pass-plugin %plugin_dce -passes=dead-func-elim -S %s | FileCheck %s

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

@fp = global ptr @dangling

define i32 @main() {
entry:
  %v = call i32 @used()
  ret i32 %v
}

; helper1/helper2 call each other but nothing reaches them from the
; roots; @dangling survives because its address escaped into @fp
; CHECK: [dead-func] removing 'helper1'
; CHECK: [dead-func] removing 'helper2'
; CHECK: [dead-func] removing 'helper3'
; CHECK: dead-func-elim: removed 3 function(s)
; CHECK: @fp = global ptr @dangling
; CHECK-NOT: define internal i32 @helper
