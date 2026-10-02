#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Optional-service and ordinary FIXED complete-operation software evidence."""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import subprocess
from report_host_delta import counts
ROOT=Path(__file__).resolve().parents[1]
FIXED=[f'fixed/{n}-symbol/{mode}' for n in (7,93) for mode in ('prepared','convenience')]
ELASTIC=[f'elastic/{kind}/abi{abi}/{n}/{mode}' for abi in (6,7) for n in (8,64)
         for kind,mode in (('session','accepted'),('session','caller-reject'),('session','growth'),('occurrences','late-reject'),('groups','late-reject'))]
ORDER=('base-aa1','base-aa2','base-aa3','base-aa4','head-aa1','head-aa2','head-aa3','head-aa4','base-ab1','head-ab1','base-ab2','head-ab2')
PROFILES=('SMALL','LARGE')

def read_run(path,cases,software=True):
    with (path/'receipts.csv').open() as f:receipts=list(csv.DictReader(f))
    values={r['case'].split('/',1)[1]:{k:v for k,v in r.items() if k!='case'} for r in receipts}
    if len(receipts)!=len(cases) or set(values)!=set(cases) or any(x['transactions']!='200' for x in values.values()):raise ValueError(f'invalid receipts {path}')
    regions={}
    if software:
        for p in sorted(path.glob('counts.*')):
            r=counts(p)
            if r:
                key=r.pop('label').split('/',1)[1]
                if key in regions:raise ValueError('duplicate region')
                regions[key]=r
        if set(regions)!=set(cases):raise ValueError(f'incomplete counts {path}')
    return dict(receipts=values,regions=regions)

def same(a,b):
    if a!=b:raise ValueError('nonidentical repetition (outputs, totals or exclusive functions)')

def compare(a,b):
    names=sorted(set(a['functions'])|set(b['functions']))
    return dict(base=a,head=b,delta=[y-x for x,y in zip(a['total'],b['total'])],
        delta_by_function={n:[y-x for x,y in zip(a['functions'].get(n,[0]*3),b['functions'].get(n,[0]*3))] for n in names},
        percent=[100*(y/x-1) if x else None for x,y in zip(a['total'],b['total'])])

