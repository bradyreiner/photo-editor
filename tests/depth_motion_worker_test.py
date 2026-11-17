"""Geometry, temporal stability, real codec, and cache regression checks."""
import importlib.util
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

SPEC = importlib.util.spec_from_file_location("depth_motion", Path(__file__).parents[1] / "scripts/depth-motion-worker.py")
worker = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(worker)
try:
    worker.load_libraries()
except ImportError:
    raise SystemExit(77)
np, Image = worker.np, worker.Image


class Geometry(unittest.TestCase):
    def setUp(self):
        self.settings = worker.validate({})
        yy, xx = np.indices((72, 96))
        self.rgb = np.stack((xx * 2, yy * 3, xx + yy), axis=-1).astype(np.uint8)
        self.depth = np.where(xx < 48, 0.1, 0.9).astype(np.float32)

    def test_identity_is_exact_and_depth_changes_projection(self):
        for strength in (0, 0.65, 1.5):
            self.settings["depth"] = strength
            output, holes = worker.render(self.rgb, self.depth, 0, self.settings)
            np.testing.assert_array_equal(output, self.rgb)
            self.assertEqual(holes, 0)
        self.settings["motion"] = 0
        output, _ = worker.render(self.rgb, self.depth, 0.25, self.settings)
        np.testing.assert_array_equal(output, self.rgb)

    def test_pinhole_round_trip_and_foreground_parallax(self):
        points, focal = worker.point_cloud(self.depth)
        np.testing.assert_allclose(points[..., 0] / points[..., 2] * focal + 47.5,
                                   np.indices(self.depth.shape)[1], atol=1e-5)
        near = np.array([[[0, 0, 0.7], [0, 0, 1.3]]], np.float32)
        moved = worker.camera_coordinates(near, 0.25, 0.025, "slide")
        displacement = moved[..., 0] / moved[..., 2]
        self.assertGreater(abs(displacement[0, 0]), abs(displacement[0, 1]))
        self.assertLess(displacement[0, 0] * displacement[0, 1], 0)
        for convergence in (0, 1):
            points, _ = worker.point_cloud(self.depth, 1.5, convergence)
            self.assertTrue(np.isfinite(points).all())
            self.assertTrue((points[..., 2] > 0).all())

    def test_loop_closes_and_motion_is_visible(self):
        for path in ("slide", "arc", "push"):
            self.settings["path"] = path
            beginning, _ = worker.render(self.rgb, self.depth, 0, self.settings)
            end, _ = worker.render(self.rgb, self.depth, 1, self.settings)
            np.testing.assert_array_equal(beginning, end)
            moved, _ = worker.render(self.rgb, self.depth, 0.25, self.settings)
            self.assertGreater(float(np.abs(moved.astype(float) - beginning).mean()), 0.05)

    def test_point_gradient_matches_live_depth_coordinates_and_custom_stops(self):
        settings = worker.validate({"settings": {"style": "points", "motion": 0,
            "pointAppearance": {"colorMode": 2}}})
        for depth_value, expected in ((0, [138, 117, 0]), (1, [0, 171, 84])):
            image, _ = worker.render(self.rgb, np.full(self.depth.shape, depth_value, np.float32), 0, settings)
            colors = image.reshape(-1, 3)
            colors = colors[np.any(colors != 0, axis=1)]
            self.assertGreater(len(colors), 100)
            np.testing.assert_array_equal(np.unique(colors, axis=0), [expected])
        settings["pointAppearance"].update({"gradientNear": "#ff0080", "gradientMid": "#ff0080", "gradientFar": "#ff0080"})
        pink, _ = worker.render(self.rgb, self.depth, .25, settings)
        colors = pink.reshape(-1, 3)
        np.testing.assert_array_equal(np.unique(colors[np.any(colors != 0, axis=1)], axis=0), [[255, 0, 128]])
        # Photographic rendering keeps its image colors even with a saved point gradient.
        settings["style"] = "photo"
        photo, _ = worker.render(self.rgb, self.depth, 0, settings)
        np.testing.assert_array_equal(photo, self.rgb)
        for bad in ({"colorMode": 8}, {"colorMode": True}, {"brightness": float("nan")},
                    {"gradientMid": "invalid"}, {"background": "#ffff"}):
            with self.subTest(appearance=bad), self.assertRaises(ValueError):
                worker.validate({"settings": {"pointAppearance": bad}})

    def test_invalid_depth_and_recipe_are_rejected(self):
        with self.assertRaises(ValueError):
            worker.normalize_depth(np.array([[float("nan")]]))
        for key, value in (("motion", 0.5), ("duration", 0), ("fps", 24.5), ("edge", 100000),
                           ("depth", float("inf")), ("path", "unknown"), ("invert", "false")):
            with self.subTest(key=key), self.assertRaises(ValueError):
                worker.validate({"settings": {key: value}})
        np.testing.assert_array_equal(worker.normalize_depth(np.ones((2, 2))), np.full((2, 2), 0.5))

    def test_temporal_filter_does_not_blend_across_a_cut(self):
        old = (np.zeros(self.depth.shape, np.uint8), np.zeros(self.depth.shape, np.float32))
        white = np.full(self.rgb.shape, 255, np.uint8)
        filtered, _ = worker.stabilize_depth(self.depth, white, old)
        np.testing.assert_array_equal(filtered, self.depth)
        noisy = np.full(self.depth.shape, 0.55, np.float32)
        old = (worker.cv2.cvtColor(self.rgb, worker.cv2.COLOR_RGB2GRAY), np.full(self.depth.shape, 0.5, np.float32))
        filtered, _ = worker.stabilize_depth(noisy, self.rgb, old)
        self.assertLess(float(np.abs(filtered - 0.5).mean()), 0.04)

    def test_point_style_is_distinct_and_ply_is_real_xyzrgb(self):
        self.settings["style"] = "points"
        output, holes = worker.render(self.rgb, self.depth, 0.25, self.settings)
        self.assertGreater(holes, 0.4)
        self.assertGreater(np.count_nonzero(output), 100)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "cloud.ply"
            worker.write_ply(path, self.rgb, self.depth, self.settings)
            data = path.read_bytes()
            header, body = data.split(b"end_header\n", 1)
            self.assertIn(f"element vertex {self.depth.size}".encode(), header)
            self.assertEqual(len(body), self.depth.size * 15)


