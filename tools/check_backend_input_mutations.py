#!/usr/bin/env python3
"""Rebuilt ABI 7 delivery/commit controls; public provider, sanitizers, no private backend."""
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
PROVIDER="sdk/conformance/input_provider.c"
TEST="tests/test_maelys_datalog_backend_inputs.c"
MUTATIONS=[
    ("window_stays_replacement",INPUTS,"if(h->owner->pending_commit) {","if(0) {"),
    ("window_common_not_excluded",INPUTS,"if(!order) ++common;","if(0) ++common;"),
    ("window_removal_uses_candidate",INPUTS,"{h,h->live,h->count-common,","{h,h->candidate,h->count-common,"),
    ("view_backward_read_not_reset",INPUTS,"if(index<c->ordinal) *c=(retained_view_cursor){0};","if(0) *c=(retained_view_cursor){0};"),
    ("view_common_fact_not_skipped",INPUTS,"!maelys_datalog_fact_cmp(&view->excluded[c->excluded],fact)) continue;","!maelys_datalog_fact_cmp(&view->excluded[c->excluded],fact)) { /* retain common */ }"),
    ("window_abort_advances_base",INPUTS,"static void retained_abort(maelys_datalog_session_inputs_t *h) { if(h) h->pending=0; }","static void retained_abort(maelys_datalog_session_inputs_t *h) { if(h) {++h->base.generation;h->pending=0;} }"),
    ("delta_as_replacement",INPUTS,"h->delivery_kind=snapshot?MAELYS_DATALOG_BACKEND_INPUT_REPLACE:MAELYS_DATALOG_BACKEND_INPUT_DELTA;","h->delivery_kind=MAELYS_DATALOG_BACKEND_INPUT_REPLACE;"),
    ("removals_not_delivered",INPUTS,"h->delivered_removals=r;","h->delivered_removals=0;"),
    ("next_base_stale",INPUTS,"h->base.generation+1}","h->base.generation}"),
    ("view_uses_current_dictionary",INPUTS,"&view->owner->vocabulary,fact","&view->owner->owner->inputs->symbols,fact"),
    ("skip_first_view_fact",INPUTS,"&view->owner->vocabulary,fact,&value","&view->owner->vocabulary,fact+1,&value"),
    ("snapshot_export_restored",RUNTIME,"(s->borrows_inputs || s->transaction_solve) ? 0 :","s->borrows_inputs ? 0 :"),
    ("delivery_identity_omitted",RESOURCES,"if (!rc && s->transaction_solve)","if (0)"),
    ("packet_version_ignored",INPUTS,"if(b->abi_version!=MAELYS_DATALOG_BACKEND_V7_ABI_VERSION)","if(0)"),
    ("abort_publishes_provider",PROVIDER,"if(s->staged) { if(s->committed)","if(s->staged && !s->committed)input_fixture_commit(state,result);\n    if(s->staged) { if(s->committed)"),
    ("provider_base_not_committed",PROVIDER,"s->count=s->candidate_count;s->base=s->next;","s->count=s->candidate_count;"),
    ("provider_removals_ignored",PROVIDER,"size_t n=side?packet->addition_count:packet->removal_count;","size_t n=side?packet->addition_count:0;"),
    ("sticky_output_ignored",RUNTIME,"if (output.error)\n        status = output.error;","if (0)\n        status = output.error;"),
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
    def link_run(directory,unit=None,obj=None):
        binary=directory/"witness"
        run([*flags,*[str(obj if s==unit else o) for s,o in zip(sources,objects)],"-o",str(binary)],directory/"link.log")
        return run([str(binary)],directory/"run.log",False)
    if link_run(out):raise RuntimeError("baseline failed")
    print("baseline PASS",flush=True);results=[]
    for name,file,old,new in MUTATIONS:
        directory=out/name;target=directory/file;target.parent.mkdir(parents=True,exist_ok=True)
        before=(ROOT/file).read_text()
        if before.count(old)!=1:raise RuntimeError(f"nonunique mutation anchor: {name}")
        after=before.replace(old,new);target.write_text(after)
        (directory/"mutation.patch").write_text("".join(difflib.unified_diff(before.splitlines(True),after.splitlines(True),fromfile=file,tofile=file)))
        unit=RUNTIME if file in (INPUTS,RESOURCES) else file
        obj=compile_one(unit,directory);code=link_run(directory,unit,obj)
        results.append(dict(name=name,detected=code!=0,exit_code=code));print(name,"DETECTED" if code else "SURVIVED",flush=True)
    (out/"results.json").write_text(json.dumps(results,indent=2)+"\n")
    if not all(x["detected"] for x in results):raise SystemExit(1)
if __name__=="__main__":main()
