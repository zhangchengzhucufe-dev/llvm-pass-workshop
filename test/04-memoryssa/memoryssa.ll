; RUN: opt -load-pass-plugin %plugin_memoryssa -passes=memoryssa-demo -disable-output %s | FileCheck %s

define i32 @f() {
entry:
  %p = alloca i32
  store i32 1, ptr %p
  store i32 2, ptr %p
  %v = load i32, ptr %p
  ret i32 %v
}

; the load reads the *second* store: the first one is dead on the version
; chain, which is exactly what the walker should report
; CHECK: load in 'entry' reads result of: store i32 2, ptr %p
