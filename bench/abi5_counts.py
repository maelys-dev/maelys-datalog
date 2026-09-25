#!/usr/bin/env python3
"""ABI 5 acceptance: unchanged derivation, bounded publication-only extra work."""
import csv
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys
from compare_sessions import CASES, KEY
from diagnose_sessions import CACHE_FLAGS, events
from session_proof import exclusive_dump, EVENTS

STRICT = ('Ir', 'Dr', 'Dw')
TARGET = 'solve_once_derive_ordered'
# Predeclared publication functions. Every other positive delta is a rejection.
ALLOWED = {'maelys_datalog_session_solve', 'commit'}
# A conservative predeclared finite ceiling per solve, checked separately per function.
LIMITS = {'maelys_datalog_session_solve': (32, 16, 16), 'commit': (4, 2, 2)}

def command(args, log=None):
    return subprocess.run(args, check=True, stdout=log, stderr=subprocess.STDOUT if log else None)

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def strict(costs):
    return {name: [v.get(e, 0) for e in STRICT] for name, v in costs.items() if any(v.get(e, 0) for e in STRICT)}

def main():
    driver, root, work = map(Path, sys.argv[1:4])
    base, head = sys.argv[4:6]
    root.mkdir(parents=True); work.mkdir(parents=True)
    meta = dict(base=base, head=head, target=TARGET, events=STRICT, allowed=sorted(ALLOWED), ceilings=LIMITS,
                inventory=sorted(CASES), profiles=['SMALL', 'LARGE'], repetitions=2,
                region='one solve_edb after 50 warmups, excluding oracle, release, clocks and preparation',
                schedule=['SMALL/A/1','SMALL/B/1','SMALL/B/2','SMALL/A/2',
                          'LARGE/B/1','LARGE/A/1','LARGE/A/2','LARGE/B/2'])
    meta['compiler'] = subprocess.check_output(['clang','--version'], text=True)
    meta['host'] = subprocess.check_output(['uname','-a'], text=True)
    meta['cpu'] = subprocess.check_output(['lscpu'], text=True)
    meta['driver_sha256'] = sha(driver/'bench/bench_sessions_all_counts.c')
    (root/'protocol.json').write_text(json.dumps(meta, indent=2)+'\n')
    binaries = {}; oracle = {}; manifest = []
    for role, revision in [('A', base), ('B', head)]:
        source = work/role; source.mkdir()
        archive = work/f'{role}.tar'
        command(['git','-C',str(driver),'archive','--format=tar','-o',str(archive),revision])
        command(['tar','-xf',str(archive),'-C',str(source)])
        for profile in ['SMALL','LARGE']:
            out = work/f'bin-{role}-{profile}'
            with (root/f'build-{role}-{profile}.log').open('w') as log:
                command(['make','-j4','-C',str(source),'-f',str(driver/'bench/Makefile.abi5-counts'),
                         f'DRIVER={driver}',f'OUT={out}',f'PROFILE={profile}',f'REVISION={revision}',
                         str(out/'abi5-counts')], log)
            binary = out/'abi5-counts'; binaries[profile,role] = binary
            # Preserve the binary and disassembly, not only a claimed digest.
            import shutil
            shutil.copy2(binary, root/f'{profile}-{role}.binary')
            manifest.append(dict(role=role,profile=profile,sha256=sha(binary)))
            with (root/f'{profile}-{role}.disassembly').open('w') as log:
                command(['objdump','-d','--no-show-raw-insn',str(binary)], log)
    (root/'binaries.json').write_text(json.dumps(manifest,indent=2)+'\n')
    print('All four builds complete before execution', flush=True)
    for (profile,role),binary in binaries.items():
        prefix=root/f'oracle-{profile}-{role}'
        command([str(binary),str(prefix)+'.csv',str(prefix)+'.samples.csv'])
        rows=list(csv.DictReader(Path(str(prefix)+'.csv').open()))
        assert len(rows)==200 and {tuple(r[k] for k in KEY) for r in rows}==CASES
        oracle[profile,role]={tuple(r[k] for k in KEY):r['result_digest'] for r in rows}
    assert all(oracle[p,'A']==oracle[p,'B'] for p in ('SMALL','LARGE'))
    costs={}
    for scheduled in meta['schedule']:
        profile,role,repeat=scheduled.split('/'); repeat=int(repeat)
        prefix=root/f'{profile}-{role}-{repeat}'
        with Path(str(prefix)+'.log').open('w') as log:
            command(['valgrind','--tool=callgrind',*CACHE_FLAGS,'--collect-atstart=no','--error-exitcode=3',
                     f'--callgrind-out-file={prefix}.out',str(binaries[profile,role]),
                     str(prefix)+'.csv',str(prefix)+'.samples.csv'],log)
        rows=list(csv.DictReader(Path(str(prefix)+'.csv').open()))
        assert len(rows)==200 and {tuple(r[k] for k in KEY) for r in rows}==CASES
        for row in rows:
            key=tuple(row[k] for k in KEY)
            assert row['result_digest']==oracle[profile,role][key]
            assert row['profile']==profile and row['commit']==(base if role=='A' else head)
            assert row['samples']=='1' and all(row[m]=='0.000000' for m in ('min_us','median_us','p95_us'))
        paths=list(root.glob(prefix.name+'.out.*')); assert len(paths)==200
        seen=set()
        for path in paths:
            text=path.read_text(); labels=re.findall(r'^desc: Trigger: Client Request: (.+)$',text,re.M)
            assert len(labels)==1
            key=tuple(labels[0].split('/')); assert key in CASES and key not in seen; seen.add(key)
            counts=exclusive_dump(text); totals=events(path)
            counts['<unattributed-summary-remainder>']={e:totals[e]-sum(v[e] for v in counts.values()) for e in EVENTS}
            assert all(counts['<unattributed-summary-remainder>'][e]>=0 for e in STRICT)
            costs[profile,role,repeat,key]=counts
        print('Counted',scheduled,'200 cases',flush=True)
    failures=[]; table=[]; changes=[]; bounds={}
    report=['# Backend ABI 5 — exhaustive session instruction proof','',
            f'Base `{base}`; head `{head}`. Four builds completed before execution. No timing or padding sweep.',
            'All 400 cases, two independent processes per revision/profile; checked output oracles. Ir/Dr/Dw are exclusive software events, not hardware cycles. Bcm/I1mr retained separately.', '',
            '| Profile | Case | A Ir/Dr/Dw | B Ir/Dr/Dw | Repeat | Verdict |',
            '|---|---|---|---|---|---|']
    for profile in ('SMALL','LARGE'):
        for key in sorted(CASES):
            a,b=(strict(costs[profile,role,1,key]) for role in ('A','B'))
            repeated=all(strict(costs[profile,role,1,key])==strict(costs[profile,role,2,key]) for role in ('A','B'))
            av=a.get(TARGET,[0,0,0]); bv=b.get(TARGET,[0,0,0])
            good=repeated and av==bv and (key[0]!='derive' or av[0]>0)
            if not good: failures.append(dict(profile=profile,case=key,reason='ordered derivation/repetition'))
            report.append(f'| {profile} | {"/".join(key)} | {av} | {bv} | {repeated} | {"PASS" if good else "FAIL"} |')
            table.append(dict(profile=profile,case=key,base=a,head=b,repeat_identical=repeated))
            for fn in sorted(a.keys()|b.keys()):
                delta=[y-x for x,y in zip(a.get(fn,[0,0,0]),b.get(fn,[0,0,0]))]
                if not any(delta): continue
                changes.append(dict(profile=profile,case=key,function=fn,delta=delta))
                bound=bounds.setdefault(fn,dict(min=delta.copy(),max=delta.copy()))
                bound['min']=[min(x,y) for x,y in zip(bound['min'],delta)]
                bound['max']=[max(x,y) for x,y in zip(bound['max'],delta)]
                if any(d>0 for d in delta) and (fn not in ALLOWED or any(d>cap for d,cap in zip(delta,LIMITS[fn]))):
                    failures.append(dict(profile=profile,case=key,function=fn,delta=delta,reason='unexpected/beyond publication ceiling'))
    report+=['','| Changed exclusive function | Min delta Ir/Dr/Dw | Max delta Ir/Dr/Dw |', '|---|---|---|']
    report += [f'| `{fn}` | {v["min"]} | {v["max"]} |' for fn,v in sorted(bounds.items())]
    report+=['',f'Failures: {len(failures)}. No cycle/cache attribution is inferred.']
    (root/'REPORT.md').write_text('\n'.join(report)+'\n')
    (root/'counts.json').write_text(json.dumps(table,indent=2)+'\n')
    (root/'changes.json').write_text(json.dumps(changes,indent=2)+'\n')
    (root/'acceptance.json').write_text(json.dumps(dict(failures=failures,bounds=bounds,cases=len(table)),indent=2)+'\n')
    assert not failures, f'{len(failures)} acceptance failures; see preserved report'
if __name__=='__main__': main()
