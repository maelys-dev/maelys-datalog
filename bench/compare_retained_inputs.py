#!/usr/bin/env python3
"""Public snapshot/retained-replace/delta software counts; no latency claims.

32 declared fixtures per profile, 200 transactions each, complete output checks
outside the counted regions. Both installed SDKs and drivers are built before
collection. Repetitions must agree in Ir/Dr/Dw per function, totals and residual.
"""
import argparse
import csv
import hashlib
import json
import os
from pathlib import Path
import subprocess
from report_host_delta import counts

ROOT=Path(__file__).resolve().parents[1]
ORDER=("snapshot1","delta1","replace1","replace2","delta2","snapshot2")
CASES=[f"{work}/{n}/{kind}/{change}" for work in ("inert","projection") for kind in ("integer","symbol")
       for n in (64,256) for change in ("empty","one","small","replace")]

def report(root):
    comparisons=[]
    for profile in ("SMALL","LARGE"):
        runs={}
        for label in ORDER:
            directory=root/profile/label
            with (directory/"receipts.csv").open() as stream: receipts=list(csv.DictReader(stream))
            rows={}
            for path in sorted(directory.glob("counts*")):
                row=counts(path)
                if row:
                    key=row["label"].split("/",1)[1]
                    if key in rows:raise ValueError(f"duplicate region: {path}")
                    rows[key]=row
            if set(rows)!=set(CASES) or len(receipts)!=len(CASES):raise ValueError(f"missing regions: {directory}")
            receipt_map={r["case"].split("/",1)[1]:(r["transactions"],r["digest"]) for r in receipts}
            if set(receipt_map)!=set(CASES) or any(r[0]!="200" for r in receipt_map.values()):raise ValueError("invalid receipt inventory")
            runs[label]=(rows,receipt_map)
        for role in ("snapshot","delta","replace"):
            first,second=runs[role+"1"],runs[role+"2"]
            if first!=second:raise ValueError(f"nonidentical repetition: {profile}/{role}")
            if first[1]!=runs["snapshot1"][1]:raise ValueError(f"outputs differ: {profile}/{role}")
        for case in CASES:
            row={"profile":profile,"case":case,"paths":{}}
            reference=runs["snapshot1"][0][case]["total"]
            for role in ("snapshot","replace","delta"):
                measured=runs[role+"1"][0][case]
                row["paths"][role]={**measured,"vs_snapshot_percent":[100*(x/y-1) if y else None for x,y in zip(measured["total"],reference)]}
            comparisons.append(row)
    result={"schema":1,"scope":"SDK input transaction, solve, commit and result release; oracle outside collection",
            "limits":"Software Ir/Dr/Dw, not hardware counters or latency. Same candidate SDK for all three entries. No revision speedup claim.",
            "transactions_per_region":200,"profiles":2,"cases_per_profile":32,"regions":384,
            "repeated_regions":192,"identical_repetitions":True,"comparisons":comparisons}
    (root/"report.json").write_text(json.dumps(result,indent=2)+"\n")
    lines=["# Retained input software counts","",result["limits"],"",
           "All 192 region pairs repeat exactly, including per-function Ir/Dr/Dw and attribution residuals.","",
           "| Profile | Case | Snapshot Ir | Replace ΔIr | Delta ΔIr |",
           "|---|---|---:|---:|---:|"]
    for row in comparisons:
        paths=row["paths"]
        lines.append(f"| {row['profile']} | {row['case']} | {paths['snapshot']['total'][0]} | {paths['replace']['vs_snapshot_percent'][0]:+.2f}% | {paths['delta']['vs_snapshot_percent'][0]:+.2f}% |")
    (root/"report.md").write_text("\n".join(lines)+"\n")
    return result

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument("output",type=Path);p.add_argument("--report-only",action="store_true")
    args=p.parse_args();out=args.output.resolve()
    if ROOT==out or ROOT in out.parents:p.error("output must be outside Git")
    if args.report_only:report(out);return
    out.mkdir(parents=True,exist_ok=True)
    if (out/"manifest.json").exists():p.error("refusing to overwrite evidence")
    def command(argv,log):
        with log.open("w") as stream:subprocess.run(argv,cwd=ROOT,check=True,stdout=stream,stderr=subprocess.STDOUT)
    tracked=subprocess.check_output(["git","ls-files","--cached","--others","--exclude-standard","-z"],cwd=ROOT).decode().split("\0")
    manifest={"head":subprocess.check_output(["git","rev-parse","HEAD"],cwd=ROOT).decode().strip(),
              "status":subprocess.check_output(["git","status","--porcelain"],cwd=ROOT).decode(),
              "cases":CASES,"order":ORDER,"steps":200,"sources":{f:hashlib.sha256((ROOT/f).read_bytes()).hexdigest() for f in sorted(set(tracked)) if f and (ROOT/f).is_file()}}
    if os.environ.get("BASE_REF"):
        manifest["contextual_base"]=subprocess.check_output(
            ["git","rev-parse",os.environ["BASE_REF"]+"^{commit}"],cwd=ROOT).decode().strip()
        manifest["base_scope"]="Provenance only: all three measured entries use head's installed SDK."
    (out/"manifest.json").write_text(json.dumps(manifest,indent=2)+"\n")
    (out/"cpu.txt").write_text(Path("/proc/cpuinfo").read_text())
    command(["clang","--version"],out/"compiler.txt");command(["valgrind","--version"],out/"valgrind.txt")
    command(["uname","-a"],out/"platform.txt")
    for profile in ("SMALL","LARGE"):
        path=out/profile;path.mkdir()
        command(["cmake","-S",str(ROOT),"-B",str(path/"build"),"-DCMAKE_BUILD_TYPE=Release","-DBUILD_TESTING=OFF",
                 "-DCMAKE_C_COMPILER=clang","-DMAELYS_DATALOG_PROFILE_LARGE="+("ON" if profile=="LARGE" else "OFF"),
                 "-DCMAKE_INSTALL_PREFIX="+str(path/"sdk")],path/"configure.log")
        command(["cmake","--build",str(path/"build"),"--target","maelys_datalog","-j4"],path/"build.log")
        for component in ("sdk","sdk-static"):command(["cmake","--install",str(path/"build"),"--component",component],path/(component+".log"))
        binary=path/"retained-inputs"
        command(["clang","-std=c11","-O3","-g","-UNDEBUG","-Wall","-Wextra","-Werror","-I"+str(path/"sdk/include"),
                 str(ROOT/"bench/retained_inputs.c"),str(path/"sdk/lib/libmaelys_datalog.a"),"-o",str(binary)],path/"driver-build.log")
        command(["nm","-n",str(binary)],path/"symbols.txt");command(["objdump","-dr",str(binary)],path/"disassembly.txt")
        (path/"binary.sha256").write_text(hashlib.sha256(binary.read_bytes()).hexdigest()+"\n")
        for role in ("snapshot","replace","delta"):command([str(binary),role],path/(role+"-check.csv"))
    # All builds completed. Collection processes run sequentially, never timed.
    for profile in ("SMALL","LARGE"):
        for label in ORDER:
            path=out/profile/label;path.mkdir()
            with (path/"receipts.csv").open("w") as stdout,(path/"valgrind.log").open("w") as stderr:
                subprocess.run(["valgrind","--tool=callgrind","--cache-sim=yes","--branch-sim=no","--collect-atstart=no",
                                "--error-exitcode=3","--callgrind-out-file="+str(path/"counts"),
                                str(out/profile/"retained-inputs"),label[:-1]],cwd=ROOT,check=True,stdout=stdout,stderr=stderr)
    report(out)

if __name__=="__main__":main()
