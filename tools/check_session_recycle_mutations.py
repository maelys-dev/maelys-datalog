#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Rebuild idle-storage safety mutants under ASan/UBSan; retain every log."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[1]
UNIT="src/runtime/maelys_datalog_runtime.c"
CACHE="src/runtime/maelys_datalog_recycle.inc"
MUTATIONS=[
    ("cache_hit_allocates",CACHE,"if (p) return p;","if (p) { free(p); return malloc(bytes); }","warm_and_live"),
    ("wrong_size_reused",CACHE,"recycle_bytes == bytes","recycle_bytes != 0","sized_and_caller"),
    ("live_slot_not_detached",CACHE,"p = recycle_storage; recycle_storage = NULL; recycle_bytes = 0;","p = recycle_storage;","warm_and_live"),
    ("evicted_block_leaked",CACHE,"            if (old) free(old);","            (void)old;","warm_and_live"),
    ("oversized_block_retained",CACHE,"bytes <= maelys_datalog_session_recycle_bound()","bytes != 0","oversized_provider"),
    ("caller_storage_retained",UNIT,"if (!s->planned || s->owns_arena) session_storage_release(s, s->arena_bytes);","session_storage_release(s, s->arena_bytes);","sized_and_caller"),
    ("live_result_recycled",UNIT,"if (s->active || s->busy)\n        return MAELYS_DATALOG_STATUS_INVALID_STATE;\n    s->busy = 1;","if (s->busy)\n        return MAELYS_DATALOG_STATUS_INVALID_STATE;\n    s->busy = 1;","warm_and_live"),
    ("provider_not_destroyed",UNIT,"    s->backend.destroy(s->state);\n    resource_session_unlink(s);","    resource_session_unlink(s);","reentrancy"),
]


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("--output",type=Path,required=True)
    p.add_argument("--profile",choices=("SMALL","LARGE"),default="SMALL")
    p.add_argument("--cc",default=os.environ.get("CC","clang"))
    args=p.parse_args(); out=args.output.resolve()
    if out==ROOT or ROOT in out.parents: p.error("output must be outside Git")
    out.mkdir(parents=True,exist_ok=True)
    sources=[]
    for manifest in ("core","standard","native"):
        sources += [s for s in (ROOT/f"build-support/{manifest}-sources.txt").read_text().splitlines() if s and not s.startswith("#")]
    flags=["-std=c11","-D_POSIX_C_SOURCE=200809L","-O1","-g","-Wall","-Wextra","-Werror","-UNDEBUG",
           "-pthread","-fsanitize=address,undefined","-fno-sanitize-recover=all","-fno-omit-frame-pointer",
           "-include",str(ROOT/"tests/fixtures/allocation_guard.h"),f"-DMAELYS_DATALOG_PROFILE_{args.profile}"]
    env=dict(os.environ,ASAN_OPTIONS="detect_leaks=0:halt_on_error=1",UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1")

    def run(cmd,log,check=True):
        with log.open("w") as f:
            r=subprocess.run(cmd,cwd=ROOT,env=env,stdout=f,stderr=subprocess.STDOUT,timeout=120)
        if check and r.returncode: raise RuntimeError(f"build/baseline failure, not a killed mutant: {log}")
        return r.returncode

    def compile_one(source):
        obj=out/(source.replace("/","_")+".o")
        run([args.cc,*flags,"-I.","-Iinclude","-c",source,"-o",str(obj)],obj.with_suffix(".log"))
        return obj
    with ThreadPoolExecutor(max_workers=4) as pool: objects=list(pool.map(compile_one,sources))
    test=compile_one("tests/test_maelys_datalog_session_recycle.c")
    baseline=out/"baseline"
    run([args.cc,*flags,*map(str,objects),str(test),"-o",str(baseline)],out/"baseline-build.log")
    run([str(baseline)],out/"baseline-run.log"); print("baseline PASS",flush=True)
    results=[]
    for name,file,old,new,case in MUTATIONS:
        overlay=out/name; dest=overlay/file;dest.parent.mkdir(parents=True,exist_ok=True)
        text=(ROOT/file).read_text();assert text.count(old)==1,(name,text.count(old))
        dest.write_text(text.replace(old,new))
        obj=overlay/"mutant.o"; binary=overlay/"mutant"
        run([args.cc,*flags,"-I"+str(overlay),"-I.","-Iinclude","-c",str(dest if file==UNIT else ROOT/UNIT),"-o",str(obj)],overlay/"compile.log")
        linked=[obj if s==UNIT else o for s,o in zip(sources,objects)]
        run([args.cc,*flags,*map(str,linked),str(test),"-o",str(binary)],overlay/"link.log")
        code=run([str(binary),case],overlay/"run.log",False)
        results.append(dict(name=name,case=case,exit_code=code,detected=code!=0))
        print(name,"DETECTED" if code else "SURVIVED",flush=True)
    # Real DSO unload, not a call to an internal purge stand-in.
    name="unload_does_not_drain";overlay=out/name;dest=overlay/CACHE;dest.parent.mkdir(parents=True,exist_ok=True)
    text=(ROOT/CACHE).read_text();assert text.count("recycle_drain(1);")==1
    dest.write_text(text.replace("recycle_drain(1);","/* retained until process termination: mutant */"))
    command=[sys.executable,str(ROOT/"tools/check_session_recycle_lifecycle.py"),"--compiler",args.cc]
    if args.profile=="LARGE": command += ["--large"]
    run([*command,"--output",str(out/"unload-baseline")],out/"unload-baseline.log")
    code=run([*command,"--overlay",str(overlay),"--output",str(overlay/"build")],overlay/"run.log",False)
    # A compile error is never a successful negative control.
    if not (overlay/"build/loader").exists(): raise RuntimeError("unload mutant did not build")
    results.append(dict(name=name,case="dlclose",exit_code=code,detected=code!=0))
    print(name,"DETECTED" if code else "SURVIVED",flush=True)
    (out/"results.json").write_text(json.dumps(results,indent=2)+"\n")
    if not all(r["detected"] for r in results): raise SystemExit(1)


if __name__=="__main__": main()
