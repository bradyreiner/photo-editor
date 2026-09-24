#!/usr/bin/env python3
"""Offline explicit-fixture proof using the same encoder as the owned worker.

Not a packaged-app or RAW-decoder proof. No implicit downloads or remote code.
"""
import argparse
import json
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "scripts"))
from search_model_runtime import Encoder, digest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--model-root", type=Path, required=True)
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--device", choices=("cpu", "mps"), default="cpu")
    args = parser.parse_args()
    if args.output.exists() or args.output.is_symlink():
        raise ValueError("Output must be a new file")
    manifest = json.loads(args.manifest.read_text())
    images, queries = manifest["images"], manifest["queries"]
    if not 1 <= len(images) <= 64 or not 1 <= len(queries) <= 64:
        raise ValueError("Explicit proof scope must contain 1-64 images and queries")
    ids = [item["id"] for item in images]
    if len(set(ids)) != len(ids) or any(not isinstance(value, str) or not value for value in ids):
        raise ValueError("Image IDs must be nonempty and unique")
    for query in queries:
        if not isinstance(query["text"], str) or not query["text"].strip() or len(query["text"]) > 2000:
            raise ValueError("Invalid query")
        if not query["expectedIds"] or any(value not in ids for value in query["expectedIds"]):
            raise ValueError("Each query needs explicit expected IDs from the fixture scope")
    encoder = Encoder(args.model_root, args.device)
    image_receipts, query_receipts = [], []
    for item in images:
        result = encoder.image(Path(item["path"]))
        image_receipts.append({"id": item["id"], "assetKey": digest(item["id"].encode()), **result})
        print(f"encoded image {item['id']}: {result['milliseconds']:.1f} ms", flush=True)
    for item in queries:
        durations = []
        for _ in range(4):
            result = encoder.text(item["text"])
            durations.append(result["milliseconds"])
        scores = [sum(x * y for x, y in zip(result["vector"], image["vector"], strict=True)) for image in image_receipts]
        ranking = sorted(range(len(ids)), key=lambda i: (-scores[i], i))
        query_receipts.append({"text": item["text"], "expectedAssetKeys": [digest(value.encode()) for value in item["expectedIds"]],
                               "vector": result["vector"], "milliseconds": durations,
                               "pythonRanking": [{"id": ids[i], "similarity": scores[i]} for i in ranking]})
        print(f"query {item['text']!r}: first={ids[ranking[0]]}; times={durations}", flush=True)
    receipt = {"schema": "real-visual-embeddings-v1", "artifactVerified": True, "pythonNetworkDisabled": True,
               "contract": encoder.contract, "spaceKey": encoder.space_key, "dimensions": 768,
               "verificationMilliseconds": encoder.verification_ms, "loadMilliseconds": encoder.load_ms,
               "images": image_receipts, "queries": query_receipts}
    with args.output.open("x") as handle:
        json.dump(receipt, handle, indent=2, allow_nan=False)
    args.output.chmod(0o600)


if __name__ == "__main__":
    main()
