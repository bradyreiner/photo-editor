#!/usr/bin/env python3
"""Single-owner, serial stdin/stdout encoder. No socket service or implicit setup."""
import argparse
import contextlib
import json
from pathlib import Path
import sys

# Python -I excludes the script directory. Only the trusted sibling runtime is
# added; the caller's working directory and PYTHONPATH are not import authorities.
sys.path.insert(0, str(Path(__file__).resolve().parent))
from search_model_runtime import Encoder


def send(value):
    print(json.dumps(value, allow_nan=False, separators=(",", ":")), flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--model-root", type=Path, required=True)
    parser.add_argument("--device", choices=("cpu", "mps"), required=True)
    args = parser.parse_args()
    try:
        with contextlib.redirect_stdout(sys.stderr):
            encoder = Encoder(args.model_root, args.device)
        send({"type": "ready", "protocol": 1, "spaceKey": encoder.space_key, "dimensions": 768,
              "revision": encoder.contract["model"]["revision"], "device": args.device})
    except Exception as error:
        send({"type": "fatal", "message": str(error)[:500]})
        return 1
    while True:
        line = sys.stdin.buffer.readline(65537)
        if not line:
            return 0
        try:
            if len(line) > 65536 or not line.endswith(b"\n"):
                raise ValueError("Request exceeds line limit or is incomplete")
            request = json.loads(line)
            if not isinstance(request, dict) or request.get("kind") not in ("image", "text"):
                raise ValueError("Invalid request kind")
            kind = request["kind"]
            payload = "path" if kind == "image" else "text"
            if set(request) != {"id", "kind", payload} or not isinstance(request["id"], str) or not 1 <= len(request["id"]) <= 128:
                raise ValueError("Invalid request schema or identity")
            if not isinstance(request[payload], str) or len(request[payload]) > 4096:
                raise ValueError("Invalid request payload")
        except Exception as error:
            send({"type": "fatal", "message": str(error)[:500]})
            return 1
        try:
            with contextlib.redirect_stdout(sys.stderr):
                result = encoder.image(Path(request["path"])) if kind == "image" else encoder.text(request["text"])
            send({"type": "embedding", "id": request["id"], "kind": kind,
                  "spaceKey": encoder.space_key, "contentKey": result["contentKey"],
                  "vector": result["vector"], "milliseconds": result["milliseconds"]})
        except Exception as error:
            send({"type": "error", "id": request["id"], "message": str(error)[:500]})


if __name__ == "__main__":
    raise SystemExit(main())
