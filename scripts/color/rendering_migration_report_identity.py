#!/usr/bin/env python3
"""Exact validation for Qt-produced rendering-migration batch reports."""

from __future__ import annotations

import hashlib
import json
import re
from pathlib import Path
from typing import Any


REPORT_SCHEMA = "photo-editor-rendering-migration-batch-report:v2"
HISTORICAL_REPORT_SCHEMA = "photo-editor-rendering-migration-batch-report:v1"
PRODUCER_CONTRACT = "photo-editor-rendering-migration-batch-producer:v1"
SHA256_PATTERN = re.compile(r"^[0-9a-f]{64}$")


def file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def validate_report_identity(
    path: Path,
    executable: Path | None = None,
    *,
    allow_historical_v1: bool = False,
) -> tuple[dict[str, Any], dict[str, Any]]:
    """Return the report and producer only after exact Qt payload validation."""
    raw = path.read_bytes()
    try:
        report = json.loads(raw)
    except json.JSONDecodeError as exc:
        raise ValueError(f"{path}: invalid JSON: {exc}") from exc
    accepted_schemas = (
        {REPORT_SCHEMA, HISTORICAL_REPORT_SCHEMA}
        if allow_historical_v1
        else {REPORT_SCHEMA}
    )
    if not isinstance(report, dict) or report.get("schema") not in accepted_schemas:
        raise ValueError("report does not use an accepted rendering-migration schema")
    claimed = report.get("reportSha256")
    if not isinstance(claimed, str) or SHA256_PATTERN.fullmatch(claimed) is None:
        raise ValueError("rendering-migration report SHA-256 is invalid")
    marker = b',"reportSha256":"' + claimed.encode("ascii") + b'"'
    if raw.count(marker) != 1:
        raise ValueError("rendering-migration report is not the canonical Qt document")
    canonical = raw.replace(marker, b"", 1)
    if hashlib.sha256(canonical).hexdigest() != claimed:
        raise ValueError("rendering-migration report identity is invalid")
    producer = report.get("producer")
    if report.get("schema") == HISTORICAL_REPORT_SCHEMA:
        if executable is not None:
            raise ValueError("historical v1 report cannot prove an executable identity")
        return report, {}
    if (
        not isinstance(producer, dict)
        or producer.get("contract") != PRODUCER_CONTRACT
        or not isinstance(producer.get("executableName"), str)
        or not producer["executableName"]
        or not isinstance(producer.get("buildMode"), str)
        or not producer["buildMode"]
        or not isinstance(producer.get("executableSha256"), str)
        or SHA256_PATTERN.fullmatch(producer["executableSha256"]) is None
    ):
        raise ValueError("rendering-migration report producer is invalid")
    if executable is not None:
        if (
            not executable.is_file()
            or file_sha256(executable) != producer["executableSha256"]
        ):
            raise ValueError("rendering-migration report does not belong to the executable")
    return report, producer
