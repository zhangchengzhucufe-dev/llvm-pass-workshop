; RUN: opt -load-pass-plugin %plugin_hello -passes=hello -disable-output %s | FileCheck %s

define i32 @f(i32 %x) {
entry:
  %a = add i32 %x, 1
  ret i32 %a
}

; CHECK: ==================== hello: f ====================
; CHECK: basic blocks: 1, instructions: 2, calls: 0
; CHECK: loops (top level): 0
