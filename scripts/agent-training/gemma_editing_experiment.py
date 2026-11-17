#!/usr/bin/env python3
"""Evaluate and QLoRA-tune Gemma's language-side editing workflow behavior locally."""
from __future__ import annotations

import argparse
import hashlib
import json
import time
from pathlib import Path


def rows(path):
    return [json.loads(line) for line in path.read_text().splitlines() if line.strip()]


def write_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2, ensure_ascii=False) + "\n")


def score_response(text, row, catalog):
    from jsonschema import Draft202012Validator
    expected = json.loads(row["messages"][-1]["content"])
    try:
        actual = json.loads(text.strip())
        if actual.get("decision") != expected["decision"]:
            return False, "wrong_decision"
        if expected["decision"] == "cannot_complete_safely":
            return bool(str(actual.get("reason", "")).strip()), "safe_stop"
        calls = actual["toolCalls"]
        if len(calls) != 1:
            return False, "wrong_call_count"
        call, target = calls[0], expected["toolCalls"][0]
        if call["name"] not in catalog:
            return False, "unknown_tool"
        Draft202012Validator(catalog[call["name"]]["inputSchema"]).validate(call["arguments"])
        if call["name"] != target["name"]:
            return False, "wrong_tool"
        arguments = dict(call["arguments"])
        if call["name"] == "app.list_commands":
            if arguments.get("offset", 0) != 0:
                return False, "skipped_discovery_page"
            arguments.pop("offset", None)
            arguments.pop("limit", None)
        return arguments == target["arguments"], "arguments_match" if arguments == target["arguments"] else "wrong_arguments"
    except (ValueError, TypeError, KeyError, AttributeError) as error:
        return False, type(error).__name__
    except Exception as error:
        if type(error).__name__ == "ValidationError":
            return False, "schema_rejected"
        raise


def load_runtime(args):
    import mlx.core as mx
    from mlx_vlm import load
    mx.set_memory_limit(int(args.memory_gib * 1024**3))
    mx.set_cache_limit(512 * 1024**2)
    mx.random.seed(42)
    started = time.perf_counter()
    model, processor = load(str(args.model.resolve()), adapter_path=str(args.adapter.resolve()) if args.adapter else None)
    print(json.dumps({"event": "model_loaded", "seconds": time.perf_counter() - started,
                      "activeMemoryGB": mx.get_active_memory() / 1e9}), flush=True)
    return model, processor


def gemma_causality_probe(model):
    """Changing future tokens must not change logits for an identical prefix."""
    import mlx.core as mx
    original = mx.array([[2, 1000, 2000, 3000, 4000, 5000, 6000, 7000]])
    changed = mx.array([[2, 1000, 2000, 3000, 9000, 9100, 9200, 9300]])
    results = {}
    for label, mask in (("padding_mask_forward", mx.ones(original.shape, dtype=mx.int32)),
                        ("model_causal_forward", None)):
        a = model(original, None, mask)
        b = model(changed, None, mask)
        a, b = getattr(a, "logits", a), getattr(b, "logits", b)
        results[label] = float(mx.max(mx.abs(a[:, :3, :].astype(mx.float32) - b[:, :3, :].astype(mx.float32))).item())
    if results["model_causal_forward"] > 0.001:
        raise ValueError(f"Gemma's normal causal forward failed the future-token invariance check: {results}")
    return results


def install_gemma_causal_training_guard():
    """Keep padding for loss masking; let Gemma build its own causal/window masks.

    mlx-vlm 0.7.12 special-cases gemma4_unified but not gemma4 in the SFT
    forward helper. A [batch, sequence] padding mask overrides Gemma's own
    attention masks. This experiment uses one right-padded text sequence per
    batch, so a valid token cannot attend future padding when Gemma builds its
    normal causal masks. No installed package or inference model is modified.
    """
    from mlx_vlm.trainer.vlm.sft import runtime
    upstream_forward = runtime._forward_vlm

    def forward(model, input_ids, attention_mask, batch, **kwargs):
        if runtime._model_type(model) == "gemma4":
            if input_ids.shape[0] != 1 or batch.get("pixel_values") is not None:
                raise ValueError("The Gemma causal guard is qualified only for batch-one, right-padded text workflow training")
            return upstream_forward(model, input_ids, None, batch, **kwargs)
        return upstream_forward(model, input_ids, attention_mask, batch, **kwargs)

    runtime._forward_vlm = forward


def evaluate(args):
    import mlx.core as mx
    from mlx_vlm import generate, apply_chat_template
    model, processor = load_runtime(args)
    model.eval()
    cases = rows(args.data / (args.split + ".jsonl"))
    if args.limit:
        cases = cases[:args.limit]
    catalog = {t["name"]: t for t in json.loads(args.catalog.read_text())["tools"]}
    report = {"contract": "looksy-gemma-editing-next-action-eval-v1", "adapter": str(args.adapter) if args.adapter else None,
              "split": args.split, "cases": [], "scope": "synthetic workflow mechanics; no rendered editing quality qualification",
              "dataSha256": hashlib.sha256((args.data / (args.split + ".jsonl")).read_bytes()).hexdigest()}
    for row in cases:
        prompt = apply_chat_template(processor, model.config, row["messages"][:-1], enable_thinking=False)
        start = time.perf_counter()
        result = generate(model, processor, prompt=prompt, max_tokens=320, temperature=0.0, verbose=False)
        passed, reason = score_response(result.text, row, catalog)
        item = {"id": row["id"], "family": row["family"], "passed": passed, "reason": reason,
                "elapsedSeconds": time.perf_counter() - start, "response": result.text,
                "generationTokens": getattr(result, "generation_tokens", None)}
        report["cases"].append(item)
        report["passed"] = sum(c["passed"] for c in report["cases"])
        report["total"] = len(report["cases"])
        report["peakMemoryGB"] = mx.get_peak_memory() / 1e9
        write_json(args.output, report)
        print(json.dumps({k: item[k] for k in ("id", "passed", "reason", "elapsedSeconds")}), flush=True)
        mx.clear_cache()
    if args.image:
        prompt = apply_chat_template(processor, model.config,
            "Describe the visible photograph in two short sentences. Identify the main subject and lighting. Do not claim to have edited it.",
            num_images=1, enable_thinking=False)
        result = generate(model, processor, prompt=prompt, image=[str(args.image.resolve())], max_tokens=160,
                          temperature=0.0, verbose=False)
        report["visionSmoke"] = {"sourceSha256": hashlib.sha256(args.image.read_bytes()).hexdigest(), "response": result.text,
                                  "scope": "manual semantic readback; not a photographic editing quality score"}
        write_json(args.output, report)
        print(json.dumps({"event": "vision_smoke", "response": result.text}), flush=True)


