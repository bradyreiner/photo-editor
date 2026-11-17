#!/usr/bin/env python3
"""Create technical and visual-review artifacts for two completed movie files.

Examples (the output directory must not already exist):

  python3 review_native_exports.py \
    --native /path/to/native-10s.mp4 --reference /path/to/reference.mp4 \
    --evidence-dir /path/to/new-review-prefix10 --scope prefix10

  python3 review_native_exports.py \
    --native /path/to/native-298s.mp4 --reference /path/to/reference.mp4 \
    --evidence-dir /path/to/new-review-full298 --scope full298

This tool reads inputs only. Contact sheets and SSIM are comparison aids; it
never decides whether the recreation is visually or creatively approved.
"""

from array import array
from datetime import datetime, timezone
from fractions import Fraction
from pathlib import Path
import argparse
import csv
import json
import math
import os
import re
import shutil
import struct
import subprocess
import sys
import zlib


SCHEMA = "looksy.native-render-review/v1"
SAMPLE_RATE = 8000
SAMPLE_SECONDS_PER_CONTACT_SHEET = 40
CONTACT_COLUMNS = 4
CONTACT_ROWS = 10
CONTACT_THUMB_MAX_EDGE = 640
MAX_DIAGNOSTIC_TEXT = 20000

FONT = {
    "0": ("111", "101", "101", "101", "111"),
    "1": ("010", "110", "010", "010", "111"),
    "2": ("111", "001", "111", "100", "111"),
    "3": ("111", "001", "111", "001", "111"),
    "4": ("101", "101", "111", "001", "001"),
    "5": ("111", "100", "111", "001", "111"),
    "6": ("111", "100", "111", "101", "111"),
    "7": ("111", "001", "010", "010", "010"),
    "8": ("111", "101", "111", "101", "111"),
    "9": ("111", "101", "111", "001", "111"),
    ":": ("000", "010", "000", "010", "000"),
    "R": ("110", "101", "110", "101", "101"),
    "E": ("111", "100", "110", "100", "111"),
    "F": ("111", "100", "110", "100", "100"),
    "N": ("101", "111", "111", "111", "101"),
    "A": ("010", "101", "111", "101", "101"),
    "T": ("111", "010", "010", "010", "010"),
    "I": ("111", "010", "010", "010", "111"),
    "V": ("101", "101", "101", "101", "010"),
    " ": ("000", "000", "000", "000", "000"),
}


class ReviewError(RuntimeError):
    """Raised when review inputs or a required tool are unavailable."""


def _utc_now():
    return datetime.now(timezone.utc).isoformat(timespec="seconds").replace("+00:00", "Z")


def _run(args, *, cwd=None, timeout=None):
    try:
        return subprocess.run(args, cwd=cwd, capture_output=True, text=True,
                              timeout=timeout, check=False)
    except (OSError, subprocess.TimeoutExpired) as error:
        raise ReviewError(f"Could not run {Path(args[0]).name}: {error}") from error


def _require_tool(name):
    path = shutil.which(name)
    if not path:
        raise ReviewError(f"Required tool is not available on PATH: {name}")
    return str(Path(path).resolve())


def _canonical_input(path, label):
    raw = str(path)
    candidate = Path(raw).expanduser()
    if not candidate.is_absolute():
        raise ReviewError(f"{label} must be an absolute file path.")
    try:
        resolved = candidate.resolve(strict=True)
        info = candidate.lstat()
    except OSError as error:
        raise ReviewError(f"{label} is not readable: {error}") from error
    if candidate.is_symlink() or not resolved.is_file() or str(resolved) != str(candidate):
        raise ReviewError(f"{label} must be a canonical regular file (not a symlink).")
    if not os.access(resolved, os.R_OK):
        raise ReviewError(f"{label} is not readable.")
    return resolved, info


def _new_evidence_directory(path):
    candidate = Path(path).expanduser()
    if not candidate.is_absolute():
        raise ReviewError("--evidence-dir must be an absolute new directory path.")
    if candidate.exists() or candidate.is_symlink():
        raise ReviewError(f"Evidence directory already exists; refusing overwrite: {candidate}")
    try:
        parent = candidate.parent.resolve(strict=True)
    except OSError as error:
        raise ReviewError(f"Evidence directory parent must already exist: {error}") from error
    if str(parent) != str(candidate.parent):
        raise ReviewError("Evidence directory parent must be a canonical path.")
    return candidate


def _fraction(value):
    if not isinstance(value, str) or value in {"", "N/A", "0/0"}:
        return None
    try:
        result = Fraction(value)
    except (ValueError, ZeroDivisionError):
        return None
    return float(result)


