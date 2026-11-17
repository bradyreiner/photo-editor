#!/usr/bin/env python3
"""Serve one local MLX checkpoint/adapter through the app's Ollama chat subset.

This is a loopback-only experimental inference bridge, not an Ollama daemon.
It exposes no file operations or editing authority. Requests run serially on one
GPU model and images must arrive as bounded base64 payloads, never local paths.
"""
from __future__ import annotations

import argparse
import base64
from datetime import datetime, timezone
from http.server import BaseHTTPRequestHandler, HTTPServer
import io
import json
from pathlib import Path
import time

from gemma_editing_experiment import load_runtime


def details():
    return {"family": "gemma4", "parameter_size": "26B-A4B", "quantization_level": "MLX-affine-4bit"}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--model", type=Path, required=True)
    parser.add_argument("--adapter", type=Path)
    parser.add_argument("--name", default="looksy-gemma4-26b-editing")
    parser.add_argument("--port", type=int, default=11435)
    parser.add_argument("--memory-gib", type=float, default=34)
    args = parser.parse_args()
    if not 1024 <= args.port <= 65535 or not 0 < args.memory_gib <= 36:
        parser.error("Use an unprivileged port and at most 36 GiB MLX memory")

    import mlx.core as mx
    from PIL import Image
    from mlx_vlm import apply_chat_template, stream_generate
    from mlx_vlm.structured import build_json_schema_logits_processor

    class Handler(BaseHTTPRequestHandler):
        def log_message(self, format, *values):
            # Do not log prompts, image bytes, source paths or HTTP bodies.
            pass

        def reply(self, code, payload):
            body = json.dumps(payload, ensure_ascii=False).encode()
            self.send_response(code)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def allowed(self):
            if self.headers.get("Origin", "") not in {"", "http://127.0.0.1"}:
                self.reply(403, {"error": "Only the desktop's exact local Origin is admitted."})
                return False
            return True

        def do_GET(self):
            if not self.allowed():
                return
            if self.path == "/api/tags":
                self.reply(200, {"models": [{"name": args.name, "model": args.name, "details": details()}]})
            elif self.path == "/api/version":
                self.reply(200, {"version": "looksy-mlx-bridge-1", "runtime": "mlx-vlm"})
            else:
                self.reply(404, {"error": "Unknown inference endpoint."})

        def do_POST(self):
            if not self.allowed():
                return
            length = self.headers.get("Content-Length", "")
            if not length.isdecimal() or not 0 < int(length) <= 16 * 1024 * 1024:
                self.reply(413, {"error": "Provide a bounded JSON request (up to 16 MiB)."})
                return
            try:
                payload = json.loads(self.rfile.read(int(length)))
                if not isinstance(payload, dict) or payload.get("model", args.name) != args.name:
                    raise ValueError("This server exposes only its configured model name.")
                if self.path == "/api/show":
                    self.reply(200, {"details": details(), "capabilities": ["completion", "vision", "tools"]})
                    return
                if self.path != "/api/chat":
                    self.reply(404, {"error": "Only chat and model discovery are implemented."})
                    return
                messages, images = [], []
                for message in payload.get("messages", []):
                    if message.get("role") not in {"system", "user", "assistant"} or not isinstance(message.get("content"), str):
                        raise ValueError("Use text system/user/assistant messages with optional base64 images.")
                    messages.append({"role": message["role"], "content": message["content"]})
                    for encoded in message.get("images", []):
                        if len(images) >= 5 or not isinstance(encoded, str):
                            raise ValueError("At most five base64 images are supported.")
                        data = base64.b64decode(encoded, validate=True)
                        image = Image.open(io.BytesIO(data))
                        if image.format not in {"PNG", "JPEG", "WEBP"} or image.width * image.height > 4_000_000:
                            raise ValueError("Use bounded PNG/JPEG/WebP proof images (up to four megapixels).")
                        images.append(image.convert("RGB"))
                if not messages:
                    raise ValueError("At least one message is required.")
                options = payload.get("options", {})
                max_tokens = int(options.get("num_predict", 1536))
                if not 1 <= max_tokens <= 4096:
                    raise ValueError("The supported output budget is 1-4096 tokens.")
                prompt = apply_chat_template(processor, model.config, messages, num_images=len(images), enable_thinking=False)
                context = int(options.get("num_ctx", 24576))
                if not 1024 <= context <= 24576:
                    raise ValueError("The supported context is 1024-24576 tokens.")
                if len(tokenizer.encode(prompt)) + 1024 * len(images) + max_tokens > context:
                    raise ValueError("Request exceeds this server's conservative context budget.")
                kwargs = {"max_tokens": max_tokens, "temperature": float(options.get("temperature", 0.0))}
                schema = payload.get("format")
                if schema:
                    if schema == "json":
                        schema = {"type": "object"}
                    if not isinstance(schema, dict):
                        raise ValueError("format must be json or an object JSON Schema.")
                    kwargs["logits_processors"] = [build_json_schema_logits_processor(tokenizer, schema)]
            except (ValueError, TypeError, KeyError, AttributeError, OSError) as error:
                self.reply(400, {"error": str(error)})
                return

            streaming = payload.get("stream", False) is True
            started, text, final = time.perf_counter(), "", None
            if streaming:
                self.send_response(200)
                self.send_header("Content-Type", "application/x-ndjson")
                self.send_header("Connection", "close")
                self.end_headers()
            try:
                for chunk in stream_generate(model, processor, prompt=prompt, image=images or None, **kwargs):
                    final = chunk
                    text += chunk.text
                    if streaming:
                        self.wfile.write((json.dumps({"model": args.name, "message": {"role": "assistant", "content": chunk.text},
                                                     "done": False}) + "\n").encode())
                        self.wfile.flush()
                if final is None:
                    raise RuntimeError("Model returned no generation result.")
                result = {"model": args.name, "created_at": datetime.now(timezone.utc).isoformat(),
                          "message": {"role": "assistant", "content": "" if streaming else text},
                          "done": True, "done_reason": "length" if final.generation_tokens >= max_tokens else "stop",
                          "total_duration": int((time.perf_counter() - started) * 1e9), "load_duration": 0,
                          "prompt_eval_count": final.prompt_tokens, "eval_count": final.generation_tokens,
                          "prompt_eval_duration": int(final.prompt_tokens / max(final.prompt_tps, 1e-6) * 1e9),
                          "eval_duration": int(final.generation_tokens / max(final.generation_tps, 1e-6) * 1e9)}
                if streaming:
                    self.wfile.write((json.dumps(result) + "\n").encode())
                    self.wfile.flush()
                else:
                    self.reply(200, result)
            except (BrokenPipeError, ConnectionResetError):
                pass
            except Exception as error:
                print(json.dumps({"event": "inference_failed", "errorType": type(error).__name__}), flush=True)
                if not streaming:
                    self.reply(500, {"error": "Local MLX inference failed; inspect server diagnostics before retrying."})
            finally:
                mx.clear_cache()

    server = HTTPServer(("127.0.0.1", args.port), Handler)
    model, processor = load_runtime(args)
    model.eval()
    tokenizer = getattr(processor, "tokenizer", processor)
    print(json.dumps({"event": "ready", "endpoint": f"http://127.0.0.1:{args.port}", "model": args.name}), flush=True)
    server.serve_forever()


if __name__ == "__main__":
    main()
