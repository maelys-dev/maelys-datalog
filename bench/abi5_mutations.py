#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Isolated ABI-5 negative controls; no mutation is kept in the source checkout.
Usage: python3 bench/abi5_mutations.py SOURCE_REVISION OUTPUT_DIRECTORY
Run from a checkout containing that immutable source revision.
"""
from pathlib import Path
import json, subprocess, sys
source=Path.cwd()
revision=subprocess.check_output(['git','rev-parse',sys.argv[1]+'^{commit}'],text=True).strip()
evidence=Path(sys.argv[2]).resolve(); evidence.mkdir(parents=True,exist_ok=False)
work=evidence/'source'; work.mkdir()
(evidence/'revision.txt').write_text(revision+'\n')
archive=evidence/'mutation-source.tar'
subprocess.run(['git','-C',str(source),'archive','-o',str(archive),revision],check=True)
subprocess.run(['tar','-xf',str(archive),'-C',str(work)],check=True)
build=work/'build-mutation'
with (evidence/'mutation-configure.log').open('w') as log:
 subprocess.run(['cmake','-S',str(work),'-B',str(build),'-DCMAKE_BUILD_TYPE=Release'],check=True,stdout=log,stderr=subprocess.STDOUT)
files={f:(work/f).read_text() for f in ('src/runtime/maelys_datalog_runtime.c','src/runtime/maelys_datalog_window.c','src/runtime/maelys_datalog_group_window.c','src/registry/maelys_datalog_modules.c')}
runtime='src/runtime/maelys_datalog_runtime.c'
def early(text):
 needle='    if (!s->pending_commit) s->backend.commit(s->state, result->state);\n'
 assert text.count(needle)==1
 text=text.replace(needle,'')
 needle='    if (output.error)\n'
 assert text.count(needle)==1
 return text.replace(needle,'    if (!status && !s->pending_commit) s->backend.commit(s->state, result->state);\n'+needle)
def replace(old,new):
 def f(text):
  assert text.count(old)==1
  return text.replace(old,new)
 return f
mutations=[
 ('a-before-idb-validation',runtime,early,{'session'}),
 ('b-window-publication','src/runtime/maelys_datalog_window.c',replace('            maelys_datalog_result_commit(result);','            /* mutation: omitted commit */'),{'window'}),
 ('b-group-publication','src/runtime/maelys_datalog_group_window.c',replace('            maelys_datalog_result_commit(result);','            /* mutation: omitted commit */'),{'group'}),
 ('c-pointer-alignment',runtime,replace('         !((uintptr_t)storage->bytes % storage->alignment) &&\n',''),{'storage'}),
 ('d-abi4-rejection','src/registry/maelys_datalog_modules.c',replace('    return d && d->abi_version == MAELYS_DATALOG_BACKEND_ABI_VERSION &&\n','    return d &&\n'),{'abi'}),
]
results=[]
try:
 for name,file,mutation,expected in [('baseline',None,None,set()),*mutations,('restored',None,None,set())]:
  for path,text in files.items(): (work/path).write_text(text)
  if file: (work/file).write_text(mutation(files[file]))
  # Darwin make timestamps can share one second: force the changed objects,
  # archive and executable to be regenerated, rather than trusting that tick.
  for path in files:
   (build/'CMakeFiles/maelys_datalog.dir'/f'{path}.o').unlink(missing_ok=True)
  for artifact in ['libmaelys_datalog.a','test_backend_transaction_static']:
   (build/artifact).unlink(missing_ok=True)
  with (evidence/f'mutation-{name}-build.log').open('w') as log:
   subprocess.run(['cmake','--build',str(build),'--target','test_backend_transaction_static','--parallel','4'],check=True,stdout=log,stderr=subprocess.STDOUT)
  failed=set(); checks={}
  for test in ('abi','storage','session','context','window','group'):
   proc=subprocess.run([str(build/'test_backend_transaction_static'),test],text=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
   (evidence/f'mutation-{name}-{test}.log').write_text(proc.stdout)
   checks[test]=dict(exit_code=proc.returncode,output=proc.stdout)
   if proc.returncode: failed.add(test)
  results.append(dict(mutation=name,expected=sorted(expected),failed=sorted(failed),checks=checks))
  (evidence/'mutations.json').write_text(json.dumps(results,indent=2)+'\n')
  print(name,sorted(failed),'expected',sorted(expected),flush=True)
  assert failed==expected
finally:
 for path,text in files.items(): (work/path).write_text(text)
