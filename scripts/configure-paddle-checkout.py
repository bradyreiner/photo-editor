#!/usr/bin/env python3
"""Write public Paddle.js configuration; never accepts a private Paddle API key."""
from __future__ import annotations

import argparse
import json
import re
from pathlib import Path
from urllib.parse import urlsplit


def checkout_config(environment: str, client_token: str, checkout_origin: str) -> dict[str, str]:
    prefix = {"sandbox": "test", "production": "live"}.get(environment)
    if not prefix or not re.fullmatch(rf"{prefix}_[a-zA-Z0-9]{{27}}", client_token):
        raise ValueError("use a public client-side token matching the selected Paddle environment")
    origin = urlsplit(checkout_origin)
    if (origin.scheme != "https" or not origin.hostname or origin.username or origin.password
            or origin.path not in ("", "/") or origin.query or origin.fragment):
        raise ValueError("checkout origin must be an HTTPS origin without a path, credentials or query")
    # Validate the port before serializing a public configuration.
    port = origin.port
    host = origin.hostname.lower()
    if ":" in host:
        host = f"[{host}]"
    normalized = f"https://{host}" + (f":{port}" if port and port != 443 else "")
    return {"environment": environment, "clientToken": client_token, "checkoutOrigin": normalized}


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--environment", choices=("sandbox", "production"), required=True)
    parser.add_argument("--client-token", required=True, help="public test_/live_ client token, never an API key")
    parser.add_argument("--checkout-origin", required=True)
    parser.add_argument("--output", type=Path, default=Path("site/paddle-config.js"))
    args = parser.parse_args()
    try:
        config = checkout_config(args.environment, args.client_token, args.checkout_origin)
    except ValueError as error:
        parser.error(str(error))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text("// Public Paddle.js settings; no private API credentials.\nwindow.LooksyPaddleCheckout = "
                           + json.dumps(config, indent=2) + ";\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
