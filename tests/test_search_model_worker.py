"""Stdio framing/schema tests with a fake encoder; no model or dependency install."""
import contextlib
import importlib.util
import io
import json
from pathlib import Path
import sys
from types import SimpleNamespace
import unittest
from unittest import mock

SPEC = importlib.util.spec_from_file_location(
    "search_model_worker", Path(__file__).resolve().parents[1] / "scripts/search_model_worker.py")
WORKER = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(WORKER)


class FakeEncoder:
    space_key = "a" * 64
    contract = {"model": {"revision": "fixture"}}

    def __init__(self, *_args):
        pass

    def text(self, query):
        if query == "error":
            raise ValueError("expected failure")
        return {"contentKey": "b" * 64, "vector": [1.0] + [0.0] * 767, "milliseconds": 1}

    def image(self, path):
        return self.text(str(path))


class WorkerTest(unittest.TestCase):
    def run_worker(self, data):
        output = io.StringIO()
        with mock.patch.object(WORKER, "Encoder", FakeEncoder), \
             mock.patch.object(sys, "argv", ["worker", "--model-root", "/unused-fixture", "--device", "cpu"]), \
             mock.patch.object(sys, "stdin", SimpleNamespace(buffer=io.BytesIO(data))), \
             contextlib.redirect_stdout(output):
            status = WORKER.main()
        return status, [json.loads(line) for line in output.getvalue().splitlines()]

    def test_serial_errors_do_not_replay_or_poison_following_requests(self):
        requests = [{"id": "1", "kind": "text", "text": "ok"},
                    {"id": "2", "kind": "text", "text": "error"},
                    {"id": "3", "kind": "image", "path": "/fixture.jpg"}]
        status, records = self.run_worker(b"".join(json.dumps(r).encode() + b"\n" for r in requests))
        self.assertEqual(status, 0)
        self.assertEqual([r["type"] for r in records], ["ready", "embedding", "error", "embedding"])
        self.assertEqual([r["id"] for r in records[1:]], ["1", "2", "3"])

    def test_bad_frames_fail_before_inference(self):
        for value in [b"{}\n", b"not-json\n", b"x" * 65537,
                      b'{"id":"x","kind":"text","text":"ok"}',
                      b'{"id":"x","kind":"text","text":"ok","command":"execute"}\n',
                      b'{"id":1,"kind":"text","text":"ok"}\n',
                      b'{"id":"x","kind":"image","path":12}\n']:
            with self.subTest(value=value[:50]):
                status, records = self.run_worker(value)
                self.assertEqual(status, 1)
                self.assertEqual([r["type"] for r in records], ["ready", "fatal"])


if __name__ == "__main__":
    unittest.main()
