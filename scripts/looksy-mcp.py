#!/usr/bin/env python3
"""Expose a running Looksy editor as a standard newline-delimited stdio MCP server.

No dependencies, model, repository tooling or credentials beyond the app's local
automation session are required. Capabilities remain enforced by the app.
"""

from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import sys
from urllib.error import HTTPError, URLError
from urllib.parse import urlsplit
from urllib.request import HTTPRedirectHandler, ProxyHandler, Request, build_opener


MAX_REQUEST_BYTES = 1024 * 1024
MAX_RESPONSE_BYTES = 16 * 1024 * 1024


class ConnectionFailure(Exception):
    pass


class NoRedirects(HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        return None


def default_session_file() -> Path:
    override = os.environ.get("PHOTO_EDITOR_AUTOMATION_SESSION_FILE")
    if override:
        return Path(override).expanduser()
    app_data = os.environ.get("PHOTO_EDITOR_APP_DATA_DIR")
    if app_data:
        return Path(app_data).expanduser() / "automation/session.json"
    if sys.platform == "darwin":
        root = Path.home() / "Library/Application Support"
    elif sys.platform == "win32":
        root = Path(os.environ.get("LOCALAPPDATA", Path.home() / "AppData/Local"))
    else:
        root = Path(os.environ.get("XDG_DATA_HOME", Path.home() / ".local/share"))
    # Native launchers use the shared PhotoEditor data directory; Qt's default
    # organization/application path is nested. Prefer the launcher's record when
    # present, even if HTTP is disabled: never silently connect to another editor.
    native_session = root / "PhotoEditor/automation/session.json"
    if native_session.exists():
        return native_session
    return root / "PhotoEditor/PhotoEditor/automation/session.json"


def read_session(path: Path) -> tuple[str, str]:
    try:
        with path.open("rb") as stream:
            raw = stream.read(65537)
        if len(raw) > 65536:
            raise ValueError("oversized session")
        session = json.loads(raw)
        endpoint = session["mcpEndpoint"]
        if not endpoint and session.get("mcpTransport") == "disabled":
            raise ConnectionFailure(
                "Looksy is running with external MCP disabled. Save your work, then "
                "relaunch Looksy with --enable-automation or PHOTO_EDITOR_ENABLE_AUTOMATION=1. "
                "Connection capabilities are configured by the app; the adapter cannot grant them."
            )
        token = session["token"]
        url = urlsplit(endpoint)
        valid = (
            url.scheme == "http"
            and url.hostname in ("127.0.0.1", "::1")
            and url.port is not None
            and url.path == "/mcp"
            and not any((url.username, url.password, url.query, url.fragment))
            and isinstance(token, str)
            and bool(token)
            and all(32 < ord(char) < 127 for char in token)
        )
        if not valid:
            raise ValueError("invalid endpoint or token")
        return endpoint, token
    except (OSError, ValueError, KeyError, TypeError, AttributeError) as error:
        raise ConnectionFailure(
            "Open Looksy, enable its local MCP connection, and check --session-file. "
            "The session must contain a loopback HTTP /mcp endpoint and a token."
        ) from error


class DesktopConnection:
    def __init__(self, session_file: Path, timeout: float = 120):
        self.session_file = session_file
        self.timeout = timeout
        self.session = None
        # Never send the local bearer token through environment proxies or redirects.
        self.opener = build_opener(ProxyHandler({}), NoRedirects())
        self.protocol_version = "2025-06-18"

    def forward(self, message: dict) -> dict | None:
        if self.session is None or message.get("method") == "initialize":
            self.session = read_session(self.session_file)
        endpoint, token = self.session
        request = Request(endpoint, json.dumps(message, ensure_ascii=False).encode("utf-8"), {
            "Authorization": "Bearer " + token,
            "Content-Type": "application/json",
            "Accept": "application/json, text/event-stream",
            "MCP-Protocol-Version": self.protocol_version,
        }, method="POST")
        try:
            with self.opener.open(request, timeout=self.timeout) as response:
                body = response.read(MAX_RESPONSE_BYTES + 1)
                if "id" not in message:
                    return None
                if len(body) > MAX_RESPONSE_BYTES:
                    raise ConnectionFailure("Looksy response exceeded the adapter's output limit. Inspect current state before continuing.")
                result = json.loads(body)
                if not isinstance(result, dict) or result.get("jsonrpc") != "2.0" or result.get("id") != message["id"]:
                    raise ValueError("invalid response")
                if message.get("method") == "initialize" and isinstance(result.get("result"), dict):
                    self.protocol_version = result["result"].get("protocolVersion", self.protocol_version)
                return result
        except HTTPError as error:
            raise ConnectionFailure(
                f"Looksy returned HTTP {error.code}. Check app connection and capabilities. "
                "An uncertain mutation must not be replayed; inspect current state first."
            ) from error
        except (URLError, OSError, ValueError) as error:
            raise ConnectionFailure(
                "The Looksy connection failed or returned an invalid response. Reconnect after an app restart. "
                "An uncertain mutation must not be replayed; inspect current state first."
            ) from error


def rpc_error(request_id, code: int, message: str) -> dict:
    return {"jsonrpc": "2.0", "id": request_id, "error": {"code": code, "message": message}}


def serve(connection: DesktopConnection, source, sink, diagnostics) -> None:
    while True:
        line = source.readline(MAX_REQUEST_BYTES + 1)
        if not line:
            return
        if len(line) > MAX_REQUEST_BYTES:
            # End this stream rather than treating the rest of an oversized line as requests.
            result = rpc_error(None, -32600, "MCP request exceeds 1 MiB; reconnect with a smaller request.")
            sink.write(json.dumps(result) + "\n")
            sink.flush()
            return
        try:
            message = json.loads(line)
        except (ValueError, UnicodeError):
            result = rpc_error(None, -32700, "Invalid JSON.")
        else:
            if (not isinstance(message, dict) or message.get("jsonrpc") != "2.0"
                    or not isinstance(message.get("method"), str)
                    or ("id" in message and (isinstance(message["id"], (bool, list, dict)) or message["id"] is None))
                    or ("params" in message and not isinstance(message["params"], dict))):
                result = rpc_error(None, -32600, "Expected a JSON-RPC 2.0 method with optional object params.")
            else:
                try:
                    result = connection.forward(message)
                except ConnectionFailure as error:
                    if "id" not in message:
                        print(str(error), file=diagnostics, flush=True)
                        result = None
                    else:
                        result = rpc_error(message["id"], -32000, str(error))
        if result is not None:
            sink.write(json.dumps(result, ensure_ascii=False) + "\n")
            sink.flush()


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--session-file", type=Path, default=default_session_file())
    parser.add_argument("--timeout", type=float, default=120, help="HTTP request deadline in seconds.")
    args = parser.parse_args()
    if not 0 < args.timeout <= 3600:
        parser.error("--timeout must be greater than zero and at most 3600 seconds")
    serve(DesktopConnection(args.session_file.expanduser(), args.timeout), sys.stdin.buffer, sys.stdout, sys.stderr)


if __name__ == "__main__":
    main()
