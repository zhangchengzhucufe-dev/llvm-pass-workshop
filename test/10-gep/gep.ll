; RUN: opt -load-pass-plugin %plugin_gep -passes=gep-demo -disable-output %s | FileCheck %s

target datalayout = "e-m:e-p270:32:32-p271:32:32-p272:64:64-i64:64-f80:128-n8:16:32:64-S128"

%struct.Point = type { i8, i32, i64, i8 }

define i64 @get_z(ptr %p) {
entry:
  %fz = getelementptr %struct.Point, ptr %p, i32 0, i32 2
  %v = load i64, ptr %fz
  ret i64 %v
}

; i32 needs 4-byte alignment so it can't sit at offset 1; the i64 lands
; at 8, the trailing i8 stretches the total to 24
; CHECK: pointer size = 64 bits
; CHECK: field 0 (i8): offset=0, size=1
; CHECK: field 1 (i32): offset=4, size=4
; CHECK: field 2 (i64): offset=8, size=8
; CHECK: field 3 (i8): offset=16, size=1
; CHECK: total size = 24 (including tail padding)
; CHECK: constant byte offset from base: 8
