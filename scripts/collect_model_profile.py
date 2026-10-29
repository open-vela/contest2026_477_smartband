"""Extract real [mooncat_model] log rows. Does not synthesize missing samples."""
import argparse
import csv
from pathlib import Path
import re


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("log",type=Path)
    parser.add_argument("--device",required=True)
    parser.add_argument("--build",required=True)
    parser.add_argument("--output",type=Path,required=True)
    args=parser.parse_args()
    fields=["timing_valid","init_us","feature_us","invoke_us","total_us","arena_used","arena_capacity","intent"]
    rows=[]
    for line_number,line in enumerate(args.log.read_text(encoding="utf-8",errors="replace").splitlines(),1):
        if "[mooncat_model]" not in line:continue
        values=dict(re.findall(r"(\w+)=(\d+)",line))
        if not all(field in values for field in fields):continue
        rows.append({"device":args.device,"build":args.build,"source_line":line_number,**{f:int(values[f]) for f in fields}})
    args.output.parent.mkdir(parents=True,exist_ok=True)
    with args.output.open("w",newline="",encoding="utf-8") as stream:
        writer=csv.DictWriter(stream,fieldnames=["device","build","source_line"]+fields)
        writer.writeheader();writer.writerows(rows)
    print(f"samples={len(rows)} timing_valid={sum(r['timing_valid']==1 for r in rows)}")
    if not rows:print("NOT_MEASURED: no model profile samples in this log")


if __name__=="__main__":main()
