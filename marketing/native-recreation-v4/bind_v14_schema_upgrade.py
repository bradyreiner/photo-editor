"""Bind the corrected v14 landscape suffix to a fresh reviewed native copy.

This offline binder validates the v12 rollback and v14 schema rejection against
their immutable runner receipts plus the exact native task journal. It does not
call MCP, rewrite historical plans, or synthesize native completion receipts.
"""

from pathlib import Path
import argparse
import json
import sys

import bind_reviewed_continuation as binder


HERE = Path(__file__).resolve().parent
EVIDENCE = HERE / "evidence"
PROJECTS = HERE / "projects"


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    defaults = {
        "current_plan": HERE / "full-landscape-v14-crop-continuation.commands.json",
        "current_receipt": EVIDENCE / "native-full-landscape-v14-receipt.json",
        "current_event_log": EVIDENCE / "native-full-landscape-v14-receipt.json.ndjson",
        "prior_plan": HERE / "full-landscape-v12-reviewed-suffix.commands.json",
        "prior_receipt": EVIDENCE / "native-full-landscape-v12-receipt.json",
        "prior_event_log": EVIDENCE / "native-full-landscape-v12-receipt.json.ndjson",
        "blocked_task": EVIDENCE / "native-v14-schema-blocked-task-current-result.json",
        "corrected_plan": HERE / "full-landscape-v15-radius-corrected-parent.commands.json",
        "checkpoint_document": EVIDENCE / "native-v14-checkpoint-5-reviewed-document-result.json",
        "stop_review": EVIDENCE / "native-v14-checkpoint-5-review-result.json",
        "final_review": EVIDENCE / "native-v14-checkpoint-5-reviewed-task-result.json",
        "release_task": EVIDENCE / "native-v14-checkpoint-5-release-result.json",
        "ready_document": EVIDENCE / "native-v14-checkpoint-5-ready-document-result.json",
        "save_result": EVIDENCE / "native-v14-checkpoint-5-save-result.json",
        "native_output": PROJECTS / "looksy-native-landscape-in-progress-v15.json",
        "plan_output": HERE / "full-landscape-v15-reviewed-suffix.commands.json",
    }
    for name, default in defaults.items():
        parser.add_argument("--" + name.replace("_", "-"), type=Path, default=default)
    for name in ("import_receipt", "admission_receipt", "imported_document"):
        parser.add_argument("--" + name.replace("_", "-"), type=Path, required=True)
    args = parser.parse_args(argv)
    try:
        plan = binder.bind_v14_schema_upgrade_suffix(
            args.current_plan, args.current_receipt, args.current_event_log,
            args.prior_plan, args.prior_receipt, args.prior_event_log,
            args.blocked_task, args.corrected_plan, args.checkpoint_document,
            args.stop_review, args.final_review, args.release_task,
            args.ready_document, args.save_result, args.import_receipt,
            args.admission_receipt, args.imported_document, args.native_output)
        inputs = tuple(Path(value) for value in vars(args).values()
                       if isinstance(value, Path) and value != args.plan_output)
        output = binder.write_new_plan(plan, args.plan_output, inputs)
    except (binder.BinderError, binder.retarget.RetargetError,
            binder.continuation.ContinuationError, OSError) as error:
        print(f"v14 schema-upgrade continuation refused: {error}", file=sys.stderr)
        return 2
    provenance = plan["continuationProvenance"]
    print(json.dumps({
        "ok": True,
        "output": str(output),
        "remainingSteps": len(plan["steps"]),
        "completedSuffixPrefix": provenance["completedPrefixCount"],
        "rejectedStepIds": provenance["rejectedStepIds"],
        "resourceId": plan["resourceId"],
        "taskId": plan["taskId"],
        "startingDocumentHash": plan["startingDocumentHash"],
        "outputPath": plan["outputPath"],
        "proofStatus": plan["proofStatus"],
    }, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