def _float(value):
    if value in (None, "", "N/A"):
        return None
    try:
        result = float(value)
    except (ValueError, TypeError):
        return None
    return result if math.isfinite(result) else None


def _int(value):
    if value in (None, "", "N/A"):
        return None
    try:
        return int(value)
    except (ValueError, TypeError):
        return None


def _probe(ffprobe, path):
    args = [
        ffprobe, "-v", "error", "-count_frames", "-show_streams", "-show_format",
        "-of", "json", str(path),
    ]
    result = _run(args, timeout=3600)
    if result.returncode != 0:
        raise ReviewError(f"ffprobe failed for {path.name}: {result.stderr[-MAX_DIAGNOSTIC_TEXT:]}")
    try:
        data = json.loads(result.stdout)
    except json.JSONDecodeError as error:
        raise ReviewError(f"ffprobe returned invalid JSON for {path.name}: {error}") from error
    streams = data.get("streams")
    if not isinstance(streams, list):
        raise ReviewError(f"ffprobe found no readable streams in {path.name}.")
    videos = [stream for stream in streams if stream.get("codec_type") == "video"]
    if not videos:
        raise ReviewError(f"No video stream found in {path.name}.")
    video = videos[0]
    audio = [stream for stream in streams if stream.get("codec_type") == "audio"]
    fmt = data.get("format", {})
    fps = _fraction(video.get("avg_frame_rate")) or _fraction(video.get("r_frame_rate"))
    frames = _int(video.get("nb_read_frames"))
    frame_source = "nb_read_frames" if frames is not None else None
    if frames is None:
        frames = _int(video.get("nb_frames"))
        if frames is not None:
            frame_source = "nb_frames"
    if frames is None and fps and _float(video.get("duration")):
        frames = round(fps * _float(video.get("duration")))
        frame_source = "estimated_from_stream_duration_and_fps"
    format_duration = _float(fmt.get("duration"))
    video_duration = _float(video.get("duration")) or format_duration
    return {
        "container": {
            "formatName": fmt.get("format_name"),
            "durationSeconds": format_duration,
            "startTimeSeconds": _float(fmt.get("start_time")),
            "sizeBytes": _int(fmt.get("size")),
            "bitRate": _int(fmt.get("bit_rate")),
            "tags": fmt.get("tags", {}),
        },
        "video": {
            "codec": video.get("codec_name"),
            "width": _int(video.get("width")),
            "height": _int(video.get("height")),
            "pixelFormat": video.get("pix_fmt"),
            "averageFrameRate": video.get("avg_frame_rate"),
            "realFrameRate": video.get("r_frame_rate"),
            "frameRateHz": fps,
            "frameCount": frames,
            "frameCountSource": frame_source,
            "durationSeconds": video_duration,
            "startTimeSeconds": _float(video.get("start_time")),
            "timeBase": video.get("time_base"),
            "colorRange": video.get("color_range"),
            "colorSpace": video.get("color_space"),
            "colorTransfer": video.get("color_transfer"),
            "colorPrimaries": video.get("color_primaries"),
        },
        "audio": [
            {
                "codec": stream.get("codec_name"),
                "sampleRate": _int(stream.get("sample_rate")),
                "channels": _int(stream.get("channels")),
                "channelLayout": stream.get("channel_layout"),
                "durationSeconds": _float(stream.get("duration")),
                "startTimeSeconds": _float(stream.get("start_time")),
                "timeBase": stream.get("time_base"),
            }
            for stream in audio
        ],
    }


def _sha256_file(path):
    digest = __import__("hashlib").sha256()
    with Path(path).open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def _file_snapshot(path):
    info = Path(path).stat()
    return {
        "path": str(path),
        "sizeBytes": info.st_size,
        "mtimeNs": info.st_mtime_ns,
        "device": info.st_dev,
        "inode": info.st_ino,
        "sha256": _sha256_file(path),
    }


def _same_file_snapshot(before, after):
    return all(before[key] == after[key]
               for key in ("path", "sizeBytes", "mtimeNs", "device", "inode", "sha256"))


def _decode_all(ffmpeg, path):
    args = [ffmpeg, "-hide_banner", "-nostdin", "-v", "error", "-xerror", "-i", str(path),
            "-map", "0:v:0", "-map", "0:a?", "-f", "null", "-"]
    result = _run(args, timeout=7200)
    return {
        "status": "passed" if result.returncode == 0 else "failed",
        "returnCode": result.returncode,
        "diagnostics": result.stderr[-MAX_DIAGNOSTIC_TEXT:],
    }


def _audio_offset(probe):
    if not probe["audio"]:
        return None
    audio_start = probe["audio"][0]["startTimeSeconds"]
    video_start = probe["video"]["startTimeSeconds"]
    if audio_start is None or video_start is None:
        return None
    return audio_start - video_start


