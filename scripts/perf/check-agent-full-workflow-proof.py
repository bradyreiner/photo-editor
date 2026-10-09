#!/usr/bin/env python3
"""Check a completed editorial-cover proof from the production desktop app."""

import argparse
import json
from pathlib import Path

from agent_full_workflow_proof_acceptance import validate_proof


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("receipt", type=Path, help="workflow-receipt.json emitted inside the private fixture root")
    args = parser.parse_args()
    receipt = args.receipt.resolve()
    errors = validate_proof(receipt)
    report = {"receiptPath": str(receipt), "passed": not errors, "errors": errors}
    print(json.dumps(report, indent=2))
    return 0 if not errors else 1


if __name__ == "__main__":
    raise SystemExit(main())
