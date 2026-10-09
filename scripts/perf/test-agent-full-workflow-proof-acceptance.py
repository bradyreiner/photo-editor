#!/usr/bin/env python3
"""Focused tests for the production Agent workflow evidence gate."""

from __future__ import annotations

import hashlib
import json
from pathlib import Path
import tempfile
import unittest

from agent_full_workflow_proof_acceptance import validate_proof


def digest(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


class AgentFullWorkflowProofAcceptanceTest(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.receipt_path = self.root / "workflow-receipt.json"
        self.source = self.root / "source.jpg"
        self.source.write_bytes(b"private source pixels")
        self.cover_copy = ["SAMPLE / FIELD NOTES 01", "AFTER THE LIGHT", "A STUDY IN BLUE HOUR"]
        output_root = self.root / "workflow-evidence" / "outputs"
        output_root.mkdir(parents=True)
        recipe_files = {
            "baseline-recipe.json": b"baseline preview recipe",
            "baseline-catalog-resource.json": b"baseline catalog resource",
            "initial-reviewed-recipe.json": b"initial reviewed recipe",
            "edited-recipe.json": b"edited recipe",
        }
        recipe_hashes = {}
        for filename, content in recipe_files.items():
            path = output_root / filename
            path.write_bytes(content)
            recipe_hashes[filename] = digest(path)
        layer_bounds = [
            {"x": 64, "y": 64, "width": 380, "height": 36},
            {"x": 64, "y": 120, "width": 760, "height": 110},
            {"x": 64, "y": 270, "width": 450, "height": 32},
        ]
        layer_sizes = [24.0, 80.0, 20.0]
        initial_layers = [
            {
                "text": text,
                "layerId": f"text-{index}",
                "pixelBounds": layer_bounds[index],
                "typographySize": layer_sizes[index],
                "layerScale": 1.0,
                "effectiveTypographySize": layer_sizes[index],
                "color": "#fffff5e8",
                "fontFamily": "Editorial Serif",
                "fontFace": "Regular",
                "fontWeight": 600,
                "fontFamilyAvailable": True,
                "fontFaces": ["Regular", "Medium", "Bold"],
                "fontFaceAvailable": True,
                "fontRequestPreserved": True,
                "fontAxes": [],
                "emptyAxesValidForAvailableFace": True,
                "usesSystemDefaultFont": False,
                "typography": {"family": "Editorial Serif", "face": "Regular", "weight": 600,
                               "size": layer_sizes[index], "color": "#fffff5e8"},
            }
            for index, text in enumerate(self.cover_copy)
        ]
        revised_layers = [dict(layer) for layer in initial_layers]
        revised_layers[1] = {
            **initial_layers[1],
            "pixelBounds": {**layer_bounds[1], "width": 684, "height": 99},
            "typographySize": 72.0,
            "effectiveTypographySize": 72.0,
            "typography": {"family": "Editorial Serif", "face": "Regular", "weight": 600,
                           "size": 72.0, "color": "#fffff5e8"},
        }
        rendered_ink = {
            "passed": True,
            "minimumRgb": [210, 200, 170],
            "minimumOpaqueChangedGlyphPixels": 50,
            "minimumWarmWhiteCoveragePercent": 85,
            "layers": [
                {
                    "text": text,
                    "opaqueChangedGlyphPixels": 100,
                    "warmWhitePixels": 95,
                    "maximumRenderedRgb": [255, 245, 232],
                    "passed": True,
                }
                for text in self.cover_copy
            ],
        }
        artifacts = {
            "baselineExportPath": "baseline.png",
            "editedExportPath": "edited.png",
            "reopenedExportPath": "reopened.png",
            "revertedExportPath": "reverted.png",
        }
        payload: dict[str, object] = {
            "schema": "agent-full-workflow-runtime-proof-v1",
            "status": "passed",
            "workflowKind": "editorial-cover",
            "actualApplicationUiQualified": True,
            "planningCancellationRequestedFromVisibleStopControl": True,
            "planningCancellationSettledInterrupted": True,
            "planningCancellationHadNoLatePlanOrMutation": True,
            "stopBehaviorProven": True,
            "retryUsedVisibleControl": True,
            "baselinePreviewMatchesCatalogRecipe": True,
            "initialEditorialCoverPassed": True,
            "revisedEditorialCoverPassed": True,
            "initialRenderedWarmWhiteInk": rendered_ink,
            "revisedRenderedWarmWhiteInk": rendered_ink,
            "backgroundPixelsPreservedOutsideTextBounds": True,
            "photoColorRecipePreserved": True,
            "coverOutputDimensionsMatchSource": True,
            "reopenedPixelsAndProfileMatch": True,
            "baselinePreviewRecipeRestoredExactly": True,
            "baselineCatalogRecipeRestoredExactly": True,
            "revertedPixelsAndProfileMatchBaseline": True,
            "sourcePreservedByteForByte": True,
            "sourcePath": str(self.source),
            "sourceSha256": digest(self.source),
            "coverCopy": self.cover_copy,
            "revisionCoverCopy": self.cover_copy,
            "installedModelId": "ollama-local-test-model",
            "assetId": "asset-1",
            "initialPlan": {"exactCopy": [{"text": text} for text in self.cover_copy]},
            "revisionPlan": {"exactCopy": [{"text": text} for text in self.cover_copy]},
            "initialRunReport": {"ok": True, "modelId": "ollama-local-test-model"},
            "revisedRunReport": {"ok": True, "modelId": "ollama-local-test-model"},
            "taskId": "task-1",
            "baselinePreviewSha256": recipe_hashes["baseline-recipe.json"],
            "baselineCatalogSha256": recipe_hashes["baseline-catalog-resource.json"],
            "initialReviewedRecipeSha256": recipe_hashes["initial-reviewed-recipe.json"],
            "editedRecipeSha256": recipe_hashes["edited-recipe.json"],
            "initialTaskTransaction": {"status": "review", "taskId": "task-1"},
            "revisedTaskTransaction": {"status": "review", "taskId": "task-1"},
            "initialCoverTextLayers": initial_layers,
            "revisedCoverTextLayers": revised_layers,
            "screenshots": [],
        }
        for name, relative_path in artifacts.items():
            path = self.root / relative_path
            path.write_bytes(b"baseline" if name in ("baselineExportPath", "revertedExportPath") else b"edited")
            payload[name] = str(path)
            digest_key = name.removesuffix("Path") + "Sha256"
            payload[digest_key] = digest(path)
        stages = [
            "request", "planning-cancelled", "initial-plan", "initial-review",
            "revision-plan", "revised-review", "saved-reopened-review", "whole-task-reverted",
        ]
        screenshots = []
        for stage in stages:
            path = self.root / f"{stage}.png"
            path.write_bytes(stage.encode())
            screenshots.append({"stage": stage, "path": str(path), "sha256": digest(path)})
        payload["screenshots"] = screenshots
        capture_stages = (
            "request", "initial-plan", "revision-plan",
            "initial-review", "revised-review", "saved-reopened-review", "whole-task-reverted",
        )
        payload["screenshotCaptureDiagnostics"] = []
        for stage in capture_stages:
            is_plan = stage.endswith("-plan")
            is_review = stage.endswith("-review")
            is_reverted = stage == "whole-task-reverted"
            context = {
                "purpose": "whole-task-reverted" if is_reverted else
                           "plan" if is_plan else "review" if is_review else "request",
                "settleWaitMs": 620,
                "minimumSettleWaitMs": 420,
                "frameSwapsSinceRequest": 1,
                "frameRequestAfterSettledState": True,
            }
            if is_review:
                context["reviewRevertControlVisible"] = True
                context["reviewRevertControlEnabled"] = True
            if is_reverted:
                context.update({
                    "taskId": "task-1",
                    "assetId": "asset-1",
                    "taskTransaction": {"status": "reverted", "taskId": "task-1"},
                    "taskTransactionStatus": "reverted",
                    "taskTransactionTaskId": "task-1",
                    "baselinePreviewRecipeSha256": recipe_hashes["baseline-recipe.json"],
                    "baselineCatalogRecipeSha256": recipe_hashes["baseline-catalog-resource.json"],
                    "restoredPreviewRecipeSha256": recipe_hashes["baseline-recipe.json"],
                    "restoredCatalogRecipeSha256": recipe_hashes["baseline-catalog-resource.json"],
                })
            payload["screenshotCaptureDiagnostics"].append({
                "stage": stage,
                "status": "captured",
                "reason": "presented-image-saved",
                "purpose": "presented-frame",
                "windowPresent": True,
                "windowVisible": True,
                "windowExposed": True,
                "windowWidth": 1400,
                "windowHeight": 900,
                "presentedFrameSerial": 2,
                "dialogPresentationProgress": 1.0,
                "terminalStage": "interrupted" if is_reverted else
                                 "plan-ready" if is_plan else "review" if is_review else "idle",
                "captureContext": context,
            })
        self.receipt = payload
        for label in ("initial", "revised"):
            report = payload[f"{label}RunReport"]
            report["modelPromptAudit"] = {
                "modelDecision": "edit_intent",
                "completionMode": "preserve-after-native-composition-readback",
            }
            report["nativeCompositionCompletion"] = {
                "requested": True, "controllerVerified": True,
                "verificationContract": "agent-native-composition-completion-controller-v1",
                "assetId": "asset-1", "acceptedMutationCount": 3,
                "readbackConfirmed": True,
            }
        self._write_receipt()

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def _write_receipt(self) -> None:
        self.receipt_path.write_text(json.dumps(self.receipt), encoding="utf-8")

    def test_complete_production_receipt_passes(self) -> None:
        self.assertEqual(validate_proof(self.receipt_path), [])

    def test_fallback_draft_cannot_replace_model_completion(self) -> None:
        self.receipt["initialRunReport"].pop("modelPromptAudit")
        self._write_receipt()
        self.assertTrue(any("verified model completion" in error for error in validate_proof(self.receipt_path)))

    def test_model_completion_requires_controller_verification(self) -> None:
        self.receipt["initialRunReport"]["nativeCompositionCompletion"]["controllerVerified"] = False
        self._write_receipt()
        self.assertTrue(any("verified model completion" in error for error in validate_proof(self.receipt_path)))

    def test_model_completion_cannot_target_another_asset(self) -> None:
        self.receipt["revisedRunReport"]["nativeCompositionCompletion"]["assetId"] = "other-asset"
        self._write_receipt()
        self.assertTrue(any("verified model completion" in error for error in validate_proof(self.receipt_path)))

    def test_model_completion_requires_accepted_native_mutations(self) -> None:
        self.receipt["revisedRunReport"]["nativeCompositionCompletion"]["acceptedMutationCount"] = 0
        self._write_receipt()
        self.assertTrue(any("verified model completion" in error for error in validate_proof(self.receipt_path)))

    def test_missing_visible_stop_proof_fails(self) -> None:
        self.receipt["planningCancellationRequestedFromVisibleStopControl"] = False
        self._write_receipt()
        self.assertTrue(any("visible Stop planning" in error for error in validate_proof(self.receipt_path)))

    def test_source_byte_change_fails_even_when_receipt_claims_preservation(self) -> None:
        self.source.write_bytes(b"changed source pixels")
        self._write_receipt()
        self.assertTrue(any("source-photo bytes" in error for error in validate_proof(self.receipt_path)))

    def test_revision_must_change_recipe(self) -> None:
        self.receipt["editedRecipeSha256"] = self.receipt["initialReviewedRecipeSha256"]
        self._write_receipt()
        self.assertTrue(any("did not change the recipe" in error for error in validate_proof(self.receipt_path)))

    def test_revision_must_reduce_only_the_headline_size_by_about_ten_percent(self) -> None:
        revised_layers = self.receipt["revisedCoverTextLayers"]
        assert isinstance(revised_layers, list)
        revised_layers[1]["typographySize"] = 60.0
        revised_layers[1]["effectiveTypographySize"] = 60.0
        revised_layers[1]["typography"]["size"] = 60.0
        self._write_receipt()
        self.assertTrue(any("reduce the headline size by about 10 percent" in error
                            for error in validate_proof(self.receipt_path)))

    def test_initial_cover_requires_three_to_one_headline_hierarchy(self) -> None:
        initial_layers = self.receipt["initialCoverTextLayers"]
        assert isinstance(initial_layers, list)
        initial_layers[1]["typographySize"] = 71.0
        initial_layers[1]["effectiveTypographySize"] = 71.0
        initial_layers[1]["typography"]["size"] = 71.0
        self._write_receipt()
        self.assertTrue(any("required 3:1 hierarchy" in error for error in validate_proof(self.receipt_path)))

    def test_initial_hierarchy_uses_effective_size_after_layer_scale(self) -> None:
        initial_layers = self.receipt["initialCoverTextLayers"]
        assert isinstance(initial_layers, list)
        initial_layers[1]["layerScale"] = 0.8
        initial_layers[1]["effectiveTypographySize"] = 64.0
        self._write_receipt()
        self.assertTrue(any("required 3:1 hierarchy" in error for error in validate_proof(self.receipt_path)))

    def test_revision_cannot_use_scale_change_as_headline_size_reduction(self) -> None:
        revised_layers = self.receipt["revisedCoverTextLayers"]
        assert isinstance(revised_layers, list)
        revised_layers[1]["layerScale"] = 0.9
        revised_layers[1]["typographySize"] = 80.0
        revised_layers[1]["effectiveTypographySize"] = 72.0
        revised_layers[1]["typography"]["size"] = 80.0
        self._write_receipt()
        self.assertTrue(any("changed a text layer scale" in error for error in validate_proof(self.receipt_path)))

    def test_nonpositive_layer_scale_fails_closed(self) -> None:
        initial_layers = self.receipt["initialCoverTextLayers"]
        assert isinstance(initial_layers, list)
        initial_layers[1]["layerScale"] = 0.0
        self._write_receipt()
        self.assertTrue(any("positive finite scale" in error for error in validate_proof(self.receipt_path)))

    def test_cover_receipt_requires_explicit_font_readback(self) -> None:
        layer = self.receipt["initialCoverTextLayers"][0]
        assert isinstance(layer, dict)
        layer.pop("fontFamily")
        self._write_receipt()
        self.assertTrue(any("lacks an available, preserved font family" in error
                            for error in validate_proof(self.receipt_path)))

    def test_cover_receipt_rejects_unavailable_requested_face(self) -> None:
        layer = self.receipt["initialCoverTextLayers"][0]
        assert isinstance(layer, dict)
        layer["fontFace"] = "Missing Face"
        layer["typography"]["face"] = "Missing Face"
        layer["fontFaceAvailable"] = False
        self._write_receipt()
        self.assertTrue(any("available, preserved font family" in error
                            for error in validate_proof(self.receipt_path)))

    def test_empty_variable_axes_are_valid_for_available_fixed_face(self) -> None:
        self._write_receipt()
        self.assertEqual(validate_proof(self.receipt_path), [])

    def test_empty_axes_without_available_face_proof_fails(self) -> None:
        layer = self.receipt["initialCoverTextLayers"][0]
        assert isinstance(layer, dict)
        layer["emptyAxesValidForAvailableFace"] = False
        self._write_receipt()
        self.assertTrue(any("empty font axis list" in error
                            for error in validate_proof(self.receipt_path)))

    def test_plan_screenshot_requires_a_presented_post_settle_frame(self) -> None:
        diagnostics = self.receipt["screenshotCaptureDiagnostics"]
        assert isinstance(diagnostics, list)
        initial_plan = next(item for item in diagnostics
                            if isinstance(item, dict) and item["stage"] == "initial-plan")
        initial_plan["captureContext"]["frameSwapsSinceRequest"] = 0
        self._write_receipt()
        self.assertTrue(any("captured before its settled frame was presented" in error
                            for error in validate_proof(self.receipt_path)))

    def test_plan_screenshot_must_show_plan_ready_terminal(self) -> None:
        diagnostics = self.receipt["screenshotCaptureDiagnostics"]
        assert isinstance(diagnostics, list)
        revision_plan = next(item for item in diagnostics
                             if isinstance(item, dict) and item["stage"] == "revision-plan")
        revision_plan["terminalStage"] = "planning"
        self._write_receipt()
        self.assertTrue(any("native plan screen ready" in error
                            for error in validate_proof(self.receipt_path)))

    def test_review_screenshot_requires_a_post_settle_review_frame(self) -> None:
        diagnostics = self.receipt["screenshotCaptureDiagnostics"]
        assert isinstance(diagnostics, list)
        revised_review = next(item for item in diagnostics
                              if isinstance(item, dict) and item["stage"] == "revised-review")
        revised_review["terminalStage"] = "executing"
        self._write_receipt()
        self.assertTrue(any("native Review screen ready" in error
                            for error in validate_proof(self.receipt_path)))

    def test_review_screenshot_requires_visible_revert_control(self) -> None:
        diagnostics = self.receipt["screenshotCaptureDiagnostics"]
        assert isinstance(diagnostics, list)
        reopened_review = next(item for item in diagnostics
                               if isinstance(item, dict) and item["stage"] == "saved-reopened-review")
        reopened_review["captureContext"]["reviewRevertControlEnabled"] = False
        self._write_receipt()
        self.assertTrue(any("visible, enabled task Revert control" in error
                            for error in validate_proof(self.receipt_path)))

    def test_whole_task_revert_requires_a_successful_presented_capture(self) -> None:
        diagnostics = self.receipt["screenshotCaptureDiagnostics"]
        assert isinstance(diagnostics, list)
        diagnostics[:] = [item for item in diagnostics
                          if not isinstance(item, dict) or item.get("stage") != "whole-task-reverted"]
        self._write_receipt()
        self.assertTrue(any("one successful, current presented-frame diagnostic" in error
                            for error in validate_proof(self.receipt_path)))

    def test_whole_task_revert_capture_rejects_stale_transaction_or_recipe_hash(self) -> None:
        diagnostics = self.receipt["screenshotCaptureDiagnostics"]
        assert isinstance(diagnostics, list)
        reverted = next(item for item in diagnostics
                        if isinstance(item, dict) and item["stage"] == "whole-task-reverted")
        reverted["captureContext"]["taskTransactionStatus"] = "review"
        reverted["captureContext"]["restoredPreviewRecipeSha256"] = "0" * 64
        self._write_receipt()
        errors = validate_proof(self.receipt_path)
        self.assertTrue(any("matching reverted task transaction" in error for error in errors))
        self.assertTrue(any("stale or does not match the exact frozen recipe bytes" in error for error in errors))

    def test_rendered_gray_ink_fails_even_when_recipe_metadata_is_warm_white(self) -> None:
        report = self.receipt["initialRenderedWarmWhiteInk"]
        assert isinstance(report, dict)
        report["passed"] = False
        first_layer = report["layers"][0]
        first_layer["passed"] = False
        first_layer["warmWhitePixels"] = 0
        first_layer["maximumRenderedRgb"] = [180, 180, 180]
        self._write_receipt()
        self.assertTrue(any("initial rendered text did not pass" in error for error in validate_proof(self.receipt_path)))
        self.assertTrue(any("rendered glyph maxima" in error for error in validate_proof(self.receipt_path)))

    def test_export_digest_tampering_fails(self) -> None:
        Path(str(self.receipt["editedExportPath"])).write_bytes(b"tampered")
        self.assertTrue(any("digest mismatch" in error for error in validate_proof(self.receipt_path)))


if __name__ == "__main__":
    unittest.main()