def _decode_audio(ffmpeg, path, duration_seconds):
    args = [ffmpeg, "-hide_banner", "-nostdin", "-v", "error", "-i", str(path),
            "-map", "0:a:0", "-vn", "-sn", "-dn", "-af",
            f"aresample={SAMPLE_RATE}:async=0,asetpts=PTS-STARTPTS",
            "-ac", "1", "-ar", str(SAMPLE_RATE), "-t", f"{duration_seconds:.6f}",
            "-f", "f32le", "pipe:1"]
    try:
        result = subprocess.run(args, capture_output=True, text=False,
                                timeout=3600, check=False)
    except (OSError, subprocess.TimeoutExpired) as error:
        raise ReviewError(f"Could not decode audio for {path.name}: {error}") from error
    if result.returncode != 0:
        detail = result.stderr.decode("utf-8", errors="replace")
        raise ReviewError(f"Audio decode failed for {path.name}: {detail[-MAX_DIAGNOSTIC_TEXT:]}")
    if len(result.stdout) % 4:
        raise ReviewError(f"Audio decode returned a partial float sample for {path.name}.")
    samples = array("f")
    samples.frombytes(result.stdout)
    if sys.byteorder != "little":
        samples.byteswap()
    return samples


def _align_audio(samples, offset_seconds, requested_samples):
    aligned = array("f", [0.0]) * requested_samples
    offset = round(offset_seconds * SAMPLE_RATE)
    source_start = max(0, -offset)
    target_start = max(0, offset)
    count = min(len(samples) - source_start, requested_samples - target_start)
    if count > 0:
        aligned[target_start:target_start + count] = samples[source_start:source_start + count]
    return aligned


def _rms(samples):
    if not samples:
        return {"linear": None, "dBFS": None}
    square_mean = math.fsum(value * value for value in samples) / len(samples)
    linear = math.sqrt(square_mean)
    return {"linear": linear, "dBFS": (20.0 * math.log10(linear) if linear > 0 else None)}


def _pearson(left, right):
    count = min(len(left), len(right))
    if count < 2:
        return None
    mean_left = math.fsum(left[:count]) / count
    mean_right = math.fsum(right[:count]) / count
    covariance = math.fsum((left[index] - mean_left) * (right[index] - mean_right)
                           for index in range(count))
    variance_left = math.fsum((value - mean_left) ** 2 for value in left[:count])
    variance_right = math.fsum((value - mean_right) ** 2 for value in right[:count])
    denominator = math.sqrt(variance_left * variance_right)
    return covariance / denominator if denominator > 0 else None


def _audio_comparison(ffmpeg, native_path, reference_path, native_probe,
                      reference_probe, duration_seconds):
    native_has_audio = bool(native_probe["audio"])
    reference_has_audio = bool(reference_probe["audio"])
    offsets = {
        "nativeAudioStartRelativeToVideoSeconds": _audio_offset(native_probe),
        "referenceAudioStartRelativeToVideoSeconds": _audio_offset(reference_probe),
    }
    if not native_has_audio or not reference_has_audio:
        return {
            "status": "unavailable",
            "reason": "Both files need an audio stream for waveform comparison.",
            **offsets,
        }
    native_offset = offsets["nativeAudioStartRelativeToVideoSeconds"]
    reference_offset = offsets["referenceAudioStartRelativeToVideoSeconds"]
    if native_offset is None or reference_offset is None:
        return {
            "status": "unavailable",
            "reason": "ffprobe did not report both audio and video start times.",
            **offsets,
        }
    requested_samples = max(1, round(duration_seconds * SAMPLE_RATE))
    native_capture_duration = duration_seconds + max(0.0, -native_offset)
    reference_capture_duration = duration_seconds + max(0.0, -reference_offset)
    try:
        native_raw = _decode_audio(ffmpeg, native_path, native_capture_duration)
        reference_raw = _decode_audio(ffmpeg, reference_path, reference_capture_duration)
    except ReviewError as error:
        return {"status": "unavailable", "reason": str(error), **offsets}
    native_aligned = _align_audio(native_raw, native_offset, requested_samples)
    reference_aligned = _align_audio(reference_raw, reference_offset, requested_samples)
    native_rms = _rms(native_aligned)
    reference_rms = _rms(reference_aligned)
    return {
        "status": "diagnostic_available",
        "sampleRateHz": SAMPLE_RATE,
        "analysisDurationSeconds": duration_seconds,
        "alignment": "audio samples placed on each file's video-start timeline using ffprobe stream start times",
        **offsets,
        "nativeMinusReferenceAudioOffsetSeconds": native_offset - reference_offset,
        "nativeRms": native_rms,
        "referenceRms": reference_rms,
        "rmsDifferenceLinear": (native_rms["linear"] - reference_rms["linear"]
                                if native_rms["linear"] is not None and reference_rms["linear"] is not None
                                else None),
        "alignedPearsonCorrelation": _pearson(native_aligned, reference_aligned),
        "correlationQualification": "zero-lag waveform correlation after stream-start alignment; diagnostic only",
        "decodedSamples": {"native": len(native_raw), "reference": len(reference_raw)},
    }


