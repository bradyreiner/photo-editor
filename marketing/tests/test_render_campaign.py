from __future__ import annotations

import hashlib
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import render_campaign as renderer  # noqa: E402


class CampaignValidationTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temp = tempfile.TemporaryDirectory(prefix="looksy-campaign-test-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        (self.root / "media").mkdir()
        (self.root / "evidence").mkdir()
        Image.new("RGB", (160, 90), (48, 70, 92)).save(self.root / "media" / "still.png")
        Image.new("RGB", (320, 180), (28, 42, 55)).save(self.root / "media" / "capture.png")
        Image.new("RGBA", (400, 90), (255, 255, 255, 255)).save(self.root / "media" / "looksy.png")
        (self.root / "evidence" / "capture-review.txt").write_text("Capture compared with the local app.", encoding="utf-8")
        (self.root / "evidence" / "claim-review.txt").write_text("Claim checked against current evidence.", encoding="utf-8")
        self.manifest = {
            "schema_version": 1,
            "campaign_id": "test-campaign",
            "title": "A small campaign",
            "brand": {
                "name": "Looksy",
                "colors": {"background": "#101116", "foreground": "#F3F1EE", "accent": "#FF6254", "optical": "#65D9E8"},
            },
            "cta": {"label": "Follow the build."},
            "aspect_ratios": ["16:9"],
            "assets": [
                {"id": "still", "path": "media/still.png", "kind": "image", "rights_status": "internal-review"},
                {
                    "id": "capture", "path": "media/capture.png", "kind": "capture", "rights_status": "cleared",
                    "proof_status": "verified", "proof_refs": ["evidence/capture-review.txt"], "rights_refs": ["evidence/capture-review.txt"],
                },
            ],
            "claims": [{"id": "example", "text": "A checked claim.", "status": "pending", "evidence_refs": []}],
            "scenes": [{
                "id": "opening", "type": "image", "duration_s": 0.2, "headline": "A thoughtful frame.",
                "body": "The local renderer adds motion.", "media_refs": ["still"], "claim_refs": [], "motion": "slow_zoom",
            }],
        }

    def test_draft_accepts_review_assets_while_final_blocks_them(self) -> None:
        accepted = renderer.validate_campaign(self.manifest, self.root, "draft")
        self.assertEqual(accepted.total_duration, 0.2)
        with self.assertRaisesRegex(renderer.CampaignError, "not rights-cleared"):
            renderer.validate_campaign(self.manifest, self.root, "final")

    def test_missing_media_reference_fails_validation(self) -> None:
        self.manifest["scenes"][0]["media_refs"] = ["unlisted-image"]
        with self.assertRaisesRegex(renderer.CampaignError, "unknown asset: unlisted-image"):
            renderer.validate_campaign(self.manifest, self.root, "draft")

    def test_declared_asset_hash_detects_a_tampered_file(self) -> None:
        asset_path = self.root / "media" / "still.png"
        self.manifest["assets"][0]["sha256"] = hashlib.sha256(asset_path.read_bytes()).hexdigest()
        asset_path.write_bytes(b"modified after manifest creation")
        with self.assertRaisesRegex(renderer.CampaignError, "sha256 does not match file contents"):
            renderer.validate_campaign(self.manifest, self.root, "draft")

    def test_final_requires_resolving_local_license_evidence(self) -> None:
        self.manifest["assets"][0]["rights_status"] = "cleared"
        self.manifest["assets"][0]["rights_refs"] = ["evidence/missing-license-record.txt"]
        with self.assertRaisesRegex(renderer.CampaignError, "rights_refs.*does not resolve to an existing file"):
            renderer.validate_campaign(self.manifest, self.root, "final")

    def test_final_requires_logo_clearance_and_human_review(self) -> None:
        self.manifest["assets"][0].update({"rights_status": "cleared", "rights_refs": ["evidence/claim-review.txt"]})
        self.manifest["brand"].update({
            "logo_asset": "media/looksy.png", "logo_rights_status": "cleared", "logo_rights_refs": ["evidence/capture-review.txt"],
        })
        with self.assertRaisesRegex(renderer.CampaignError, "logo has not passed human review"):
            renderer.validate_campaign(self.manifest, self.root, "final")
        self.manifest["brand"]["logo_review_status"] = "approved"
        self.assertEqual(renderer.validate_campaign(self.manifest, self.root, "final").total_duration, 0.2)

    def test_final_requires_existing_verified_capture_and_claim_evidence(self) -> None:
        self.manifest["scenes"] = [{
            "id": "capture", "type": "app", "duration_s": 1, "headline": "Current app capture.", "body": "",
            "media_refs": ["capture"], "claim_refs": ["example"], "motion": "none",
        }]
        self.manifest["claims"][0] = {
            "id": "example", "text": "A checked claim.", "status": "verified", "evidence_refs": ["evidence/claim-review.txt"],
        }
        self.assertEqual(renderer.validate_campaign(self.manifest, self.root, "final").total_duration, 1)
        self.manifest["assets"][1]["proof_refs"] = ["evidence/missing-capture-review.txt"]
        with self.assertRaisesRegex(renderer.CampaignError, "does not resolve to an existing file"):
            renderer.validate_campaign(self.manifest, self.root, "final")


class CampaignRenderTests(unittest.TestCase):
    @unittest.skipUnless(__import__("shutil").which("ffmpeg") and __import__("shutil").which("ffprobe"), "ffmpeg and ffprobe are required")
    def test_preview_is_h264_yuv420p_with_expected_dimensions(self) -> None:
        with tempfile.TemporaryDirectory(prefix="looksy-campaign-render-test-") as temporary:
            root = Path(temporary)
            (root / "media").mkdir()
            (root / "evidence").mkdir()
            Image.new("RGB", (160, 90), (56, 78, 99)).save(root / "media" / "still.png")
            (root / "evidence" / "license.txt").write_text("test source license", encoding="utf-8")
            manifest = {
                "schema_version": 1,
                "campaign_id": "render-smoke",
                "title": "Render smoke",
                "brand": {
                    "name": "Looksy",
                    "colors": {"background": "#101116", "foreground": "#F3F1EE", "accent": "#FF6254", "optical": "#65D9E8"},
                },
                "cta": {"label": "Follow the build."},
                "aspect_ratios": ["16:9"],
                "assets": [{"id": "still", "path": "media/still.png", "kind": "image", "rights_status": "cleared", "rights_refs": ["evidence/license.txt"]}],
                "claims": [],
                "scenes": [{
                    "id": "opening", "type": "image", "duration_s": 0.2, "headline": "Render check.",
                    "body": "H.264 portrait and landscape exports stay readable.", "media_refs": ["still"], "claim_refs": [], "motion": "slow_zoom",
                }],
            }
            validation = renderer.validate_campaign(manifest, root, "final")
            output = renderer._render_one(validation, "16:9", "preview", "final", root / "out")
            probe = renderer._ffprobe(output)
            h264 = next(stream for stream in probe["streams"] if stream.get("codec_name") == "h264")
            self.assertEqual((h264["width"], h264["height"], h264["pix_fmt"]), (960, 540, "yuv420p"))
            self.assertGreater(float(probe["format"]["duration"]), 0.1)
            self.assertTrue((root / "out" / "looksy-render-smoke-16x9-thumbnail.png").is_file())
            self.assertTrue((root / "out" / "looksy-render-smoke-16x9.srt").is_file())

    @unittest.skipUnless(shutil.which("ffmpeg") and shutil.which("ffprobe"), "ffmpeg and ffprobe are required")
    def test_moving_app_capture_decodes_and_preserves_capture_gates(self) -> None:
        with tempfile.TemporaryDirectory(prefix="looksy-app-video-test-") as temporary:
            root = Path(temporary)
            (root / "media").mkdir()
            (root / "evidence").mkdir()
            (root / "evidence" / "capture-review.txt").write_text("Synthetic decoder fixture only.", encoding="utf-8")
            source_video = root / "media" / "capture.mp4"
            generated = subprocess.run([
                "ffmpeg", "-hide_banner", "-loglevel", "error", "-y", "-f", "lavfi", "-i",
                "testsrc2=size=320x180:rate=30:duration=2", "-c:v", "libx264", "-pix_fmt", "yuv420p", str(source_video),
            ], capture_output=True, check=False)
            self.assertEqual(generated.returncode, 0, generated.stderr.decode(errors="replace"))
            manifest = {
                "schema_version": 1,
                "campaign_id": "app-video-smoke",
                "title": "Moving app capture check",
                "brand": {
                    "name": "Looksy",
                    "colors": {"background": "#101116", "foreground": "#F3F1EE", "accent": "#FF6254", "optical": "#65D9E8"},
                },
                "cta": {"label": "Follow the build."},
                "aspect_ratios": ["16:9"],
                "assets": [{
                    "id": "capture", "path": "media/capture.mp4", "kind": "capture", "media_type": "video",
                    "rights_status": "cleared", "rights_refs": ["evidence/capture-review.txt"],
                    "proof_status": "verified", "proof_refs": ["evidence/capture-review.txt"],
                }],
                "claims": [],
                "scenes": [{
                    "id": "app", "type": "app", "duration_s": 0.2, "headline": "Moving capture.", "body": "",
                    "media_refs": ["capture"], "claim_refs": [], "motion": "none",
                }],
            }
            validation = renderer.validate_campaign(manifest, root, "final")
            output = renderer._render_one(validation, "16:9", "preview", "final", root / "out")
            probe = renderer._ffprobe(output)
            h264 = next(stream for stream in probe["streams"] if stream.get("codec_name") == "h264")
            self.assertEqual((h264["width"], h264["height"], h264["pix_fmt"]), (960, 540, "yuv420p"))
            manifest["assets"][0]["proof_status"] = "pending"
            with self.assertRaisesRegex(renderer.CampaignError, "no verified capture proof"):
                renderer.validate_campaign(manifest, root, "final")


class ContactSheetTests(unittest.TestCase):
    def test_contact_sheet_compacts_aspect_rows_and_preserves_scene_order(self) -> None:
        with tempfile.TemporaryDirectory(prefix="looksy-contact-sheet-test-") as temporary:
            root = Path(temporary)
            thumb_dir = root / "9x16" / "thumbnails"
            thumb_dir.mkdir(parents=True)
            images = {
                "z-opening.png": ((90, 160), (210, 40, 40)),
                "a-closing.png": ((90, 160), (40, 190, 80)),
                "z-wide-opening.png": ((160, 90), (40, 90, 210)),
                "a-wide-closing.png": ((160, 90), (220, 180, 35)),
            }
            for name, (size, color) in images.items():
                Image.new("RGB", size, color).save(thumb_dir / name)
            scenes = [{"id": "z-opening"}, {"id": "a-closing"}]
            ordered_portrait = renderer._ordered_scene_thumbnails(thumb_dir, scenes)
            self.assertEqual([path.stem for path in ordered_portrait], ["z-opening", "a-closing"])

            inputs = ordered_portrait + [thumb_dir / "z-wide-opening.png", thumb_dir / "a-wide-closing.png"]
            entries = []
            for path in inputs:
                with Image.open(path) as source:
                    entries.append((path, source.convert("RGB")))
            groups = renderer._group_contact_images(entries)
            self.assertEqual([ratio for ratio, _ in groups], ["9:16", "16:9"])
            self.assertEqual([path.stem for path, _ in groups[0][1]], ["z-opening", "a-closing"])
            self.assertEqual([path.stem for path, _ in groups[1][1]], ["z-wide-opening", "a-wide-closing"])

            sheet = renderer._contact_sheet(inputs, root / "contact-sheet.jpg", "Aspect-aware review")
            with Image.open(sheet) as image:
                self.assertLess(image.height, 1250)


if __name__ == "__main__":
    unittest.main()
