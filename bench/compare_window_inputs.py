#!/usr/bin/env python3
"""Complete window operations on two installed SDK revisions; Ir/Dr/Dw, not time."""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import subprocess
from report_host_delta import counts

ROOT=Path(__file__).resolve().parents[1]
ORDER=("base1","head1","head2","base2")
CASES=[f"{window}/{n}/{kind}/{mode}" for window in ("occurrences","groups") for kind in ("integer","symbol")
       for n in (64,256) for mode in ("roll","shared","expire","replace","reject","noop")]

def report(root):
    comparisons=[]
    for profile in ("SMALL","LARGE"):
        runs={}
        for label in ORDER:
            path=root/profile/label;rows={}
            with (path/"receipts.csv").open() as stream:receipts=list(csv.DictReader(stream))
            receipt_map={r["case"].split("/",1)[1]:(r["transactions"],r["digest"]) for r in receipts}
            if len(receipts)!=len(CASES) or set(receipt_map)!=set(CASES) or any(v[0]!="200" for v in receipt_map.values()):
                raise ValueError(f"invalid receipts: {path}")
            for file in sorted(path.glob("counts.*")):
                row=counts(file)
                if row:
                    case=row["label"].split("/",1)[1]
                    if case in rows:raise ValueError(f"duplicate region: {file}")
                    rows[case]=row
            if set(rows)!=set(CASES):raise ValueError(f"missing regions: {path}")
            runs[label]=(rows,receipt_map)
        for role in ("base","head"):
            if runs[role+"1"]!=runs[role+"2"]:raise ValueError(f"nonidentical repetition: {profile}/{role}")
        if runs["base1"][1]!=runs["head1"][1]:raise ValueError(f"outputs differ: {profile}")
        for case in CASES:
            row={"profile":profile,"case":case,"paths":{}}
            for role in ("base","head"):
                measured=runs[role+"1"][0][case]
                provider=[sum(v[i] for name,v in measured["functions"].items() if name.startswith("input_fixture_")) for i in range(3)]
                row["paths"][role]={**measured,"provider_exclusive":provider,
                    "host_shared_and_driver":[measured["total"][i]-measured["residual"][i]-provider[i] for i in range(3)]}
            row["change_percent"]=[100*(b/a-1) if a else None for a,b in zip(row["paths"]["base"]["total"],row["paths"]["head"]["total"])]
            comparisons.append(row)
    result={"schema":1,"scope":"Complete window push/static replacement/expiration or rejected attempt; includes canonical materialization, provider, old-result release, commit and abort. Preparation, output oracle and final window teardown excluded.",
        "limits":"Software instruction/read/write counts, not latency or hardware counters. Public identity-projection provider only; not a private incremental backend. Shared libc helpers remain host_shared_and_driver, attribution residuals remain explicit.",
        "transactions_per_region":200,"regions":len(CASES)*2*len(ORDER),"repeated_regions":len(CASES)*4,
        "identical_repetitions":True,"comparisons":comparisons}
    (root/"report.json").write_text(json.dumps(result,indent=2)+"\n")
    lines=["# Window ABI 7 software counts","",result["scope"],"",result["limits"],"",
        "| Profile | Case | Base Ir | Head Ir | ΔIr | ΔDr | ΔDw |","|---|---|---:|---:|---:|---:|---:|"]
    for r in comparisons:
        lines.append(f"| {r['profile']} | {r['case']} | {r['paths']['base']['total'][0]} | {r['paths']['head']['total'][0]} | "+" | ".join(f"{x:+.2f}%" if x is not None else "n/a" for x in r["change_percent"])+" |")
    (root/"report.md").write_text("\n".join(lines)+"\n")
    return result

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument("base");p.add_argument("head");p.add_argument("output",type=Path)
    p.add_argument("--report-only",action="store_true");a=p.parse_args();out=a.output.resolve()
    if ROOT==out or ROOT in out.parents:p.error("evidence must stay outside Git")
    if a.report_only:report(out);return
    out.mkdir(parents=True,exist_ok=True)
    if (out/"manifest.json").exists():p.error("refusing to overwrite evidence")
    def command(argv,log):
        with log.open("w") as stream:subprocess.run(argv,cwd=ROOT,check=True,stdout=stream,stderr=subprocess.STDOUT)
    refs={role:subprocess.check_output(["git","rev-parse",ref+"^{commit}"],cwd=ROOT,text=True).strip() for role,ref in (("base",a.base),("head",a.head))}
    checkout=subprocess.check_output(["git","rev-parse","HEAD"],cwd=ROOT,text=True).strip()
    status=subprocess.check_output(["git","status","--porcelain"],cwd=ROOT,text=True)
    if checkout!=refs["head"] or status:p.error("run the tooling from the exact clean head checkout")
    files=("bench/window_inputs.c","bench/compare_window_inputs.py","bench/report_host_delta.py","sdk/conformance/input_provider.c","sdk/conformance/input_provider.h")
    manifest={"revisions":refs,"cases":CASES,"order":ORDER,"steps":200,
        "tooling_head":checkout,"sources":{f:hashlib.sha256((ROOT/f).read_bytes()).hexdigest() for f in files}}
    (out/"manifest.json").write_text(json.dumps(manifest,indent=2)+"\n")
    (out/"cpu.txt").write_text(Path("/proc/cpuinfo").read_text())
    for name,argv in (("compiler",["clang","--version"]),("valgrind",["valgrind","--version"]),("platform",["uname","-a"])):command(argv,out/(name+".txt"))
    for role,sha in refs.items():
        source=out/(role+"-source");source.mkdir()
        command(["git","archive","--format=tar","--output="+str(out/(role+".tar")),sha],out/(role+"-archive.log"))
        command(["tar","-xf",str(out/(role+".tar")),"-C",str(source)],out/(role+"-extract.log"))
        for profile in ("SMALL","LARGE"):
            path=out/profile/role;path.mkdir(parents=True)
            command(["cmake","-S",str(source),"-B",str(path/"build"),"-DCMAKE_BUILD_TYPE=Release","-DBUILD_TESTING=OFF",
                "-DCMAKE_C_COMPILER=clang","-DMAELYS_DATALOG_PROFILE_LARGE="+("ON" if profile=="LARGE" else "OFF"),"-DCMAKE_INSTALL_PREFIX="+str(path/"sdk")],path/"configure.log")
            command(["cmake","--build",str(path/"build"),"--target","maelys_datalog","-j4"],path/"build.log")
            for part in ("sdk","sdk-static"):command(["cmake","--install",str(path/"build"),"--component",part],path/(part+".log"))
            flags=["clang","-std=c11","-O3","-g","-UNDEBUG","-Wall","-Wextra","-Werror","-I"+str(path/"sdk/include")]
            command([*flags,"-c",str(ROOT/"sdk/conformance/input_provider.c"),"-o",str(path/"provider.o")],path/"provider-build.log")
            command([*flags,"-I"+str(ROOT/"sdk/conformance"),str(ROOT/"bench/window_inputs.c"),str(path/"provider.o"),str(path/"sdk/lib/libmaelys_datalog.a"),"-o",str(path/"window-inputs")],path/"driver-build.log")
            command(["nm","-u",str(path/"provider.o")],path/"provider-undefined.txt")
            command(["nm","-n",str(path/"window-inputs")],path/"symbols.txt")
            command(["objdump","-dr",str(path/"window-inputs")],path/"disassembly.txt")
            (path/"binary.sha256").write_text(hashlib.sha256((path/"window-inputs").read_bytes()).hexdigest()+"\n")
            command([str(path/"window-inputs"),role],path/"checked.csv")
    # All four SDKs and both separately compiled public consumers finished.
    for profile in ("SMALL","LARGE"):
        for label in ORDER:
            path=out/profile/label;path.mkdir()
            with (path/"receipts.csv").open("w") as stdout,(path/"valgrind.log").open("w") as stderr:
                subprocess.run(["valgrind","--tool=callgrind","--cache-sim=yes","--branch-sim=no","--collect-atstart=no","--error-exitcode=3",
                    "--callgrind-out-file="+str(path/"counts"),str(out/profile/label[:-1]/"window-inputs"),label[:-1]],cwd=ROOT,check=True,stdout=stdout,stderr=stderr)
    report(out)
if __name__=="__main__":main()
