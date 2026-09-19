import os
import shutil

import lit.formats

config.name = "llvm-passes"
config.test_format = lit.formats.ShTest(execute_external=False)
config.suffixes = [".ll"]

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
BUILD = os.environ.get("BUILD_DIR", os.path.join(REPO, "build"))


def _find_opt():
    # $OPT wins, then whatever's on PATH, then the old in-repo install.
    env = os.environ.get("OPT")
    if env and os.path.isfile(env):
        return env
    on_path = shutil.which("opt")
    if on_path:
        return on_path
    fallback = os.path.join(REPO, "llvm-install", "bin", "opt")
    if os.path.isfile(fallback):
        return fallback
    lit_config.fatal(
        "can't find opt -- set $OPT or put llvm's bin dir on $PATH")


llvm_bin = os.path.dirname(os.path.abspath(_find_opt()))

# Tests just say `opt` and `FileCheck`; make sure both resolve to the LLVM
# we're actually testing against, not whatever else happens to be installed.
if config.environment is None:
    config.environment = dict(os.environ)
config.environment["PATH"] = llvm_bin + os.pathsep + config.environment.get("PATH", "")

# One %plugin_<tag> per pass so the RUN lines don't hardcode build paths.
PLUGINS = {
    "hello": ("01-hello-pass", "HelloPass"),
    "liveness": ("02-dataflow-liveness", "LivenessPass"),
    "alias": ("03-alias-analysis", "AliasDemoPass"),
    "memoryssa": ("04-memoryssa", "MemorySSADemo"),
    "dom": ("06-dominator", "DomDemo"),
    "licm": ("07-licm", "LICMDemo"),
    "instsimplify": ("09-instcombine-lite", "InstSimplifyDemo"),
    "gep": ("10-gep-datalayout", "GepDemo"),
    "purity": ("11-attributes", "PurityDemo"),
    "sccp": ("12-sccp", "SccpLite"),
    "inline": ("13-inline-cost", "InlineCostDemo"),
    "dce": ("14-global-dce", "DeadFuncElim"),
    "mem2reg": ("15-mem2reg", "LiteMem2Reg"),
    "dse": ("16-dse-memoryssa", "DseLite"),
    "gvn": ("17-gvn-lite", "GvnLite"),
}

missing = []
for tag, (subdir, target) in PLUGINS.items():
    path = os.path.join(BUILD, subdir, target + ".so")
    if not os.path.isfile(path):
        missing.append(path)
    config.substitutions.append(("%plugin_" + tag, '"%s"' % path))

mini_jit = os.path.join(BUILD, "05-orc-jit", "mini-jit")
if not os.path.isfile(mini_jit):
    missing.append(mini_jit)
config.substitutions.append(("%mini_jit", '"%s"' % mini_jit))

if missing:
    lit_config.fatal(
        "not built yet (run `ninja -C %s` first):\n  %s" % (BUILD, "\n  ".join(missing)))
