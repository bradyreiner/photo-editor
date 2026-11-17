#!/usr/bin/env python3
"""Guarded, single-orientation native H.264 export and verification.

Run after the exact final Timeline snapshot has a verified native save-copy
receipt and the editing task is released. The active document hash must match
that receipt. The native app chooses and owns the output path; this script
never supplies a path to the export tool.
"""
from pathlib import Path
import argparse
import fcntl
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import time
import uuid
from datetime import datetime, timezone

from native_mcp import call


HERE = Path(__file__).resolve().parent
EVIDENCE = HERE / "evidence"
GIB = 1024 ** 3
MAX_EXPORT_BYTES = 16 * GIB
MAX_SNAPSHOT_BYTES = 256 * 1024 * 1024
EXPECTED = {
    "landscape": {
        "width": 1920,
        "height": 1080,
        "fps": 30.0,
        "frames": 8940,
        "presets": {"youtube"},
    },
    "portrait": {
        "width": 1080,
        "height": 1920,
        "fps": 30.0,
        "frames": 8940,
        "presets": {"youtube_shorts", "instagram"},
    },
}
ACTIVE_STATUSES = {"Queued", "Running", "Canceling"}
TERMINAL_STATUSES = {"Completed", "Failed", "Canceled", "Rejected"}


def canonical_uuid(value):
    try:
        return str(uuid.UUID(value)) == value
    except (ValueError, AttributeError, TypeError):
        return False