@unittest.skipUnless(shutil.which("ffmpeg") and shutil.which("ffprobe"), "FFmpeg required")
class CodecAndCache(unittest.TestCase):
    def test_trim_is_applied_before_inference_and_all_layer_frames_match(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "two-colors.mp4"
            subprocess.run([shutil.which("ffmpeg"), "-v", "error", "-f", "lavfi", "-i",
                "color=c=red:s=64x48:r=24:d=1[a];color=c=blue:s=64x48:r=24:d=1[b];[a][b]concat=n=2:v=1:a=0",
                "-c:v", "libx264", str(source)], check=True)
            calls = []
            original = worker.Estimator
            def estimate(rgb):
                calls.append(rgb.mean(axis=(0, 1)))
                return np.tile(np.linspace(0, 1, rgb.shape[1]), (rgb.shape[0], 1))
            worker.Estimator = lambda _: estimate
            try:
                worker.generate({"source": str(source), "modelPath": "unused",
                    "settings": {"start": 1.25, "duration": 0.5, "fps": 24, "edge": 256}}, root / "render")
            finally:
                worker.Estimator = original
            self.assertEqual(len(calls), 12)
            self.assertTrue(all(color[2] > 240 and color[0] < 10 for color in calls))
            project = root / "render"
            recipe = json.loads((project / "project.depthmotion.json").read_text())
            self.assertEqual(recipe["settings"]["start"], 1.25)
            self.assertEqual(recipe["frames"], 12)
            for layer in ("source-video", "depth-video", "pointcloud"):
                self.assertEqual(json.loads((project / layer / "manifest.json").read_text())["frames"], 12)
            for index in range(12):
                with np.load(Path(recipe["cachePath"]) / f"{index:06d}.npz") as cached:
                    np.testing.assert_array_equal(np.array(Image.open(project / "source-video" / f"{index:06d}.png")), cached["rgb"])
            # Existing projects can backfill the source layer without inference or rewriting depth.
            shutil.rmtree(project / "source-video")
            before = (project / "depth-video/000000.png").read_bytes()
            worker.prepare_project_depth_video(project / "project.depthmotion.json")
            self.assertEqual((project / "depth-video/000000.png").read_bytes(), before)
            self.assertEqual(len(list((project / "source-video").glob("*.png"))), 12)

    def test_encoded_point_palette_changes_reuse_depth_and_persist(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            rgb = np.full((96, 128, 3), [40, 120, 200], np.uint8)
            Image.fromarray(rgb).save(root / "image.png")
            Image.fromarray(np.tile(np.linspace(0, 65535, 128, dtype=np.uint16), (96, 1))).save(root / "map.png")
            request = {"source": str(root / "image.png"), "depthMap": str(root / "map.png"),
                "settings": {"style": "points", "motion": 0, "duration": .5, "fps": 24, "edge": 256,
                    "pointAppearance": {"colorMode": 2, "gradientPreset": "Spectrum"}}}
            worker.generate(request, root / "spectrum")
            first = json.loads((root / "spectrum/project.depthmotion.json").read_text())
            request["cachePath"] = first["cachePath"]
            original_manifest = (Path(first["cachePath"]) / "manifest.json").read_bytes()
            original_frame = (Path(first["cachePath"]) / "000000.npz").read_bytes()
            # A different trim/render must create its own manifest rather than
            # relabel the completed project's old depth cache.
            request["settings"]["duration"] = 1
            worker.generate(request, root / "different-duration")
            self.assertEqual((Path(first["cachePath"]) / "manifest.json").read_bytes(), original_manifest)
            self.assertEqual((Path(first["cachePath"]) / "000000.npz").read_bytes(), original_frame)
            # Reject legacy caches whose manifest was relabeled by an older
            # cache-miss bug, even if that label matches the new requested trim.
            legacy_cache = root / "legacy-cache"
            shutil.copytree(first["cachePath"], legacy_cache)
            relabeled = json.loads(original_manifest)
            relabeled["identity"]["sampling"]["duration"] = 1
            (legacy_cache / "manifest.json").write_text(json.dumps(relabeled))
            request["cachePath"] = str(legacy_cache)
            request["cacheIdentity"] = first["identity"]
            worker.generate(request, root / "untrusted-cache")
            regenerated = json.loads((root / "untrusted-cache/project.depthmotion.json").read_text())
            self.assertNotEqual(regenerated["cachePath"], str(legacy_cache))
            request["cachePath"] = first["cachePath"]
            request["settings"]["duration"] = .5
            request["settings"]["pointAppearance"].update({"gradientPreset": "Custom", "gradientNear": "#ff0080",
                "gradientMid": "#ff0080", "gradientFar": "#ff0080"})
            # A color-only rerender must not read/infer depth again.
            original = worker.Estimator
            worker.Estimator = lambda _: self.fail("Palette changes must reuse existing depth")
            try:
                worker.generate(request, root / "pink")
            finally:
                worker.Estimator = original
            second = json.loads((root / "pink/project.depthmotion.json").read_text())
            self.assertEqual(first["cachePath"], second["cachePath"])
            self.assertEqual(second["settings"]["pointAppearance"]["gradientMid"], "#ff0080")
            self.assertEqual(second["interactiveView"]["appearance"]["gradientPreset"], "Custom")
            self.assertNotEqual((root / "spectrum/motion.mp4").read_bytes(), (root / "pink/motion.mp4").read_bytes())
            subprocess.run([shutil.which("ffmpeg"), "-v", "error", "-i", str(root / "pink/motion.mp4"),
                "-frames:v", "1", str(root / "decoded.png")], check=True)
            decoded = np.array(Image.open(root / "decoded.png").convert("RGB"))
            colored = decoded[decoded[..., 0] > 80]
            self.assertGreater(len(colored), 100)
            mean = colored.mean(axis=0)
            self.assertGreater(mean[0], mean[1] + 30)
            self.assertGreater(mean[2], mean[1] + 10)
            self.assertLess(decoded[..., 1].mean(), 15)

    def test_depth_video_uses_each_cached_frame_without_inference(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            cache = root / "cache"
            cache.mkdir()
            first = np.tile(np.linspace(0, 1, 12, dtype=np.float32), (8, 1))
            for index, depth in enumerate((first, 1 - first)):
                np.savez(cache / f"{index:06d}.npz", depth=depth)
            path = root / "project.depthmotion.json"
            recipe = {"version": 1, "source": "unavailable.mp4", "video": True,
                      "frames": 2, "cachePath": str(cache), "settings": {"fps": 24}}
            path.write_text(json.dumps(recipe))
            original = path.read_bytes()
            worker.prepare_project_depth_video(path)
            a = np.array(Image.open(root / "depth-video/000000.png"))
            b = np.array(Image.open(root / "depth-video/000001.png"))
            self.assertEqual(a.shape, (8, 12))
            self.assertEqual((int(a[0, 0]), int(a[0, -1])), (0, 255))
            np.testing.assert_array_equal(a, 255 - b)
            self.assertEqual(path.read_bytes(), original)
            worker.prepare_project_depth_video(path)  # Reopening preserves the existing sequence.
            self.assertEqual(json.loads((root / "depth-video/manifest.json").read_text())["frames"], 2)

    def test_failed_depth_video_preserves_project_and_never_publishes_partial_frames(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            cache = root / "cache"
            cache.mkdir()
            np.savez(cache / "000000.npz", depth=np.zeros((8, 12), np.float32))
            with self.assertRaises(FileNotFoundError):
                worker.write_depth_video(root, cache, ["000000.npz", "000001.npz"], worker.validate({}))
            self.assertFalse((root / "depth-video").exists())

    def test_still_depth_render_and_reuse(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            yy, xx = np.indices((128, 192))
            Image.fromarray(np.stack((xx, yy, xx), axis=-1).astype(np.uint8)).save(root / "image.png")
            Image.fromarray((xx / xx.max() * 65535).astype(np.uint16)).save(root / "map.png")
            request = {"source": str(root / "image.png"), "depthMap": str(root / "map.png"),
                       "settings": {"duration": 1, "fps": 12, "edge": 256}}
            worker.generate(request, root / "first")
            recipe = json.loads((root / "first/project.depthmotion.json").read_text())
            self.assertEqual(recipe["frames"], 12)
            self.assertEqual(recipe["model"], "supplied-depth")
            probe = subprocess.run([shutil.which("ffprobe"), "-v", "error", "-count_frames", "-show_streams",
                                    "-of", "json", str(root / "first/motion.mp4")], capture_output=True, check=True, text=True)
            self.assertEqual(json.loads(probe.stdout)["streams"][0]["nb_read_frames"], "12")
            request["cachePath"] = recipe["cachePath"]
            request["settings"]["motion"] = 0.04
            worker.generate(request, root / "second")
            second = json.loads((root / "second/project.depthmotion.json").read_text())
            self.assertEqual(recipe["cachePath"], second["cachePath"])
            self.assertNotEqual((root / "first/motion.mp4").read_bytes(), (root / "second/motion.mp4").read_bytes())
            self.assertEqual((root / "first/pointcloud/000000.xyzrgb").read_bytes(),
                             (root / "second/pointcloud/000000.xyzrgb").read_bytes(),
                             "Preset camera motion must never be baked into the live XYZ geometry")
            # Changed content at the same source path must never reuse stale depth.
            Image.fromarray(np.full((128, 192, 3), 80, np.uint8)).save(root / "image.png")
            worker.generate(request, root / "third")
            third = json.loads((root / "third/project.depthmotion.json").read_text())
            self.assertNotEqual(recipe["cachePath"], third["cachePath"])
            legacy = root / "legacy"
            legacy.mkdir()
            original_recipe = (root / "first/project.depthmotion.json").read_bytes()
            (legacy / "project.depthmotion.json").write_bytes(original_recipe)
            (Path(recipe["cachePath"]) / "manifest.json").unlink()
            worker.prepare_project_cloud(legacy / "project.depthmotion.json")
            self.assertEqual((legacy / "pointcloud/000000.xyzrgb").read_bytes(),
                             (root / "first/pointcloud/000000.xyzrgb").read_bytes())
            self.assertEqual((root / "first/project.depthmotion.json").read_bytes(), original_recipe)

    def test_video_preserves_source_motion_audio_and_short_duration(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "video.mp4"
            subprocess.run([shutil.which("ffmpeg"), "-v", "error", "-f", "lavfi", "-i", "testsrc2=size=192x128:rate=12",
                            "-f", "lavfi", "-i", "sine=frequency=440", "-t", "0.75", "-c:v", "libx264", "-c:a", "aac", str(source)], check=True)
            original_estimator = worker.Estimator
            # This test isolates temporal/codec behavior; neural inference is qualified separately.
            worker.Estimator = lambda _: lambda rgb: np.tile(np.linspace(0, 1, rgb.shape[1]), (rgb.shape[0], 1))
            try:
                worker.generate({"source": str(source), "modelPath": "unused-test-model",
                                 "settings": {"duration": 2, "fps": 12, "edge": 256}}, root / "render")
            finally:
                worker.Estimator = original_estimator
            recipe = json.loads((root / "render/project.depthmotion.json").read_text())
            self.assertTrue(recipe["video"])
            self.assertEqual(recipe["frames"], 9)
            sequence = json.loads((root / "render/pointcloud/manifest.json").read_text())
            self.assertEqual(sequence["frames"], 9)
            self.assertEqual(sequence["stride"], 15)
            self.assertTrue(all(0 < count <= 200000 for count in sequence["counts"]))
            first_cloud = np.fromfile(root / "render/pointcloud/000000.xyzrgb", dtype=[("xyz", "<f4", (3,)), ("rgb", "u1", (3,))])
            last_cloud = np.fromfile(root / "render/pointcloud/000008.xyzrgb", dtype=first_cloud.dtype)
            self.assertTrue(np.isfinite(first_cloud["xyz"]).all())
            self.assertGreater(float(np.abs(first_cloud["rgb"].astype(float) - last_cloud["rgb"]).mean()), 1)
            with np.load(Path(recipe["cachePath"]) / "000000.npz") as cached_first:
                expected_xyz, _ = worker.point_cloud(cached_first["depth"], recipe["settings"]["depth"], recipe["settings"]["convergence"])
            step = sequence["sampleStep"]
            np.testing.assert_allclose(first_cloud["xyz"], expected_xyz[::step, ::step].reshape(-1, 3))
            cached = Path(recipe["cachePath"])
            with np.load(cached / "000000.npz") as first, np.load(cached / "000008.npz") as last:
                self.assertGreater(float(np.abs(first["rgb"].astype(float) - last["rgb"]).mean()), 1)
            probe = subprocess.run([shutil.which("ffprobe"), "-v", "error", "-show_streams", "-of", "json",
                                    str(root / "render/motion.mp4")], capture_output=True, check=True, text=True)
            self.assertEqual({s["codec_type"] for s in json.loads(probe.stdout)["streams"]}, {"video", "audio"})


if __name__ == "__main__":
    unittest.main()
