from __future__ import annotations

import hashlib
import json
import shutil
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from PIL import Image

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
import import_asset as intake  # noqa: E402


class ImportAssetTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temp = tempfile.TemporaryDirectory(prefix="looksy-asset-intake-")
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name) / "marketing"
        self.campaign_dir = self.root / "campaigns" / "demo"
        self.campaign_dir.mkdir(parents=True)
        (self.root / "media").mkdir()
        self.manifest_path = self.campaign_dir / "campaign.json"

        self.old_path = self.root / "media" / "old.png"
        self.new_source = Path(self.temp.name) / "new-take.png"
        self._write_png(self.old_path, (190, 40, 30))
        self._write_png(self.new_source, (25, 110, 210))
        self.old_bytes = self.old_path.read_bytes()
        self.new_bytes = self.new_source.read_bytes()
        self.scene_data = [{"id": "hero", "duration_s": 3, "media_refs": ["hero"]}]
        self.asset = {
            "id": "hero",
            "path": "media/old.png",
            "kind": "capture",
            "media_type": "image",
            "rights_status": "cleared",
            "proof_status": "verified",
            "proof_refs": ["evidence/capture-check.txt"],
            "rights_refs": ["evidence/release.txt"],
            "sha256": hashlib.sha256(self.old_bytes).hexdigest(),
            "source": "original project capture",
            "custom_metadata": {"keep": True},
        }
        self.other_asset = {
            "id": "other",
            "path": "media/not-touched.png",
            "kind": "image",
            "rights_status": "internal-review",
            "custom_metadata": "untouched",
        }
        self.manifest = {
            "schema_version": 1,
            "campaign_id": "demo",
            "title": "Demo campaign",
            "brand": {"name": "Looksy", "colors": {"accent": "#FF6254"}},
            "assets": [self.asset, self.other_asset],
            "claims": [{"id": "claim", "status": "pending"}],
            "scenes": self.scene_data,
            "extra_field": {"preserve": [1, 2, 3]},
        }
        self._write_manifest()

    @staticmethod
    def _write_png(path: Path, color: tuple[int, int, int]) -> None:
        path.parent.mkdir(parents=True, exist_ok=True)
        Image.new("RGB", (12, 8), color).save(path, format="PNG")

    def _write_manifest(self) -> None:
        self.manifest_path.write_text(json.dumps(self.manifest, indent=2) + "\n", encoding="utf-8")

    def _plan(self, *, source: Path | None = None, asset_id: str = "hero", **options: object) -> intake.ImportPlan:
        return intake.plan_import(
            self.manifest_path,
            asset_id,
            source or self.new_source,
            root=self.root,
            **options,
        )

    def test_changed_bytes_reset_all_clearance_and_proof_references(self) -> None:
        original_source = self.new_source.read_bytes()
        original_asset_file = self.old_path.read_bytes()
        plan = self._plan(note="Capture from the clean demo project.")

        intake.apply_import(plan, root=self.root)

        saved = json.loads(self.manifest_path.read_text(encoding="utf-8"))
        updated = saved["assets"][0]
        self.assertEqual(updated["rights_status"], "internal-review")
        self.assertEqual(updated["proof_status"], "pending")
        self.assertEqual(updated["rights_refs"], [])
        self.assertEqual(updated["proof_refs"], [])
        self.assertEqual(updated["import_note"], "Capture from the clean demo project.")
        self.assertEqual(updated["source"], "original project capture")
        self.assertEqual(updated["custom_metadata"], {"keep": True})
        self.assertEqual(updated["path"], plan.destination_relative)
        self.assertEqual(updated["sha256"], hashlib.sha256(self.new_bytes).hexdigest())
        self.assertEqual((self.root / updated["path"]).read_bytes(), self.new_bytes)
        self.assertEqual(self.new_source.read_bytes(), original_source)
        self.assertEqual(self.old_path.read_bytes(), original_asset_file)
        self.assertEqual(saved["assets"][1], self.other_asset)
        self.assertEqual(saved["scenes"], self.scene_data)
        self.assertEqual(saved["extra_field"], self.manifest["extra_field"])

    def test_identical_bytes_keep_existing_clearance_and_proof(self) -> None:
        same_source = Path(self.temp.name) / "same-image.png"
        same_source.write_bytes(self.old_bytes)
        plan = self._plan(source=same_source)
        self.assertFalse(plan.bytes_changed)

        intake.apply_import(plan, root=self.root)

        updated = json.loads(self.manifest_path.read_text(encoding="utf-8"))["assets"][0]
        self.assertEqual(updated["rights_status"], "cleared")
        self.assertEqual(updated["proof_status"], "verified")
        self.assertEqual(updated["rights_refs"], ["evidence/release.txt"])
        self.assertEqual(updated["proof_refs"], ["evidence/capture-check.txt"])
        self.assertEqual(updated["sha256"], hashlib.sha256(self.old_bytes).hexdigest())

    def test_tampered_existing_file_cannot_carry_old_clearance_forward(self) -> None:
        recorded_hash = self.asset["sha256"]
        self._write_png(self.old_path, (10, 210, 80))
        matching_recorded_source = Path(self.temp.name) / "recorded-original.png"
        matching_recorded_source.write_bytes(self.old_bytes)

        plan = self._plan(source=matching_recorded_source)
        self.assertTrue(plan.old_integrity_mismatch)
        self.assertTrue(plan.bytes_changed)
        intake.apply_import(plan, root=self.root)

        updated = json.loads(self.manifest_path.read_text(encoding="utf-8"))["assets"][0]
        self.assertEqual(updated["sha256"], recorded_hash)
        self.assertEqual(updated["rights_status"], "internal-review")
        self.assertEqual(updated["proof_status"], "pending")
        self.assertEqual(updated["rights_refs"], [])
        self.assertEqual(updated["proof_refs"], [])

    def test_missing_asset_id_fails_without_writing(self) -> None:
        manifest_before = self.manifest_path.read_bytes()
        with self.assertRaisesRegex(intake.IntakeError, "asset id not found"):
            self._plan(asset_id="does-not-exist")
        self.assertEqual(self.manifest_path.read_bytes(), manifest_before)
        self.assertFalse((self.root / "media" / "incoming").exists())

    def test_dry_run_prints_plan_without_creating_media_or_changing_manifest(self) -> None:
        manifest_before = self.manifest_path.read_bytes()
        with patch.object(intake, "MARKETING_ROOT", self.root):
            result = intake.main([
                "--manifest", str(self.manifest_path),
                "--asset", "hero",
                "--source", str(self.new_source),
                "--dry-run",
            ])
        self.assertEqual(result, 0)
        self.assertEqual(self.manifest_path.read_bytes(), manifest_before)
        self.assertFalse((self.root / "media" / "incoming").exists())

    def test_collision_with_different_bytes_uses_longer_hash_name(self) -> None:
        digest = hashlib.sha256(self.new_bytes).hexdigest()
        incoming = self.root / "media" / "incoming"
        incoming.mkdir(parents=True)
        collision = incoming / f"{digest[:12]}-new-take.png"
        collision.write_bytes(b"preserve this unrelated file")
        collision_before = collision.read_bytes()

        plan = self._plan()
        self.assertNotEqual(plan.destination_path, collision)
        self.assertTrue(plan.destination_path.name.startswith(f"{digest[:16]}-"))
        intake.apply_import(plan, root=self.root)

        self.assertEqual(collision.read_bytes(), collision_before)
        self.assertEqual(plan.destination_path.read_bytes(), self.new_bytes)

    def test_invalid_image_payload_is_rejected_before_copy(self) -> None:
        invalid_source = Path(self.temp.name) / "broken.png"
        invalid_source.write_bytes(b"this is not a PNG")

        with self.assertRaisesRegex(intake.IntakeError, "fully readable image"):
            self._plan(source=invalid_source)
        self.assertFalse((self.root / "media" / "incoming").exists())

    def test_capture_requires_an_explicit_or_existing_media_type(self) -> None:
        self.asset.pop("media_type")
        self._write_manifest()
        with self.assertRaisesRegex(intake.IntakeError, "capture assets require --media-type"):
            self._plan(source=self.new_source)

    @unittest.skipUnless(shutil.which("ffmpeg") and shutil.which("ffprobe"), "FFmpeg tools are required")
    def test_video_and_audio_are_fully_decoded(self) -> None:
        video = Path(self.temp.name) / "short-clip.mp4"
        audio = Path(self.temp.name) / "short-tone.wav"
        video_result = subprocess.run(
            [
                shutil.which("ffmpeg") or "ffmpeg", "-hide_banner", "-loglevel", "error",
                "-f", "lavfi", "-i", "color=c=blue:s=32x32:r=5:d=0.4",
                "-an", "-c:v", "mpeg4", "-pix_fmt", "yuv420p", "-y", str(video),
            ],
            check=False,
            capture_output=True,
            text=True,
        )
        audio_result = subprocess.run(
            [
                shutil.which("ffmpeg") or "ffmpeg", "-hide_banner", "-loglevel", "error",
                "-f", "lavfi", "-i", "sine=frequency=440:duration=0.3",
                "-c:a", "pcm_s16le", "-y", str(audio),
            ],
            check=False,
            capture_output=True,
            text=True,
        )
        self.assertEqual(video_result.returncode, 0, video_result.stderr)
        self.assertEqual(audio_result.returncode, 0, audio_result.stderr)

        intake.validate_media(video, "video")
        intake.validate_media(audio, "audio")

    @unittest.skipUnless(shutil.which("ffmpeg") and shutil.which("ffprobe"), "FFmpeg tools are required")
    def test_corrupt_video_is_rejected(self) -> None:
        invalid_video = Path(self.temp.name) / "broken.mp4"
        invalid_video.write_bytes(b"not a video container")
        with self.assertRaisesRegex(intake.IntakeError, "not a readable video file"):
            intake.validate_media(invalid_video, "video")

    def test_raw_source_is_not_staged_as_a_campaign_image(self) -> None:
        raw_source = Path(self.temp.name) / "camera-file.cr3"
        raw_source.write_bytes(b"synthetic raw placeholder")
        with self.assertRaisesRegex(intake.IntakeError, "use a decoded PNG/JPEG proxy"):
            self._plan(source=raw_source)
        self.assertFalse((self.root / "media" / "incoming").exists())


if __name__ == "__main__":
    unittest.main()
