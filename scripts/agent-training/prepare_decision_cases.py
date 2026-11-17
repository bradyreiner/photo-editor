#!/usr/bin/env python3
"""Create public fictional routing fixtures, not successful editing demonstrations."""
import json
from pathlib import Path


TOOLS = ["app.list_commands", "app.command_schema", "photo.inspect_composition",
         "photo.edit_composition", "app.invoke_command", "photo.render_current_proof",
         "photo.validate_edit", "photo.export_batch"]


def cases():
    rows = []

    def add(name, brief, observations, expected, *, tools=None, required=True, category="workflow"):
        rows.append({"id": name, "category": category, "acceptableChoices": expected,
                     "labelStatus": "developer-authored contract fixture; not human creative judgment",
                     "tools": [{"name": name} for name in (TOOLS if tools is None else tools)],
                     "state": {"brief": brief, "phase": "sample", "assetId": "fixture-photo",
                               "toolPlanRequired": required, "observations": observations,
                               "evidenceScope": "Fictional text-state fixture. No rendered pixels are attached."}})

    add("unknown-title-id", "Change the existing headline to FIELD NOTES.",
        {"documentLoaded": True, "knownLayerIds": [], "compositionInspected": False}, ["inspect_state"])
    add("created-title-id", "Set the new headline to warm white #FFF8F0.",
        {"creationReceipt": {"ok": True, "createdLayerId": "title-17"},
         "currentLayer": {"id": "title-17", "text": "FIELD NOTES", "color": "#FFFFFF"}}, ["use_document_tools"])
    add("read-after-edit", "Change the existing headline to FIELD NOTES and verify it.",
        {"editReceipt": {"ok": True, "layerId": "title-17", "changed": True},
         "lastCompositionRead": "before this edit", "currentReadbackAvailable": False}, ["inspect_state"])
    add("verify-visible-result", "Review the completed poster for clipping and readability.",
        {"currentCompositionReadback": "All requested text and typography fields match.",
         "proof": {"available": False}, "pendingEdits": []}, ["review_proof"])
    add("missing-native-schema", "Apply a native outer shadow to the title.",
        {"commandDiscovery": {"commandId": "edit.layer.shadow", "available": True},
         "commandInputSchema": None, "layerId": "title-17"}, ["discover_controls"])
    add("native-command-ready", "Set the title shadow opacity to 0.4.",
        {"taskAdmission": "ready", "layerId": "title-17", "commandId": "edit.layer.shadow",
         "commandInputSchema": {"values": {"opacity": "number 0..1"}},
         "currentOpacity": 0, "available": True}, ["use_document_tools"])
    add("undiscovered-fill", "Create an editable background shape using the native shape controls.",
        {"currentCanvasInspected": True, "nativeShapeCommand": "not discovered",
         "currentLayers": []}, ["discover_controls"])
    add("duplicate-receipt", "Create the headline, then set its color to #FFF8F0.",
        {"lastCreationAttempt": {"replayPrevented": True, "alreadyCompleted": True,
                                   "createdLayerId": "title-17", "mutationNotApplied": True},
         "freshInspectionAfterReceipt": False}, ["inspect_state"])
    add("schema-rejection", "Set title tracking to 4 pixels.",
        {"lastCall": {"tool": "app.invoke_command", "recoverableError": True,
                       "mutationNotApplied": True, "error": "Input schema missing required field units."},
         "commandSchemaAvailable": False, "layerId": "title-17"}, ["discover_controls"])
    add("stale-id", "Move the subtitle below the headline.",
        {"priorSubtitleId": "subtitle-old", "freshInspection": {"layerIds": ["title-17", "subtitle-new"]},
         "subtitleIdentityInCurrentDocument": "unknown", "oldIdStillValid": False}, ["inspect_state"])
    add("crop-parameters-missing", "Set a tighter portrait crop around the subject.",
        {"currentCrop": "unknown", "canvasDimensions": "unknown", "subjectBounds": "unknown"}, ["inspect_state"])
    add("proof-stale", "Check the finished composition before presenting it.",
        {"lastProofRevision": "r4", "currentRevision": "r5", "freshStateReadbackComplete": True,
         "proofAfterLastEdit": False}, ["review_proof"])
    add("export-ready", "Export the approved poster as PNG.",
        {"approvedExport": {"format": "PNG", "destination": "approved local destination"},
         "nativePreflight": "passed", "currentBoundRenderedReview": "passed",
         "documentSaved": True, "exportStarted": False, "taskAdmission": "ready"}, ["deliver_output"])
    add("export-before-review", "Finish the poster and export it once verified.",
        {"requestedEditsMatchCurrentReadback": True, "nativePreflight": "passed",
         "currentRenderedProof": "not generated", "exportStarted": False}, ["review_proof"])
    add("essential-date", "Create a concert poster with the correct venue and date. Ask me for missing facts.",
        {"venue": "provided", "date": None, "dateAvailableInDocumentOrReceipts": False,
         "allOtherUserInputs": "provided"}, ["ask_user"])
    add("permission-absent", "Delete the existing text layers.",
        {"capabilities": ["read"], "nativeAdmission": "denied", "reason": "No mutation capability.",
         "inspectionComplete": True}, ["stop_safely"], tools=["photo.inspect_composition"])
    add("target-closed", "Continue adjusting the title in the approved document.",
        {"approvedTarget": "document-a", "approvedTargetOpen": False, "activeTarget": "document-b",
         "reopenApprovedTarget": "unsupported", "targetPolicy": "cannot substitute targets"}, ["stop_safely"])
    add("unknown-scope", "Make it better.", {}, ["insufficient_evidence", "ask_user"], category="ambiguity")
    add("omitted-repair-context", "Repair the issue found in the last review.",
        {"projectionOmittedFields": True, "lastReviewIssues": "omitted",
         "availableEvidence": "no current review details or pixels"},
        ["insufficient_evidence", "inspect_state", "review_proof"], category="ambiguity")
    add("photo-intent", "Warm the whole photo slightly while keeping natural color.",
        {"currentPhotoReadback": {"temperature": 0}, "visualAssessment": "neutral daylight",
         "noCompositionChangesRequested": True}, ["finish_plan"], tools=[], required=False)
    add("clipped-title-repair", "Keep the headline at least three times the subtitle size and fix clipping.",
        {"currentReadback": {"headlineSize": 144, "subtitleSize": 48, "headlineTracking": 40,
                              "headlineId": "title-17", "subtitleId": "subtitle-8"},
         "currentBoundReview": {"issue": "Headline clips at right canvas edge; excessive tracking.",
                                 "repairHint": "Reduce title tracking while preserving requested size ratio."},
         "typographyInputSchemaAvailable": True, "nativeTaskAdmission": "ready"}, ["use_document_tools"])
    add("async-export-pending", "Export the approved Timeline as H.264 once.",
        {"exportReceipt": {"accepted": True, "requestId": "81563087-38ce-49bf-a422-dd3c254147b3",
                           "resourceId": "timeline-fixture", "documentHash": "a" * 64, "status": "running"},
         "instruction": "Never submit a duplicate export while this job is pending."},
        ["inspect_state"], tools=["timeline.inspect_export", "timeline.export_h264"])
    add("missing-font", "Use the exact requested font family and face; do not substitute.",
        {"fontInventoryRead": True, "requestedFaceAvailable": False,
         "fontInstallationToolAvailable": False, "nativeInventoryIsComplete": True},
        ["ask_user", "stop_safely"])
    add("completed-in-progress", "Change headline copy, then verify it. Do not export.",
        {"currentReadback": {"requestedCopyMatches": True}, "currentBoundRenderedReview": "passed",
         "allExplicitRequirementsComplete": True, "pendingOperations": []},
        ["finish_plan"], required=False)
    return rows


if __name__ == "__main__":
    catalog = {t["name"] for t in json.loads(Path("training/gemma4-editing/tool-catalog.json").read_text())["tools"]}
    unknown = {t["name"] for row in cases() for t in row["tools"]} - catalog
    if unknown:
        raise SystemExit(f"Fixture references unknown native tools: {sorted(unknown)}")
    path = Path("training/agent-decisions/cases.json")
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(cases(), indent=2) + "\n")
    print(f"Wrote {len(cases())} fictional routing fixtures to {path}")
