; RUN: opt -load-pass-plugin %plugin_mem2reg -passes=mem2reg-lite -S %s | FileCheck %s
; RUN: opt -passes=mem2reg -S %s | FileCheck %s --check-prefix=BUILTIN

; two def paths merging -> one real phi
define i32 @f(i32 %n) {
entry:
  %x = alloca i32
  store i32 5, ptr %x
  %c = icmp sgt i32 %n, 0
  br i1 %c, label %a, label %b

a:
  store i32 10, ptr %x
  br label %join

b:
  store i32 20, ptr %x
  br label %join

join:
  %v = load i32, ptr %x
  ret i32 %v
}

; only one def path: the phi ends up [7, %a], [undef, %entry], which is a
; trivial phi (undef means "any value", so 7 is a legal answer on every
; path) and gets folded away
define i32 @g(i32 %n) {
entry:
  %x = alloca i32
  %c = icmp sgt i32 %n, 0
  br i1 %c, label %a, label %join

a:
  store i32 7, ptr %x
  br label %join

join:
  %w = load i32, ptr %x
  ret i32 %w
}

; CHECK: mem2reg-lite: promoted 1 load(s) through 1 new phi(s), folded 0 trivial phi(s) in 'f'
; CHECK: folding trivial phi
; CHECK: mem2reg-lite: promoted 1 load(s) through 1 new phi(s), folded 1 trivial phi(s) in 'g'
; CHECK: %x.phi = phi i32 [ 10, %a ], [ 20, %b ]
; CHECK: ret i32 %x.phi
; CHECK-NOT: alloca
; CHECK: ret i32 7
; the built-in pass should agree on the important part: no allocas left
; BUILTIN: define i32 @f
; BUILTIN-NOT: alloca
