"""Evaluate dev (default) or heldout text with host TFLite + actual C dispatcher.
No network/tool execution. The compiled harness emits planned tool calls only.
"""
import argparse
import importlib.util
import json
import platform
from pathlib import Path
import subprocess
import time

import numpy as np

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", required=True)
    parser.add_argument("--wsl", help="Run compiled Linux harness inside this local WSL distro")
    parser.add_argument("--split", choices=["dev", "heldout"], default="dev")
    parser.add_argument("--dataset",type=Path,default=ROOT / "local_router/finals_cases_v2.jsonl")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    import tensorflow as tf
    spec = importlib.util.spec_from_file_location("train_router", ROOT / "local_router/train_router.py")
    training = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(training)
    model = tf.lite.Interpreter(model_path=str(ROOT / "local_router/artifacts/mooncat_intent_router_int8.tflite"), num_threads=1)
    model.allocate_tensors()
    inp, out = model.get_input_details()[0], model.get_output_details()[0]
    scale, zero = inp["quantization"]
    output_scale, output_zero = out["quantization"]
    cases = [json.loads(line) for line in args.dataset.read_text(encoding="utf-8").splitlines()]
    cases = [c for c in cases if c["split"] == args.split]
    predictions = []
    for case in cases:
        t0 = time.perf_counter_ns()
        features = training.featurize(case["text"])
        quantized = np.clip(np.rint(features / scale + zero), -128, 127).astype(inp["dtype"]).reshape(inp["shape"])
        t1 = time.perf_counter_ns()
        model.set_tensor(inp["index"], quantized)
        model.invoke()
        t2 = time.perf_counter_ns()
        probs = (model.get_tensor(out["index"])[0].astype(float) - output_zero) * output_scale
        top = np.argsort(probs)[::-1];confidence=float(probs[top[0]]);margin=float(probs[top[0]]-probs[top[1]])
        intent = int(top[0]) if confidence>=0.5 and margin>=0.05 else 4
        predictions.append({"intent":intent,"confidence":confidence,"margin":margin,
                            "host_features_us":(t1-t0)/1000,"host_set_and_invoke_us":(t2-t1)/1000})
    results = {}
    for mode in ["rules_only", "rules_plus_model"]:
        lines = []
        for case, pred in zip(cases, predictions):
            intent = 4 if mode == "rules_only" else pred["intent"]
            lines.append(f'{intent} {pred["confidence"]} {pred["margin"]} {case["text"].encode("utf-8").hex()}')
        command = [args.binary]
        if args.wsl:
            command = ["wsl", "-d", args.wsl, "--exec", args.binary]
        run = subprocess.run(command, input="\n".join(lines)+"\n", capture_output=True, encoding="utf-8", errors="replace", check=True)
        actual = run.stdout.splitlines()
        if len(actual)!=len(cases):
            raise RuntimeError("harness output count mismatch: " + run.stdout)
        rows=[]
        for case,pred,line in zip(cases,predictions,actual):
            route,tool,tool_args = line.split("\t")
            params=json.loads(tool_args) if tool_args else {}
            correct=tool==case["tool"]
            if "mode" in case: correct=correct and params.get("mode")==case["mode"]
            if "op" in case: correct=correct and params.get("op")==case["op"]
            if "delay" in case:
                correct=correct and (params.get("delay")==case["delay"] if tool=="mooncat_app_control" else params.get("at_epoch")==1800000000+case["delay"])
            rows.append({**case,**pred,"route":route,"actual_tool":tool,"actual_args":params,"plan_correct":correct})
        results[mode]={"cases":len(rows),"planned_tool_calls":sum(bool(r["actual_tool"]) for r in rows),
                       "plan_correct":sum(r["plan_correct"] for r in rows),
                       "negative_takeovers":sum(not r["tool"] and bool(r["actual_tool"]) for r in rows),"rows":rows}
    report={"dataset":args.dataset.stem,"split":args.split,"environment":platform.platform(),"tensorflow":tf.__version__,
            "scope":"Host TFLite and compiled rule/router plans with tool spies; excludes full agent guard, live tools, network and device measurements.","results":results}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report,ensure_ascii=False,indent=2)+"\n",encoding="utf-8")
    print(json.dumps({k:{a:b for a,b in v.items() if a!="rows"} for k,v in results.items()},ensure_ascii=False))


if __name__ == "__main__":
    main()