def write_json(path, value, exclusive=False):
    path.parent.mkdir(parents=True, exist_ok=True)
    payload = (json.dumps(value, indent=2, sort_keys=True) + "\n").encode()
    if exclusive:
        descriptor = os.open(path, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
        with os.fdopen(descriptor, "wb") as stream:
            stream.write(payload)
            stream.flush()
            os.fsync(stream.fileno())
        return
    temporary = path.with_name(path.name + f".tmp-{os.getpid()}")
    descriptor = os.open(temporary, os.O_WRONLY | os.O_CREAT | os.O_EXCL, 0o600)
    with os.fdopen(descriptor, "wb") as stream:
        stream.write(payload)
        stream.flush()
        os.fsync(stream.fileno())
    os.replace(temporary, path)


def body(response, tool_name):
    result = response.get("result", {})
    structured = result.get("structuredContent")
    if response.get("error") or result.get("isError") or not isinstance(structured, dict):
        raise RuntimeError(f"{tool_name} failed: {json.dumps(response, ensure_ascii=False)}")
    return structured


def call_body(tool_name, arguments=None):
    return body(call(tool_name, arguments or {}), tool_name)


def explicit_source_gate_rejection(error_text):
    """Recognize only the native source guard that returns before enqueue.

    Older progress files retained this structured response inside the exception
    string. Transport failures and other errors must remain enqueue-uncertain.
    """
    prefix = "timeline.export_h264 failed: "
    if not isinstance(error_text, str) or not error_text.startswith(prefix):
        return None
    try:
        response = json.loads(error_text[len(prefix):])
    except (ValueError, TypeError):
        return None
    result = response.get("result", {}) if isinstance(response, dict) else {}
    if not isinstance(result, dict):
        return None
    structured = result.get("structuredContent", {})
    if (result.get("isError") is not True or not isinstance(structured, dict) or
            structured.get("error") !=
            "All Timeline source files must be online and fully checksum-verified before export." or
            any(key in structured for key in ("accepted", "jobId", "status", "outputPath"))):
        return None
    return response


def resolve_explicit_source_rejection(progress):
    if progress.get("status") != "enqueue-uncertain":
        return progress
    response = explicit_source_gate_rejection(progress.get("error"))
    if response is None:
        return progress
    return dict(progress, status="Rejected", receipt={
        "status": "Rejected", "requestId": progress.get("requestId"),
        "error": response["result"]["structuredContent"]["error"],
        "classification": "explicit-native-source-guard-before-enqueue",
        "nativeResponse": response,
    }, note="Native source guard rejected before enqueue. No export request was replayed.")


def expected_paths(orientation, request_id):
    stem = f"native-298-export-{orientation}-{request_id}"
    return {
        "intent": EVIDENCE / f"{stem}-intent.json",
        "progress": EVIDENCE / f"{stem}-progress.json",
        "verification": EVIDENCE / f"{stem}-verification.json",
    }


def verified_backup_receipt(path_text, resource_id, document_hash):
    if not isinstance(path_text, str) or not path_text:
        raise RuntimeError("A verified native save-copy receipt is required to start export.")
    receipt_path = Path(path_text).expanduser()
    if receipt_path.is_symlink():
        raise RuntimeError("The native save-copy receipt path must not be a symlink.")
    try:
        resolved_receipt = receipt_path.resolve(strict=True)
    except OSError as error:
        raise RuntimeError("The native save-copy receipt file is unavailable.") from error
    if not resolved_receipt.is_file() or resolved_receipt.stat().st_size > 1024 * 1024:
        raise RuntimeError("The native save-copy receipt must be a regular JSON file under 1 MiB.")
    try:
        envelope = json.loads(resolved_receipt.read_text())
    except (OSError, UnicodeDecodeError, json.JSONDecodeError) as error:
        raise RuntimeError("The native save-copy receipt is not readable JSON.") from error

    receipt = envelope
    if isinstance(envelope, dict) and isinstance(envelope.get("structuredContent"), dict):
        receipt = envelope["structuredContent"]
    elif isinstance(envelope, dict) and isinstance(envelope.get("result"), dict):
        result = envelope["result"]
        if isinstance(result.get("structuredContent"), dict):
            receipt = result["structuredContent"]
        else:
            content = result.get("content")
            text_block = content[0].get("text") if isinstance(content, list) and content and isinstance(content[0], dict) else None
            if isinstance(text_block, str):
                try:
                    receipt = json.loads(text_block)
                except json.JSONDecodeError as error:
                    raise RuntimeError("The native save-copy receipt body is invalid JSON.") from error

    if not isinstance(receipt, dict):
        raise RuntimeError("The native save-copy receipt has no structured result.")
    timeline_id = resource_id.removeprefix("timeline:")
    if (not canonical_uuid(timeline_id) or
            receipt.get("ok") is not True or
            receipt.get("commandId") != "timeline.action.saveTimeline" or
            receipt.get("readbackVerified") is not True or
            receipt.get("resourceId") != resource_id or
            receipt.get("timelineId") != timeline_id or
            receipt.get("documentHash") != document_hash or
            receipt.get("taskStateHash") != document_hash or
            not isinstance(receipt.get("taskStepId"), int) or
            isinstance(receipt.get("taskStepId"), bool) or
            receipt["taskStepId"] <= 0):
        raise RuntimeError("The native save-copy receipt does not prove a readback-verified snapshot for the current Timeline hash.")

    output_text = receipt.get("outputPath")
    output_path = Path(output_text) if isinstance(output_text, str) else Path()
    if not output_path.is_absolute() or output_path.is_symlink():
        raise RuntimeError("The native save-copy receipt path must name an absolute, non-symlink file.")
    try:
        resolved_output = output_path.resolve(strict=True)
    except OSError as error:
        raise RuntimeError("The native save-copy project file is unavailable.") from error
    if (resolved_output != output_path or not resolved_output.is_file() or
            resolved_output.stat().st_size > MAX_SNAPSHOT_BYTES):
        raise RuntimeError("The native save-copy project must be a canonical regular file under the snapshot size limit.")
    expected_hash = receipt.get("fileSha256")
    if not isinstance(expected_hash, str) or not re.fullmatch(r"[0-9a-f]{64}", expected_hash):
        raise RuntimeError("The native save-copy receipt has no valid project-file SHA-256.")
    digest = hashlib.sha256()
    with resolved_output.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    if digest.hexdigest() != expected_hash:
        raise RuntimeError("The native save-copy project bytes do not match the receipt SHA-256.")
    return {
        "receiptPath": str(resolved_receipt),
        "outputPath": str(resolved_output),
        "fileSha256": expected_hash,
        "documentHash": document_hash,
        "resourceId": resource_id,
        "taskStepId": receipt["taskStepId"],
        "readbackVerified": True,
    }


def check_document(orientation, resource_id, document_hash, backup_receipt_path):
    expected = EXPECTED[orientation]
    inspection = call_body("timeline.inspect_document")
    if (inspection.get("ok") is not True or inspection.get("active") is not True or
            inspection.get("ready") is not True):
        raise RuntimeError("The active Timeline must be ready before export.")
    if inspection.get("resourceId") != resource_id:
        raise RuntimeError("The active Timeline resourceId does not match the supplied receipt guard.")
    if inspection.get("documentHash") != document_hash:
        raise RuntimeError("The active Timeline documentHash changed after it was reviewed.")
    backup = verified_backup_receipt(backup_receipt_path, resource_id, document_hash)
    if (inspection.get("width") != expected["width"] or
            inspection.get("height") != expected["height"] or
            float(inspection.get("frameRate", 0.0)) != expected["fps"] or
            abs(float(inspection.get("durationSeconds", 0.0)) - 298.0) > 1e-7):
        raise RuntimeError("The active Timeline is not the expected 298-second, 30 fps orientation.")

    task = call_body("timeline.inspect_task")
    if task.get("ok") is not True or task.get("active") is True:
        raise RuntimeError("Release or resolve the active editing task before native export.")

    preflight = call_body("timeline.inspect_delivery_preflight")
    if (preflight.get("ok") is not True or
            preflight.get("preset") not in expected["presets"] or
            preflight.get("width") != expected["width"] or
            preflight.get("height") != expected["height"] or
            float(preflight.get("frameRate", 0.0)) != expected["fps"]):
        raise RuntimeError(
            "The saved delivery preset must pass preflight for this orientation "
            f"(expected one of {sorted(expected['presets'])}).")
    estimated = int(preflight.get("estimatedOutputBytes", 0))
    required = int(preflight.get("requiredDiskBytes", 0))
    free = int(preflight.get("freeDiskBytes", -1))
    if estimated <= 0 or required <= 0 or free < 0:
        raise RuntimeError("Delivery preflight returned no trustworthy output or disk estimate.")
    # Keep the two 2 GiB native procedural caches available, reserve another
    # estimated output for bounded chunk/assembly staging, then require the
    # app's own two-times-output disk allowance. This is conservative headroom,
    # not a guarantee against unrelated processes consuming disk space.
    minimum_free = required + estimated + 4 * GIB
    if free < minimum_free:
        raise RuntimeError(
            f"Only {free} bytes are free; this export requires at least "
            f"{minimum_free} bytes with cache and staging headroom.")

    jobs = call_body("timeline.inspect_render_jobs")
    # inspect_render_jobs returns a successful owned snapshot without an `ok`
    # field. The MCP envelope is validated by call_body; still require the exact
    # resource and a bounded well-formed queue before deciding no job is active.
    if (jobs.get("ok", True) is not True or
            jobs.get("resourceId") != resource_id or
            not isinstance(jobs.get("jobs"), list) or
            len(jobs["jobs"]) > 20 or
            any(not isinstance(job, dict) or not isinstance(job.get("status"), str)
                for job in jobs["jobs"])):
        raise RuntimeError("The active Timeline render queue could not be inspected.")
    for job in jobs.get("jobs", []):
        if job.get("status") in ACTIVE_STATUSES:
            raise RuntimeError("A Timeline render is already queued or running; wait before starting export.")
    return inspection, preflight, backup


def verify_output(receipt, orientation, request_id, resource_id, document_hash):
    expected = EXPECTED[orientation]
    integrity = receipt.get("outputIntegrity")
    if not isinstance(integrity, dict) or integrity.get("valid") is not True:
        raise RuntimeError("The completed native job has no valid output-integrity receipt.")
    expected_hash = integrity.get("sha256")
    expected_bytes = integrity.get("fileBytes")
    if (not isinstance(expected_hash, str) or not re.fullmatch(r"[0-9a-f]{64}", expected_hash) or
            not isinstance(expected_bytes, int) or expected_bytes <= 0 or
            expected_bytes >= MAX_EXPORT_BYTES):
        raise RuntimeError("The native output-integrity receipt has invalid size or SHA-256 metadata.")

    timeline_id = resource_id.removeprefix("timeline:")
    if not canonical_uuid(timeline_id):
        raise RuntimeError("The export receipt returned a noncanonical Timeline resource ID.")
    output_text = receipt.get("outputPath")
    output_path = Path(output_text) if isinstance(output_text, str) else Path()
    if (not output_path.is_absolute() or output_path.is_symlink() or
            output_path.name != f"{timeline_id}-{request_id}.mp4" or
            output_path.parent.name != "renders" or
            output_path.parent.parent.name != "timelines"):
        raise RuntimeError("The native export output path is outside the expected managed renders layout.")
    resolved = output_path.resolve(strict=True)
    if str(resolved) != str(output_path) or not resolved.is_file():
        raise RuntimeError("The native export output path is not a canonical regular file.")
    if resolved.stat().st_size != expected_bytes:
        raise RuntimeError("The published file size does not match the durable native integrity receipt.")

    digest = hashlib.sha256()
    with resolved.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    if digest.hexdigest() != expected_hash:
        raise RuntimeError("The published output SHA-256 does not match the durable native integrity receipt.")

    ffprobe = shutil.which("ffprobe")
    ffmpeg = shutil.which("ffmpeg")
    if not ffprobe or not ffmpeg:
        raise RuntimeError("FFprobe and FFmpeg are required for post-export frame and decode verification.")
    probe = subprocess.run(
        [ffprobe, "-v", "error", "-count_frames", "-show_entries",
         "format=duration:stream=codec_name,codec_type,width,height,pix_fmt,color_range,color_space,"
         "color_transfer,color_primaries,r_frame_rate,avg_frame_rate,nb_read_frames,duration,"
         "sample_rate,channels", "-of", "json", str(resolved)],
        capture_output=True, text=True, timeout=900, check=True)
    metadata = json.loads(probe.stdout)
    streams = metadata.get("streams", [])
    video = [stream for stream in streams if stream.get("codec_type") == "video"]
    audio = [stream for stream in streams if stream.get("codec_type") == "audio"]
    if len(video) != 1 or len(audio) != 1:
        raise RuntimeError("The native master must contain exactly one video and one mixed audio stream.")
    video_stream = video[0]
    video_duration = float(video_stream.get("duration", 0.0))
    frames_read = int(video_stream.get("nb_read_frames", -1))
    if (video_stream.get("codec_name") != "h264" or
            video_stream.get("width") != expected["width"] or
            video_stream.get("height") != expected["height"] or
            video_stream.get("pix_fmt") != "yuv420p" or
            video_stream.get("color_range") != "tv" or
            video_stream.get("color_space") != "bt709" or
            video_stream.get("color_primaries") != "bt709" or
            video_stream.get("color_transfer") != "bt709" or
            video_stream.get("r_frame_rate") != "30/1" or
            video_stream.get("avg_frame_rate") != "30/1" or
            frames_read != expected["frames"] or abs(video_duration - 298.0) > 0.25 / 30.0):
        raise RuntimeError("The encoded video does not match the required dimensions, 8,940-frame clock or duration.")
    audio_stream = audio[0]
    if (audio_stream.get("codec_name") != "aac" or audio_stream.get("sample_rate") != "48000" or
            audio_stream.get("channels") != 2 or
            abs(float(audio_stream.get("duration", 0.0)) - 298.0) > 1024.0 / 48000.0):
        raise RuntimeError("The native master does not contain the expected 48 kHz stereo AAC program mix.")
    decode = subprocess.run(
        [ffmpeg, "-v", "error", "-i", str(resolved), "-f", "null", "-"],
        capture_output=True, text=True, timeout=1800)
    if decode.returncode != 0:
        raise RuntimeError("The completed native master failed a full FFmpeg decode.")
    return {
        "schema": "native-298-export-verification:v1",
        "verifiedAt": datetime.now(timezone.utc).isoformat(),
        "orientation": orientation,
        "resourceId": resource_id,
        "documentHash": document_hash,
        "requestId": request_id,
        "outputPath": str(resolved),
        "fileBytes": expected_bytes,
        "sha256": expected_hash,
        "probe": metadata,
        "fullDecodeExitCode": decode.returncode,
        "decodeStderr": decode.stderr,
    }


def start_or_resume(args):
    if not canonical_uuid(args.request_id):
        raise RuntimeError("request-id must be a canonical lowercase UUID.")
    if not re.fullmatch(r"timeline:[0-9a-f-]{36}", args.resource_id):
        raise RuntimeError("resource-id must be the exact timeline:<UUID> value from the active inspector.")
    if not re.fullmatch(r"[0-9a-f]{64}", args.document_hash):
        raise RuntimeError("document-hash must be the exact lowercase SHA-256 from the active inspector.")
    paths = expected_paths(args.orientation, args.request_id)
    intent_path = paths["intent"]
    progress_path = paths["progress"]

    lock_path = EVIDENCE / "native-298-export-worker.lock"
    lock_path.parent.mkdir(parents=True, exist_ok=True)
    with lock_path.open("a+") as lock:
        try:
            fcntl.flock(lock.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
        except BlockingIOError as error:
            raise RuntimeError("Another guarded native 298s export script is active.") from error

        if args.resume:
            if not intent_path.is_file():
                raise RuntimeError("No durable intent exists for this requestId; refusing to guess or enqueue.")
            intent = json.loads(intent_path.read_text())
            expected_args = (args.orientation, args.resource_id, args.document_hash, args.request_id)
            actual_args = (intent.get("orientation"), intent.get("resourceId"),
                           intent.get("documentHash"), intent.get("requestId"))
            if expected_args != actual_args:
                raise RuntimeError("Resume arguments do not exactly match the durable export intent.")
            if (paths["verification"].is_file() and progress_path.is_file() and
                    json.loads(progress_path.read_text()).get("verificationPath") ==
                    str(paths["verification"])):
                print(paths["verification"].read_text())
                return 0
            progress = json.loads(progress_path.read_text()) if progress_path.exists() else {
                "status": "receipt-inspection-pending"}
            resolved = resolve_explicit_source_rejection(progress)
            if resolved != progress:
                progress = resolved
                write_json(progress_path, progress)
        else:
            if intent_path.exists() or progress_path.exists() or paths["verification"].exists():
                raise RuntimeError("This requestId already has evidence; use --resume or choose a new requestId.")
            for prior_path in EVIDENCE.glob("native-298-export-*-progress.json"):
                prior = json.loads(prior_path.read_text())
                prior_status = prior.get("status")
                completed_but_unverified = (
                    prior_status == "Completed" and
                    not (prior.get("verificationPath") and
                         Path(prior["verificationPath"]).is_file()))
                if prior_status not in TERMINAL_STATUSES or completed_but_unverified:
                    raise RuntimeError(
                        f"Prior export {prior_path.name} is {prior.get('status')}; resume it to a terminal state first.")
            inspection, preflight, backup_receipt = check_document(
                args.orientation, args.resource_id, args.document_hash,
                args.backup_receipt)
            intent = {
                "schema": "native-298-export-intent:v1",
                "createdAt": datetime.now(timezone.utc).isoformat(),
                "orientation": args.orientation,
                "resourceId": args.resource_id,
                "documentHash": args.document_hash,
                "requestId": args.request_id,
                "confirmed": True,
                "dimensions": {"width": inspection["width"], "height": inspection["height"]},
                "frameRate": inspection["frameRate"],
                "durationSeconds": inspection["durationSeconds"],
                "preset": preflight["preset"],
                "estimatedOutputBytes": preflight["estimatedOutputBytes"],
                "requiredDiskBytes": preflight["requiredDiskBytes"],
                "freeDiskBytes": preflight["freeDiskBytes"],
                "backupReceipt": backup_receipt,
            }
            write_json(intent_path, intent, exclusive=True)
            try:
                receipt = body(call("timeline.export_h264", {
                    "resourceId": args.resource_id,
                    "documentHash": args.document_hash,
                    "requestId": args.request_id,
                    "confirmed": True,
                }), "timeline.export_h264")
            except Exception as error:
                progress = {
                    "status": "enqueue-uncertain",
                    "requestId": args.request_id,
                    "error": str(error),
                    "note": "Use --resume to inspect this exact receipt; do not submit a new requestId.",
                }
                progress = resolve_explicit_source_rejection(progress)
                write_json(progress_path, progress)
                raise
            if (receipt.get("accepted") is not True or
                    receipt.get("requestId") != args.request_id or
                    receipt.get("resourceId") != args.resource_id or
                    receipt.get("documentHash") != args.document_hash):
                progress = {"status": "Rejected", "response": receipt}
                write_json(progress_path, progress)
                raise RuntimeError("The app did not return the exact durable export receipt.")
            progress = {"status": receipt.get("status", "Queued"), "receipt": receipt}
            write_json(progress_path, progress)

        deadline = time.monotonic() + args.timeout_hours * 3600
        while True:
            if progress.get("status") in TERMINAL_STATUSES:
                receipt = progress.get("receipt", {})
                break
            if time.monotonic() >= deadline:
                write_json(progress_path, progress)
                print(json.dumps({"timedOutWaiting": True, "progress": progress,
                                  "intentPath": str(intent_path),
                                  "resumeCommand": "rerun with --resume and the same arguments"}, indent=2))
                return 2
            try:
                receipt = call_body("timeline.inspect_export", {
                    "resourceId": args.resource_id,
                    "documentHash": args.document_hash,
                    "requestId": args.request_id,
                })
                status = receipt.get("status")
                if status not in ACTIVE_STATUSES | TERMINAL_STATUSES:
                    raise RuntimeError(f"Unknown native export status {status!r}.")
                progress = {"status": status, "receipt": receipt,
                            "updatedAt": datetime.now(timezone.utc).isoformat()}
                write_json(progress_path, progress)
                print(json.dumps({"orientation": args.orientation, "status": status,
                                  "progress": receipt.get("progress"),
                                  "stage": receipt.get("stage"),
                                  "requestId": args.request_id}), flush=True)
            except Exception as error:
                progress["lastInspectError"] = str(error)
                progress["updatedAt"] = datetime.now(timezone.utc).isoformat()
                write_json(progress_path, progress)
                raise RuntimeError(
                    "Receipt inspection failed; the durable intent is preserved. "
                    "Rerun --resume with the same resourceId, hash and requestId after restoring app access. "
                    f"Details: {error}") from error
            time.sleep(args.poll_seconds)

        if receipt.get("status") != "Completed":
            print(json.dumps({"terminalReceipt": receipt, "intentPath": str(intent_path)}, indent=2))
            return 1

        verification = verify_output(
            receipt, args.orientation, args.request_id, args.resource_id, args.document_hash)
        write_json(paths["verification"], verification, exclusive=True)
        progress["verificationPath"] = str(paths["verification"])
        write_json(progress_path, progress)
        print(json.dumps(verification, indent=2))
        return 0


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("orientation", choices=sorted(EXPECTED))
    parser.add_argument("--resource-id", required=True,
                        help="Exact timeline:<UUID> from timeline.inspect_document")
    parser.add_argument("--document-hash", required=True,
                        help="Exact lowercase hash from timeline.inspect_document")
    parser.add_argument("--backup-receipt",
                        help="Path to the readback-verified native saveTimeline receipt for this document hash (required with --start)")
    parser.add_argument("--request-id", required=True,
                        help="Canonical UUID retained for durable idempotency/resume")
    action = parser.add_mutually_exclusive_group(required=True)
    action.add_argument("--start", action="store_true",
                        help="Queue one export; requires --confirm-export")
    action.add_argument("--resume", action="store_true",
                        help="Inspect and wait for the already-intended exact receipt")
    parser.add_argument("--confirm-export", action="store_true",
                        help="Explicitly confirm this one native export")
    parser.add_argument("--poll-seconds", type=int, default=30)
    parser.add_argument("--timeout-hours", type=float, default=12.0,
                        help="Stop waiting without canceling if exceeded")
    args = parser.parse_args()
    if args.start != args.confirm_export:
        parser.error("--start requires --confirm-export; --resume must not include it")
    if args.start and not args.backup_receipt:
        parser.error("--start requires --backup-receipt for the current Timeline hash")
    if args.resume and args.backup_receipt:
        parser.error("--resume inspects the existing receipt and must not supply a new backup receipt")
    if args.poll_seconds < 10 or args.poll_seconds > 300 or args.timeout_hours <= 0:
        parser.error("poll-seconds must be 10–300 and timeout-hours must be positive")
    try:
        return start_or_resume(args)
    except Exception as error:
        print(f"native export stopped safely: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
