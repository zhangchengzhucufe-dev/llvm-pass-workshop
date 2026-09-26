; RUN: opt -load-pass-plugin %plugin_dse -passes=dse-lite -S %s | FileCheck %s

define i32 @f() {
entry:
  %p = alloca i32
  %q = alloca i32
  store i32 1, ptr %p
  store i32 2, ptr %p
  store i32 7, ptr %q
  %v = load i32, ptr %p
  %w = load i32, ptr %q
  %s = add i32 %v, %w
  store i32 9, ptr %p
  store i32 10, ptr %p
  ret i32 %s
}

; store 1 dies to the overwrite by 2, store 9 to the overwrite by 10 --
; and the unrelated store/load on %q in between doesn't save either of
; them. The loads keep stores 2 and 7 alive, and store 10 survives only
; because this lite version doesn't reason past the end of the block.
; CHECK: removing dead store: store i32 1, ptr %p
; CHECK: removing dead store: store i32 9, ptr %p
; CHECK: removed 2 dead store(s)
; CHECK: removed 0 dead store(s) in 'narrow_kill'
; CHECK: store i32 2, ptr %p
; CHECK: store i32 7, ptr %q
; CHECK: store i32 10, ptr %p
; CHECK-NOT: store i32 1, ptr %p
; CHECK-NOT: store i32 9, ptr %p
; CHECK: ret i32 %s

; MustAlias is "same address", not "same size": the i32 store only
; overwrites the low half of the i64 one, and the high half is read
; afterwards -- deleting the i64 store would hand back stack garbage,
; so it has to survive.
define i32 @narrow_kill(i64 %a, i32 %b) {
entry:
  %p = alloca i64
  store i64 %a, ptr %p
  store i32 %b, ptr %p
  %q = getelementptr i8, ptr %p, i64 4
  %r = load i32, ptr %q
  ret i32 %r
}

; CHECK-LABEL: define i32 @narrow_kill
; CHECK: store i64 %a, ptr %p
; CHECK: store i32 %b, ptr %p
; CHECK: ret i32 %r
