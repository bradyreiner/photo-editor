#!/usr/bin/env python3
"""Offline validation of the P20 timeline resource-ID correction and scope."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path


HERE = Path(__file__).resolve().parent
POLISH = HERE.parent
ROOT = POLISH.parents[2]
DEFAULT_ORIGINAL = HERE / "p20-carousel-native-plan.json"
DEFAULT_PLAN = HERE / "p20-carousel-native-plan.resource-corrected.json"
DEFAULT_VALIDATION = HERE / "p20-native-plan-validation.resource-corrected.json"
DEFAULT_P22 = POLISH / "p22-photo-proof/p22-photo-proof.fragment.json"
CANONICAL_RESOURCE = "timeline:3db4e4fb-6737-415c-b6ee-dde766e7c237"
AUDITED_TRUNCATED_RESOURCE = "timeline:3db4e4fb-6737-415c-b6ee-dd766e7c237"


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def read_json(path: Path):
    require(path.is_file(), f"Missing evidence file: {path}")
    return json.loads(path.read_text())


def sha256(path: Path) -> str:
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def direct_target_ids(plan: dict) -> set[str]:
    targets: set[str] = set()
    for step in plan.get("steps", []):
        values = step.get("values", {})
        clip_id = values.get("clipId")
        if isinstance(clip_id, str):
            targets.add(clip_id)
    return targets


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--original", type=Path, default=DEFAULT_ORIGINAL)
    parser.add_argument("--plan", type=Path, default=DEFAULT_PLAN)
    parser.add_argument("--validation", type=Path, default=DEFAULT_VALIDATION)
    parser.add_argument("--p22", type=Path, default=DEFAULT_P22)
    parser.add_argument("--output", type=Path,
                        default=HERE / "p20-resource-correction-offline-proof.json")
    args = parser.parse_args()

    original = read_json(args.original)
    plan = read_json(args.plan)
    validation = read_json(args.validation)
    p22 = read_json(args.p22)
    snapshot_ref = plan["planningSnapshot"]
    snapshot_path = Path(snapshot_ref["path"])
    snapshot = read_json(snapshot_path)

    require(original["resourceId"] == AUDITED_TRUNCATED_RESOURCE,
            "The preserved original plan no longer contains the exact audited truncated ID.")
    require(plan["resourceId"] == CANONICAL_RESOURCE,
            "Corrected plan does not have the canonical timeline resource ID.")
    require(snapshot.get("timelineId") == CANONICAL_RESOURCE.removeprefix("timeline:"),
            "Planning snapshot intrinsic timelineId disagrees with the canonical resource.")
    require(snapshot_ref.get("sha256") == sha256(snapshot_path),
            "Planning snapshot raw SHA-256 does not match the corrected plan reference.")
    require(plan["planningSnapshot"].get("documentHash") ==
            "d6a5023c817f0fe79109e7a6647b87032e5ed8a48de394697ac4f6a11be6c422",
            "P20 planning snapshot document hash unexpectedly changed.")

    corrected_original = json.loads(json.dumps(original))
    corrected_original["resourceId"] = CANONICAL_RESOURCE
    require(plan == corrected_original,
            "The corrected plan contains changes beyond the single resourceId correction.")
    require(len(plan["steps"]) == 225, "P20 plan step count changed from the audited 225.")
    require(plan.get("status") == "offline-proposed-unbound" and
            plan.get("requiresFreshBinding") is True,
            "Corrected source fragment must remain unbound and require fresh native binding.")
    require(not plan.get("taskId") and not plan.get("startingDocumentHash"),
            "The offline fragment unexpectedly contains a live task/hash binding.")
    require(validation.get("resourceId") == CANONICAL_RESOURCE or
            validation.get("resourceCorrection", {}).get("correctedResourceId") == CANONICAL_RESOURCE,
            "Corrected validation does not attest the canonical resource ID.")
    require(validation.get("planPath") == str(args.plan) or
            validation.get("correctedPlanPath") == str(args.plan) or
            validation.get("planSha256") == sha256(args.plan),
            "Corrected validation does not identify the corrected plan.")
    require(validation.get("planSha256") == sha256(args.plan),
            "Corrected validation plan hash does not match the corrected plan bytes.")
    require(validation.get("planningSnapshotSha256") == sha256(snapshot_path),
            "Corrected validation snapshot hash does not match the planning snapshot.")

    p20_existing = set(plan["placementGuards"]["existingTargetClipIds"])
    p20_tracks = set(plan["placementGuards"]["destinationTrackIds"])
    p22_existing = {row["clipId"] for row in
                    p22.get("executionPreconditions", {}).get("targetPreconditions", [])}
    p22_created_tracks: set[str] = set()
    p22_steps = p22.get("steps", [])
    p22_touched_tracks: set[str] = set()
    p22_created_ids: set[str] = set()
    for step in p22_steps:
        values = step.get("values", {})
        if isinstance(values.get("clipId"), str):
            p22_existing.add(values["clipId"])
        if isinstance(values.get("trackId"), str):
            p22_touched_tracks.add(values["trackId"])
        if step.get("commandId", "").endswith("createTimelineTrack"):
            p22_created_tracks.add(step["id"])
    for row in p22.get("executionPreconditions", {}).get("targetPreconditions", []):
        if isinstance(row.get("trackId"), str):
            p22_touched_tracks.add(row["trackId"])
    for row in p22.get("executionPreconditions", {}).get("trackAvailability", []):
        if isinstance(row.get("trackId"), str):
            p22_touched_tracks.add(row["trackId"])
    require(not p20_existing.intersection(p22_existing),
            "P20 and P22 unexpectedly target the same existing clip IDs.")
    require(not p20_tracks.intersection(p22_touched_tracks | p22_created_tracks),
            "P20 and P22 unexpectedly share a destination/created track.")
    p20_range = plan["placementGuards"]["halfOpenInterval"]
    p22_range = [16.0, 39.0]
    require(p20_range[1] <= p22_range[0] or p22_range[1] <= p20_range[0],
            "The audited P20/P22 sequence intervals are no longer disjoint.")

    changed_fields = sorted(key for key in original.keys() | plan.keys()
                            if original.get(key) != plan.get(key))
    require(changed_fields == ["resourceId"],
            f"Expected only resourceId to differ, got {changed_fields}.")
    output = {
        "schema": "looksy.p20-resource-correction-offline-proof/v1",
        "status": "passed-offline-only",
        "originalPlanPath": str(args.original.resolve()),
        "originalPlanSha256": sha256(args.original),
        "correctedPlanPath": str(args.plan.resolve()),
        "correctedPlanSha256": sha256(args.plan),
        "validationPath": str(args.validation.resolve()),
        "validationSha256": sha256(args.validation),
        "planningSnapshotPath": str(snapshot_path.resolve()),
        "planningSnapshotSha256": sha256(snapshot_path),
        "timelineId": snapshot["timelineId"],
        "canonicalResourceId": CANONICAL_RESOURCE,
        "changedPlanFields": changed_fields,
        "stepCount": len(plan["steps"]),
        "p20ExistingTargets": len(p20_existing),
        "p20DestinationTracks": len(p20_tracks),
        "p22ExistingTargets": len(p22_existing),
        "p22TouchedTracks": len(p22_touched_tracks),
        "targetIdsDisjoint": True,
        "destinationTracksDisjoint": True,
        "sequenceIntervalsDisjoint": True,
        "noWorkerCallsOrMutations": True,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("x") as stream:
        json.dump(output, stream, indent=2)
        stream.write("\n")
    print(json.dumps(output, indent=2))


if __name__ == "__main__":
    main()