def train(args):
    import mlx.core as mx
    import mlx.optimizers as optim
    from mlx.utils import tree_flatten
    from mlx_vlm.trainer import Trainer, TrainingCallback, VLMTrainingArgs, prepare_model_for_training
    model, processor = load_runtime(args)
    args.output.mkdir(parents=True, exist_ok=False)
    probe = gemma_causality_probe(model)
    write_json(args.output / "causality-probe.json", probe)
    print(json.dumps({"event": "causality_probe", **probe}), flush=True)
    install_gemma_causal_training_guard()
    # Full qualified targets avoid touching experts, router, embeddings or vision.
    targets = [name for name, _ in model.named_modules()
               if name.startswith("language_model.model.layers.")
               and int(name.split(".")[3]) >= 24
               and name.split(".")[-1] in {"q_proj", "o_proj"}]
    if len(targets) != 12:
        raise ValueError(f"Expected 12 attention projections in the last six layers, got {targets}")
    prepare_model_for_training(model, train_type="lora", lora_rank=8, lora_alpha=16, target_modules=targets)
    trainable = tree_flatten(model.trainable_parameters())
    if not trainable or any(not name.startswith("language_model.") or "lora_" not in name for name, _ in trainable):
        raise ValueError("Training must update language-side LoRA tensors only")
    config = VLMTrainingArgs(iters=args.iters, batch_size=1, gradient_accumulation_steps=1,
        max_seq_length=1536, train_on_completions=True, grad_checkpoint=True,
        val_batches=4, steps_per_report=5, steps_per_eval=25, steps_per_save=25,
        adapter_file=str(args.output / "adapters.safetensors"), compile=False, cache_size=2)
    metrics = []

    class Callback(TrainingCallback):
        def record(self, kind, info):
            item = {"kind": kind, **info}
            metrics.append(item)
            write_json(args.output / "metrics.json", metrics)
            print(json.dumps(item), flush=True)
        def on_train_loss_report(self, info):
            self.record("train", info)
        def on_val_loss_report(self, info):
            self.record("validation", info)

    metadata = {"contract": "looksy-gemma-editing-qlora-pilot-v1", "baseModel": str(args.model.resolve()),
                "targetModules": targets, "rank": 8, "alpha": 16, "iterations": args.iters,
                "learningRate": 5e-5, "seed": 42, "maxSequenceLength": 1536,
                "trainableParameters": sum(value.size for _, value in trainable),
                "causalTrainingGuard": "gemma4 batch-one right-padded text; derive model attention masks, retain padding/completion loss masks",
                "validationBatchesPerCheck": config.val_batches,
                "visionWeightsFrozen": True, "expertsFrozen": True, "baseWeightsFrozen": True,
                "corpus": json.loads((args.data / "manifest.json").read_text())}
    write_json(args.output / "experiment.json", metadata)
    trainer = Trainer(model, config, optimizer=optim.AdamW(learning_rate=5e-5),
        train_dataset=rows(args.data / "train.jsonl"), eval_dataset=rows(args.data / "valid.jsonl"),
        processing_class=processor, dataset_config={"train_on_completions": True}, training_callback=Callback())
    before = trainer.evaluate()
    write_json(args.output / "validation-before.json", before)
    print(json.dumps({"event": "validation_before", **before}), flush=True)
    started = time.perf_counter()
    result = trainer.train()
    trainer.save_adapter()
    after = trainer.evaluate()
    write_json(args.output / "validation-after.json", after)
    write_json(args.output / "result.json", {"train": result, "validationBefore": before, "validationAfter": after,
        "elapsedSeconds": time.perf_counter() - started, "peakMemoryGB": mx.get_peak_memory() / 1e9})
    print(json.dumps({"event": "training_complete", "validation": after}), flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("mode", choices=["evaluate", "train"])
    parser.add_argument("--model", type=Path, required=True)
    parser.add_argument("--data", type=Path, default=Path("training/gemma4-editing/data"))
    parser.add_argument("--catalog", type=Path, default=Path("training/gemma4-editing/tool-catalog.json"))
    parser.add_argument("--adapter", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--split", choices=["valid", "test"], default="test")
    parser.add_argument("--limit", type=int)
    parser.add_argument("--iters", type=int, default=100)
    parser.add_argument("--memory-gib", type=float, default=34)
    parser.add_argument("--image", type=Path)
    args = parser.parse_args()
    if not 0 < args.memory_gib <= 36 or not 0 < args.iters <= 200:
        parser.error("Use at most 36 GiB of MLX memory and 1-200 training iterations for this local pilot")
    if args.mode == "train" and args.adapter:
        parser.error("Train a fresh adapter from the immutable base; --adapter is for evaluation")
    (evaluate if args.mode == "evaluate" else train)(args)


if __name__ == "__main__":
    main()
