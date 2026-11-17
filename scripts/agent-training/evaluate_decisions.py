#!/usr/bin/env python3
"""Compare local typed next-step judgments on identical advisory decision frames.

This measures text-state routing, not edited pixels or autonomous completion.
Model requests exclude labels and previous planner choices. No network serving,
app mutations, customer images, or automatic promotion are involved.
"""
from __future__ import annotations

import argparse
import hashlib
import importlib.util
import json
import math
from pathlib import Path
import random
import statistics
import time


def canonical(value):
    return json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":")).encode()


def validate_frame(frame):
    content = {k: v for k, v in frame.items() if k != "requestDigest"}
    if frame.get("contract") != "looksy-agent-decision-v1" or frame.get("advisoryOnly") is not True:
        raise ValueError("Expected an advisory Looksy decision frame")
    if hashlib.sha256(canonical(content)).hexdigest() != frame.get("requestDigest"):
        raise ValueError("Decision frame digest mismatch")
    candidates = frame["candidates"]
    ids = [candidate["id"] for candidate in candidates]
    if not 1 <= len(ids) <= 16 or len(set(ids)) != len(ids) or "insufficient_evidence" not in ids:
        raise ValueError("Invalid candidates or missing abstention")
    return ids


def choice_schema(frame):
    return {"type": "object", "additionalProperties": False,
            "properties": {"choice": {"type": "string", "enum": validate_frame(frame)}},
            "required": ["choice"]}


def systemone_request(frame):
    validate_frame(frame)
    return {"model": "clef-flash", "state": frame["state"],
            "questions": {"next_step": {"type": "choice", "instructions": frame["instructions"],
                "criteria": {c["id"]: {k: v for k, v in c.items() if k != "id"}
                             for c in frame["candidates"]}}}}


def validate_choice(frame, choice, probabilities=None):
    ids = validate_frame(frame)
    if choice not in ids:
        raise ValueError("Model chose an unavailable candidate")
    if probabilities is not None:
        if set(probabilities) != set(ids) or any(isinstance(v, bool) or not isinstance(v, (int, float)) or
                not math.isfinite(v) or not 0 <= v <= 1 for v in probabilities.values()):
            raise ValueError("Malformed probability distribution")
        if abs(sum(probabilities.values()) - 1) > 0.005:
            raise ValueError("Probabilities do not sum to one")
        if probabilities[choice] < max(probabilities.values()) - 0.0001:
            raise ValueError("Chosen option is not the maximum-probability option")
    return {"requestDigest": frame["requestDigest"], "choice": choice}