def _canvas_size(reference_probe):
    width = reference_probe["video"]["width"]
    height = reference_probe["video"]["height"]
    if not width or not height:
        raise ReviewError("Reference video dimensions are unavailable.")
    scale = min(1.0, CONTACT_THUMB_MAX_EDGE / max(width, height))
    canvas_width = max(2, int(round(width * scale)) // 2 * 2)
    canvas_height = max(2, int(round(height * scale)) // 2 * 2)
    return canvas_width, canvas_height


def _read_frame(pipe, expected_bytes):
    result = bytearray()
    while len(result) < expected_bytes:
        block = pipe.read(expected_bytes - len(result))
        if not block:
            break
        result.extend(block)
    return bytes(result) if len(result) == expected_bytes else None


def _sample_process(ffmpeg, path, canvas_width, canvas_height, count, stderr_file):
    filters = (
        "setpts=PTS-STARTPTS,fps=1:round=near,"
        f"scale={canvas_width}:{canvas_height}:force_original_aspect_ratio=decrease:flags=lanczos,"
        f"pad={canvas_width}:{canvas_height}:(ow-iw)/2:(oh-ih)/2:color=black,"
        "setsar=1,format=rgb24"
    )
    args = [ffmpeg, "-hide_banner", "-nostdin", "-v", "error", "-i", str(path),
            "-map", "0:v:0", "-vf", filters, "-frames:v", str(count),
            "-fps_mode", "passthrough", "-f", "rawvideo", "-pix_fmt", "rgb24", "pipe:1"]
    try:
        return subprocess.Popen(args, stdout=subprocess.PIPE, stderr=stderr_file)
    except OSError as error:
        raise ReviewError(f"Could not start frame sampler for {path.name}: {error}") from error


def _set_rgb(pixels, width, height, x, y, color):
    if 0 <= x < width and 0 <= y < height:
        offset = (y * width + x) * 3
        pixels[offset:offset + 3] = bytes(color)


def _draw_text(pixels, width, height, x, y, text, color, scale=2):
    cursor = x
    for character in text:
        glyph = FONT.get(character)
        if glyph is None:
            cursor += 4 * scale
            continue
        for row, bits in enumerate(glyph):
            for column, bit in enumerate(bits):
                if bit == "1":
                    for dy in range(scale):
                        for dx in range(scale):
                            _set_rgb(pixels, width, height,
                                     cursor + column * scale + dx,
                                     y + row * scale + dy, color)
        cursor += 4 * scale


def _draw_border(pixels, width, height, x, y, box_width, box_height, color):
    for offset in range(box_width):
        _set_rgb(pixels, width, height, x + offset, y, color)
        _set_rgb(pixels, width, height, x + offset, y + box_height - 1, color)
    for offset in range(box_height):
        _set_rgb(pixels, width, height, x, y + offset, color)
        _set_rgb(pixels, width, height, x + box_width - 1, y + offset, color)


def _png_chunk(tag, payload):
    return (struct.pack(">I", len(payload)) + tag + payload +
            struct.pack(">I", zlib.crc32(tag + payload) & 0xFFFFFFFF))


def _write_png_rgb(path, width, height, pixels):
    row_bytes = width * 3
    if len(pixels) != row_bytes * height:
        raise ReviewError("Internal contact sheet dimensions do not match pixel buffer size.")
    compressor = zlib.compressobj(level=6)
    with Path(path).open("xb") as output:
        output.write(b"\x89PNG\r\n\x1a\n")
        output.write(_png_chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)))
        for row in range(height):
            start = row * row_bytes
            encoded = compressor.compress(b"\x00" + pixels[start:start + row_bytes])
            if encoded:
                output.write(_png_chunk(b"IDAT", encoded))
        final = compressor.flush()
        if final:
            output.write(_png_chunk(b"IDAT", final))
        output.write(_png_chunk(b"IEND", b""))


def _format_time(seconds):
    whole = max(0, int(seconds))
    return f"{whole // 60:02d}:{whole % 60:02d}"


def _copy_rgb_frame(sheet, sheet_width, cell_x, image_y, frame, frame_width, frame_height):
    row_bytes = frame_width * 3
    for row in range(frame_height):
        source_start = row * row_bytes
        target_start = ((image_y + row) * sheet_width + cell_x) * 3
        sheet[target_start:target_start + row_bytes] = frame[source_start:source_start + row_bytes]


