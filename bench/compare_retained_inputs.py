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
DELIVERY_ORDER=("snapshot61","delta61","delta71","delta72","delta62","snapshot62")
CASES=[f"{work}/{n}/{kind}/{change}" for work in ("inert","projection") for kind in ("integer","symbol")
       for n in (64,256) for change in ("empty","one","small","replace")]

def report(root):
    delivery=(root/"manifest.json").exists() and json.loads((root/"manifest.json").read_text()).get("backend_delivery",False)
    roles=("snapshot6","delta6","delta7") if delivery else ("snapshot","replace","delta")
    order=DELIVERY_ORDER if delivery else ORDER
    reference_role=roles[0]
    comparisons=[]
    for profile in ("SMALL","LARGE"):
        runs={}
        for label in order:
            directory=root/profile/label
            with (directory/"receipts.csv").open() as stream: receipts=list(csv.DictReader(stream))
            rows={}
            for path in sorted(directory.glob("counts.*")):
                row=counts(path)
                if row:
                    key=row["label"].split("/",1)[1]
                    if key in rows:raise ValueError(f"duplicate region: {path}")
                    rows[key]=row
            if set(rows)!=set(CASES) or len(receipts)!=len(CASES):raise ValueError(f"missing regions: {directory}")
            receipt_map={r["case"].split("/",1)[1]:(r["transactions"],r["digest"]) for r in receipts}
            if set(receipt_map)!=set(CASES) or any(r[0]!="200" for r in receipt_map.values()):raise ValueError("invalid receipt inventory")
            runs[label]=(rows,receipt_map)
        for role in roles:
            first,second=runs[role+"1"],runs[role+"2"]
            if first!=second:raise ValueError(f"nonidentical repetition: {profile}/{role}")
            if first[1]!=runs[reference_role+"1"][1]:raise ValueError(f"outputs differ: {profile}/{role}")
        for case in CASES:
            row={"profile":profile,"case":case,"paths":{}}
            reference=runs[reference_role+"1"][0][case]["total"]
            for role in roles:
                measured=runs[role+"1"][0][case]
                row["paths"][role]={**measured,"vs_snapshot_percent":[100*(x/y-1) if y else None for x,y in zip(measured["total"],reference)]}
                if delivery:
                    provider=[sum(v[i] for name,v in measured["functions"].items() if name.startswith("input_fixture_")) for i in range(3)]
                    row["paths"][role]["provider_exclusive"]=provider
                    row["paths"][role]["host_shared_and_driver"]=[measured["total"][i]-measured["residual"][i]-provider[i] for i in range(3)]
                    row["paths"][role]["vs_delta6_percent"]=[100*(x/y-1) if y else None for x,y in zip(measured["total"],runs["delta61"][0][case]["total"])]
            comparisons.append(row)
    result={"schema":1,"roles":roles,"reference_role":reference_role,"backend_delivery":delivery,
            "scope":"SDK input transaction, solve, commit and result release; oracle outside collection",
            "limits":"Software Ir/Dr/Dw, not hardware counters or latency. Same candidate SDK for all three entries. No revision speedup claim.",
            "transactions_per_region":200,"profiles":2,"cases_per_profile":32,"regions":384,
            "repeated_regions":192,"identical_repetitions":True,"comparisons":comparisons}
    (root/"report.json").write_text(json.dumps(result,indent=2)+"\n")
    lines=["# Retained input software counts","",result["limits"],"",
           "All 192 region pairs repeat exactly, including per-function Ir/Dr/Dw and attribution residuals.","",
           f"| Profile | Case | {roles[0]} Ir | {roles[1]} ΔIr | {roles[2]} ΔIr |",
           "|---|---|---:|---:|---:|"]
    for row in comparisons:
        paths=row["paths"]
        lines.append(f"| {row['profile']} | {row['case']} | {paths[roles[0]]['total'][0]} | {paths[roles[1]]['vs_snapshot_percent'][0]:+.2f}% | {paths[roles[2]]['vs_snapshot_percent'][0]:+.2f}% |")
    (root/"report.md").write_text("\n".join(lines)+"\n")
    return result

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument("output",type=Path);p.add_argument("--report-only",action="store_true")
    p.add_argument("--backend-delivery",action="store_true",help="Same public fixture provider, snapshot6/delta6/delta7")
    args=p.parse_args();out=args.output.resolve()
    roles=("snapshot6","delta6","delta7") if args.backend_delivery else ("snapshot","replace","delta")
    order=DELIVERY_ORDER if args.backend_delivery else ORDER
    if ROOT==out or ROOT in out.parents:p.error("output must be outside Git")
    if args.report_only:report(out);return
    out.mkdir(parents=True,exist_ok=True)
    if (out/"manifest.json").exists():p.error("refusing to overwrite evidence")
    def command(argv,log):
        with log.open("w") as stream:subprocess.run(argv,cwd=ROOT,check=True,stdout=stream,stderr=subprocess.STDOUT)
    tracked=subprocess.check_output(["git","ls-files","--cached","--others","--exclude-standard","-z"],cwd=ROOT).decode().split("\0")
    manifest={"head":subprocess.check_output(["git","rev-parse","HEAD"],cwd=ROOT).decode().strip(),
              "status":subprocess.check_output(["git","status","--porcelain"],cwd=ROOT).decode(),
              "cases":CASES,"order":order,"steps":200,"backend_delivery":args.backend_delivery,
              "sources":{f:hashlib.sha256((ROOT/f).read_bytes()).hexdigest() for f in sorted(set(tracked)) if f and (ROOT/f).is_file()}}
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
        extra=[]
        if args.backend_delivery:
            provider=path/"input-provider.o"
            command(["clang","-std=c11","-O3","-g","-Wall","-Wextra","-Werror","-I"+str(path/"sdk/include"),
                     "-c",str(ROOT/"sdk/conformance/input_provider.c"),"-o",str(provider)],path/"provider-build.log")
            command(["nm","-u",str(provider)],path/"provider-undefined.txt")
            extra=["-DINPUT_DELIVERY_BENCH","-I"+str(ROOT/"sdk/conformance"),str(provider)]
        command(["clang","-std=c11","-O3","-g","-UNDEBUG","-Wall","-Wextra","-Werror","-I"+str(path/"sdk/include"),
                 str(ROOT/"bench/retained_inputs.c"),*extra,str(path/"sdk/lib/libmaelys_datalog.a"),"-o",str(binary)],path/"driver-build.log")
        command(["nm","-n",str(binary)],path/"symbols.txt");command(["objdump","-dr",str(binary)],path/"disassembly.txt")
        (path/"binary.sha256").write_text(hashlib.sha256(binary.read_bytes()).hexdigest()+"\n")
        for role in roles:command([str(binary),role],path/(role+"-check.csv"))
    # All builds completed. Collection processes run sequentially, never timed.
    for profile in ("SMALL","LARGE"):
        for label in order:
            path=out/profile/label;path.mkdir()
            with (path/"receipts.csv").open("w") as stdout,(path/"valgrind.log").open("w") as stderr:
                subprocess.run(["valgrind","--tool=callgrind","--cache-sim=yes","--branch-sim=no","--collect-atstart=no",
                                "--error-exitcode=3","--callgrind-out-file="+str(path/"counts"),
                                str(out/profile/"retained-inputs"),label[:-1]],cwd=ROOT,check=True,stdout=stdout,stderr=stderr)
    report(out)

if __name__=="__main__":main()
