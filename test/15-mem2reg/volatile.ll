; RUN: opt -load-pass-plugin %plugin_mem2reg -passes=mem2reg-lite -S %s | FileCheck %s
; RUN: opt -passes=mem2reg -S %s | FileCheck %s

; volatile accesses are observable in themselves, so the alloca has to
; stay in memory -- the builtin refuses to promote it, and so does
; mem2reg-lite (this exact case used to get promoted into "ret i32 5",
; silently erasing the volatile pair)
define i32 @vol(i32 %n) {
entry:
  %x = alloca i32
  store volatile i32 5, ptr %x
  %v = load volatile i32, ptr %x
  ret i32 %v
}

; atomics on a dead local alloca are a different story: the slot can't
; be shared with anyone, so both passes promote it to a plain value
define i32 @atom(i32 %n) {
entry:
  %x = alloca i32
  store atomic i32 5, ptr %x unordered, align 4
  %v = load atomic i32, ptr %x unordered, align 4
  ret i32 %v
}

; CHECK: store volatile i32 5, ptr %x
; CHECK: load volatile i32, ptr %x
; CHECK: ret i32 %v
; CHECK: ret i32 5
; CHECK-NOT: store atomic
