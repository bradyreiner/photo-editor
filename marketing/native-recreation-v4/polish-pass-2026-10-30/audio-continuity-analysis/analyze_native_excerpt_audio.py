#!/usr/bin/env python3
"""Measure source-bed alignment and AAC/container timing for a native excerpt.

No listening or native-app access is performed. Requires ffmpeg, ffprobe, and
NumPy from marketing/.venv. See README.md for the P11 invocation.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import shutil
import subprocess
import sys
import tempfile
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

import numpy as np


HERE = Path(__file__).resolve().parent
POLISH_ROOT = HERE.parent
DEFAULT_SOURCE = (
    POLISH_ROOT
    / "music-candidates/downloads/public-candidates/HoliznaCC0 - New Gods.mp3"
)
DEFAULT_BASELINE = (
    POLISH_ROOT
    / "native-intro-new-gods-pass10/looksy-intro-new-gods-0-8.mp4"
)
DEFAULT_BASELINE_DIAGNOSTIC = (
    POLISH_ROOT
    / "native-intro-new-gods-pass10/audio-source-continuity-diagnostic.json"
)
WINDOWS_SECONDS = ((1.0, 2.0), (4.0, 5.0), (5.0, 6.0), (6.0, 7.0))


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def run_json(command: list[str]) -> dict[str, Any]:
    result = subprocess.run(
        command,
        check=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    return json.loads(result.stdout)


def ffprobe(path: Path) -> dict[str, Any]:
    return run_json(
        [
            "ffprobe",
            "-v",
            "error",
            "-show_streams",
            "-show_format",
            "-count_frames",
            "-of",
            "json",
            str(path),
        ]
    )


def stream_of_type(probe: dict[str, Any], stream_type: str) -> dict[str, Any] | None:
    return next(
        (stream for stream in probe.get("streams", []) if stream.get("codec_type") == stream_type),
        None,
    )


def decode_f32le(path: Path, sample_rate: int, channels: int) -> tuple[np.ndarray, str]:
    """Decode one audio stream to a temporary raw PCM file and memory map it."""
    with tempfile.TemporaryDirectory(prefix="looksy-audio-continuity-") as temp_dir:
        raw_path = Path(temp_dir) / "decoded.f32le"
        subprocess.run(
            [
                "ffmpeg",
                "-hide_banner",
                "-v",
                "error",
                "-y",
                "-i",
                str(path),
                "-map",
                "0:a:0",
                "-vn",
                "-sn",
                "-dn",
                "-ac",
                str(channels),
                "-ar",
                str(sample_rate),
                "-f",
                "f32le",
                str(raw_path),
            ],
            check=True,
            stdout=subprocess.DEVNULL,
            stderr=subprocess.PIPE,
            text=True,
        )
        byte_count = raw_path.stat().st_size
        frame_bytes = channels * np.dtype("<f4").itemsize
        if byte_count == 0 or byte_count % frame_bytes:
            raise RuntimeError(
                f"Decoded PCM size {byte_count} is not a nonempty {channels}-channel frame sequence."
            )
        digest = sha256_file(raw_path)
        samples = np.memmap(raw_path, dtype="<f4", mode="r", shape=(byte_count // 4,))
        samples = samples.reshape((-1, channels))
        # The returned view is copied before TemporaryDirectory cleanup.
        return np.asarray(samples, dtype=np.float32).copy(), digest


def bounded_lag_correlation(
    reference: np.ndarray,
    measured: np.ndarray,
    reference_start: int,
    measured_start: int,
    frame_count: int,
    max_lag: int,
    sample_rate: int,
) -> dict[str, Any]:
    """Find the source offset maximizing centered stereo correlation.

    Sign convention matches the earlier pass-10 diagnostic: lag L compares
    measured[n] with reference[n + L]. A negative lag means the excerpt audio
    is delayed relative to the requested source position.
    """
    if reference.ndim != 2 or measured.ndim != 2 or reference.shape[1] != measured.shape[1]:
        raise ValueError("PCM arrays must both be frame-major stereo arrays.")
    left = reference_start - max_lag
    right = reference_start + frame_count + max_lag
    output_right = measured_start + frame_count
    if left < 0 or right > len(reference) or measured_start < 0 or output_right > len(measured):
        raise ValueError("Selected analysis window exceeds decoded PCM bounds.")

    x = np.asarray(reference[left:right], dtype=np.float64)
    y = np.asarray(measured[measured_start:output_right], dtype=np.float64)
    if not np.isfinite(x).all() or not np.isfinite(y).all():
        raise ValueError("Decoded PCM contains non-finite samples.")

    # A single FFT computes correlation for all 2*max_lag+1 candidate offsets.
    fft_size = 1 << (len(x) + len(y) - 2).bit_length()
    correlations: list[np.ndarray] = []
    channel_metrics: list[dict[str, Any]] = []
    for channel in range(y.shape[1]):
        source_channel = x[:, channel]
        excerpt_channel = y[:, channel]
        cross = np.fft.irfft(
            np.fft.rfft(source_channel, n=fft_size)
            * np.conjugate(np.fft.rfft(excerpt_channel, n=fft_size)),
            n=fft_size,
        )
        # Correlation index k compares source[k:k+N] with excerpt[0:N].
        candidate_cross = cross[: 2 * max_lag + 1]
        source_sum_prefix = np.concatenate(([0.0], np.cumsum(source_channel)))
        source_square_prefix = np.concatenate(
            ([0.0], np.cumsum(source_channel * source_channel))
        )
        starts = np.arange(0, 2 * max_lag + 1, dtype=np.int64)
        source_sum = source_sum_prefix[starts + frame_count] - source_sum_prefix[starts]
        source_square = (
            source_square_prefix[starts + frame_count] - source_square_prefix[starts]
        )
        excerpt_sum = float(np.sum(excerpt_channel))
        excerpt_square = float(np.dot(excerpt_channel, excerpt_channel))
        covariance = candidate_cross - (source_sum * excerpt_sum / frame_count)
        source_variance = np.maximum(source_square - source_sum * source_sum / frame_count, 0.0)
        excerpt_variance = max(excerpt_square - excerpt_sum * excerpt_sum / frame_count, 0.0)
        denominator = np.sqrt(source_variance * excerpt_variance)
        correlation = np.divide(
            covariance,
            denominator,
            out=np.zeros_like(covariance),
            where=denominator > 1e-24,
        )
        correlations.append(correlation)
        own_index = int(np.argmax(correlation))
        own_lag = own_index - max_lag
        own_var = float(source_variance[own_index])
        own_cov = float(covariance[own_index])
        own_gain = own_cov / own_var if own_var > 1e-24 else 0.0
        channel_metrics.append(
            {
                "channel": channel,
                "bestSourceOffsetSamples": own_lag,
                "bestSourceOffsetMilliseconds": own_lag * 1000.0 / sample_rate,
                "alignedCorrelation": float(correlation[own_index]),
                "measuredGainDb": (
                    20.0 * math.log10(abs(own_gain)) if abs(own_gain) > 1e-24 else None
                ),
                "gainLinear": own_gain,
            }
        )

    # Channel-normalized scores prevent a louder side from dominating the lag.
    overall_scores = np.mean(np.stack(correlations, axis=0), axis=0)
    best_index = int(np.argmax(overall_scores))
    best_lag = best_index - max_lag

    # Estimate gain across both channels at the selected shared lag.
    reference_slice = np.asarray(
        reference[reference_start + best_lag : reference_start + best_lag + frame_count],
        dtype=np.float64,
    )
    measured_slice = y
    reference_centered = reference_slice - np.mean(reference_slice, axis=0, keepdims=True)
    measured_centered = measured_slice - np.mean(measured_slice, axis=0, keepdims=True)
    source_energy = float(np.sum(reference_centered * reference_centered))
    cross_energy = float(np.sum(reference_centered * measured_centered))
    gain = cross_energy / source_energy if source_energy > 1e-24 else 0.0
    gain_db = 20.0 * math.log10(abs(gain)) if abs(gain) > 1e-24 else None
    sorted_scores = np.partition(overall_scores, -2)
    return {
        "bestSourceOffsetSamples": best_lag,
        "bestSourceOffsetMilliseconds": best_lag * 1000.0 / sample_rate,
        "alignedCorrelation": float(overall_scores[best_index]),
        "measuredGainDb": gain_db,
        "gainLinear": gain,
        "secondBestCorrelation": float(sorted_scores[-2]),
        "channels": channel_metrics,
    }


def analyze_video(
    video_path: Path,
    source_pcm: np.ndarray,
    source_probe: dict[str, Any],
    sample_rate: int,
    trim_start_seconds: float,
    expected_gain_db: float,
    windows: tuple[tuple[float, float], ...],
    max_lag_samples: int,
) -> dict[str, Any]:
    video_probe = ffprobe(video_path)
    audio_stream = stream_of_type(video_probe, "audio")
    video_stream = stream_of_type(video_probe, "video")
    if audio_stream is None or video_stream is None:
        raise RuntimeError(f"{video_path} must contain both video and audio streams.")

    measured_pcm, decoded_pcm_sha = decode_f32le(video_path, sample_rate, 2)
    source_stream = stream_of_type(source_probe, "audio")
    measured_audio_start = float(audio_stream.get("start_time", 0.0) or 0.0)
    results = []
    lag_ok = True
    gain_ok = True
    for window_start, window_end in windows:
        count = int(round((window_end - window_start) * sample_rate))
        # Decoded PCM sample zero corresponds to each audio stream's reported start.
        # FFmpeg's complete raw decode is indexed from source media time zero;
        # MP3 stream start_time is reported separately and must not be
        # subtracted from clip trim seconds. Reproduction against Pass 10's
        # known -790-sample artifact confirms this mapping.
        source_start = int(round((trim_start_seconds + window_start) * sample_rate))
        measured_start = int(round((window_start - measured_audio_start) * sample_rate))
        measurement = bounded_lag_correlation(
            source_pcm,
            measured_pcm,
            source_start,
            measured_start,
            count,
            max_lag_samples,
            sample_rate,
        )
        measured_gain_db = measurement["measuredGainDb"]
        gain_delta_db = (
            measured_gain_db - expected_gain_db if measured_gain_db is not None else None
        )
        lag_ok = lag_ok and abs(measurement["bestSourceOffsetSamples"]) <= 1
        gain_ok = gain_ok and gain_delta_db is not None and abs(gain_delta_db) <= 0.1
        results.append(
            {
                "sequenceWindowSeconds": [window_start, window_end],
                "referenceSourceWindowSeconds": [
                    trim_start_seconds + window_start,
                    trim_start_seconds + window_end,
                ],
                **measurement,
                "expectedGainDb": expected_gain_db,
                "gainErrorDb": gain_delta_db,
            }
        )

    fmt = video_probe.get("format", {})
    container_duration = float(fmt.get("duration", 0.0) or 0.0)
    video_duration = float(video_stream.get("duration", 0.0) or 0.0)
    audio_duration = float(audio_stream.get("duration", 0.0) or 0.0)
    video_start = float(video_stream.get("start_time", 0.0) or 0.0)
    av_start_skew = measured_audio_start - video_start
    frame_rate = video_stream.get("avg_frame_rate", "0/0")
    try:
        numerator, denominator = (int(value) for value in frame_rate.split("/", 1))
        frame_rate_value = numerator / denominator if denominator else None
    except (AttributeError, ValueError, ZeroDivisionError):
        frame_rate_value = None
    audio_stream_rate = int(audio_stream.get("sample_rate", 0) or 0)
    audio_channels = int(audio_stream.get("channels", 0) or 0)
    expected_frames = int(round(video_duration * (frame_rate_value or 0.0)))
    try:
        video_frame_count = int(video_stream.get("nb_read_frames", video_stream.get("nb_frames")) or 0)
    except (TypeError, ValueError):
        video_frame_count = 0
    container_ok = (
        audio_stream.get("codec_name") == "aac"
        and video_stream.get("codec_name") == "h264"
        and audio_channels == 2
        and audio_stream_rate == sample_rate
        and abs(container_duration - 8.0) <= 0.02
        and abs(video_duration - 8.0) <= 0.02
        and abs(audio_duration - 8.0) <= 0.02
        and expected_frames == 240
        and video_frame_count == 240
        and abs(av_start_skew) <= 1.0 / sample_rate
    )
    return {
        "path": str(video_path),
        "sha256": sha256_file(video_path),
        "fileBytes": video_path.stat().st_size,
        "decodedAudioPcmSha256": decoded_pcm_sha,
        "decodedPcmSampleRate": sample_rate,
        "decodedPcmChannels": 2,
        "decodedPcmSamplesPerChannel": int(len(measured_pcm)),
        "decodedPcmDurationSeconds": len(measured_pcm) / sample_rate,
        "audioStartTimeSeconds": measured_audio_start,
        "videoStartTimeSeconds": video_start,
        "avStartSkewSeconds": av_start_skew,
        "videoDurationSeconds": video_duration,
        "audioDurationSeconds": audio_duration,
        "containerDurationSeconds": container_duration,
        "videoFrameRate": frame_rate,
        "videoFrameRateDecimal": frame_rate_value,
        "videoFrameCount": video_frame_count,
        "expectedVideoFrameCountFromDuration": expected_frames,
        "audioCodec": audio_stream.get("codec_name"),
        "audioSampleRate": audio_stream_rate,
        "audioChannels": audio_channels,
        "videoCodec": video_stream.get("codec_name"),
        "videoWidth": video_stream.get("width"),
        "videoHeight": video_stream.get("height"),
        "probe": video_probe,
        "measurements": results,
        "checks": {
            "allWindowLagsWithinPlusMinusOneSample": lag_ok,
            "allWindowGainWithinPlusMinus0_1Db": gain_ok,
            "h264Aac48kStereo30fps240Frames8sAndAlignedStarts": container_ok,
        },
        "pass": lag_ok and gain_ok and container_ok,
    }


def parse_windows(value: str) -> tuple[tuple[float, float], ...]:
    windows = []
    for entry in value.split(","):
        start_text, separator, end_text = entry.strip().partition("-")
        if not separator:
            raise argparse.ArgumentTypeError(f"Invalid window {entry!r}; expected start-end.")
        try:
            start, end = float(start_text), float(end_text)
        except ValueError as error:
            raise argparse.ArgumentTypeError(f"Invalid window {entry!r}.") from error
        if not (math.isfinite(start) and math.isfinite(end) and end > start >= 0):
            raise argparse.ArgumentTypeError(f"Invalid window bounds {entry!r}.")
        windows.append((start, end))
    return tuple(windows)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("video", type=Path, help="Completed native H.264/AAC excerpt MP4.")
    parser.add_argument("--source", type=Path, default=DEFAULT_SOURCE)
    parser.add_argument("--baseline-video", type=Path, default=DEFAULT_BASELINE)
    parser.add_argument("--baseline-diagnostic", type=Path, default=DEFAULT_BASELINE_DIAGNOSTIC)
    parser.add_argument("--report", type=Path, required=True)
    parser.add_argument("--native-request-id")
    parser.add_argument("--native-job-id")
    parser.add_argument("--native-document-hash")
    parser.add_argument("--trim-start-seconds", type=float, default=17.828)
    parser.add_argument("--expected-gain-db", type=float, default=-3.7)
    parser.add_argument("--sample-rate", type=int, default=48000)
    parser.add_argument("--max-lag-samples", type=int, default=1500)
    parser.add_argument(
        "--windows",
        type=parse_windows,
        default=WINDOWS_SECONDS,
        help="Comma-separated sequence windows, e.g. 1-2,4-5,5-6,6-7.",
    )
    args = parser.parse_args()

    if shutil.which("ffmpeg") is None or shutil.which("ffprobe") is None:
        parser.error("ffmpeg and ffprobe must be available on PATH.")
    if not args.video.is_file():
        parser.error(f"Excerpt does not exist: {args.video}")
    if not args.source.is_file():
        parser.error(f"Source audio does not exist: {args.source}")
    if args.sample_rate <= 0 or args.max_lag_samples < 0:
        parser.error("sample rate must be positive and max lag must be nonnegative")

    source_probe = ffprobe(args.source)
    source_stream = stream_of_type(source_probe, "audio")
    if source_stream is None:
        parser.error("Source file has no audio stream.")
    source_pcm, source_pcm_sha = decode_f32le(args.source, args.sample_rate, 2)
    source_index = int(round(args.trim_start_seconds * args.sample_rate))
    needed_end = max(end for _, end in args.windows)
    if source_index - args.max_lag_samples < 0 or source_index + int(round(needed_end * args.sample_rate)) + args.max_lag_samples > len(source_pcm):
        parser.error("Source PCM does not cover the configured trim, windows, and lag margin.")

    primary = analyze_video(
        args.video,
        source_pcm,
        source_probe,
        args.sample_rate,
        args.trim_start_seconds,
        args.expected_gain_db,
        args.windows,
        args.max_lag_samples,
    )
    baseline = None
    baseline_path = args.baseline_video
    if baseline_path and baseline_path.is_file() and baseline_path.resolve() != args.video.resolve():
        baseline = analyze_video(
            baseline_path,
            source_pcm,
            source_probe,
            args.sample_rate,
            args.trim_start_seconds,
            args.expected_gain_db,
            args.windows,
            args.max_lag_samples,
        )

    prior_diagnostic = None
    if args.baseline_diagnostic and args.baseline_diagnostic.is_file():
        prior_diagnostic = json.loads(args.baseline_diagnostic.read_text())
    report = {
        "schema": "looksy.native-excerpt-audio-continuity-analysis/v1",
        "analyzedAtUtc": datetime.now(timezone.utc).isoformat(),
        "analysisPerformed": "offline decoded PCM correlation and ffprobe only",
        "audioListeningPerformed": False,
        "humanListeningVerdict": None,
        "nativeRunContext": {
            "requestId": args.native_request_id,
            "jobId": args.native_job_id,
            "documentHash": args.native_document_hash,
        },
        "parameters": {
            "sampleRate": args.sample_rate,
            "channels": 2,
            "sourceTrimStartSeconds": args.trim_start_seconds,
            "sourceSampleIndexMapping": "FFmpeg decoded PCM is indexed from media time zero; the ffprobe stream start_time is reported separately, not subtracted from the authored clip trim.",
            "expectedGainDb": args.expected_gain_db,
            "maxLagSamples": args.max_lag_samples,
            "windowsSeconds": [list(window) for window in args.windows],
            "lagConvention": "L compares excerpt[n] against source[n+L]; negative L means excerpt audio is delayed.",
            "lagPassToleranceSamples": 1,
            "gainPassToleranceDb": 0.1,
        },
        "source": {
            "path": str(args.source),
            "sha256": sha256_file(args.source),
            "fileBytes": args.source.stat().st_size,
            "decodedPcmSha256": source_pcm_sha,
            "decodedPcmSamplesPerChannel": int(len(source_pcm)),
            "decodedPcmDurationSeconds": len(source_pcm) / args.sample_rate,
            "probe": source_probe,
        },
        "currentExcerpt": primary,
        "baselineExcerpt": baseline,
        "priorPass10Diagnostic": prior_diagnostic,
        "comparison": {
            "expectedCurrentLagSamples": "0 ± 1 in each configured window",
            "knownPass10LagSamplesAt4To7Seconds": -790,
            "baselineVideoAnalyzed": baseline is not None,
            "baselinePath": str(baseline_path) if baseline is not None else None,
            "baselineDiagnosticPath": (
                str(args.baseline_diagnostic)
                if prior_diagnostic is not None
                else None
            ),
            "oldOffsetReproducedAt4To7Seconds": (
                all(
                    next(
                        measurement["bestSourceOffsetSamples"]
                        for measurement in baseline["measurements"]
                        if measurement["sequenceWindowSeconds"][0] == window_start
                    ) == -790
                    for window_start in (4.0, 5.0, 6.0)
                )
                if baseline is not None
                else None
            ),
        },
        "result": {
            "pass": primary["pass"],
            "notes": [
                "Correlation and fitted gain are signal comparisons, not a human listening verdict.",
                "AAC is lossy; small correlation and gain deviations are expected.",
                "The output, source, decoded PCM, and ffprobe payloads are hash/probe evidence for this run.",
            ],
        },
    }
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, indent=2) + "\n")
    print(json.dumps({
        "report": str(args.report),
        "reportSha256": sha256_file(args.report),
        "sourceSha256": report["source"]["sha256"],
        "currentExcerptSha256": primary["sha256"],
        "currentPass": primary["pass"],
        "baselineSha256": baseline["sha256"] if baseline else None,
        "oldOffsetReproduced": report["comparison"]["oldOffsetReproducedAt4To7Seconds"],
        "audioListeningPerformed": False,
    }, indent=2))
    return 0 if primary["pass"] else 2


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except subprocess.CalledProcessError as error:
        print(
            f"Command failed ({error.returncode}): {error.cmd}\n{error.stderr or ''}",
            file=sys.stderr,
        )
        raise SystemExit(error.returncode or 1)