def save(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    tmp = path.with_suffix(path.suffix + ".tmp")
    tmp.write_text(json.dumps(value, indent=2) + "\n")
    tmp.replace(path)


def load_records(path):
    if path.is_dir():
        records, seen = [], set()
        for file in sorted(path.glob("*.json")):
            record = json.loads(file.read_text())
            frame = record["frame"]
            validate_frame(frame)
            if frame["requestDigest"] in seen:
                continue
            seen.add(frame["requestDigest"])
            # Original planner behavior is excluded from the replay input and
            # cannot become a correctness label. An optional independent review
            # can add acceptable choices explicitly, outside the frame.
            records.append({"id": frame["requestDigest"], "frame": frame,
                            "acceptableChoices": record.get("review", {}).get("acceptableChoices")})
    else:
        records = json.loads(path.read_text())
    if not records or len({r["id"] for r in records}) != len(records):
        raise ValueError("Provide nonempty frames with unique IDs")
    for record in records:
        ids = validate_frame(record["frame"])
        labels = record.get("acceptableChoices")
        if labels is not None and (not isinstance(labels, list) or not labels or not set(labels) <= set(ids)):
            raise ValueError("Invalid independent review labels")
    # Excludes original planner behavior, includes any independent review labels.
    return records, hashlib.sha256(canonical(records)).hexdigest()


def verify_loader(model_path, lock_path):
    lock = json.loads(lock_path.read_text())
    item = next(f for f in lock["files"] if f["path"] == "clef_mlx.py")
    source = model_path / item["path"]
    if hashlib.sha256(source.read_bytes()).hexdigest() != item["sha256"]:
        raise ValueError("Custom Clef loader differs from the reviewed, pinned source")
    return source


def run(args):
    import mlx.core as mx
    mx.set_memory_limit(30 * 1024**3)
    mx.set_cache_limit(512 * 1024**2)
    mx.random.seed(42)
    records, records_digest = load_records(args.frames)
    load_started = time.perf_counter()
    if args.engine == "clef":
        path = verify_loader(args.model, args.lock)
        spec = importlib.util.spec_from_file_location("looksy_pinned_clef", path)
        module = importlib.util.module_from_spec(spec)
        import sys
        sys.modules[spec.name] = module
        spec.loader.exec_module(module)
        model = module.load(args.model.resolve())

        def decide(frame):
            response = model.systemone(systemone_request(frame), max_length=16384, truncate=False)
            answer = response["answers"]["next_step"]
            bound = validate_choice(frame, answer["choice"], answer["probabilities"])
            return bound, {"probabilities": answer["probabilities"], "usage": response["usage"],
                           "probabilitySemantics": "uncalibrated on Looksy; never admission authority"}
    else:
        from mlx_vlm import load, apply_chat_template, generate
        from mlx_vlm.structured import build_json_schema_logits_processor
        model, processor = load(str(args.model.resolve()),
            adapter_path=str(args.adapter.resolve()) if args.adapter else None)
        model.eval()
        tokenizer = getattr(processor, "tokenizer", processor)

        def decide(frame):
            # Both engines receive identical state, criteria and instructions;
            # the LLM sees them in its chat format. The runner binds the digest
            # for both, so neither model spends inference copying a checksum.
            request = systemone_request(frame)
            prompt = apply_chat_template(processor, model.config, [
                {"role": "system", "content": frame["instructions"] + " Return only JSON with choice."},
                {"role": "user", "content": json.dumps({"state": request["state"],
                    "criteria": request["questions"]["next_step"]["criteria"]}, ensure_ascii=False)}],
                enable_thinking=False)
            result = generate(model, processor, prompt=prompt, temperature=0.0, max_tokens=64,
                logits_processors=[build_json_schema_logits_processor(tokenizer, choice_schema(frame))],
                verbose=False)
            raw = json.loads(result.text)
            if set(raw) != {"choice"}:
                raise ValueError("Unexpected LLM response fields")
            return validate_choice(frame, raw["choice"]), {"generationTokens": result.generation_tokens,
                "promptTokens": result.prompt_tokens, "rawResponse": result.text}

    load_seconds = time.perf_counter() - load_started
    # Same unlabeled warm-up for both engines, excluded from scored latency.
    decide(records[0]["frame"])
    mx.clear_cache()
    order = list(range(len(records)))
    random.Random(42).shuffle(order)
    report = {"contract": "looksy-agent-decision-comparison-v1", "engine": args.engine,
        "model": str(args.model), "adapter": str(args.adapter) if args.adapter else None,
        "framesSha256": records_digest,
        "scope": "text-state routing only; no executed edits, visual quality, human preference or autonomous completion score; correctness is unscored without separate review labels",
        "loadSeconds": load_seconds, "repeats": args.repeats, "cases": [],
        "uniqueFrames": len(records), "modelInputsExcludeLabels": True, "authority": "advisory only"}
    for repeat in range(args.repeats):
        for index in order:
            record = records[index]
            started = time.perf_counter()
            item = {"id": record["id"], "repeat": repeat,
                    "requestDigest": record["frame"]["requestDigest"]}
            try:
                response, extra = decide(record["frame"])
                labels = record.get("acceptableChoices")
                item.update({"response": response, "passed": response["choice"] in labels if labels is not None else None,
                             **extra})
            except Exception as error:
                item.update({"passed": False if record.get("acceptableChoices") is not None else None,
                             "errorType": type(error).__name__, "error": str(error)[:500]})
            item["elapsedMs"] = (time.perf_counter() - started) * 1000
            report["cases"].append(item)
            report.update({"passed": sum(c["passed"] is True for c in report["cases"]),
                "scoredTotal": sum(c["passed"] is not None for c in report["cases"]),
                "total": len(report["cases"]), "peakMemoryGB": mx.get_peak_memory() / 1e9,
                "medianLatencyMs": statistics.median(c["elapsedMs"] for c in report["cases"])})
            save(args.output, report)
            print(json.dumps({k: item[k] for k in ("id", "repeat", "passed", "elapsedMs")}), flush=True)
            mx.clear_cache()


def compare(args):
    a, b = [json.loads(path.read_text()) for path in args.reports]
    if a["framesSha256"] != b["framesSha256"] or a["repeats"] != b["repeats"]:
        raise ValueError("Reports must use byte-identical frames and repeat counts")
    left = {(r["id"], r["repeat"]): r for r in a["cases"]}
    right = {(r["id"], r["repeat"]): r for r in b["cases"]}
    unique = a.get("uniqueFrames", 24)
    if a["engine"] == b["engine"] or set(left) != set(right) or len(left) != unique * a["repeats"] or \
            any(sum(key[0] == identity for key in left) != a["repeats"] for identity, _ in left):
        raise ValueError("Reports are incomplete or use different cases")
    result = {"contract": "looksy-agent-decision-paired-summary-v1", "framesSha256": a["framesSha256"],
        "scope": a["scope"], "repeatsAreNotIndependentTasks": True,
        "engines": [{k: r.get(k, r["total"] if k == "scoredTotal" else None)
                     for k in ("engine", "passed", "total", "scoredTotal", "medianLatencyMs", "peakMemoryGB", "loadSeconds")}
                    for r in (a, b)],
        "disagreements": [{"id": key[0], "repeat": key[1], a["engine"]: left[key].get("response"),
                            b["engine"]: right[key].get("response")}
                          for key in left if left[key].get("response") != right[key].get("response")],
        "promotion": "none; requires actual app trajectories and independent visible-result review"}
    save(args.output, result)
    print(json.dumps(result["engines"], indent=2))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--engine", choices=["gemma", "clef", "compare"], required=True)
    parser.add_argument("--frames", type=Path, default=Path("training/agent-decisions/frames.json"))
    parser.add_argument("--model", type=Path)
    parser.add_argument("--adapter", type=Path)
    parser.add_argument("--lock", type=Path, default=Path("training/agent-decisions/clef.lock.json"))
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--reports", nargs=2, type=Path)
    parser.add_argument("--repeats", type=int, default=3)
    args = parser.parse_args()
    if args.engine == "compare":
        if not args.reports:
            parser.error("Comparison requires two --reports")
        compare(args)
    else:
        if not args.model or not 1 <= args.repeats <= 5:
            parser.error("Provide a local model and 1-5 repeats")
        run(args)


if __name__ == "__main__":
    main()
