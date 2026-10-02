#!/usr/bin/env python3
"""Rebuilt optional-allocation controls with targeted assertions under ASan/UBSan."""
import argparse
from concurrent.futures import ThreadPoolExecutor
import difflib
import json
import os
from pathlib import Path
import subprocess
ROOT=Path(__file__).resolve().parents[1]
INPUTS="src/runtime/maelys_datalog_inputs.inc"
RUNTIME="src/runtime/maelys_datalog_runtime.c"
RESOURCES="src/runtime/maelys_datalog_resources.inc"
PROVIDER="sdk/conformance/allocation_provider.c"
TEST="tests/test_maelys_datalog_backend_allocation.c"
ALLOCATION="src/runtime/maelys_datalog_allocation.inc"
MUTATIONS=[
    ("wrong_padding",ALLOCATION,"sizeof(allocation_block_t) + (a-1) + n;","sizeof(allocation_block_t) + n;","inspection","b.required_charge=="),
    ("inspect_changes_peak",ALLOCATION,"*out = value;","++a->peak; *out = value;","inspection","!memcmp(&old,&b"),
    ("ignored_cap",ALLOCATION,"if (charge > a->cap-a->current)","if (0)","admission","initialize(&f,&d)==FULL"),
    ("swallowed_null",RUNTIME,"if (s->allocation && s->allocation->error)","if (0)","failures_and_reuse","maelys_datalog_session_solve(f.session,x,16"),
    ("cache_after_abort",ALLOCATION,"if (all || (*entry)->provisional)","if (all)","failures_and_reuse","stats(&f).current_bytes==charge"),
    ("release_old_in_solve",ALLOCATION,"&& !(*entry)->provisional","&& 0","forbidden_service","maelys_datalog_session_solve(a.session"),
    ("missing_prepare_cleanup",RESOURCES,"if (s->allocation) allocation_sweep(s->allocation,1);","if (0) allocation_sweep(s->allocation,1);","preparation_failures","!f.ledger.live"),
    ("cap_missing_identity",RESOURCES,"if (!rc && s->allocation)","if (0)","telemetry_and_identity","strcmp(x,z)"),
    ("premature_publication",RUNTIME,"if (status != MAELYS_DATALOG_STATUS_OK) {\n        if (output.derived_quota && !(s->allocation && s->allocation->error))","if (0) {\n        if (output.derived_quota && !(s->allocation && s->allocation->error))","failures_and_reuse","maelys_datalog_session_solve(f.session,x,16"),
    ("current_not_decreased",ALLOCATION,"a->current -= charge;","a->current -= 0;","inspection","b.current_bytes==old"),
    ("retries_after_sticky",ALLOCATION,"if (a->error) { allocation_copy_diagnostic(diag,&a->diagnostic); return a->error; }","if (0) { allocation_copy_diagnostic(diag,&a->diagnostic); return a->error; }","inspection","s->acquire(s->context,1,1,&block,&d)==FULL"),
    ("exact_fit_refused",ALLOCATION,"if (charge > a->cap-a->current)","if (charge >= a->cap-a->current)","exact_boundaries","initialize(&f,&d)"),
]

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument("--output",type=Path,required=True)
    p.add_argument("--profile",choices=("SMALL","LARGE"),default="SMALL");args=p.parse_args();out=args.output.resolve()
    if ROOT==out or ROOT in out.parents:p.error("evidence must stay outside Git")
    out.mkdir(parents=True,exist_ok=True)
    sources=[]
    for part in ("core","standard","native"):
        sources += [s for s in (ROOT/f"build-support/{part}-sources.txt").read_text().splitlines() if s and not s.startswith("#")]
    sources += [PROVIDER,TEST]
    flags=[os.environ.get("CC","clang"),"-std=c11","-D_POSIX_C_SOURCE=200809L","-O1","-g","-UNDEBUG",
           "-Wall","-Wextra","-Werror","-Wno-unused-function","-Wno-unused-variable","-fsanitize=address,undefined",
           "-fno-sanitize-recover=all","-fno-omit-frame-pointer",f"-DMAELYS_DATALOG_PROFILE_{args.profile}"]
    env=dict(os.environ,ASAN_OPTIONS="detect_leaks=0:halt_on_error=1",UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1")
    def run(command,log,required=True):
        with log.open("w") as stream:code=subprocess.run(command,cwd=ROOT,env=env,stdout=stream,stderr=subprocess.STDOUT).returncode
        if required and code:raise RuntimeError(f"baseline/build failure, not detection: {log}")
        return code
    def compile_one(source,directory=out):
        obj=directory/(source.replace("/","_")+".o")
        actual=directory/source if (directory/source).exists() else ROOT/source
        command=[*flags,"-I"+str(directory),"-I.","-Iinclude","-Isdk/conformance"]
        if source!=TEST:command += ["-include","tests/fixtures/allocation_guard.h"]
        run([*command,"-c",str(actual),"-o",str(obj)],obj.with_suffix(".log"));return obj
    with ThreadPoolExecutor(max_workers=4) as pool:objects=list(pool.map(compile_one,sources))
    def link_run(directory,unit=None,obj=None,test="all"):
        binary=directory/"witness"
        run([*flags,*[str(obj if s==unit else o) for s,o in zip(sources,objects)],"-o",str(binary)],directory/"link.log")
        return run([str(binary),test],directory/"run.log",False)
    if link_run(out):raise RuntimeError("baseline failed")
    print("baseline PASS",flush=True);results=[]
    for name,file,old,new,test,assertion in MUTATIONS:
        directory=out/name;target=directory/file;target.parent.mkdir(parents=True,exist_ok=True)
        before=(ROOT/file).read_text()
        if not before.count(old):raise RuntimeError(f"missing mutation anchor: {name}")
        after=before.replace(old,new);target.write_text(after)
        (directory/"mutation.patch").write_text("".join(difflib.unified_diff(before.splitlines(True),after.splitlines(True),fromfile=file,tofile=file)))
        unit=RUNTIME if file in (INPUTS,RESOURCES,ALLOCATION) else file
        obj=compile_one(unit,directory);code=link_run(directory,unit,obj,test)
        evidence=(directory/"run.log").read_text()
        detected=code!=0 and assertion in evidence
        if code and not detected:raise RuntimeError(f"failure without targeted assertion: {name}")
        results.append(dict(name=name,detected=detected,exit_code=code,test=test,assertion=assertion));print(name,"DETECTED" if detected else "SURVIVED",flush=True)
    (out/"results.json").write_text(json.dumps(results,indent=2)+"\n")
    if not all(x["detected"] for x in results):raise SystemExit(1)
if __name__=="__main__":main()
