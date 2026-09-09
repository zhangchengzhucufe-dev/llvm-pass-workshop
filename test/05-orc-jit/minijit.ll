; RUN: echo '1 + 2 * 3' > %t && echo '2 * (3 + 4)' >> %t && echo quit >> %t && %mini_jit < %t | FileCheck %s
; RUN: echo 'define f(x) = x * x + 2 * x + 1' > %t && echo 'f(10)' >> %t && echo 'f(3) + f(4)' >> %t && echo quit >> %t && %mini_jit < %t | FileCheck %s --check-prefix=DEF
; malformed input: every line errors out, but the REPL has to stay alive
; and exit cleanly. An unchecked null operand here used to take the
; whole process down ("1 +" was enough), and "1 2" used to silently
; evaluate to 1 with junk left in the token stream.
; RUN: echo '1 +' > %t && echo '1 *' >> %t && echo '(2 +' >> %t && echo '1 2' >> %t && echo 'g(1)' >> %t && echo quit >> %t && %mini_jit < %t > %t.out 2>&1
; RUN: grep -c "skipping" %t.out

; CHECK: = 7
; CHECK: = 14

; the definition goes through the JIT as a real function, and the calls
; resolve against it from a later module
; DEF: defined f/1
; DEF: = 121
; DEF: = 41
