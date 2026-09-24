#!/usr/bin/env python3
"""Validate the static release site without browser or network dependencies."""

from __future__ import annotations

import argparse
import json
from html.parser import HTMLParser
from pathlib import Path
from urllib.parse import urlparse

from release_commerce import validate_commerce


class References(HTMLParser):
    def __init__(self) -> None:
        super().__init__()
        self.ids: set[str] = set()
        self.references: list[str] = []

    def handle_starttag(self, _tag: str, attrs: list[tuple[str, str | None]]) -> None:
        values = dict(attrs)
        if values.get("id"):
            self.ids.add(values["id"] or "")
        for key in ("href", "src"):
            value = values.get(key)
            if value:
                self.references.append(value)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("site", type=Path)
    parser.add_argument("--production", action="store_true", help="Require complete commercial settings")
    args = parser.parse_args()
    errors: list[str] = []
    required_pages = ("index.html", "privacy.html", "terms.html", "refunds.html", "support.html", "licenses.html")
    required_ids = {
        "mac-arm-download",
        "mac-intel-download",
        "windows-download",
        "support-payment",
        "release-status",
        "purchase-button",
        "purchase-recovery",
    }

    for page_name in required_pages:
        page = args.site / page_name
        if not page.is_file():
            errors.append(f"missing page: {page_name}")
            continue
        text = page.read_text(encoding="utf-8")
        if "PhotoEditor" in text or "Private alpha" in text:
            errors.append(f"stale pre-production branding in {page_name}")
        if "data-legal-publisher" not in text:
            errors.append(f"{page_name} does not expose the configured legal publisher")
        parser_state = References()
        parser_state.feed(text)
        if page_name == "index.html":
            for required_id in sorted(required_ids - parser_state.ids):
                errors.append(f"index.html is missing #{required_id}")
        for reference in parser_state.references:
            parsed = urlparse(reference)
            if parsed.scheme or parsed.netloc or reference.startswith(("#", "mailto:")):
                continue
            local_path = (page.parent / parsed.path).resolve()
            if parsed.path and not local_path.exists():
                errors.append(f"broken local reference in {page_name}: {reference}")

    configuration = (args.site / "download-config.js").read_text(encoding="utf-8")
    if "window.LooksyDownloads" not in configuration:
        errors.append("download-config.js does not define window.LooksyDownloads")
    if '"legalPublisher"' not in configuration:
        errors.append("download-config.js does not define legalPublisher")
    try:
        config = json.loads(configuration.split("=", 1)[1].strip().removesuffix(";"))
        validate_commerce(config, require_ready=args.production)
        if args.production and not config.get("legalPublisher"):
            errors.append("production site needs a legal publisher")
        if args.production and not config.get("supportEmail"):
            errors.append("production site needs a support email")
    except (ValueError, IndexError, TypeError) as error:
        errors.append(f"invalid download configuration: {error}")
    if not (args.site / "legal-page.js").is_file():
        errors.append("legal-page.js is missing")

    terms = (args.site / "terms.html").read_text(encoding="utf-8")
    for invalid_claim in (
        "distributed by Looksy",
        "Looksy retains its rights",
        "Looksy is not liable",
    ):
        if invalid_claim in terms:
            errors.append(f"terms.html treats the product name as the legal entity: {invalid_claim}")

    if errors:
        for error in errors:
            print(f"site: error: {error}")
        return 1
    print("site: static release surface passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
