# 08 pass pipeline experiments

Nothing to build here, it's all opt commands. With the new PM the pipeline is just a piece of text you compose in `-passes=`.

```
OPT=/home/zcz/llvm/llvm-build/bin/opt
```

Plugin passes and built-in passes mix freely; `function(...)` is the module-to-function adaptor:

```
$OPT -load-pass-plugin build/01-hello-pass/HelloPass.so \
     -passes='function(sroa,hello,instcombine,hello)' -S examples/loops.ll
```

The two hello runs report different instruction counts -- same pass, different position, different result.

Pairing liveness with mem2reg is fun:

```
$OPT -load-pass-plugin build/02-dataflow-liveness/LivenessPass.so \
     -passes='function(liveness,mem2reg,liveness)' -disable-output examples/trivial.ll
```

After mem2reg the hand-written liveness finds nothing (all the allocas got promoted into SSA registers). Running this pair is what made mem2reg click for me.

Diff around licm:

```
$OPT -S examples/licm.ll
$OPT -load-pass-plugin build/07-licm/LICMDemo.so -passes='function(licm-demo)' -S examples/licm.ll
```

The `%k = mul i32 %n, 3` lines should end up in the preheader.

To see what the real O2 pipeline actually runs:

```
$OPT -passes='default<O2>' -disable-output -print-passes examples/trivial.ll
```
