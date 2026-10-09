#!/usr/bin/env python3
"""Strictly check the artifacts emitted by the real-app Agent workflow proof."""

from __future__ import annotations

import hashlib
import json
import math
from pathlib import Path
from typing import Any


PROOF_SCHEMA = "agent-full-workflow-runtime-proof-v1"


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def _inside(root: Path, value: Any) -> Path | None:
    if not isinstance(value, str) or not value:
        return None
    path = Path(value).resolve()
    try:
        path.relative_to(root.resolve())
    except ValueError:
        return None
    return path


def validate_proof(receipt_path: Path) -> list[str]:
    """Return acceptance failures for one production workflow receipt."""
    errors: list[str] = []
    try:
        receipt = json.loads(receipt_path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        return [f"Could not read workflow receipt: {exc}"]
    if not isinstance(receipt, dict):
        return ["Workflow receipt root must be an object."]

    root = receipt_path.resolve().parent

    def require(condition: bool, message: str) -> None:
        if not condition:
            errors.append(message)

    require(receipt.get("schema") == PROOF_SCHEMA, "Unexpected production workflow receipt schema.")
    require(receipt.get("status") == "passed", "The production workflow did not finish with status=passed.")
    require(receipt.get("workflowKind") == "editorial-cover", "The receipt does not identify the editorial-cover workflow.")
    require(receipt.get("actualApplicationUiQualified") is True, "The visible production application path was not qualified.")
    require(receipt.get("planningCancellationRequestedFromVisibleStopControl") is True,
            "The visible Stop planning control was not exercised.")
    require(receipt.get("planningCancellationSettledInterrupted") is True,
            "Planning Stop did not settle to interrupted state.")
    require(receipt.get("planningCancellationHadNoLatePlanOrMutation") is True,
            "The cancellation did not prove a stable, mutation-free result.")
    require(receipt.get("stopBehaviorProven") is True, "The workflow receipt does not pass its scoped Stop gate.")
    require(receipt.get("retryUsedVisibleControl") is True, "The canceled request was not retried through the visible UI.")
    require(receipt.get("baselinePreviewMatchesCatalogRecipe") is True,
            "Preview and Catalog did not match at the durable baseline.")
    require(receipt.get("initialEditorialCoverPassed") is True, "Initial editorial-cover checks failed.")
    require(receipt.get("revisedEditorialCoverPassed") is True, "Revised editorial-cover checks failed.")
    early_copy = receipt.get("coverCopy")
    expected_copy = early_copy if isinstance(early_copy, list) else []
    for label in ("initial", "revised"):
        ink = receipt.get(f"{label}RenderedWarmWhiteInk")
        require(isinstance(ink, dict) and ink.get("passed") is True,
                f"The {label} rendered text did not pass the warm-white pixel gate.")
        if not isinstance(ink, dict):
            continue
        minimum_rgb = ink.get("minimumRgb")
        minimum_opaque_pixels = ink.get("minimumOpaqueChangedGlyphPixels")
        minimum_coverage = ink.get("minimumWarmWhiteCoveragePercent")
        ink_layers = ink.get("layers")
        require(isinstance(minimum_rgb, list) and len(minimum_rgb) == 3 and
                all(isinstance(value, (int, float)) for value in minimum_rgb) and
                minimum_rgb[0] >= 210 and minimum_rgb[1] >= 200 and minimum_rgb[2] >= 170,
                f"The {label} rendered text report weakens the warm-white pixel thresholds.")
        require(isinstance(minimum_coverage, (int, float)) and minimum_coverage >= 85,
                f"The {label} rendered text report weakens the opaque-glyph coverage threshold.")
        require(isinstance(minimum_opaque_pixels, int) and minimum_opaque_pixels >= 50,
                f"The {label} rendered text report weakens the opaque glyph sample count.")
        require(isinstance(ink_layers, list) and len(ink_layers) == 3,
                f"The {label} rendered text report lacks three per-line pixel records.")
        if isinstance(ink_layers, list):
            require([item.get("text") for item in ink_layers if isinstance(item, dict)] == expected_copy,
                    f"The {label} rendered text pixel records differ from the approved copy.")
            for item in ink_layers:
                if not isinstance(item, dict):
                    require(False, f"A {label} rendered text pixel record is malformed.")
                    continue
                opaque_pixels = item.get("opaqueChangedGlyphPixels")
                warm_pixels = item.get("warmWhitePixels")
                maximum_rgb = item.get("maximumRenderedRgb")
                require(item.get("passed") is True and isinstance(opaque_pixels, int) and opaque_pixels >= 50 and
                        isinstance(warm_pixels, int) and warm_pixels * 100 >= opaque_pixels * 85,
                        f"The {label} rendered glyph pixels fail the warm-white coverage gate.")
                require(isinstance(maximum_rgb, list) and len(maximum_rgb) == 3 and
                        all(isinstance(value, (int, float)) for value in maximum_rgb) and
                        maximum_rgb[0] >= 210 and maximum_rgb[1] >= 200 and maximum_rgb[2] >= 170 and
                        maximum_rgb[0] >= maximum_rgb[2] - 20,
                        f"The {label} rendered glyph maxima do not show warm-white ink.")
    require(receipt.get("backgroundPixelsPreservedOutsideTextBounds") is True,
            "The saved cover changed photo pixels outside its text layers.")
    require(receipt.get("photoColorRecipePreserved") is True,
            "The cover recipe changed the photograph's color fields.")
    require(receipt.get("coverOutputDimensionsMatchSource") is True, "The cover changed the original photo dimensions.")
    require(receipt.get("reopenedPixelsAndProfileMatch") is True, "Reopened export pixels or profile changed.")
    require(receipt.get("baselinePreviewRecipeRestoredExactly") is True, "Revert did not restore Preview recipe bytes.")
    require(receipt.get("baselineCatalogRecipeRestoredExactly") is True, "Revert did not restore Catalog recipe bytes.")
    require(receipt.get("revertedPixelsAndProfileMatchBaseline") is True, "Revert did not restore baseline export pixels/profile.")
    require(receipt.get("sourcePreservedByteForByte") is True, "The source-photo preservation gate failed.")

    capture_diagnostics = receipt.get("screenshotCaptureDiagnostics")
    require(isinstance(capture_diagnostics, list),
            "The receipt lacks per-stage screenshot presentation diagnostics.")
    if isinstance(capture_diagnostics, list):
        for stage in (
            "request", "initial-plan", "revision-plan",
            "initial-review", "revised-review", "saved-reopened-review", "whole-task-reverted",
        ):
            diagnostic = next((item for item in capture_diagnostics
                               if isinstance(item, dict) and item.get("stage") == stage and
                               item.get("status") == "captured"), None)
            require(isinstance(diagnostic, dict),
                    f"The {stage} screen lacks a successful screenshot capture diagnostic.")
            if not isinstance(diagnostic, dict):
                continue
            context = diagnostic.get("captureContext")
            require(diagnostic.get("reason") == "presented-image-saved" and
                    diagnostic.get("purpose") == "presented-frame" and
                    diagnostic.get("windowPresent") is True and
                    diagnostic.get("windowVisible") is True and
                    diagnostic.get("windowExposed") is True and
                    isinstance(diagnostic.get("windowWidth"), (int, float)) and
                    diagnostic.get("windowWidth", 0) > 0 and
                    isinstance(diagnostic.get("windowHeight"), (int, float)) and
                    diagnostic.get("windowHeight", 0) > 0 and
                    isinstance(diagnostic.get("dialogPresentationProgress"), (int, float)) and
                    diagnostic.get("dialogPresentationProgress", 0) >= 0.999,
                    f"The {stage} screen was not captured from a settled, visible app frame.")
            require(isinstance(context, dict) and
                    isinstance(context.get("minimumSettleWaitMs"), (int, float)) and
                    context.get("minimumSettleWaitMs", 0) >= 420 and
                    isinstance(context.get("settleWaitMs"), (int, float)) and
                    context.get("settleWaitMs", 0) >= context.get("minimumSettleWaitMs", float("inf")) and
                    context.get("frameRequestAfterSettledState") is True and
                    isinstance(context.get("frameSwapsSinceRequest"), int) and
                    context.get("frameSwapsSinceRequest", 0) >= 1,
                    f"The {stage} screen was captured before its settled frame was presented.")
            if stage in ("initial-plan", "revision-plan"):
                require(diagnostic.get("terminalStage") == "plan-ready",
                        f"The {stage} screenshot was not taken with the native plan screen ready.")
            if stage.endswith("-review"):
                require(diagnostic.get("terminalStage") == "review",
                        f"The {stage} screenshot was not taken with the native Review screen ready.")
                require(isinstance(context, dict) and
                        context.get("reviewRevertControlVisible") is True and
                        context.get("reviewRevertControlEnabled") is True,
                        f"The {stage} screenshot lacks a visible, enabled task Revert control.")

        reverted_diagnostics = [item for item in capture_diagnostics
                                if isinstance(item, dict) and item.get("stage") == "whole-task-reverted"]
        require(len(reverted_diagnostics) == 1 and reverted_diagnostics[0].get("status") == "captured",
                "The whole-task Revert screen needs one successful, current presented-frame diagnostic.")
        if len(reverted_diagnostics) == 1 and reverted_diagnostics[0].get("status") == "captured":
            diagnostic = reverted_diagnostics[0]
            context = diagnostic.get("captureContext")
            transaction = context.get("taskTransaction") if isinstance(context, dict) else None
            require(diagnostic.get("terminalStage") == "interrupted",
                    "The whole-task Revert capture does not show the interrupted terminal state.")
            if not isinstance(context, dict):
                require(False, "The whole-task Revert capture lacks task and recipe context.")
            else:
                require(context.get("purpose") == "whole-task-reverted" and
                        context.get("taskId") == receipt.get("taskId") and
                        context.get("assetId") == receipt.get("assetId"),
                        "The whole-task Revert capture is not bound to the frozen task and asset.")
                require(context.get("taskTransactionStatus") == "reverted" and
                        context.get("taskTransactionTaskId") == receipt.get("taskId") and
                        isinstance(transaction, dict) and transaction.get("status") == "reverted" and
                        transaction.get("taskId") == receipt.get("taskId"),
                        "The whole-task Revert capture lacks a matching reverted task transaction.")
                require(context.get("baselinePreviewRecipeSha256") == receipt.get("baselinePreviewSha256") and
                        context.get("baselineCatalogRecipeSha256") == receipt.get("baselineCatalogSha256") and
                        context.get("restoredPreviewRecipeSha256") == receipt.get("baselinePreviewSha256") and
                        context.get("restoredCatalogRecipeSha256") == receipt.get("baselineCatalogSha256"),
                        "The whole-task Revert capture is stale or does not match the exact frozen recipe bytes.")

    source_path = _inside(root, receipt.get("sourcePath"))
    source_sha = receipt.get("sourceSha256")
    require(source_path is not None and source_path.is_file(), "The original source photo is missing or outside the private fixture.")
    if source_path is not None and source_path.is_file():
        require(isinstance(source_sha, str) and sha256(source_path) == source_sha,
                "The original source-photo bytes do not match the recorded digest.")

    cover_copy = receipt.get("coverCopy")
    revision_copy = receipt.get("revisionCoverCopy")
    require(isinstance(cover_copy, list) and len(cover_copy) == 3 and
            all(isinstance(text, str) and bool(text) for text in cover_copy),
            "The acceptance brief must contain exactly three copy lines.")
    require(cover_copy == revision_copy, "The revision changed approved cover copy.")
    expected_copy = cover_copy if isinstance(cover_copy, list) else []
    for label in ("initial", "revision"):
        plan = receipt.get(f"{label}Plan")
        exact_copy = plan.get("exactCopy", []) if isinstance(plan, dict) else []
        plan_copy = [item.get("text") for item in exact_copy if isinstance(item, dict)] if isinstance(exact_copy, list) else []
        require(all(text in plan_copy for text in expected_copy),
                f"The {label} approved plan omitted one or more exact cover lines.")

    initial_report = receipt.get("initialRunReport")
    revised_report = receipt.get("revisedRunReport")
    model_id = receipt.get("installedModelId")
    require(isinstance(model_id, str) and bool(model_id), "The configured local model ID is missing.")
    for label, report in (("initial", initial_report), ("revised", revised_report)):
        require(isinstance(report, dict) and report.get("ok") is True,
                f"The {label} model run report is missing or unsuccessful.")
        if isinstance(report, dict):
            require(report.get("modelId") == model_id, f"The {label} run used a different model ID.")
            audit = report.get("modelPromptAudit", {})
            completion = report.get("nativeCompositionCompletion", {})
            verified_completion = (
                isinstance(audit, dict) and isinstance(completion, dict)
                and audit.get("modelDecision") == "edit_intent"
                and audit.get("completionMode") == "preserve-after-native-composition-readback"
                and completion.get("requested") is True
                and completion.get("controllerVerified") is True
                and completion.get("verificationContract") == "agent-native-composition-completion-controller-v1"
                and isinstance(receipt.get("assetId"), str) and bool(receipt.get("assetId"))
                and completion.get("assetId") == receipt.get("assetId")
                and type(completion.get("acceptedMutationCount")) is int
                and completion["acceptedMutationCount"] > 0
                and completion.get("readbackConfirmed") is True
            )
            require(verified_completion, f"The {label} run lacks a verified model completion after fresh native readback.")
    require(receipt.get("taskId") is not None, "The editable task identity is missing.")
    for label in ("initial", "revised"):
        transaction = receipt.get(f"{label}TaskTransaction")
        require(isinstance(transaction, dict) and transaction.get("status") == "review",
                f"The {label} run did not reach the reversible Review state.")
        if isinstance(transaction, dict):
            require(transaction.get("taskId") == receipt.get("taskId"),
                    f"The {label} run changed task identity during the revision.")
    require(receipt.get("initialReviewedRecipeSha256") != receipt.get("editedRecipeSha256"),
            "The approved revision did not change the recipe.")

    output_root = root / "workflow-evidence" / "outputs"
    for filename, digest_key in (
        ("baseline-recipe.json", "baselinePreviewSha256"),
        ("baseline-catalog-resource.json", "baselineCatalogSha256"),
        ("initial-reviewed-recipe.json", "initialReviewedRecipeSha256"),
        ("edited-recipe.json", "editedRecipeSha256"),
    ):
        path = output_root / filename
        require(path.is_file(), f"Required durable recipe artifact is missing: {filename}.")
        if path.is_file():
            require(sha256(path) == receipt.get(digest_key), f"Durable recipe digest mismatch for {filename}.")

    initial_layers = receipt.get("initialCoverTextLayers")
    revised_layers = receipt.get("revisedCoverTextLayers")
    for label, layers in (("initial", initial_layers), ("revised", revised_layers)):
        require(isinstance(layers, list) and len(layers) == 3,
                f"The {label} review did not retain exactly three editable text layers.")
        if isinstance(layers, list):
            require([layer.get("text") for layer in layers if isinstance(layer, dict)] == expected_copy,
                    f"The {label} editable layer copy differs from the brief.")
    if isinstance(initial_layers, list) and isinstance(revised_layers, list) and len(initial_layers) == len(revised_layers) == 3:
        initial_ids = [layer.get("layerId") for layer in initial_layers if isinstance(layer, dict)]
        revised_ids = [layer.get("layerId") for layer in revised_layers if isinstance(layer, dict)]
        require(initial_ids == revised_ids, "The revision replaced successful cover layers instead of editing them.")
        for index, (before, after) in enumerate(zip(initial_layers, revised_layers)):
            if not isinstance(before, dict) or not isinstance(after, dict):
                require(False, "A cover text layer record is malformed.")
                continue
            require(before.get("text") == after.get("text") and before.get("color") == after.get("color"),
                    "The revision changed approved copy or text color.")
            before_typography = before.get("typography")
            after_typography = after.get("typography")
            require(isinstance(before_typography, dict) and isinstance(after_typography, dict),
                    "A cover text layer is missing explicit typography evidence.")
            if isinstance(before_typography, dict) and isinstance(after_typography, dict):
                for layer in (before, after):
                    style = layer.get("typography")
                    family = layer.get("fontFamily")
                    face = layer.get("fontFace")
                    faces = layer.get("fontFaces")
                    weight = layer.get("fontWeight")
                    require(isinstance(family, str) and bool(family.strip()) and
                            isinstance(face, str) and bool(face.strip()) and
                            isinstance(weight, (int, float)) and weight > 0 and
                            layer.get("fontFamilyAvailable") is True and
                            layer.get("fontFaceAvailable") is True and
                            layer.get("fontRequestPreserved") is True and
                            isinstance(faces, list) and
                            any(isinstance(value, str) and value.casefold() == face.casefold()
                                for value in faces) and
                            isinstance(layer.get("usesSystemDefaultFont"), bool),
                            "A cover text layer lacks an available, preserved font family, exact face or weight readback.")
                    axes = layer.get("fontAxes")
                    require(isinstance(axes, list),
                            "A cover text layer is missing font axis inspection evidence.")
                    if isinstance(axes, list) and not axes:
                        require(layer.get("emptyAxesValidForAvailableFace") is True,
                                "An empty font axis list must remain valid when the native face is available.")
                    if isinstance(style, dict):
                        require(family == style.get("family") and face == style.get("face") and
                                weight == style.get("weight") and
                                layer.get("color") == style.get("color") and
                                isinstance(style.get("size"), (int, float)) and style.get("size") > 0,
                                "The cover font family, face, size, weight or color readback differs from its recipe style.")
                    layer_scale = layer.get("layerScale")
                    raw_size = layer.get("typographySize")
                    effective_size = layer.get("effectiveTypographySize")
                    valid_scale = (isinstance(layer_scale, (int, float)) and
                                   math.isfinite(layer_scale) and 0.05 <= layer_scale <= 32.0)
                    valid_raw_size = (isinstance(raw_size, (int, float)) and
                                      math.isfinite(raw_size) and raw_size > 0)
                    valid_effective_size = (isinstance(effective_size, (int, float)) and
                                            math.isfinite(effective_size) and effective_size > 0)
                    require(valid_scale and valid_raw_size and valid_effective_size,
                            "A cover text layer lacks a positive finite scale and effective typography size.")
                    if valid_scale and valid_raw_size and valid_effective_size:
                        require(math.isclose(effective_size, raw_size * layer_scale,
                                             rel_tol=1e-9, abs_tol=1e-6),
                                "A cover effective typography size does not match font size times layer scale.")
                before_non_size = {key: value for key, value in before_typography.items() if key != "size"}
                after_non_size = {key: value for key, value in after_typography.items() if key != "size"}
                require(before_non_size == after_non_size,
                        "The revision changed typography properties other than size.")
                require(before_typography.get("size") == before.get("typographySize") and
                        after_typography.get("size") == after.get("typographySize"),
                        "A cover typography size does not match its rendered layer evidence.")
            before_bounds = before.get("pixelBounds")
            after_bounds = after.get("pixelBounds")
            require(isinstance(before_bounds, dict) and isinstance(after_bounds, dict),
                    "A cover text layer is missing rendered bounds evidence.")
            if isinstance(before_bounds, dict) and isinstance(after_bounds, dict):
                if index == 1:
                    positions = (
                        before_bounds.get("x"), after_bounds.get("x"),
                        before_bounds.get("y"), after_bounds.get("y"),
                    )
                    fixed_position = (
                        all(isinstance(value, (int, float)) for value in positions) and
                        abs(positions[0] - positions[1]) <= 1 and
                        abs(positions[2] - positions[3]) <= 1
                    )
                    require(fixed_position,
                            "The revision moved the headline while resizing it.")
                else:
                    require(before_bounds == after_bounds,
                            "The revision moved or resized the kicker/footer bounds.")
            before_scale = before.get("layerScale")
            after_scale = after.get("layerScale")
            require(isinstance(before_scale, (int, float)) and isinstance(after_scale, (int, float)) and
                    math.isfinite(before_scale) and math.isfinite(after_scale) and
                    math.isclose(before_scale, after_scale, rel_tol=0.0, abs_tol=1e-6),
                    "The revision changed a text layer scale instead of preserving geometry.")
            before_size = before.get("typographySize")
            after_size = after.get("typographySize")
            if index == 1:
                before_effective_size = before.get("effectiveTypographySize")
                after_effective_size = after.get("effectiveTypographySize")
                ratio = (after_effective_size / before_effective_size
                         if isinstance(before_effective_size, (int, float)) and before_effective_size > 0 and
                         isinstance(after_effective_size, (int, float)) and
                         math.isfinite(before_effective_size) and math.isfinite(after_effective_size)
                         else None)
                require(ratio is not None and 0.88 <= ratio <= 0.92,
                        "The revision did not reduce the headline size by about 10 percent.")
                require(isinstance(before_typography, dict) and isinstance(after_typography, dict) and
                        before_typography.get("size") == before_size and after_typography.get("size") == after_size,
                        "The headline typography size does not match the recorded size evidence.")
            else:
                require(before_size == after_size and
                        before.get("effectiveTypographySize") == after.get("effectiveTypographySize"),
                        "The revision changed the kicker/footer typography size.")
        require(all(isinstance(layer_id, str) and layer_id for layer_id in initial_ids) and len(set(initial_ids)) == 3,
                "The three editable text layers do not have distinct stable IDs.")
        initial_sizes = [layer.get("effectiveTypographySize") for layer in initial_layers if isinstance(layer, dict)]
        revised_sizes = [layer.get("effectiveTypographySize") for layer in revised_layers if isinstance(layer, dict)]
        if len(initial_sizes) == len(revised_sizes) == 3 and all(
            isinstance(size, (int, float)) and size > 0 for size in initial_sizes + revised_sizes
        ):
            require(initial_sizes[1] >= 3 * initial_sizes[0] and initial_sizes[1] >= 3 * initial_sizes[2],
                    "The initial cover headline lacks the required 3:1 hierarchy.")
            require(revised_sizes[1] >= 2 * revised_sizes[0] and revised_sizes[1] >= 2 * revised_sizes[2],
                    "The revised cover headline lacks the required 2:1 hierarchy.")

    artifact_pairs = (
        ("baselineExportPath", "baselineExportSha256"),
        ("editedExportPath", "editedExportSha256"),
        ("reopenedExportPath", "reopenedExportSha256"),
        ("revertedExportPath", "revertedExportSha256"),
    )
    artifact_files: dict[str, Path] = {}
    for path_key, digest_key in artifact_pairs:
        path = _inside(root, receipt.get(path_key))
        require(path is not None and path.is_file(), f"Required export artifact {path_key} is missing or outside the fixture.")
        if path is not None and path.is_file():
            artifact_files[path_key] = path
            require(sha256(path) == receipt.get(digest_key), f"Export artifact digest mismatch for {path_key}.")
    if "editedExportPath" in artifact_files and "reopenedExportPath" in artifact_files:
        require(sha256(artifact_files["editedExportPath"]) == sha256(artifact_files["reopenedExportPath"]),
                "The saved and reopened production exports are not byte-identical.")
    if "baselineExportPath" in artifact_files and "revertedExportPath" in artifact_files:
        require(sha256(artifact_files["baselineExportPath"]) == sha256(artifact_files["revertedExportPath"]),
                "The baseline and reverted production exports are not byte-identical.")

    screenshots = receipt.get("screenshots", [])
    require(isinstance(screenshots, list), "Workflow screenshots must be a list.")
    if not isinstance(screenshots, list):
        screenshots = []
    screenshot_stages = {item.get("stage") for item in screenshots if isinstance(item, dict)}
    required_stages = {
        "request", "planning-cancelled", "initial-plan", "initial-review",
        "revision-plan", "revised-review", "saved-reopened-review", "whole-task-reverted",
    }
    require(required_stages.issubset(screenshot_stages), "Visible screenshots do not cover every required workflow stage.")
    for item in screenshots:
        if not isinstance(item, dict):
            continue
        path = _inside(root, item.get("path"))
        require(path is not None and path.is_file(), "A recorded workflow screenshot is missing or outside the fixture.")
        if path is not None and path.is_file():
            require(sha256(path) == item.get("sha256"), "A recorded workflow screenshot digest does not match.")

    return errors