def _contact_sheets(ffmpeg, native_path, reference_path, duration_seconds,
                    canvas_width, canvas_height, evidence_dir):
    sample_count = max(1, math.ceil(duration_seconds - 1e-9))
    frame_bytes = canvas_width * canvas_height * 3
    cell_gap = 8
    header_height = 24
    cell_width = canvas_width * 2 + cell_gap
    cell_height = canvas_height + header_height
    sheet_width = CONTACT_COLUMNS * cell_width
    sheet_height = CONTACT_ROWS * cell_height
    sheet_capacity = CONTACT_COLUMNS * CONTACT_ROWS
    file_records = []
    extracted = {"native": 0, "reference": 0}
    missing = {"native": 0, "reference": 0}

    with open(evidence_dir / "contact-native-ffmpeg.log", "w", encoding="utf-8") as native_log, \
            open(evidence_dir / "contact-reference-ffmpeg.log", "w", encoding="utf-8") as reference_log:
        native_process = None
        reference_process = None
        sheet = bytearray(sheet_width * sheet_height * 3)
        sheet_number = 0
        in_sheet = 0

        def flush_sheet(count):
            nonlocal sheet, sheet_number, in_sheet
            if count == 0:
                return
            sheet_number += 1
            first_index = sheet_number * sheet_capacity - sheet_capacity
            first_time = _format_time(first_index)
            last_time = _format_time(first_index + count - 1)
            filename = f"contact-{sheet_number:03d}-{first_time.replace(':', '')}-{last_time.replace(':', '')}.png"
            output_path = evidence_dir / filename
            _write_png_rgb(output_path, sheet_width, sheet_height, sheet)
            file_records.append({
                "path": filename,
                "firstTimestampSeconds": first_index,
                "lastTimestampSeconds": first_index + count - 1,
                "samplePairs": count,
            })
            sheet = bytearray(sheet_width * sheet_height * 3)
            in_sheet = 0

        try:
            native_process = _sample_process(ffmpeg, native_path, canvas_width, canvas_height,
                                             sample_count, native_log)
            reference_process = _sample_process(ffmpeg, reference_path, canvas_width, canvas_height,
                                                sample_count, reference_log)
            for sample_index in range(sample_count):
                native_frame = _read_frame(native_process.stdout, frame_bytes)
                reference_frame = _read_frame(reference_process.stdout, frame_bytes)
                if native_frame is not None:
                    extracted["native"] += 1
                else:
                    missing["native"] += 1
                if reference_frame is not None:
                    extracted["reference"] += 1
                else:
                    missing["reference"] += 1

                cell_row, cell_column = divmod(in_sheet, CONTACT_COLUMNS)
                cell_x = cell_column * cell_width
                cell_y = cell_row * cell_height
                timestamp = _format_time(sample_index)
                _draw_text(sheet, sheet_width, sheet_height, cell_x + 6, cell_y + 5,
                           timestamp, (255, 226, 80))
                _draw_text(sheet, sheet_width, sheet_height, cell_x + 62, cell_y + 5,
                           "REF", (255, 255, 255))
                _draw_text(sheet, sheet_width, sheet_height,
                           cell_x + canvas_width + cell_gap + 6, cell_y + 5,
                           "NATIVE", (255, 255, 255))
                image_y = cell_y + header_height
                if reference_frame is not None:
                    _copy_rgb_frame(sheet, sheet_width, cell_x, image_y,
                                    reference_frame, canvas_width, canvas_height)
                else:
                    _draw_border(sheet, sheet_width, sheet_height, cell_x, image_y,
                                 canvas_width, canvas_height, (230, 40, 40))
                native_x = cell_x + canvas_width + cell_gap
                if native_frame is not None:
                    _copy_rgb_frame(sheet, sheet_width, native_x, image_y,
                                    native_frame, canvas_width, canvas_height)
                else:
                    _draw_border(sheet, sheet_width, sheet_height, native_x, image_y,
                                 canvas_width, canvas_height, (230, 40, 40))

                in_sheet += 1
                if in_sheet == sheet_capacity:
                    flush_sheet(in_sheet)
            flush_sheet(in_sheet)
        finally:
            for process in (native_process, reference_process):
                if process is not None and process.stdout:
                    process.stdout.close()
            for process in (native_process, reference_process):
                if process is None:
                    continue
                try:
                    process.wait(timeout=120)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()

    return {
        "status": "generated" if file_records else "unavailable",
        "sampling": "one sample per second, starting at each video's first frame",
        "comparisonBasis": "left panel is reference; right panel is native; black padding preserves reference aspect ratio",
        "canvasWidth": canvas_width,
        "canvasHeight": canvas_height,
        "requestedSamplePairs": sample_count,
        "extractedFrameCounts": extracted,
        "missingFrameCounts": missing,
        "sheets": file_records,
        "ffmpegLogs": ["contact-native-ffmpeg.log", "contact-reference-ffmpeg.log"],
    }


