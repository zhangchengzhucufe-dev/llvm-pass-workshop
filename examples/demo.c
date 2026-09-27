// Input for run-demo.sh: compiled with clang -O0, then fed through the
// hand-written passes stage by stage. Written so every stage gets
// something to do.

// allocas + a loop: mem2reg-lite turns the stack slots into phis
int running_sum(int n) {
  int total = 0;
  for (int i = 0; i < n; i++)
    total = total + i;
  return total;
}

// the first store is dead: dse-lite drops it
void fill(int *out, int v) {
  *out = 0;
  *out = v;
}

// commuted + repeated adds: gvn-lite collapses them into one
int twice(int x, int y) {
  int a = x + y;
  int b = y + x;
  int c = x + y;
  return a + b + c;
}

// identities first (x*1, x-x), then constant folding on what they expose
int fold_me(int n) {
  int k = n * 1; // instsimplify-lite: x * 1 -> x
  int m = k - k; // instsimplify-lite: x - x -> 0
  if (m == 0)
    return m + 42; // becomes 0 + 42 after the two above
  return -1;
}
