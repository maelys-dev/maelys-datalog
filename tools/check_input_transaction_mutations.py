#!/usr/bin/env python3
"""Rebuild retained-input negative controls under ASan/UBSan, outside Git.

Compilation failure is not detection. Baselines run first; every mutation gets
its exact patch, compiler log and complete failing-test log. All engine units
are allocation-guarded, including the white-box publication/rollback witness.
"""
import argparse
from concurrent.futures import ThreadPoolExecutor
import difflib
import json
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
RUNTIME = "src/runtime/maelys_datalog_runtime.c"
INPUTS = "src/runtime/maelys_datalog_inputs.inc"
PREPARED = "src/core/maelys_datalog_prepared_session.c"
PUBLIC = "tests/test_maelys_datalog_input_transactions.c"
ROLLBACK = "tests/test_maelys_datalog_input_transactions_alloc.c"
# name, edited file, anchor, replacement, witness
MUTATIONS = [
    ("removals_win", INPUTS,
     "while(base<n && maelys_datalog_fact_cmp(&h->candidate[base],&h->added[i])<0) ++base;",
     "maelys_datalog_fact_set_t removed;maelys_datalog_fact_set_init(&removed,h->removed,nr);removed.count=nr;\n"
     "        if(maelys_datalog_fact_set_contains(&removed,&h->added[i])) continue;\n"
     "        while(base<n && maelys_datalog_fact_cmp(&h->candidate[base],&h->added[i])<0) ++base;", PUBLIC),
    ("cancelled_raw_not_validated", INPUTS, "size_t a=0,r=0;",
     "if(added==removed && na==nr)na=nr=0;size_t a=0,r=0;", PUBLIC),
    ("raw_capacity_ignored", INPUTS,
     "if(na>(snapshot?h->capacity:h->addition_capacity) || nr>h->removal_capacity)",
     "if(0)", PUBLIC),
    ("stale_generation", INPUTS, "base.generation!=h->base.generation ||", "0 ||", PUBLIC),
    ("foreign_incarnation", INPUTS, "base.incarnation!=h->base.incarnation ||", "0 ||", PUBLIC),
    ("retirement_ignored", INPUTS,
     "if(remove<nr && !maelys_datalog_fact_cmp(&h->removed[remove],&h->live[read])) continue;",
     "if(0) continue;", PUBLIC),
    ("last_addition_lost", INPUTS, "size_t left=n,right=fresh;write=n+fresh;",
     "size_t left=n,right=fresh?fresh-1:0;write=n+fresh;", PUBLIC),
    ("merge_left_underflow", INPUTS, "if(left && maelys_datalog_fact_cmp", "if(maelys_datalog_fact_cmp", PUBLIC),
    ("existing_addition_duplicated", INPUTS,
     "if(base<n && !maelys_datalog_fact_cmp(&h->candidate[base],&h->added[i])) continue;",
     "if(0) continue;", PUBLIC),
    ("exact_capacity_refused", INPUTS, "if(fresh>h->capacity-n)", "if(fresh>=h->capacity-n)", PUBLIC),
    ("abort_commits", INPUTS, "static void retained_abort(maelys_datalog_session_inputs_t *h) { if(h) h->pending=0; }",
     "static void retained_abort(maelys_datalog_session_inputs_t *h) { retained_commit(h); }", ROLLBACK),
    ("generation_not_advanced", INPUTS, "++h->base.generation;h->pending=0;", "h->pending=0;", PUBLIC),
    ("boolean_not_normalized", INPUTS, "maelys_datalog_fact_set_boolean(&item, t, !!v->as.boolean);",
     "maelys_datalog_fact_set_boolean(&item, t, v->as.boolean);", PUBLIC),
    ("canonical_ids_not_remapped", PREPARED,
     "maelys_datalog_fact_set_symbol(&s->fact_pool[i], t, map[maelys_datalog_fact_term(in, t).as.symbol-1u]);",
     "maelys_datalog_fact_set_symbol(&s->fact_pool[i], t, maelys_datalog_fact_term(in, t).as.symbol);", PUBLIC),
    ("generation_wrap_allowed", INPUTS, "h->base.generation==UINT64_MAX)", "0)", ROLLBACK),
]

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--output", required=True, type=Path)
    p.add_argument("--profile", choices=("SMALL", "LARGE"), default="SMALL")
    p.add_argument("--cc", default=os.environ.get("CC", "clang"))
    p.add_argument("--jobs", type=int, default=4)
    args = p.parse_args()
    out = args.output.resolve()
    if ROOT == out or ROOT in out.parents:
        p.error("output must be outside the checkout")
    out.mkdir(parents=True, exist_ok=True)
    sources = []
    for manifest in ("core", "standard", "native"):
        sources += [x for x in (ROOT/f"build-support/{manifest}-sources.txt").read_text().splitlines()
                    if x and not x.startswith("#")]
    flags = ["-std=c11", "-D_POSIX_C_SOURCE=200809L", "-O1", "-g", "-UNDEBUG",
             "-Wall", "-Wextra", "-Werror", "-Wno-unused-function", "-Wno-unused-variable",
             "-fsanitize=address,undefined", "-fno-sanitize-recover=all", "-fno-omit-frame-pointer",
             f"-DMAELYS_DATALOG_PROFILE_{args.profile}"]
    env = dict(os.environ, ASAN_OPTIONS="detect_leaks=0:halt_on_error=1",
               UBSAN_OPTIONS="halt_on_error=1:print_stacktrace=1")

    def invoke(command, log, required=True):
        with log.open("w") as stream:
            code = subprocess.run(command, cwd=ROOT, env=env, stdout=stream, stderr=subprocess.STDOUT).returncode
        if required and code:
            raise RuntimeError(f"build/baseline failure, not detection: {log}")
        return code

    def compile_one(source, directory=out, guard=True):
        obj = directory/(source.replace("/", "_")+".o")
        command = [args.cc, *flags, "-I"+str(directory), "-I.", "-Iinclude"]
        if guard: command += ["-include", "tests/fixtures/allocation_guard.h"]
        actual = directory/source if (directory/source).exists() else ROOT/source
        invoke([*command, "-c", str(actual), "-o", str(obj)], obj.with_suffix(".log"))
        return obj

    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        objects = list(pool.map(compile_one, sources))
    shim = out/"shim.c"
    shim.write_text('#include <stdlib.h>\n#include <string.h>\n'
                    'void *maelys_test_malloc(size_t n){return malloc(n);}\n'
                    'void *maelys_test_calloc(size_t n,size_t s){return calloc(n,s);}\n'
                    'void *maelys_test_realloc(void *p,size_t n){return realloc(p,n);}\n'
                    'void maelys_test_free(void *p){free(p);}\n'
                    'void *maelys_test_memset(void *p,int c,size_t n){return memset(p,c,n);}\n')
    shim_obj = out/"shim.o"
    invoke([args.cc,*flags,"-c",str(shim),"-o",str(shim_obj)],out/"shim.log")
    tests = {test: compile_one(test, guard=False) for test in (PUBLIC, ROLLBACK)}

    def link_run(directory, witness, mutant=None, unit=None):
        linked = [mutant if original == unit else obj for original,obj in zip(sources,objects)
                  if witness != ROLLBACK or original != RUNTIME]
        test = compile_one(witness, directory, guard=False) if witness == ROLLBACK and mutant else tests[witness]
        binary = directory/("rollback" if witness == ROLLBACK else "public")
        invoke([args.cc,*flags,*map(str,linked),str(test),
                *([] if witness == ROLLBACK else [str(shim_obj)]),"-o",str(binary)],binary.with_suffix(".link.log"))
        return invoke([str(binary)],binary.with_suffix(".run.log"),False)

    for witness in tests:
        if link_run(out,witness): raise RuntimeError(f"baseline failed: {witness}")
    print("baseline public + rollback PASS",flush=True)
    results=[]
    for name,edited,old,new,witness in MUTATIONS:
        directory=out/name;file=directory/edited;file.parent.mkdir(parents=True,exist_ok=True)
        before=(ROOT/edited).read_text()
        if before.count(old)!=1: raise RuntimeError(f"nonunique anchor: {name}")
        after=before.replace(old,new);file.write_text(after)
        (directory/"mutation.patch").write_text("".join(difflib.unified_diff(before.splitlines(True),after.splitlines(True),fromfile=edited,tofile=edited)))
        unit=RUNTIME if edited==INPUTS else edited
        mutant=compile_one(unit,directory)
        code=link_run(directory,witness,mutant,unit)
        results.append(dict(name=name,witness=witness,exit_code=code,detected=code!=0))
        print(name,"DETECTED" if code else "SURVIVED",flush=True)
    (out/"results.json").write_text(json.dumps(results,indent=2)+"\n")
    if not all(x["detected"] for x in results): raise SystemExit(1)

if __name__ == "__main__": main()