def report(out):
    result=dict(schema=1,steps=200,order=ORDER,fixed=[],elastic=[],
        scope='FIXED: native input reset/append, solve, workload queries, result release; convenience also creates/closes session and input buffer. Elastic: solve/retained replacement with accepted cleanup or failed attempt, including alternating growth/shrink; real-window expiry rejection after output growth. Preparation, full typed output checks and telemetry queries excluded. Driver/control and shared libc costs retained.',
        limits='Ir/Dr/Dw are software counts, not hardware instructions/cycles or latency. Public bounded conformance provider only. Explicit memcpy/memmove/memset/strcpy byte requests come from separate instrumented binaries; they can retain dead writes, exclude implicit struct assignments and are not physical traffic. No private-backend or Python speed claim. Window reservation excludes policy, caller-owned retained-input handles and explanation buffers as specified by the service cap; no aggregate unbounded SIZE_MAX sum.',
        repeated_regions=0)
    for profile in PROFILES:
        runs={label:read_run(out/profile/label,FIXED) for label in ORDER}
        for role in ('base','head'):
            labels=[l for l in ORDER if l.startswith(role+'-')]
            for label in labels[1:]:same(runs[labels[0]],runs[label])
            result['repeated_regions']+=len(FIXED)*(len(labels)-1)
        same(runs['base-aa1']['receipts'],runs['head-aa1']['receipts'])
        for case in FIXED:result['fixed'].append(dict(profile=profile,case=case,**compare(runs['base-aa1']['regions'][case],runs['head-aa1']['regions'][case])))
        x=read_run(out/profile/'elastic1',ELASTIC);y=read_run(out/profile/'elastic2',ELASTIC);same(x,y);result['repeated_regions']+=len(ELASTIC)
        telemetry=read_run(out/profile/'bytes',ELASTIC,False)
        for case in ELASTIC:
            ordinary=x['receipts'][case];instrumented=telemetry['receipts'][case]
            for k,v in ordinary.items():
                if k not in ('copy_requests','move_requests','set_requests') and instrumented[k]!=v:raise ValueError(f'telemetry changed operation: {profile}/{case}/{k}')
            if any(ordinary[k]!='0' for k in ('copy_requests','move_requests','set_requests')):raise ValueError('instrumented binary used for software counts')
            data=x['regions'][case];provider=[sum(v[i] for name,v in data['functions'].items() if name.startswith('allocation_fixture_')) for i in range(3)]
            service=[sum(v[i] for name,v in data['functions'].items() if name.startswith('allocation_') and not name.startswith('allocation_fixture_')) for i in range(3)]
            result['elastic'].append(dict(profile=profile,case=case,receipt=instrumented,counts=data,provider_exclusive=provider,service_named_exclusive=service,
                host_shared_driver_and_inlined_service=[data['total'][i]-data['residual'][i]-provider[i]-service[i] for i in range(3)]))
    result['identical_repetitions']=True
    (out/'report.json').write_text(json.dumps(result,indent=2)+'\n')
    lines=['# Optional backend allocation — software counts','',result['scope'],'',result['limits'],'',
           '| Profile | Fixed case | Base Ir | Head Ir | ΔIr/request | ΔIr | ΔDr | ΔDw |','|---|---|---:|---:|---:|---:|---:|---:|---:|']
    for r in result['fixed']:lines.append(f"| {r['profile']} | {r['case']} | {r['base']['total'][0]} | {r['head']['total'][0]} | {r['delta'][0]/200:+.3f} | "+' | '.join(f'{p:+.6f}%' for p in r['percent'])+' |')
    lines+=['','Every nonzero function delta and attribution residual is retained in report.json. No automatic instruction tolerance or release decision.','',
        '| Profile | Elastic case | Ir | Fixed bytes | Banks × cap | Current before/after | Peak | Acquires/releases | Copy/move/set requests |','|---|---|---:|---:|---:|---:|---:|---:|---:|']
    for r in result['elastic']:
        t=r['receipt'];lines.append(f"| {r['profile']} | {r['case']} | {r['counts']['total'][0]} | {t['fixed_bytes']} | {t['banks']} × {t['cap_per_bank']} | {t['current_before']}/{t['current_after']} | {t['operation_peak']} | {t['acquire_calls']}/{t['release_calls']} | {t['copy_requests']}/{t['move_requests']}/{t['set_requests']} |")
    (out/'report.md').write_text('\n'.join(lines)+'\n');return result

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('base');p.add_argument('head');p.add_argument('output',type=Path)
    p.add_argument('--report-only',action='store_true');p.add_argument('--smoke',action='store_true',help='build/check binaries only; no performance evidence');a=p.parse_args();out=a.output.resolve()
    if ROOT==out or ROOT in out.parents:p.error('evidence must stay outside Git')
    if a.report_only:report(out);return
    out.mkdir(parents=True,exist_ok=False)
    def cmd(argv,path):
        with path.open('w') as f:subprocess.run(argv,cwd=ROOT,check=True,stdout=f,stderr=subprocess.STDOUT)
    refs={role:subprocess.check_output(['git','rev-parse',ref+'^{commit}'],cwd=ROOT,text=True).strip() for role,ref in (('base',a.base),('head',a.head))}
    current=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip()
    if refs['head']!=current or subprocess.check_output(['git','status','--porcelain'],cwd=ROOT):p.error('run from exact clean head')
    files=['bench/allocation_fixed.c','bench/allocation_elastic.c','bench/allocation_writes.h','bench/allocation_writes.c','bench/compare_backend_allocation.py','bench/report_host_delta.py','sdk/conformance/allocation_provider.c','sdk/conformance/allocation_provider.h']
    manifest=dict(revisions=refs,cases=dict(fixed=FIXED,elastic=ELASTIC),steps=200,order=ORDER,profiles=PROFILES,build_mode='CMake Release, clang -O3 -DNDEBUG; consumers -O3 -g -UNDEBUG',
        tooling=current,smoke=a.smoke,sha256={f:hashlib.sha256((ROOT/f).read_bytes()).hexdigest() for f in files})
    (out/'manifest.json').write_text(json.dumps(manifest,indent=2)+'\n');(out/'cpu.txt').write_text(Path('/proc/cpuinfo').read_text())
    for name,argv in (('compiler',['clang','--version']),('valgrind',['valgrind','--version']),('platform',['uname','-a'])):cmd(argv,out/(name+'.txt'))
    for role,sha in refs.items():
        source=out/(role+'-source');source.mkdir();cmd(['git','archive','--format=tar','--output='+str(out/(role+'.tar')),sha],out/(role+'-archive.log'));cmd(['tar','-xf',str(out/(role+'.tar')),'-C',str(source)],out/(role+'-extract.log'))
    for profile in PROFILES:
        for role in ('base','head','bytes'):
            d=out/profile/role;d.mkdir(parents=True)
            flags=['clang','-std=c11','-O3','-g','-UNDEBUG','-Wall','-Wextra','-Werror','-I'+str(d/'sdk/include'),'-I'+str(ROOT/'sdk/conformance'),'-I'+str(ROOT/'bench')]
            telemetry=['-DALLOCATION_WRITE_TELEMETRY','-include',str(ROOT/'bench/allocation_writes.h')] if role=='bytes' else []
            cmd(['cmake','-S',str(out/('base-source' if role=='base' else 'head-source')),'-B',str(d/'build'),'-DCMAKE_BUILD_TYPE=Release','-DBUILD_TESTING=OFF','-DCMAKE_C_COMPILER=clang',
                 '-DMAELYS_DATALOG_PROFILE_LARGE='+('ON' if profile=='LARGE' else 'OFF'),'-DCMAKE_INSTALL_PREFIX='+str(d/'sdk'),'-DCMAKE_C_FLAGS='+' '.join(telemetry)],d/'configure.log')
            cmd(['cmake','--build',str(d/'build'),'--target','maelys_datalog','-j4'],d/'build.log')
            for part in ('sdk','sdk-static'):cmd(['cmake','--install',str(d/'build'),'--component',part],d/(part+'.log'))
            lib=d/'sdk/lib/libmaelys_datalog.a'
            if role!='bytes':cmd([*flags,str(ROOT/'bench/allocation_fixed.c'),str(lib),'-o',str(d/'fixed')],d/'fixed-build.log')
            if role!='base':
                cmd([*flags,*telemetry,'-c',str(ROOT/'sdk/conformance/allocation_provider.c'),'-o',str(d/'provider.o')],d/'provider-build.log')
                cmd([*flags,'-c',str(ROOT/'bench/allocation_writes.c'),'-o',str(d/'writes.o')],d/'writes-build.log')
                cmd([*flags,*telemetry,str(ROOT/'bench/allocation_elastic.c'),str(d/'provider.o'),str(d/'writes.o'),str(lib),'-o',str(d/'elastic')],d/'elastic-build.log')
            for executable in ('fixed','elastic'):
                binary=d/executable
                if not binary.exists():continue
                cmd(['nm','-n',str(binary)],d/(executable+'-symbols.txt'));cmd(['objdump','-dr',str(binary)],d/(executable+'-disassembly.txt'))
                (d/(executable+'.sha256')).write_text(hashlib.sha256(binary.read_bytes()).hexdigest()+'\n')
                cmd([str(binary),role],d/(executable+'-checked.csv'))
    # Every SDK, provider and consumer is complete before any Callgrind process.
    if a.smoke:return
    for profile in PROFILES:
        for label in ORDER+('elastic1','elastic2'):
            d=out/profile/label;d.mkdir();elastic=label.startswith('elastic');role='head' if elastic else label.split('-')[0]
            with (d/'receipts.csv').open('w') as f,(d/'valgrind.log').open('w') as err:
                subprocess.run(['valgrind','--tool=callgrind','--cache-sim=yes','--branch-sim=no','--collect-atstart=no','--error-exitcode=3','--callgrind-out-file='+str(d/'counts'),str(out/profile/role/('elastic' if elastic else 'fixed')),role],cwd=ROOT,check=True,stdout=f,stderr=err)
        # Separate instrumented run is telemetry only; no Callgrind/time claim.
        cmd([str(out/profile/'bytes/elastic'),'head'],out/profile/'bytes/receipts.csv')
    report(out)
if __name__=='__main__':main()
