"""Bind the corrected portrait suffix after its exact unjournaled scope rejection.

The tool is offline. It requires proof of the 1,637-action prefix, the exact
pre-dispatch path guard, a settled source save/review/release, and a fresh
native import/admission before it writes a new plan.
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
        "parent_plan": HERE / "full-portrait-v8-crop-corrected.commands.json",
        "receipt": EVIDENCE / "native-full-portrait-v8-receipt.json",
        "event_log": EVIDENCE / "native-full-portrait-v8-receipt.json.ndjson",
        "review_task": EVIDENCE / "portrait-v8-upgrade-review-result.json",
        "corrected_plan": HERE / "full-portrait-v9-schema-corrected-parent.commands.json",
        "final_review": EVIDENCE / "portrait-v8-checkpoint-1-review-result.json",
        "release_task": EVIDENCE / "portrait-v8-checkpoint-1-release-result.json",
        "checkpoint_document": EVIDENCE / "portrait-v8-checkpoint-1-document-result.json",
        "ready_document": EVIDENCE / "portrait-v8-checkpoint-1-ready-document-2-result.json",
        "save_result": EVIDENCE / "portrait-v8-checkpoint-1-save-result.json",
        "import_receipt": EVIDENCE / "portrait-v9-copy-result.json",
        "admission_receipt": EVIDENCE / "portrait-v9-admit-result.json",
        "imported_document": EVIDENCE / "portrait-v9-document-result.json",
        "native_output": PROJECTS / "looksy-native-full-portrait-v9-in-progress.json",
        "plan_output": HERE / "full-portrait-v9-reviewed-suffix.commands.json",
    }
    for name, default in defaults.items():
        parser.add_argument("--" + name.replace("_", "-"), type=Path, default=default)
    args = parser.parse_args(argv)
    try:
        plan = binder.bind_portrait_v8_scope_recovery_suffix(
            args.parent_plan, args.receipt, args.event_log, args.review_task,
            args.corrected_plan, args.final_review, args.release_task,
            args.checkpoint_document, args.ready_document, args.save_result,
            args.import_receipt, args.admission_receipt, args.imported_document,
            args.native_output)
        inputs = tuple(value for value in vars(args).values()
                       if isinstance(value, Path) and value != args.plan_output)
        output = binder.write_new_plan(plan, args.plan_output, input_paths=inputs)
    except (binder.BinderError, binder.retarget.RetargetError,
            binder.continuation.ContinuationError, OSError) as error:
        print(f"portrait scope-recovery continuation refused: {error}", file=sys.stderr)
        return 2
    provenance = plan["continuationProvenance"]
    print(json.dumps({
        "ok": True,
        "output": str(output),
        "remainingSteps": len(plan["steps"]),
        "completedPrefixCount": provenance["completedPrefixCount"],
        "firstRemainingStepId": plan["steps"][0]["id"],
        "resourceId": plan["resourceId"],
        "taskId": plan["taskId"],
        "startingDocumentHash": plan["startingDocumentHash"],
        "outputPath": plan["outputPath"],
        "proofStatus": plan["proofStatus"],
    }, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
