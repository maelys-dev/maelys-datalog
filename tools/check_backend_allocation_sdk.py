#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Separate compiled callers/providers across installed v0.18 and candidate SDKs."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess

ROOT=Path(__file__).resolve().parents[1]
def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--old',type=Path,required=True);p.add_argument('--new',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True);a=p.parse_args()
    out=a.output.resolve();out.mkdir(parents=True,exist_ok=False)
    sdk={'old':a.old.resolve(),'new':a.new.resolve()};commands=[]
    env=dict(os.environ)
    for key in ('CPATH','C_INCLUDE_PATH','LIBRARY_PATH','DYLD_LIBRARY_PATH','LD_LIBRARY_PATH'):env.pop(key,None)
    cc=shlex.split(env.get('CC','cc'))
    def run(command,label):
        r=subprocess.run([str(x) for x in command],cwd=out,env=env,text=True,capture_output=True)
        (out/(label+'.log')).write_text(r.stdout+r.stderr)
        commands.append(dict(name=label,command=[str(x) for x in command],exit_code=r.returncode))
        (out/'commands.json').write_text(json.dumps(commands,indent=2)+'\n')
        if r.returncode:raise RuntimeError(label+': '+r.stderr[-3000:])
    def compile(name,prefix):
        obj=out/(name+'-'+prefix+'.o')
        run([*cc,'-std=c11','-O2','-UNDEBUG','-pedantic','-Wall','-Wextra','-Werror',
             '-I'+str(sdk[prefix]/'include'),'-c',out/(name+'.c'),'-o',obj],name+'-'+prefix)
        return obj
    for name in ('caller6','provider5','provider6'):shutil.copyfile(ROOT/'tests/fixtures/resources_sdk'/f'{name}.c',out/f'{name}.c')
    for name in ('allocation_provider.c','allocation_provider.h'):
        shutil.copyfile(sdk['new']/'share/maelys-datalog/conformance'/name,out/name)
    shutil.copyfile(ROOT/'tests/fixtures/allocation_sdk/negotiation.c',out/'negotiation.c')
    shutil.copyfile(ROOT/'tests/test_maelys_datalog_backend_allocation.c',out/'consumer.c')
    old=[compile(name,'old') for name in ('caller6','provider5','provider6')]
    provider=compile('allocation_provider','new');consumer=compile('consumer','new');negotiation=compile('negotiation','new')
    for host in ('old','new'):
        lib=sdk[host]/'lib/libmaelys_datalog.a'
        for name,objs,args in (('fixed',old,[]),('negotiation',[negotiation,old[2],provider],[host])):
            binary=out/(host+'-'+name)
            run([*cc,*objs,lib,'-o',binary],host+'-'+name+'-link');run([binary,*args],host+'-'+name+'-run')
    binary=out/'installed-consumer';run([*cc,consumer,provider,sdk['new']/'lib/libmaelys_datalog.a','-o',binary],'consumer-link')
    run([binary],'consumer-run')
    hashes={str(x):hashlib.sha256(x.read_bytes()).hexdigest() for x in [*out.glob('*.o'),*(v/'lib/libmaelys_datalog.a' for v in sdk.values())]}
    (out/'sha256.json').write_text(json.dumps(hashes,indent=2)+'\n')
    print('Installed allocation SDK matrix: old fixed callers/providers, new opt-ins and ABI 6/7 refusals, full consumer PASS')
if __name__=='__main__':main()