def _ssim(ffmpeg, native_path, reference_path, duration_seconds,
          canvas_width, canvas_height, evidence_dir):
    filters = (
        f"trim=duration={duration_seconds:.6f},setpts=PTS-STARTPTS,fps=1:round=near,"
        f"scale={canvas_width}:{canvas_height}:force_original_aspect_ratio=decrease:flags=lanczos,"
        f"pad={canvas_width}:{canvas_height}:(ow-iw)/2:(oh-ih)/2:color=black,"
        "setsar=1,format=yuv420p"
    )
    graph = f"[0:v]{filters}[reference];[1:v]{filters}[native];[reference][native]ssim=stats_file=ssim-raw.log"
    args = [ffmpeg, "-hide_banner", "-nostdin", "-v", "error", "-i", str(reference_path),
            "-i", str(native_path), "-filter_complex", graph, "-an", "-f", "null", "-"]
    result = _run(args, cwd=evidence_dir, timeout=7200)
    if result.returncode != 0:
        detail = result.stderr[-MAX_DIAGNOSTIC_TEXT:]
        return {"status": "unavailable", "reason": detail or "FFmpeg SSIM graph failed."}

    raw_path = evidence_dir / "ssim-raw.log"
    try:
        raw_text = raw_path.read_text(errors="replace")
    except OSError:
        return {"status": "unavailable", "reason": "FFmpeg completed without writing per-frame SSIM stats."}
    pattern = re.compile(
        r"n:(\d+)\s+Y:([0-9.]+)\s+U:([0-9.]+)\s+V:([0-9.]+)\s+All:([0-9.]+)"
    )
    measurements = []
    for match in pattern.finditer(raw_text):
        frame_number = int(match.group(1))
        measurements.append({
            "sampleIndex": frame_number,
            "timestampSeconds": float(frame_number - 1),
            "Y": float(match.group(2)),
            "U": float(match.group(3)),
            "V": float(match.group(4)),
            "All": float(match.group(5)),
        })
    if not measurements:
        return {"status": "unavailable", "reason": "No per-second SSIM rows were parsed from FFmpeg."}
    csv_path = evidence_dir / "ssim-per-second.csv"
    with csv_path.open("x", newline="", encoding="utf-8") as output:
        writer = csv.DictWriter(output, fieldnames=("sampleIndex", "timestampSeconds", "Y", "U", "V", "All"))
        writer.writeheader()
        writer.writerows(measurements)
    return {
        "status": "diagnostic_available",
        "sampleCount": len(measurements),
        "meanAll": math.fsum(row["All"] for row in measurements) / len(measurements),
        "csv": csv_path.name,
        "rawStats": raw_path.name,
        "qualification": "per-second luma/chroma SSIM after scale-and-pad; diagnostic only, not a visual acceptance threshold",
    }


