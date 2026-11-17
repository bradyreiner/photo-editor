#!/usr/bin/env python3
"""Exercise the distributable stdio adapter against a local mock MCP editor."""

import contextlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import importlib.util
import io
import json
from pathlib import Path
import tempfile
import threading
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location("looksy_mcp", Path(__file__).with_name("looksy-mcp.py"))
mcp = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mcp)


@contextlib.contextmanager
def editor(mode="normal"):
    requests = []

    class Handler(BaseHTTPRequestHandler):
        def log_message(self, *args):
            pass

        def do_POST(self):
            message = json.loads(self.rfile.read(int(self.headers["Content-Length"])))
            requests.append((dict(self.headers), message))
            if mode == "redirect":
                self.send_response(307)
                self.send_header("Location", "http://127.0.0.1:1/secret-destination")
                self.end_headers()
                return
            if mode == "denied":
                self.send_response(401)
                self.end_headers()
                self.wfile.write(b"sensitive-token-from-error-body")
                return
            if "id" not in message:
                self.send_response(202)
                self.send_header("Content-Length", "0")
                self.end_headers()
                return
            result = {"protocolVersion": "2025-11-25"} if message["method"] == "initialize" else {"tools": []}
            response = {"jsonrpc": "2.0", "id": message["id"] if mode != "wrong-id" else "wrong", "result": result}
            body = json.dumps(response).encode()
            self.send_response(200)
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

    with ThreadingHTTPServer(("127.0.0.1", 0), Handler) as server:
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        try:
            yield server.server_address[1], requests
        finally:
            server.shutdown()
            thread.join()


class AdapterTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.session_file = Path(self.temporary.name) / "session.json"

    def session(self, port, **overrides):
        session = {"mcpEndpoint": f"http://127.0.0.1:{port}/mcp", "token": "local-test-token"}
        session.update(overrides)
        self.session_file.write_text(json.dumps(session))
        return mcp.DesktopConnection(self.session_file, 2)

    def serve(self, connection, messages):
        source = io.BytesIO(b"\n".join(json.dumps(message).encode() for message in messages) + b"\n")
        sink, diagnostics = io.StringIO(), io.StringIO()
        mcp.serve(connection, source, sink, diagnostics)
        return [json.loads(line) for line in sink.getvalue().splitlines()], diagnostics.getvalue()

    def test_session_discovery_respects_explicit_paths_and_native_launcher(self):
        home = Path(self.temporary.name)
        native = home / "Library/Application Support/PhotoEditor/automation/session.json"
        legacy = home / "Library/Application Support/PhotoEditor/PhotoEditor/automation/session.json"
        with patch.dict(mcp.os.environ, {}, clear=True), patch.object(mcp.sys, "platform", "darwin"), patch.object(mcp.Path, "home", return_value=home):
            self.assertEqual(mcp.default_session_file(), legacy)
            native.parent.mkdir(parents=True)
            native.write_text('{"mcpTransport":"disabled"}')
            self.assertEqual(mcp.default_session_file(), native)
            with patch.dict(mcp.os.environ, {"PHOTO_EDITOR_APP_DATA_DIR": str(home / "custom")}):
                self.assertEqual(mcp.default_session_file(), home / "custom/automation/session.json")
                with patch.dict(mcp.os.environ, {"PHOTO_EDITOR_AUTOMATION_SESSION_FILE": str(self.session_file)}):
                    self.assertEqual(mcp.default_session_file(), self.session_file)

    def test_disabled_mcp_reports_the_actual_launch_requirement_without_token(self):
        self.session_file.write_text(json.dumps({"mcpEndpoint": "", "mcpTransport": "disabled", "token": "private-session-token"}))
        output, _ = self.serve(mcp.DesktopConnection(self.session_file), [
            {"jsonrpc": "2.0", "id": 1, "method": "initialize"},
        ])
        self.assertIn("--enable-automation", output[0]["error"]["message"])
        self.assertNotIn("private-session-token", json.dumps(output))

    def test_standard_handshake_notifications_and_tools_over_stdio(self):
        with editor() as (port, requests):
            output, diagnostics = self.serve(self.session(port), [
                {"jsonrpc": "2.0", "id": 1, "method": "initialize", "params": {
                    "protocolVersion": "2025-11-25", "capabilities": {}, "clientInfo": {"name": "test", "version": "1"}}},
                {"jsonrpc": "2.0", "method": "notifications/initialized"},
                {"jsonrpc": "2.0", "id": 2, "method": "tools/list"},
            ])
        self.assertEqual([result["id"] for result in output], [1, 2])
        self.assertEqual(output[0]["result"]["protocolVersion"], "2025-11-25")
        self.assertEqual(diagnostics, "")
        self.assertEqual(len(requests), 3)
        self.assertEqual(requests[0][0]["Authorization"], "Bearer local-test-token")
        self.assertEqual(requests[-1][0]["Mcp-Protocol-Version"], "2025-11-25")
        self.assertNotIn("local-test-token", json.dumps(output))

    def test_reject_nonlocal_or_credential_redirecting_endpoints(self):
        for endpoint in ["https://127.0.0.1:123/mcp", "http://example.com:123/mcp", "http://localhost:123/mcp",
                         "http://user@127.0.0.1:123/mcp", "http://127.0.0.1:123/other", "http://127.0.0.1:123/mcp?token=x",
                         "http://127.0.0.1:123/mcp#fragment", "http://127.0.0.1/mcp"]:
            with self.subTest(endpoint=endpoint):
                self.session(123, mcpEndpoint=endpoint)
                with self.assertRaises(mcp.ConnectionFailure):
                    mcp.read_session(self.session_file)
        self.session(123, token="bad\r\nAuthorization: another")
        with self.assertRaises(mcp.ConnectionFailure):
            mcp.read_session(self.session_file)

    def test_transport_errors_are_redacted_and_never_replayed(self):
        for mode in ("denied", "redirect", "wrong-id"):
            with editor(mode) as (port, requests):
                output, _ = self.serve(self.session(port), [{"jsonrpc": "2.0", "id": 5, "method": "tools/call"}])
                self.assertEqual(len(requests), 1)
            self.assertEqual(output[0]["error"]["code"], -32000)
            self.assertIn("inspect current state first", output[0]["error"]["message"])
            self.assertNotIn("sensitive-token", json.dumps(output))
            self.assertNotIn("local-test-token", json.dumps(output))

    def test_missing_session_and_notification_errors_keep_stdout_clean(self):
        output, diagnostics = self.serve(mcp.DesktopConnection(self.session_file), [
            {"jsonrpc": "2.0", "method": "notifications/initialized"},
            {"jsonrpc": "2.0", "id": "request", "method": "initialize"},
        ])
        self.assertEqual(len(output), 1)
        self.assertEqual(output[0]["id"], "request")
        self.assertIn("Open Looksy", output[0]["error"]["message"])
        self.assertIn("Open Looksy", diagnostics)

    def test_invalid_requests_and_input_limit_do_not_reach_editor(self):
        with editor() as (port, requests):
            connection = self.session(port)
            output, _ = self.serve(connection, [[], {"jsonrpc": "2.0", "id": 1, "method": "tools/list", "params": []}])
            self.assertEqual([item["error"]["code"] for item in output], [-32600, -32600])
            sink = io.StringIO()
            mcp.serve(connection, io.BytesIO(b"x" * (mcp.MAX_REQUEST_BYTES + 1)), sink, io.StringIO())
            self.assertEqual(json.loads(sink.getvalue())["error"]["code"], -32600)
            self.assertEqual(requests, [])

    def test_connection_pins_session_until_new_handshake(self):
        with editor() as (first, first_requests), editor() as (second, second_requests):
            connection = self.session(first)
            connection.forward({"jsonrpc": "2.0", "id": 1, "method": "ping"})
            self.session(second)
            connection.forward({"jsonrpc": "2.0", "id": 2, "method": "ping"})
            connection.forward({"jsonrpc": "2.0", "id": 3, "method": "initialize"})
            self.assertEqual(len(first_requests), 2)
            self.assertEqual(len(second_requests), 1)


if __name__ == "__main__":
    unittest.main()
