#!/usr/bin/env python3
"""Local, fail-closed planar surface tracking for staged timeline PNGs."""
from __future__ import annotations

import argparse
import json
import math
import sys
from collections import OrderedDict
from pathlib import Path


def emit(value: dict) -> None:
    print(json.dumps(value, separators=(",", ":"), allow_nan=False), flush=True)


def fail(message: str, status: str = "failed") -> int:
    emit({"type": "error", "status": status, "message": message})
    return 2


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--request", required=True)
    args = parser.parse_args()
    try:
        import cv2
        import numpy as np
    except Exception as exc:
        return fail(f"OpenCV surface runtime is unavailable ({type(exc).__name__}): {exc}. Install opencv-python-headless in the optional surface runtime.", "unavailable")
    try:
        request = json.loads(Path(args.request).read_text(encoding="utf-8"))
        root = Path(request["framesDirectory"])
        times = request["times"]
        seed = request["seed"]
        if not isinstance(times, list) or not 2 <= len(times) <= 900:
            return fail("frame count must be in the range 2..900", "invalid_input")
        if any(not isinstance(t, (int, float)) or not math.isfinite(t) or t < 0 for t in times) or any(a >= b for a, b in zip(times, times[1:])):
            return fail("source times must be finite, nonnegative, and increasing", "invalid_input")
        paths = [root / f"{i:06d}.png" for i in range(len(times))]
        staged = sorted(p.name for p in root.glob("*.png"))
        if staged != [p.name for p in paths] or not all(p.is_file() for p in paths):
            return fail("staged PNG directory must contain exactly the requested numbered frames", "invalid_input")
        first_image = cv2.imread(str(paths[0]), cv2.IMREAD_GRAYSCALE)
        if first_image is None:
            return fail("a staged frame is unreadable", "invalid_input")
        height, width = first_image.shape
        if width < 2 or height < 2 or max(width, height) > 960:
            return fail("frames must have matching dimensions no larger than 960 pixels", "invalid_input")
        image_cache = OrderedDict([(0, first_image)])

        def get_image(index):
            image = image_cache.get(index)
            if image is None:
                image = cv2.imread(str(paths[index]), cv2.IMREAD_GRAYSCALE)
                if image is None or image.shape != (height, width):
                    raise ValueError(f"staged frame {index} is unreadable or has mismatched dimensions")
                image_cache[index] = image
            image_cache.move_to_end(index)
            while len(image_cache) > 12:
                image_cache.popitem(last=False)
            return image
        x, y, w, h = (float(seed[k]) for k in ("x", "y", "width", "height"))
        if not all(math.isfinite(v) for v in (x, y, w, h)) or min(x, y) < 0 or min(w, h) <= 0 or x + w > 1 or y + h > 1:
            return fail("seed rectangle must be normalized and inside the first frame", "invalid_input")
        seed_quad = np.float32([[x * width, y * height], [(x+w)*width, y*height], [x*width, (y+h)*height], [(x+w)*width, (y+h)*height]])

        def valid_quad(q, fw, fh):
            if q.shape != (4, 2) or not np.isfinite(q).all(): return False
            # TL, TR, BL, BR ordering must preserve a convex, non-flipped surface.
            tl, tr, bl, br = q
            def cross(a, b, c):
                ab, ac = b-a, c-a
                return float(ab[0]*ac[1] - ab[1]*ac[0])
            area = abs(cv2.contourArea(np.float32([tl,tr,br,bl])))
            if area < fw*fh*0.0001 or area > fw*fh*4: return False
            if cross(tl,tr,br) <= 0 or cross(tr,br,bl) <= 0 or cross(br,bl,tl) <= 0 or cross(bl,tl,tr) <= 0: return False
            return bool(np.all(q[:,0] >= -fw*.15) and np.all(q[:,0] <= fw*1.15) and np.all(q[:,1] >= -fh*.15) and np.all(q[:,1] <= fh*1.15))

        corrections = {}
        for correction in seed.get("correctionFrames", []):
            index = int(correction["frameIndex"])
            if index < 0 or index >= len(times) or index in corrections:
                return fail("correction frame index is invalid or duplicated", "invalid_input")
            quad = np.float32([[float(p["x"])*width, float(p["y"])*height] for p in correction["quad"]])
            if quad.shape != (4, 2) or not np.isfinite(quad).all() or not valid_quad(quad, width, height):
                return fail("correction quad is invalid", "invalid_input")
            corrections[index] = quad
        exclusions = seed.get("exclusions", [])
        exclusion_polys = []
        for box in exclusions:
            ex, ey, ew, eh = (float(box[k]) for k in ("x", "y", "width", "height"))
            if not all(math.isfinite(v) for v in (ex, ey, ew, eh)) or ex < 0 or ey < 0 or ew <= 0 or eh <= 0 or ex + ew > 1 or ey + eh > 1:
                return fail("exclusion boxes must be normalized and inside the frame", "invalid_input")
            exclusion_polys.append(np.float32([[ex*width,ey*height],[(ex+ew)*width,ey*height],[(ex+ew)*width,(ey+eh)*height],[ex*width,(ey+eh)*height]]))

        def make_mask(quad, initial_to_reference):
            mask = np.zeros((height, width), np.uint8)
            tl,tr,bl,br = quad
            poly = np.int32(np.round([tl,tr,br,bl]))
            cv2.fillConvexPoly(mask, poly, 255)
            if exclusion_polys:
                for ep in exclusion_polys:
                    warped = cv2.perspectiveTransform(ep.reshape(-1,1,2), initial_to_reference).reshape(-1,2)
                    cv2.fillConvexPoly(mask, np.int32(np.round(warped)), 0)
            return mask

        def fit_model(p0, p1, src_quad):
            if len(p0) < 10: return None
            homography, inlier_mask = cv2.findHomography(p0, p1, cv2.RANSAC, 3.0, maxIters=2500, confidence=.995)
            if homography is None or inlier_mask is None: return None
            inliers = inlier_mask.reshape(-1).astype(bool)
            n = int(inliers.sum())
            if n < 10: return None
            spread = cv2.convexHull(p0[inliers].astype(np.float32))
            coverage = abs(cv2.contourArea(spread)) / max(1.0, cv2.contourArea(cv2.convexHull(src_quad.astype(np.float32))))
            if coverage < .10: return None
            projected = cv2.perspectiveTransform(src_quad.reshape(-1,1,2), homography).reshape(-1,2)
            if not valid_quad(projected, width, height): return None
            predicted = cv2.perspectiveTransform(p0[inliers].reshape(-1,1,2),homography).reshape(-1,2)
            error = float(np.median(np.linalg.norm(predicted-p1[inliers],axis=1)))
            if not math.isfinite(error) or error > 3.0: return None
            confidence = min(1.0, (n/80.0) * min(1.0,coverage/.35) * math.exp(-error/4.0))
            if confidence < 0.30:
                return None
            return projected, n, error, confidence, homography

        def feature_mask(quad, initial_to_reference):
            return make_mask(quad, initial_to_reference)

        def support(image, quad, initial_to_reference):
            points = cv2.goodFeaturesToTrack(image, maxCorners=1200, qualityLevel=0.005, minDistance=3,
                                             blockSize=3, mask=feature_mask(quad, initial_to_reference))
            if points is None: return None
            xy = points.reshape(-1, 2)
            if len(xy) < 10: return None
            area = max(1.0, cv2.contourArea(cv2.convexHull(quad.astype(np.float32))))
            coverage = abs(cv2.contourArea(cv2.convexHull(xy.astype(np.float32)))) / area
            if coverage < .10: return None
            return len(xy), coverage

        def lk_estimate(source, target, src_quad, initial_to_source):
            points = cv2.goodFeaturesToTrack(source, maxCorners=1200, qualityLevel=0.005, minDistance=3,
                                             blockSize=3, mask=feature_mask(src_quad, initial_to_source))
            if points is None or len(points) < 10: return None
            moved, status, _ = cv2.calcOpticalFlowPyrLK(source, target, points, None, winSize=(31,31), maxLevel=3,
                                                       criteria=(cv2.TERM_CRITERIA_EPS|cv2.TERM_CRITERIA_COUNT,30,.01))
            if moved is None: return None
            back, back_status, _ = cv2.calcOpticalFlowPyrLK(target, source, moved, None, winSize=(31,31), maxLevel=3,
                                                           criteria=(cv2.TERM_CRITERIA_EPS|cv2.TERM_CRITERIA_COUNT,30,.01))
            keep = (status.reshape(-1) == 1) & (back_status.reshape(-1) == 1) & (np.linalg.norm(points.reshape(-1,2)-back.reshape(-1,2),axis=1) < 1.2)
            p0, p1 = points.reshape(-1,2)[keep], moved.reshape(-1,2)[keep]
            return fit_model(p0, p1, src_quad)

        orb = cv2.ORB_create(nfeatures=1600, scaleFactor=1.2, nlevels=8, edgeThreshold=12, fastThreshold=8)

        def orb_estimate(source, target, src_quad, initial_to_source):
            key_a, desc_a = orb.detectAndCompute(source, feature_mask(src_quad, initial_to_source))
            key_b, desc_b = orb.detectAndCompute(target, None)
            if desc_a is None or desc_b is None or len(key_a) < 12 or len(key_b) < 12: return None
            pairs = cv2.BFMatcher(cv2.NORM_HAMMING, crossCheck=False).knnMatch(desc_a, desc_b, k=2)
            matches = [first for pair in pairs if len(pair) == 2 for first, second in [pair] if first.distance < .75 * second.distance]
            matches.sort(key=lambda match: match.distance)
            unique = []
            seen = set()
            for match in matches:
                if match.trainIdx not in seen:
                    seen.add(match.trainIdx)
                    unique.append(match)
            if len(unique) < 12: return None
            p0 = np.float32([key_a[m.queryIdx].pt for m in unique])
            p1 = np.float32([key_b[m.trainIdx].pt for m in unique])
            return fit_model(p0, p1, src_quad)

        def model_distance(a, b):
            return float(np.mean(np.linalg.norm(a[0] - b[0], axis=1)))

        def choose_models(local, fixed, rolling):
            candidates = [model for model in (local, fixed, rolling) if model is not None]
            if not candidates: return None, 0
            threshold = max(5.0, math.hypot(width, height) * .008)
            # The fixed correction ORB estimate is the independent drift check.
            # Rolling flow and rolling ORB are correlated because both inherit
            # recent accepted geometry; they may never vote a valid fixed
            # correction reference out of the result.
            if fixed is not None:
                secondary = [model for model in (local, rolling) if model is not None]
                agreeing = [model for model in secondary if model_distance(fixed, model) <= threshold]
                if secondary and not agreeing:
                    return None, 0
                return max([fixed, *agreeing], key=lambda model: (model[3], model[1], -model[2])), 1 + len(agreeing)
            if len(candidates) == 1:
                return candidates[0], 1
            # Without a valid fixed model, local flow and the rolling ORB
            # reference must agree before they can carry a frame.
            clusters = []
            for candidate in candidates:
                peers = [other for other in candidates
                         if model_distance(candidate, other) <= threshold]
                clusters.append(peers)
            best = max(clusters, key=lambda peers: (len(peers), max(m[1] for m in peers)))
            if len(best) < 2:
                return None, 0
            return max(best, key=lambda model: (model[3], model[1], -model[2])), len(best)

        # Seed/reference resets let a user correction recover a later segment.
        def anchor_sample(index, quad):
            image = get_image(index)
            transform = cv2.getPerspectiveTransform(seed_quad.astype(np.float32), quad.astype(np.float32))
            observed = support(image, quad, transform)
            if observed is None:
                return None
            inliers, coverage = observed
            confidence = min(1.0, (inliers/80.0) * min(1.0,coverage/.35))
            if confidence < 0.30:
                return None
            return (quad.copy(), inliers, 0.0, confidence)

        def track_direction(start, stop, anchor_quad, step):
            # Each direction is measured independently from its manual anchor.
            # A rolling LK source handles inter-frame motion; the fixed anchor
            # ORB model limits accumulated drift.
            anchor_image = get_image(start)
            anchor_transform = cv2.getPerspectiveTransform(seed_quad.astype(np.float32), anchor_quad.astype(np.float32))
            samples = {start: anchor_sample(start, anchor_quad)}
            if samples[start] is None:
                return samples
            source_quad, source_image = anchor_quad.copy(), anchor_image
            seed_to_source = anchor_transform.copy()
            rolling_index, rolling_quad, rolling_image = start, anchor_quad.copy(), anchor_image
            rolling_transform = anchor_transform.copy()
            for index in range(start + step, stop + step, step):
                image = get_image(index)
                local = lk_estimate(source_image, image, source_quad, seed_to_source)
                keyframe = orb_estimate(anchor_image, image, anchor_quad, anchor_transform)
                rolling = None if rolling_index == start else orb_estimate(
                    rolling_image, image, rolling_quad, rolling_transform)
                tracked, consensus_count = choose_models(local, keyframe, rolling)
                if tracked is None:
                    samples[index] = None
                    # Keep walking independently from the last measured frame;
                    # the other endpoint can recover this index if it has support.
                    continue
                quad, inliers, error, confidence, _ = tracked
                samples[index] = (quad, inliers, error, confidence)
                # This chosen quad can come from fixed-reference or rolling ORB
                # rather than the local LK transform. Rebuild the seed mapping
                # from the accepted geometry so exclusion masks and future LK
                # feature masks use the exact current coordinates.
                seed_to_source = cv2.getPerspectiveTransform(
                    seed_quad.astype(np.float32), quad.astype(np.float32))
                source_quad, source_image = quad.copy(), image
                if consensus_count >= 2 and abs(index - rolling_index) >= 12 and confidence >= .50:
                    rolling_index, rolling_quad, rolling_image = index, quad.copy(), image
                    rolling_transform = cv2.getPerspectiveTransform(seed_quad.astype(np.float32), quad.astype(np.float32))
            return samples

        anchors = {0: seed_quad}
        anchors.update(corrections)
        anchor_indices = sorted(anchors)
        measured = {}
        for segment_number, left in enumerate(anchor_indices):
            right = anchor_indices[segment_number + 1] if segment_number + 1 < len(anchor_indices) else len(times) - 1
            left_quad = anchors[left]
            forward = track_direction(left, right, left_quad, 1)
            backward = {}
            if right != left and right in corrections:
                backward = track_direction(right, left, anchors[right], -1)
            if right == len(times) - 1 and right not in corrections and right != left:
                # The uncorrected tail has no right-hand measurement anchor.
                backward = {}
            for index in range(left, right + 1):
                new_sample = index not in measured
                if index in anchors:
                    chosen = anchor_sample(index, anchors[index])
                    if chosen is None:
                        return fail(f"surface reference has insufficient texture coverage at frame {index}")
                    measured[index] = chosen
                    if new_sample:
                        emit({"type":"progress", "completed":index + 1, "total":len(times)})
                    continue
                a, b = forward.get(index), backward.get(index)
                if a is None and b is None:
                    if right in corrections:
                        return fail(f"surface support unavailable from either correction anchor at frame {index}; add a correction")
                    return fail(f"surface support was lost or ambiguous at frame {index}; add a correction frame to recover")
                if a is not None and b is not None:
                    disagreement = float(np.mean(np.linalg.norm(a[0] - b[0], axis=1)))
                    if disagreement > max(5.0, math.hypot(width, height) * .008):
                        return fail(f"forward and backward surface tracks disagree at frame {index}; add a correction")
                    chosen = a if a[1] > b[1] else b
                else:
                    chosen = a if a is not None else b
                measured[index] = chosen
                if new_sample:
                    emit({"type":"progress", "completed":index + 1, "total":len(times)})

        samples = []
        for i in range(len(times)):
            quad, inliers, error, confidence = measured[i]
            points = [{"x": float(px/width), "y": float(py/height)} for px,py in quad]
            samples.append({"sourceSeconds": float(times[i]), "quad": points, "confidence": float(confidence), "inliers": int(inliers), "errorPx": float(error)})
        emit({"type":"complete", "width":width, "height":height, "backend":"opencv-lk-ransac-homography", "samples":samples})
        return 0
    except Exception as exc:
        return fail(f"Surface tracking failed ({type(exc).__name__}): {exc}")


if __name__ == "__main__":
    raise SystemExit(main())