def _comparison(native_probe, reference_probe, expected_seconds, window_seconds):
    nv = native_probe["video"]
    rv = reference_probe["video"]
    na = native_probe["audio"][0] if native_probe["audio"] else None
    ra = reference_probe["audio"][0] if reference_probe["audio"] else None
    native_video_start = nv["startTimeSeconds"]
    reference_video_start = rv["startTimeSeconds"]
    native_audio_offset = _audio_offset(native_probe)
    reference_audio_offset = _audio_offset(reference_probe)
    native_duration = nv["durationSeconds"]
    reference_duration = rv["durationSeconds"]
    common_duration = min(native_duration, reference_duration)
    rate = max(nv["frameRateHz"] or 0, rv["frameRateHz"] or 0)
    tolerance = 1.0 / rate if rate > 0 else 0.0
    native_expected_frames = (round(expected_seconds * nv["frameRateHz"])
                              if nv["frameRateHz"] is not None else None)
    reference_expected_frames = (round(expected_seconds * rv["frameRateHz"])
                                 if rv["frameRateHz"] is not None else None)
    covers_duration = common_duration + tolerance >= expected_seconds
    covers_frame_counts = (
        native_expected_frames is not None and reference_expected_frames is not None and
        nv["frameCount"] is not None and rv["frameCount"] is not None and
        nv["frameCountSource"] in {"nb_read_frames", "nb_frames"} and
        rv["frameCountSource"] in {"nb_read_frames", "nb_frames"} and
        nv["frameCount"] >= native_expected_frames and rv["frameCount"] >= reference_expected_frames
    )
    return {
        "scope": {
            "expectedSeconds": expected_seconds,
            "reviewWindowSeconds": window_seconds,
            "availableCommonVideoSeconds": common_duration,
            "coversRequestedScope": covers_duration and covers_frame_counts,
            "coversDuration": covers_duration,
            "coversExpectedFrameCounts": covers_frame_counts,
            "expectedFrameCounts": {"native": native_expected_frames,
                                    "reference": reference_expected_frames},
            "timestampBasis": "seconds from each video's first decoded frame",
        },
        "video": {
            "durationDifferenceNativeMinusReferenceSeconds": native_duration - reference_duration,
            "containerDurationDifferenceSeconds": (
                native_probe["container"]["durationSeconds"] - reference_probe["container"]["durationSeconds"]
                if native_probe["container"]["durationSeconds"] is not None and reference_probe["container"]["durationSeconds"] is not None
                else None),
            "frameRateEqual": (nv["frameRateHz"] == rv["frameRateHz"]
                               if nv["frameRateHz"] is not None and rv["frameRateHz"] is not None
                               else None),
            "frameRateDifferenceHz": (nv["frameRateHz"] - rv["frameRateHz"]
                                      if nv["frameRateHz"] is not None and rv["frameRateHz"] is not None
                                      else None),
            "frameCountDifferenceNativeMinusReference": (nv["frameCount"] - rv["frameCount"]
                                                          if nv["frameCount"] is not None and rv["frameCount"] is not None
                                                          else None),
            "dimensionsEqual": ((nv["width"], nv["height"]) == (rv["width"], rv["height"])
                                if None not in (nv["width"], nv["height"], rv["width"], rv["height"])
                                else None),
            "nativeDimensions": [nv["width"], nv["height"]],
            "referenceDimensions": [rv["width"], rv["height"]],
            "videoStartDifferenceNativeMinusReferenceSeconds": (
                native_video_start - reference_video_start
                if native_video_start is not None and reference_video_start is not None else None),
            "colorMetadataComparison": {
                key: {"native": nv[key], "reference": rv[key],
                      "equal": (nv[key] == rv[key]
                                if nv[key] is not None and rv[key] is not None else None)}
                for key in ("colorRange", "colorSpace", "colorTransfer", "colorPrimaries")
            },
        },
        "audio": {
            "nativeStreamPresent": na is not None,
            "referenceStreamPresent": ra is not None,
            "nativeAudioVideoOffsetSeconds": native_audio_offset,
            "referenceAudioVideoOffsetSeconds": reference_audio_offset,
            "audioVideoOffsetDifferenceSeconds": (
                native_audio_offset - reference_audio_offset
                if native_audio_offset is not None and reference_audio_offset is not None else None),
            "durationDifferenceNativeMinusReferenceSeconds": (
                na["durationSeconds"] - ra["durationSeconds"]
                if na and ra and na["durationSeconds"] is not None and ra["durationSeconds"] is not None
                else None),
        },
    }


def _write_json_new(path, value):
    payload = (json.dumps(value, indent=2, ensure_ascii=False, allow_nan=False) + "\n").encode("utf-8")
    flags = os.O_WRONLY | os.O_CREAT | os.O_EXCL
    fd = os.open(path, flags, 0o600)
    with os.fdopen(fd, "wb") as output:
        output.write(payload)
        output.flush()
        os.fsync(output.fileno())


