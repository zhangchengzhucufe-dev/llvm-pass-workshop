# llvm-passes

[![ci](https://github.com/zhangchengzhucufe-dev/llvm-pass-workshop/actions/workflows/ci.yml/badge.svg)](https://github.com/zhangchengzhucufe-dev/llvm-pass-workshop/actions/workflows/ci.yml)

LLVM passes I wrote while teaching myself how LLVM actually works. Environment is a local build of llvm-project main (24.0.0git).

Every directory is a standalone `opt` plugin with no dependencies on the others, so jump straight to whichever one looks interesting. Build is the same for all of them:

```
cmake -B build -G Ninja -DLLVM_DIR=/home/zcz/llvm/llvm-build/lib/cmake/llvm -DCMAKE_BUILD_TYPE=Release
ninja -C build
```

## The directories

- **01-hello-pass** — the new-PM starter plugin, prints per-function stats. Also tried `registerOptimizerLastEPCallback` to inject the pass into default<O2>, and yes, it hooks in.
- **02-dataflow-liveness** — hand-written liveness: the textbook backward equations plus a worklist. Now I get why nobody does dataflow by hand outside a compiler course.
- **03-alias-analysis** — the alias analysis API from the consumer's side: query BasicAA over a bunch of store pairs, plus a toy DSE that only handles two *adjacent* stores.
- **04-memoryssa** — dumping the MemorySSA graph + walker queries. Read the output next to the .ll file, line by line.
- **05-orc-jit** — a calculator REPL whose parser emits IR directly, executed through LLJIT. Named functions too: `define f(x) = x * x` then `f(10)` runs through real JIT'd machine code (redefinitions pick the newest version, and recursive definitions work). I stepped on three landmines here, all documented in comments; the duplicate-symbol one kept me stuck for a while.
- **06-dominator** — the dominator tree. dominates() queries and walking up the idom chain. The dominance chain is really just "what must have already executed by the time this block runs".
- **07-licm** — a simplified loop-invariant code motion, my first pass that actually *changes* IR. Why you can't just hoist a division is spelled out in the comments.
- **08-pipeline** — no code, just a bunch of opt command experiments to compare against each other.
- **09-instcombine-lite** — constant folding + identities like x+0. Simplifications unlock each other, so there's a fixpoint loop on the outside.
- **10-gep-datalayout** — taking GEPs apart + struct layout. I did not know GEP doesn't read memory before writing this one.
- **11-attributes** — inferring memory(none) from function bodies, then deleting dead calls. This one went through the most revisions; see the gotchas below.
- **12-sccp** — a simplified sparse conditional constant propagation: three-level lattice + two worklists. It folds constants through ifs and skips never-executed branches entirely. Read it next to 02: one is dense, one is sparse.
- **13-inline-cost** — inline cost estimation, analysis-only. Cost approximates the callee's size; the benefit term is dead arguments from constant actuals. Doing actual inlining would mean writing clone + RAUW, which is its own project.
- **14-global-dce** — coloring the call graph from the module entry points and deleting unreachable internal functions. Functions whose address was taken (e.g. stored in a callback table) must survive; the comments cover why.
- **15-mem2reg** — the classic SSA construction: phi placement at iterated dominance frontiers + the rename walk over the dominator tree, then the Briggs-style cleanup that folds trivial phis away (iteratively, since folding one can trivialize another). My favorite of the bunch; it makes "why does SSA need phis" concrete.
- **16-dse-memoryssa** — dead store elimination on top of MemorySSA, the real version of 03's toy. The deadness check is: does the next clobbering access fully overwrite me, and did any load peek at my version in between?
- **17-gvn-lite** — dominance-based value numbering with a scoped hash table, commutativity canonicalization, and wrap-flag-aware keys. My first version shared one global table and happily merged values across *sibling* branches. The scope stack exists because of that bug.

## Running them

```
OPT=/home/zcz/llvm/llvm-build/bin/opt

$OPT -load-pass-plugin build/01-hello-pass/HelloPass.so -passes=hello -disable-output examples/loops.ll
$OPT -load-pass-plugin build/02-dataflow-liveness/LivenessPass.so -passes=liveness -disable-output examples/loops.ll
$OPT -load-pass-plugin build/03-alias-analysis/AliasDemoPass.so -passes=alias-demo -disable-output examples/trivial.ll
$OPT -load-pass-plugin build/04-memoryssa/MemorySSADemo.so -passes=memoryssa-demo -disable-output examples/trivial.ll
$OPT -load-pass-plugin build/06-dominator/DomDemo.so -passes=dom-demo -disable-output examples/loops.ll
$OPT -load-pass-plugin build/07-licm/LICMDemo.so -passes='function(licm-demo)' -S examples/licm.ll
$OPT -load-pass-plugin build/09-instcombine-lite/InstSimplifyDemo.so -passes=instsimplify-demo -S examples/instsimplify.ll
$OPT -load-pass-plugin build/10-gep-datalayout/GepDemo.so -passes=gep-demo -disable-output examples/struct.ll
$OPT -load-pass-plugin build/11-attributes/PurityDemo.so -passes=purity-demo -S examples/purity.ll
$OPT -load-pass-plugin build/12-sccp/SccpLite.so -passes=sccp-lite -S examples/sccp.ll
$OPT -load-pass-plugin build/13-inline-cost/InlineCostDemo.so -passes=inline-cost -disable-output examples/inline.ll
$OPT -load-pass-plugin build/14-global-dce/DeadFuncElim.so -passes=dead-func-elim -S examples/deadfunc.ll
$OPT -load-pass-plugin build/15-mem2reg/LiteMem2Reg.so -passes=mem2reg-lite -S examples/mem2reg.ll
$OPT -load-pass-plugin build/16-dse-memoryssa/DseLite.so -passes=dse-lite -S examples/dse.ll
$OPT -load-pass-plugin build/17-gvn-lite/GvnLite.so -passes=gvn-lite -S examples/gvn.ll
```

05 is an executable: `./build/05-orc-jit/mini-jit`. Type `1 + 2 * 3`, or define a function (`define f(x) = x * x + 2 * x + 1`) and call it (`f(10)` -> 121).

## Tests

Every pass has a lit + FileCheck test under `test/` -- the same test style LLVM itself uses. With the plugins built:

```
python3 -m pip install lit          # or use llvm-lit from an llvm build
OPT=/path/to/opt python3 -m lit test -v
```

or, if cmake found a lit binary, `ninja -C build check` does the same. The tests check the printed diagnostics *and* the transformed IR, so a pass that silently stops catching its pattern fails loudly.

## Real frontend output

`examples/run-demo.sh` compiles `examples/demo.c` with clang -O0 and runs the hand-written passes over it stage by stage (mem2reg -> instsimplify -> gvn -> dse), printing what each stage caught and how the instruction count moved. At the end it runs the same file through the built-in `default<O2>` for calibration -- the lite passes catch a fraction of what the real pipeline does, and the count at the bottom shows exactly how big that fraction is.

    ./examples/run-demo.sh

## CI

GitHub Actions (`.github/workflows/ci.yml`) builds LLVM from llvm-project main (opt + FileCheck, x86 only), builds every plugin, runs the lit suite and smoke-tests the JIT. Tracking main means the build catches API drift early. The first run compiles LLVM and takes a while; ccache makes later runs much cheaper.

Two comparisons worth running yourself:

```
# my mem2reg vs the real one, same file:
$OPT -load-pass-plugin build/15-mem2reg/LiteMem2Reg.so -passes=mem2reg-lite -S examples/mem2reg.ll
$OPT -passes=mem2reg -S examples/mem2reg.ll
```

## Gotchas I've collected so far

- The plugin entry header moved from `llvm/Passes/PassPlugin.h` to `llvm/Plugins/PassPlugin.h`. Every tutorial on the internet still has the old path.
- `Attribute::ReadNone` is gone -- the verifier rejects it with "does not apply to functions". The current spelling is `Attribute::getWithMemoryEffects(ctx, MemoryEffects::none())`.
- From 11: marking a function memory(none) is *not* enough to remove a dead call, because `mayHaveSideEffects` is `mayThrow || mayWrite || !willReturn`, so you need to infer nounwind and willreturn too. Figured this out by staring at debug output line by line.
- From 05: a Module handed to LLJIT is gone afterwards (ownership transferred); forgetting `InitializeNativeTarget()` gives you "no targets are registered"; and a second REPL evaluation used to die with "duplicate definition" -- weak symbols don't save you, unique names do. One more that bit quietly: the module and the LLVMContext it was built on have to move into the JIT *together* (`ThreadSafeModule(std::move(M), std::move(Ctx))`) -- pairing the module with a fresh context only works while the original context happens to still be alive.
- Out-of-tree plugins want `-fno-rtti` to match the main LLVM build, otherwise you get weird link errors. AddLLVM handles most of this.
- In this LLVM, `BranchInst` no longer exists: it's split into `UncondBrInst` and `CondBrInst`, so every old `isa<BranchInst>` needs rewriting. Hit this while writing 12.
- 12 again: my first SCCP folded nothing, because constant *literals* aren't instructions, so they had no entry in the lattice table and everything stayed unknown. The state lookup has to special-case ConstantInt -- the comment sits right at the fix in the code.
- 15 taught me phis need one incoming value per predecessor *edge*, not per predecessor block: a branch that jumps to the same block twice counts as two edges. The rename walk handles it naturally because it iterates successor edges.
- 16 taught me not to just `eraseFromParent()` a store that has a MemoryDef: MemorySSA is left holding a dangling access. Remove the access first (`MemorySSAUpdater::removeMemoryAccess`), then erase the instruction.
- Rebuilding against main in September: `PassInfoMixin` moved into `detail::` and split -- passes now inherit `RequiredPassInfoMixin`/`OptionalPassInfoMixin`, and the hand-written `isRequired()` goes away.
- Same sweep: `DenseMapInfo<T *>` lost `getEmptyKey`/`getTombstoneKey`, so custom key info rolls its own sentinels (17); `moveBefore` and `PHINode::Create` take `InsertPosition`/iterators instead of raw instructions (07, 15); `llvm/IR/Operator.h` had to be included explicitly for `OverflowingBinaryOperator` (17).
- In `llvm_map_components_to_libnames` the component is lowercase `native` (host target). Uppercase `Native` happily maps to a library named LLVMNative that does not exist, and the linker error makes no sense until you look (05).
- lit 23 deprecated `execute_external=True`; the internal shell handles everything these tests need (pipes, redirects), it just doesn't do `( a; b )` subshells -- 05's RUN lines build an input file instead.

## examples/

trivial.ll is the smallest one, loops.ll has a loop, and licm.ll was made specifically for 07 (loops.ll has no invariants -- the first time I ran 07 on it, it hoisted 0 instructions, which is when I realized I needed a dedicated example). The newer ones: mem2reg.ll has a branchy function and a loop so both phi placements show up; dse.ll goes from trivially dead stores to ones only AA can justify; gvn.ll includes a sibling-block case that's specifically there to punish a global hash table.