def review(native_path, reference_path, evidence_dir, scope, ffmpeg="ffmpeg", ffprobe="ffprobe"):
    native_path, _ = _canonical_input(native_path, "Native movie")
    reference_path, _ = _canonical_input(reference_path, "Reference movie")
    evidence_dir = _new_evidence_directory(evidence_dir)
    ffmpeg_path = _require_tool(ffmpeg)
    ffprobe_path = _require_tool(ffprobe)
    expected_seconds = 10.0 if scope == "prefix10" else 298.0

    native_probe = _probe(ffprobe_path, native_path)
    reference_probe = _probe(ffprobe_path, reference_path)
    native_video_duration = native_probe["video"]["durationSeconds"]
    reference_video_duration = reference_probe["video"]["durationSeconds"]
    if native_video_duration is None or reference_video_duration is None:
        raise ReviewError("Both movies need a reported video-stream duration.")
    window_seconds = min(expected_seconds, native_video_duration, reference_video_duration)
    if window_seconds <= 0:
        raise ReviewError("The movies have no shared positive-duration video range.")

    # The evidence directory is created only after paths and stream metadata are valid.
    try:
        evidence_dir.mkdir(mode=0o700, parents=False, exist_ok=False)
    except OSError as error:
        raise ReviewError(f"Could not create a new evidence directory: {error}") from error

    initial = {
        "schema": SCHEMA,
        "status": "in_progress",
        "createdAtUtc": _utc_now(),
        "scope": scope,
        "expectedSeconds": expected_seconds,
        "native": {"file": _file_snapshot(native_path), "probe": native_probe},
        "reference": {"file": _file_snapshot(reference_path), "probe": reference_probe},
        "comparison": _comparison(native_probe, reference_probe, expected_seconds, window_seconds),
        "fullDecode": {},
        "audioSignalComparison": None,
        "contactSheets": None,
        "perSecondSsim": None,
        "reviewQualification": {
            "automatedVisualApproval": False,
            "creativeApproval": "not_assessed",
            "humanReviewRequired": True,
            "note": "Technical metadata, decode, contact sheets, RMS/correlation, and SSIM are diagnostics; they do not establish visual fidelity or completion.",
        },
    }
    report_path = evidence_dir / "review-report.json"
    _write_json_new(report_path, initial)
    failures = []
    try:
        initial["fullDecode"] = {
            "native": _decode_all(ffmpeg_path, native_path),
            "reference": _decode_all(ffmpeg_path, reference_path),
            "scope": "entire input files, independent of prefix/full comparison window",
        }
        for name, result in initial["fullDecode"].items():
            if isinstance(result, dict) and result.get("status") != "passed":
                failures.append(f"Full decode failed for {name} input.")
        initial["audioSignalComparison"] = _audio_comparison(
            ffmpeg_path, native_path, reference_path, native_probe, reference_probe, window_seconds)
        canvas_width, canvas_height = _canvas_size(reference_probe)
        initial["contactSheets"] = _contact_sheets(
            ffmpeg_path, native_path, reference_path, window_seconds,
            canvas_width, canvas_height, evidence_dir)
        if any(initial["contactSheets"]["missingFrameCounts"].values()):
            failures.append("Contact-sheet sampling could not extract every requested frame pair.")
        initial["perSecondSsim"] = _ssim(
            ffmpeg_path, native_path, reference_path, window_seconds,
            canvas_width, canvas_height, evidence_dir)
    except (ReviewError, OSError, ValueError) as error:
        failures.append(str(error))
    finally:
        try:
            native_after = _file_snapshot(native_path)
            reference_after = _file_snapshot(reference_path)
            initial["inputIntegrityAfterReview"] = {
                "nativeUnchanged": _same_file_snapshot(initial["native"]["file"], native_after),
                "referenceUnchanged": _same_file_snapshot(initial["reference"]["file"], reference_after),
                "nativeAfter": native_after,
                "referenceAfter": reference_after,
            }
        except OSError as error:
            initial["inputIntegrityAfterReview"] = {"status": "unavailable", "reason": str(error)}
            failures.append(f"Could not recheck input files after review: {error}")
        initial["errors"] = failures
        initial["status"] = "artifacts_generated_with_errors" if failures else "artifacts_generated"
        initial["completedAtUtc"] = _utc_now()
        _write_report_replacement(report_path, initial)
    return initial


def _write_report_replacement(path, value):
    payload = (json.dumps(value, indent=2, ensure_ascii=False, allow_nan=False) + "\n").encode("utf-8")
    temporary = path.with_suffix(".json.tmp")
    with temporary.open("xb") as output:
        output.write(payload)
        output.flush()
        os.fsync(output.fileno())
    os.replace(temporary, path)


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--native", required=True, type=Path, help="Exact completed native movie file.")
    parser.add_argument("--reference", required=True, type=Path, help="Exact reference movie file.")
    parser.add_argument("--evidence-dir", required=True, type=Path,
                        help="New, nonexistent directory for all review artifacts.")
    parser.add_argument("--scope", choices=("prefix10", "full298"), required=True,
                        help="Review a 10-second prefix or the full 298-second target window.")
    parser.add_argument("--ffmpeg", default="ffmpeg")
    parser.add_argument("--ffprobe", default="ffprobe")
    args = parser.parse_args(argv)
    try:
        report = review(args.native, args.reference, args.evidence_dir,
                        args.scope, args.ffmpeg, args.ffprobe)
    except ReviewError as error:
        print(f"native export review refused: {error}", file=sys.stderr)
        return 2
    print(json.dumps({
        "status": report["status"],
        "scope": report["scope"],
        "coversRequestedScope": report["comparison"]["scope"]["coversRequestedScope"],
        "evidenceDirectory": str(args.evidence_dir),
        "report": str(Path(args.evidence_dir) / "review-report.json"),
        "humanReviewRequired": True,
        "automatedVisualApproval": False,
    }, indent=2))
    return 1 if (report["errors"] or
                 not report["comparison"]["scope"]["coversRequestedScope"]) else 0


if __name__ == "__main__":
    raise SystemExit(main())
