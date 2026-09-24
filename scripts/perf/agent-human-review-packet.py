#!/usr/bin/env python3
"""Build and apply an auditable local photographer review packet for Agent proofs."""

from __future__ import annotations

import argparse
import copy
import hashlib
import html
import json
import shutil
import sys
from datetime import datetime
from pathlib import Path
from typing import Any


PACKET_CONTRACT = "agent-human-review-packet-v2"
RATINGS_CONTRACT = "agent-human-review-ratings-v2"
CALIBRATION_CONTRACT = "agent-photographer-calibration-profile-v1"

RATING_DIMENSIONS = {
    "intentMatch": "Intent match",
    "naturalness": "Naturalness",
    "artifactFreedom": "Artifact freedom",
    "protectedFidelity": "Protected fidelity",
}

ISSUE_TAGS = {
    "intent-missed": "Intent missed",
    "too-warm": "Too warm",
    "too-cool": "Too cool",
    "too-bright": "Too bright",
    "too-dark": "Too dark",
    "too-saturated": "Too saturated",
    "too-flat": "Too flat",
    "too-contrasty": "Too contrasty",
    "visible-artifact": "Visible artifact",
    "protected-area-changed": "Protected area changed",
    "retouch-incomplete": "Retouch incomplete",
    "retouch-repetition": "Retouch repetition",
}

CHECK_THRESHOLD_KEYS = {
    "brighter": ["brightness"],
    "darker": ["brightness"],
    "strongerContrast": ["contrast"],
    "softerContrast": ["contrast"],
    "warmer": ["warmth"],
    "moreSaturated": ["saturation"],
    "monochrome": ["monochromeSaturationLimit"],
    "preserveTone": ["preserveLuma", "preserveContrast", "preserveSaturation"],
}


def fail(message: str) -> int:
    print(message, file=sys.stderr)
    return 1


def load_object(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as handle:
        payload = json.load(handle)
    if not isinstance(payload, dict):
        raise ValueError(f"{path}: root must be an object")
    return payload


def canonical_json_value(value: Any) -> Any:
    """Normalize JSON numbers so Python and Qt sign identical payload bytes."""
    if isinstance(value, dict):
        return {key: canonical_json_value(item) for key, item in value.items()}
    if isinstance(value, list):
        return [canonical_json_value(item) for item in value]
    if isinstance(value, float) and value.is_integer():
        return int(value)
    return value


def canonical_bytes(value: Any) -> bytes:
    return json.dumps(
        canonical_json_value(value),
        sort_keys=True,
        separators=(",", ":"),
        ensure_ascii=False,
    ).encode("utf-8")


def value_sha256(value: Any) -> str:
    return hashlib.sha256(canonical_bytes(value)).hexdigest()


def file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def write_json(path: Path, payload: dict[str, Any]) -> None:
    path.write_text(
        json.dumps(payload, indent=2, sort_keys=True, ensure_ascii=False) + "\n",
        encoding="utf-8",
    )


def manifest_cases(
    manifest: dict[str, Any],
) -> tuple[dict[str, tuple[dict[str, Any], dict[str, Any]]], list[str]]:
    result: dict[str, tuple[dict[str, Any], dict[str, Any]]] = {}
    order: list[str] = []
    assets = manifest.get("assets")
    if not isinstance(assets, list):
        raise ValueError("manifest assets must be an array")
    for asset in assets:
        if not isinstance(asset, dict):
            continue
        cases = asset.get("agent_cases", [])
        if not isinstance(cases, list):
            raise ValueError(f"{asset.get('asset_id', 'asset')}: agent_cases must be an array")
        for case in cases:
            if not isinstance(case, dict):
                continue
            case_id = case.get("case_id")
            if not isinstance(case_id, str) or not case_id.strip():
                raise ValueError("every agent case needs a case_id")
            if case_id in result:
                raise ValueError(f"duplicate agent case: {case_id}")
            result[case_id] = (asset, case)
            order.append(case_id)
    if not result:
        raise ValueError("manifest contains no agent cases")
    return result, order


def report_cases(report: dict[str, Any]) -> dict[str, dict[str, Any]]:
    if report.get("contract") != "agent-real-photo-corpus-report-v1":
        raise ValueError("report must use agent-real-photo-corpus-report-v1")
    result: dict[str, dict[str, Any]] = {}
    cases = report.get("cases")
    if not isinstance(cases, list):
        raise ValueError("report cases must be an array")
    for case in cases:
        if not isinstance(case, dict):
            continue
        case_id = case.get("caseId")
        if not isinstance(case_id, str) or not case_id:
            raise ValueError("every report case needs caseId")
        if case_id in result:
            raise ValueError(f"duplicate report case: {case_id}")
        result[case_id] = case
    return result


def proof_path(report_case: dict[str, Any], key: str) -> Path:
    artifacts = report_case.get("artifacts")
    if not isinstance(artifacts, dict):
        raise ValueError(f"{report_case.get('caseId')}: artifacts are missing")
    value = artifacts.get(key)
    if not isinstance(value, str) or not value:
        raise ValueError(f"{report_case.get('caseId')}: {key} is missing")
    path = Path(value).expanduser().resolve()
    if not path.is_file():
        raise ValueError(f"{report_case.get('caseId')}: proof does not exist: {path}")
    return path


def review_regions(agent_case: dict[str, Any]) -> list[dict[str, Any]]:
    regions: list[dict[str, Any]] = []
    protected = agent_case.get("protected_regions", [])
    if isinstance(protected, list):
        for region in protected:
            if isinstance(region, dict):
                regions.append({"kind": "protected", **region})
    retouch = agent_case.get("retouch")
    if isinstance(retouch, dict) and isinstance(retouch.get("region"), dict):
        region = retouch["region"]
        center_x = float(region.get("center_x", 0.5))
        center_y = float(region.get("center_y", 0.5))
        radius_x = float(region.get("radius_x", 0.0))
        radius_y = float(region.get("radius_y", 0.0))
        regions.append(
            {
                "kind": "retouch-target",
                "label": retouch.get("target_label", "Retouch target"),
                "x": center_x - radius_x,
                "y": center_y - radius_y,
                "width": radius_x * 2.0,
                "height": radius_y * 2.0,
            }
        )
    return regions


def packet_html(packet: dict[str, Any]) -> str:
    review_payload = {
        key: value
        for key, value in packet.items()
        if key not in {"sourceManifest", "sourceMachineReport"}
    }
    review_payload["cases"] = [
        {
            key: value
            for key, value in case.items()
            if key not in {"machinePass", "machineFailures"}
        }
        for case in packet["cases"]
    ]
    payload = json.dumps(review_payload, ensure_ascii=False).replace("</", "<\\/")
    case_sections: list[str] = []
    star_buttons = "".join(
        f'<button class="star" data-score="{score}">{score}</button>'
        for score in range(1, 6)
    )
    for index, case in enumerate(packet["cases"], start=1):
        case_id = html.escape(str(case.get("caseId", "")), quote=True)
        case_kind = html.escape(str(case.get("caseKind", "")))
        asset_id = html.escape(str(case.get("assetId", "")))
        instructions = html.escape(str(case.get("instructions", "")))
        before_proof = html.escape(str(case.get("beforeProof", "")), quote=True)
        after_proof = html.escape(str(case.get("afterProof", "")), quote=True)
        checks = case.get("expectedPixelChecks", [])
        chips = "".join(
            f'<span class="chip">{html.escape(str(check))}</span>'
            for check in checks
            if isinstance(check, str)
        )
        regions = ""
        for region in case.get("reviewRegions", []):
            if not isinstance(region, dict):
                continue
            kind = "protected" if region.get("kind") == "protected" else ""
            label = html.escape(str(region.get("label", "")), quote=True)
            try:
                left = float(region.get("x", 0.0)) * 100.0
                top = float(region.get("y", 0.0)) * 100.0
                width = float(region.get("width", 0.0)) * 100.0
                height = float(region.get("height", 0.0)) * 100.0
            except (TypeError, ValueError):
                continue
            regions += (
                f'<i class="region {kind}" title="{label}" '
                f'style="left:{left:.6f}%;top:{top:.6f}%;'
                f'width:{width:.6f}%;height:{height:.6f}%"></i>'
            )
        dimensions = "".join(
            f'<div class="dimension" data-dimension="{name}"><span>{label}</span>'
            f'<div>{star_buttons}</div></div>'
            for name, label in RATING_DIMENSIONS.items()
        )
        issue_tags = "".join(
            f'<button class="issue" data-issue="{name}">{label}</button>'
            for name, label in ISSUE_TAGS.items()
        )
        case_sections.append(
            f'''<section class="case" data-id="{case_id}">
<div class="case-head"><div class="case-num">{index}</div><div><div class="eyebrow">{case_kind} · {asset_id}</div><h2>{instructions}</h2><div class="chips">{chips}</div></div><div class="blind">Blind judgment<small>Machine result hidden</small></div></div>
<div class="light-table" data-mode="side"><div class="proof-tools"><div class="mode-label">BLIND LIGHT TABLE</div><button class="selected" data-proof-mode="side">Side by side</button><button data-proof-mode="overlay">Split compare</button><button data-proof-mode="after">After only</button><span>Hold B for before · ← → moves the split</span></div><div class="proofs"><div class="proof before"><span class="tag">BEFORE</span><img src="{before_proof}" alt="Before proof for {case_id}"></div><div class="proof after"><span class="tag">AFTER</span><img src="{after_proof}" alt="After proof for {case_id}">{regions}</div></div><div class="split-proof"><img class="split-before" src="{before_proof}" alt="Before proof for split comparison of {case_id}"><div class="split-after"><img src="{after_proof}" alt="After proof for split comparison of {case_id}">{regions}</div><i class="split-line"></i><span class="tag split-before-tag">BEFORE</span><span class="tag split-after-tag">AFTER</span><input data-split type="range" min="0" max="100" value="50" aria-label="Before and after split position"></div></div>
<div class="review"><div class="verdict"><div class="question"><b>Would you deliver this edit?</b><br>Judge only the visible result. A fail needs at least one reason tag.</div><div class="choices"><button data-status="pass">Deliver</button><button data-status="fail">Do not deliver</button></div><label>Overall quality · 1 rough, 5 ready</label><div class="stars overall">{star_buttons}</div></div><div class="dimensions"><div class="section-title">Photographic read</div>{dimensions}</div><div class="critique"><div class="section-title">What needs attention?</div><div class="issues">{issue_tags}</div><textarea placeholder="Describe what looks right or wrong. Notes are required."></textarea></div></div>
</section>'''
        )
    initial_cases = "\n".join(case_sections)
    return f"""<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Looksy Agent · Taste Calibration</title>
<style>
:root{{--ink:#0d0e0f;--panel:#18191b;--paper:#f1ede5;--muted:#99958e;--line:#343538;--coral:#ff5d57;--green:#73d7a2}}
*{{box-sizing:border-box}} body{{margin:0;background:var(--ink);color:var(--paper);font:14px Inter,ui-sans-serif,system-ui,sans-serif}} header{{position:sticky;top:0;z-index:8;background:rgba(13,14,15,.94);backdrop-filter:blur(18px);border-bottom:1px solid var(--line)}}
.head{{max-width:1420px;margin:auto;padding:22px 28px;display:flex;gap:28px;align-items:end}} h1{{font:600 31px Georgia,serif;margin:0}} .eyebrow{{font:10px ui-monospace,monospace;letter-spacing:.16em;color:var(--muted);text-transform:uppercase}} .sub{{color:var(--muted);margin-top:7px;max-width:760px}}
.progress{{margin-left:auto;text-align:right;min-width:180px}} .progress strong{{font:600 24px Georgia,serif}} .bar{{height:5px;background:#292a2c;border-radius:9px;margin-top:8px;overflow:hidden}} .bar i{{display:block;height:100%;width:0;background:var(--coral);transition:.2s}}
main{{max-width:1420px;margin:auto;padding:26px 28px 130px}} .case{{background:var(--panel);border:1px solid var(--line);border-radius:22px;margin:0 0 24px;overflow:hidden;scroll-margin-top:118px;transition:border-color .2s,box-shadow .2s}} .case.complete{{border-color:rgba(115,215,162,.5);box-shadow:0 0 0 1px rgba(115,215,162,.08)}} .case-head{{padding:19px 21px;display:flex;gap:16px;align-items:start;border-bottom:1px solid var(--line)}} .case-num{{width:31px;height:31px;border-radius:50%;display:grid;place-items:center;background:var(--coral);font-weight:700}} .case.complete .case-num{{background:#2f8a61}}
.case h2{{font:600 22px Georgia,serif;margin:1px 0 6px}} .chips{{margin-top:10px;display:flex;gap:6px;flex-wrap:wrap}} .chip{{border:1px solid var(--line);border-radius:999px;padding:5px 9px;color:#c8c4bd;font-size:11px}} .blind{{margin-left:auto;color:#cbc7c0;font-size:12px;white-space:nowrap;text-align:right}} .blind small{{display:block;color:var(--muted);margin-top:4px}}
.proof-tools{{min-height:48px;padding:8px 16px;display:flex;gap:7px;align-items:center;background:#111214;border-bottom:1px solid var(--line)}} .proof-tools .mode-label{{font:10px ui-monospace,monospace;letter-spacing:.14em;color:var(--muted);margin-right:4px}} .proof-tools button{{padding:7px 10px;font-size:11px}} .proof-tools button.selected{{background:#42302f;border-color:var(--coral)}} .proof-tools span{{margin-left:auto;color:var(--muted);font:10px ui-monospace,monospace}}
.proofs{{display:grid;grid-template-columns:1fr 1fr;gap:1px;background:var(--line)}} .proof{{position:relative;background:#090a0b;min-height:380px;display:grid;place-items:center;overflow:hidden}} .proof img{{display:block;width:100%;height:100%;max-height:620px;object-fit:contain}} .tag{{position:absolute;z-index:3;top:13px;left:13px;background:rgba(8,9,10,.78);border:1px solid #555;border-radius:999px;padding:6px 10px;font:10px ui-monospace,monospace;letter-spacing:.12em}} .region{{position:absolute;border:2px solid var(--coral);box-shadow:0 0 0 9999px rgba(0,0,0,.08);pointer-events:none}} .region.protected{{border-color:#70a7ff}}
.split-proof{{position:relative;display:none;background:#090a0b;min-height:380px;overflow:hidden}} .split-proof>img,.split-after>img{{display:block;width:100%;height:100%;max-height:700px;object-fit:contain}} .split-after{{position:absolute;inset:0;clip-path:inset(0 50% 0 0)}} .split-after .region{{display:none}} .split-line{{position:absolute;z-index:2;top:0;bottom:0;left:50%;width:2px;background:rgba(255,255,255,.9);box-shadow:0 0 0 1px rgba(0,0,0,.35)}} .split-after-tag{{left:auto;right:13px}} .split-proof input{{position:absolute;z-index:4;inset:0;width:100%;height:100%;opacity:0;cursor:ew-resize}}
.light-table[data-mode="overlay"] .proofs{{display:none}} .light-table[data-mode="overlay"] .split-proof{{display:block}} .light-table[data-mode="after"] .proofs{{grid-template-columns:1fr}} .light-table[data-mode="after"] .proof.before{{display:none}} .light-table.hold-before[data-mode="after"] .proof.before{{display:grid}} .light-table.hold-before[data-mode="after"] .proof.after{{display:none}} .light-table.hold-before[data-mode="overlay"] .split-after{{clip-path:inset(0 100% 0 0)!important}} .light-table.hold-before[data-mode="overlay"] .split-line{{display:none}}
.review{{padding:20px;display:grid;grid-template-columns:.8fr 1.1fr 1.25fr;gap:24px;align-items:start}} .question{{color:#cbc7c0;line-height:1.55;margin-bottom:14px}} .question b{{color:var(--paper)}} .choices{{display:flex;gap:8px}} button{{font:inherit;color:var(--paper);background:#292a2c;border:1px solid #3a3b3e;border-radius:11px;padding:11px 15px;cursor:pointer}} button:hover{{border-color:#777}} button.selected.pass{{background:#17442f;border-color:var(--green)}} button.selected.fail{{background:#572421;border-color:var(--coral)}}
.verdict label{{display:block;color:var(--muted);font-size:11px;margin-top:15px}} .stars{{display:flex;gap:4px;margin-top:7px}} .star{{padding:7px 10px}} .star.selected{{background:#55472d;border-color:#d4ad58}} .section-title{{font:10px ui-monospace,monospace;letter-spacing:.13em;color:var(--muted);text-transform:uppercase;margin-bottom:10px}} .dimension{{display:flex;justify-content:space-between;align-items:center;gap:12px;padding:7px 0;border-bottom:1px solid #292a2c}} .dimension>div{{display:flex;gap:3px}} .dimension .star{{padding:5px 8px;font-size:11px}}
.issues{{display:flex;flex-wrap:wrap;gap:6px;margin-bottom:12px}} .issue{{padding:7px 9px;font-size:11px}} .issue.selected{{background:#572421;border-color:var(--coral)}} textarea,input[type=text]{{width:100%;color:var(--paper);background:#101112;border:1px solid var(--line);border-radius:11px;padding:11px;font:inherit}} textarea{{min-height:100px;resize:vertical}} .dock{{position:fixed;z-index:10;left:0;right:0;bottom:0;background:rgba(18,19,20,.96);border-top:1px solid var(--line);backdrop-filter:blur(18px)}} .dock-in{{max-width:1420px;margin:auto;padding:15px 28px;display:flex;gap:12px;align-items:center}} .dock input{{max-width:280px}} .dock .hint{{color:var(--muted)}} .dock .saved{{color:var(--green)}} .next{{white-space:nowrap}} .clear{{padding:10px 12px;color:var(--muted)}} .copy{{margin-left:auto;background:var(--coral);border-color:var(--coral);font-weight:700;white-space:nowrap}} .export{{white-space:nowrap}} .error{{color:#ff9c97}}
@media(max-width:900px){{.proofs{{grid-template-columns:1fr}}.review{{grid-template-columns:1fr}}.blind{{display:none}}.head{{align-items:start}}.proof-tools{{flex-wrap:wrap}}.proof-tools span{{width:100%;margin-left:0}}.dock-in{{flex-wrap:wrap}}.dock .hint{{order:3;width:100%}}}}
</style></head><body>
<header><div class="head"><div><div class="eyebrow">Local proof · private taste profile</div><h1>Photographer Taste Calibration</h1><div class="sub">Judge the pixels without seeing the machine verdict. Compare deliberately, leave whenever you need to, and return to the exact local draft on this Mac.</div></div><div class="progress"><strong id="progressText">0 / {len(packet['cases'])}</strong><div class="bar"><i id="progressBar"></i></div></div></div></header>
<main id="cases">{initial_cases}</main>
<div class="dock"><div class="dock-in"><input id="reviewer" type="text" placeholder="Photographer name"><span id="message" class="hint">Private draft autosaves locally · no proof pixels stored</span><button class="clear" id="clear">Clear draft</button><button class="next" id="next">Next unfinished</button><button class="copy" id="copy">Copy for Looksy</button><button class="export" id="export">Download JSON</button></div></div>
<script>
const packet={payload};
const dimensionNames={json.dumps(list(RATING_DIMENSIONS))};
const experienceContract='agent-blind-light-table-v1';
const storageKey=`framehouse-agent-calibration:${{packet.packetDigestSha256}}`;
const emptyState=()=>Object.fromEntries(packet.cases.map(c=>[c.caseId,{{status:'pending',rating:0,dimensions:Object.fromEntries(dimensionNames.map(k=>[k,0])),issueTags:[],notes:''}}]));
let state=emptyState();let reviewer='';let activeCard=null;
try{{const saved=JSON.parse(localStorage.getItem(storageKey)||'null');if(saved&&saved.contract===experienceContract&&saved.packetDigestSha256===packet.packetDigestSha256&&saved.cases&&Object.keys(saved.cases).length===packet.cases.length){{state=saved.cases;reviewer=String(saved.reviewer||'')}}}}catch(error){{/* Review remains usable when private local storage is unavailable. */}}
function complete(x){{return x.status!=='pending'&&x.rating&&Object.values(x.dimensions).every(Boolean)&&x.notes.trim()&&(x.status!=='fail'||x.issueTags.length)}}
function saveDraft(){{try{{localStorage.setItem(storageKey,JSON.stringify({{contract:experienceContract,packetDigestSha256:packet.packetDigestSha256,reviewer,cases:state}}));return true}}catch(error){{return false}}}}
const cards=[...document.querySelectorAll('.case')];
function update(messageText=''){{const done=Object.values(state).filter(complete).length;document.getElementById('progressText').textContent=`${{done}} / ${{packet.cases.length}}`;document.getElementById('progressBar').style.width=`${{100*done/packet.cases.length}}%`;cards.forEach(card=>card.classList.toggle('complete',complete(state[card.dataset.id])));const saved=saveDraft();const message=document.getElementById('message');message.textContent=messageText||(saved?'Draft saved on this Mac · proof pixels stay in the packet':'Private draft unavailable · review still works');message.className=saved?'saved':'hint'}}
cards.forEach(card=>{{const id=card.dataset.id;const value=state[id];const table=card.querySelector('.light-table');activeCard=activeCard||card;card.onmouseenter=()=>activeCard=card;card.onclick=()=>activeCard=card;card.querySelectorAll('[data-proof-mode]').forEach(button=>button.onclick=()=>{{table.dataset.mode=button.dataset.proofMode;card.querySelectorAll('[data-proof-mode]').forEach(x=>x.classList.toggle('selected',x===button))}});const split=card.querySelector('[data-split]');split.oninput=()=>{{const position=Number(split.value);card.querySelector('.split-after').style.clipPath=`inset(0 ${{100-position}}% 0 0)`;card.querySelector('.split-line').style.left=`${{position}}%`}};card.querySelectorAll('[data-status]').forEach(button=>{{const selected=value.status===button.dataset.status;button.classList.toggle('selected',selected);button.classList.toggle(button.dataset.status,selected);button.onclick=()=>{{value.status=button.dataset.status;card.querySelectorAll('[data-status]').forEach(x=>{{const active=x===button;x.classList.toggle('selected',active);x.classList.toggle('pass',active&&value.status==='pass');x.classList.toggle('fail',active&&value.status==='fail')}});update()}}}});card.querySelectorAll('.overall [data-score]').forEach(button=>{{button.classList.toggle('selected',value.rating===Number(button.dataset.score));button.onclick=()=>{{value.rating=Number(button.dataset.score);card.querySelectorAll('.overall [data-score]').forEach(x=>x.classList.toggle('selected',x===button));update()}}}});card.querySelectorAll('[data-dimension]').forEach(row=>row.querySelectorAll('[data-score]').forEach(button=>{{button.classList.toggle('selected',value.dimensions[row.dataset.dimension]===Number(button.dataset.score));button.onclick=()=>{{value.dimensions[row.dataset.dimension]=Number(button.dataset.score);row.querySelectorAll('[data-score]').forEach(x=>x.classList.toggle('selected',x===button));update()}}}}));card.querySelectorAll('[data-issue]').forEach(button=>{{button.classList.toggle('selected',value.issueTags.includes(button.dataset.issue));button.onclick=()=>{{const tag=button.dataset.issue;value.issueTags=value.issueTags.includes(tag)?value.issueTags.filter(x=>x!==tag):[...value.issueTags,tag];button.classList.toggle('selected',value.issueTags.includes(tag));update()}}}});const notes=card.querySelector('textarea');notes.value=value.notes||'';notes.oninput=event=>{{value.notes=event.target.value;update()}}}});
const reviewerInput=document.getElementById('reviewer');reviewerInput.value=reviewer;reviewerInput.oninput=event=>{{reviewer=event.target.value;update()}};
document.getElementById('next').onclick=()=>{{const next=cards.find(card=>!complete(state[card.dataset.id]));if(next){{activeCard=next;next.scrollIntoView({{behavior:'smooth',block:'start'}})}}else{{const message=document.getElementById('message');message.className='saved';message.textContent='Every case is complete · add your name and export'}}}};
document.getElementById('clear').onclick=()=>{{if(!confirm('Clear every local calibration decision for this exact packet?'))return;state=emptyState();reviewer='';reviewerInput.value='';try{{localStorage.removeItem(storageKey)}}catch(error){{}}location.reload()}};
document.addEventListener('keydown',event=>{{if(!activeCard||event.target.matches('textarea,input[type=text]'))return;const table=activeCard.querySelector('.light-table');if(event.key.toLowerCase()==='b'){{table.classList.add('hold-before');event.preventDefault()}}else if((event.key==='ArrowLeft'||event.key==='ArrowRight')&&table.dataset.mode==='overlay'){{const split=activeCard.querySelector('[data-split]');split.value=Math.max(0,Math.min(100,Number(split.value)+(event.key==='ArrowLeft'?-2:2)));split.oninput();event.preventDefault()}}}});document.addEventListener('keyup',event=>{{if(event.key.toLowerCase()==='b')document.querySelectorAll('.light-table').forEach(table=>table.classList.remove('hold-before'))}});window.addEventListener('blur',()=>document.querySelectorAll('.light-table').forEach(table=>table.classList.remove('hold-before')));
function signedCalibration(){{reviewer=reviewerInput.value.trim();const missing=packet.cases.filter(c=>!complete(state[c.caseId]));const message=document.getElementById('message');if(!reviewer||missing.length){{message.className='error';message.textContent=!reviewer?'Add the photographer name.':`Complete: ${{missing.map(x=>x.caseId).join(', ')}}`;const first=missing.length?cards.find(card=>card.dataset.id===missing[0].caseId):null;if(first)first.scrollIntoView({{behavior:'smooth',block:'start'}});return ''}}return JSON.stringify({{contract:'{RATINGS_CONTRACT}',blindReview:true,reviewExperienceContract:experienceContract,packetDigestSha256:packet.packetDigestSha256,manifestSha256:packet.manifestSha256,machineReportSha256:packet.machineReportSha256,reviewer,reviewedAt:new Date().toISOString(),cases:state}},null,2)+'\\n'}}
async function copyCalibration(text){{if(navigator.clipboard&&window.isSecureContext){{try{{await navigator.clipboard.writeText(text);return true}}catch(error){{/* Fall through to the local-file-safe copy path. */}}}}const area=document.createElement('textarea');area.value=text;area.setAttribute('readonly','');area.style.position='fixed';area.style.opacity='0';document.body.appendChild(area);area.select();let copied=false;try{{copied=document.execCommand('copy')}}catch(error){{copied=false}}area.remove();return copied}}
document.getElementById('copy').onclick=async()=>{{const text=signedCalibration();if(!text)return;const message=document.getElementById('message');if(await copyCalibration(text)){{saveDraft();message.className='saved';message.textContent='Signed ratings copied · return to Looksy and choose Finish copied ratings'}}else{{message.className='error';message.textContent='Clipboard unavailable · use Download JSON instead'}}}};
document.getElementById('export').onclick=()=>{{const text=signedCalibration();if(!text)return;const blob=new Blob([text],{{type:'application/json'}});const anchor=document.createElement('a');anchor.href=URL.createObjectURL(blob);anchor.download='agent-photographer-calibration.json';anchor.click();URL.revokeObjectURL(anchor.href);saveDraft();const message=document.getElementById('message');message.className='saved';message.textContent='Calibration JSON downloaded · local draft kept until you clear it'}};
update(reviewer?'Private draft restored for this exact proof packet':'Private draft autosaves locally · no proof pixels stored');
</script></body></html>"""


def build_packet(manifest_path: Path, report_path: Path, output: Path) -> int:
    manifest_path = manifest_path.resolve()
    report_path = report_path.resolve()
    output = output.resolve()
    if output.exists():
        return fail(f"output already exists: {output}")
    manifest = load_object(manifest_path)
    report = load_object(report_path)
    manifest_by_id, case_order = manifest_cases(manifest)
    report_by_id = report_cases(report)
    manifest_ids = set(manifest_by_id)
    report_ids = set(report_by_id)
    if manifest_ids != report_ids:
        missing = sorted(manifest_ids - report_ids)
        stale = sorted(report_ids - manifest_ids)
        return fail(
            "manifest/report case mismatch; "
            f"missing from report={missing or 'none'}, stale report cases={stale or 'none'}"
        )

    prepared: list[dict[str, Any]] = []
    for case_id in case_order:
        asset, agent_case = manifest_by_id[case_id]
        report_case = report_by_id[case_id]
        before = proof_path(report_case, "beforeProofPath")
        after = proof_path(report_case, "afterProofPath")
        prepared.append(
            {
                "caseId": case_id,
                "assetId": asset.get("asset_id", ""),
                "caseKind": agent_case.get("kind", "global_edit"),
                "instructions": agent_case.get("instructions", ""),
                "expectedPixelChecks": agent_case.get("expected_pixel_checks", []),
                "reviewRegions": review_regions(agent_case),
                "machinePass": report_case.get("machinePass") is True,
                "machineFailures": report_case.get("failures", []),
                "beforeSource": before,
                "afterSource": after,
                "beforeProofSha256": file_sha256(before),
                "afterProofSha256": file_sha256(after),
            }
        )

    output.mkdir(parents=True)
    assets_dir = output / "proofs"
    assets_dir.mkdir()
    packet_cases: list[dict[str, Any]] = []
    for case in prepared:
        case_id = case["caseId"]
        before_source: Path = case.pop("beforeSource")
        after_source: Path = case.pop("afterSource")
        before_name = f"{case_id}-before{before_source.suffix.lower() or '.png'}"
        after_name = f"{case_id}-after{after_source.suffix.lower() or '.png'}"
        shutil.copy2(before_source, assets_dir / before_name)
        shutil.copy2(after_source, assets_dir / after_name)
        case["beforeProof"] = f"proofs/{before_name}"
        case["afterProof"] = f"proofs/{after_name}"
        packet_cases.append(case)

    packet: dict[str, Any] = {
        "contract": PACKET_CONTRACT,
        "version": 2,
        "manifestSha256": file_sha256(manifest_path),
        "machineReportSha256": file_sha256(report_path),
        "manifestCaseCount": len(manifest_ids),
        "reportCaseCount": len(report_ids),
        "humanDecisionRequired": True,
        "machineResultCannotSetHumanRating": True,
        "blindReviewRequired": True,
        "machineEvidenceEmbeddedInReviewPage": False,
        "sourcePathsEmbeddedInReviewPage": False,
        "reviewExperienceContract": "agent-blind-light-table-v1",
        "localDraftAutosave": True,
        "localDraftContainsProofPixels": False,
        "directBeforeAfterComparison": True,
        "sourceManifest": str(manifest_path),
        "sourceMachineReport": str(report_path),
        "cases": packet_cases,
    }
    packet["packetDigestSha256"] = value_sha256(packet)
    write_json(output / "review-contract.json", packet)
    ratings_template = {
        "contract": RATINGS_CONTRACT,
        "packetDigestSha256": packet["packetDigestSha256"],
        "manifestSha256": packet["manifestSha256"],
        "machineReportSha256": packet["machineReportSha256"],
        "reviewer": "",
        "reviewedAt": "",
        "blindReview": True,
        "cases": {
            case_id: {
                "status": "pending",
                "rating": 0,
                "dimensions": {name: 0 for name in RATING_DIMENSIONS},
                "issueTags": [],
                "notes": "",
            }
            for case_id in case_order
        },
    }
    write_json(output / "ratings.template.json", ratings_template)
    (output / "index.html").write_text(packet_html(packet), encoding="utf-8")
    print(f"packet={output}")
    print(f"cases={len(packet_cases)}")
    print(f"packet_digest_sha256={packet['packetDigestSha256']}")
    print(f"open={output / 'index.html'}")
    return 0


def valid_reviewed_at(value: Any) -> bool:
    if not isinstance(value, str) or not value.strip():
        return False
    try:
        parsed = datetime.fromisoformat(value.replace("Z", "+00:00"))
    except ValueError:
        return False
    return parsed.tzinfo is not None


def apply_ratings(
    manifest_path: Path,
    packet_path: Path,
    ratings_path: Path,
    output_manifest: Path,
) -> int:
    manifest_path = manifest_path.resolve()
    packet_path = packet_path.resolve()
    ratings_path = ratings_path.resolve()
    output_manifest = output_manifest.resolve()
    if output_manifest == manifest_path:
        return fail("refusing to overwrite the source manifest; choose a new output manifest")
    if output_manifest.exists():
        return fail(f"output manifest already exists: {output_manifest}")
    manifest = load_object(manifest_path)
    packet = load_object(packet_path)
    ratings = load_object(ratings_path)
    if packet.get("contract") != PACKET_CONTRACT:
        return fail(f"packet must use {PACKET_CONTRACT}")
    claimed_packet_digest = packet.get("packetDigestSha256")
    packet_without_digest = dict(packet)
    packet_without_digest.pop("packetDigestSha256", None)
    actual_packet_digest = value_sha256(packet_without_digest)
    if claimed_packet_digest != actual_packet_digest:
        return fail("review packet digest does not match its contents")
    if packet.get("manifestSha256") != file_sha256(manifest_path):
        return fail("source manifest changed after this review packet was built")
    if ratings.get("contract") != RATINGS_CONTRACT:
        return fail(f"ratings must use {RATINGS_CONTRACT}")
    if ratings.get("blindReview") is not True:
        return fail("ratings must attest that the machine verdict stayed hidden")
    if ratings.get("packetDigestSha256") != claimed_packet_digest:
        return fail("ratings were produced for a different review packet")
    if ratings.get("manifestSha256") != packet.get("manifestSha256"):
        return fail("ratings manifest digest does not match the reviewed manifest")
    if ratings.get("machineReportSha256") != packet.get("machineReportSha256"):
        return fail("ratings machine-report digest does not match the reviewed proof run")
    reviewer = ratings.get("reviewer")
    reviewed_at = ratings.get("reviewedAt")
    if not isinstance(reviewer, str) or not reviewer.strip():
        return fail("ratings reviewer is required")
    if not valid_reviewed_at(reviewed_at):
        return fail("ratings reviewedAt must be a timezone-aware ISO-8601 timestamp")
    manifest_by_id, case_order = manifest_cases(manifest)
    rating_cases = ratings.get("cases")
    if not isinstance(rating_cases, dict) or set(rating_cases) != set(case_order):
        return fail("ratings must contain exactly every manifest agent case")
    packet_cases = packet.get("cases")
    if not isinstance(packet_cases, list):
        return fail("packet cases are missing")
    packet_by_id = {
        case.get("caseId"): case
        for case in packet_cases
        if isinstance(case, dict) and isinstance(case.get("caseId"), str)
    }
    for case_id in case_order:
        rating = rating_cases[case_id]
        if not isinstance(rating, dict):
            return fail(f"{case_id}: rating must be an object")
        status = rating.get("status")
        score = rating.get("rating")
        dimensions = rating.get("dimensions")
        issue_tags = rating.get("issueTags")
        notes = rating.get("notes")
        if status not in {"pass", "fail"}:
            return fail(f"{case_id}: human status must be pass or fail")
        if not isinstance(score, int) or isinstance(score, bool) or score < 1 or score > 5:
            return fail(f"{case_id}: rating must be an integer from 1 to 5")
        if not isinstance(dimensions, dict) or set(dimensions) != set(RATING_DIMENSIONS):
            return fail(f"{case_id}: every photographic rating dimension is required")
        for name, dimension_score in dimensions.items():
            if (
                not isinstance(dimension_score, int)
                or isinstance(dimension_score, bool)
                or dimension_score < 1
                or dimension_score > 5
            ):
                return fail(f"{case_id}: {name} must be an integer from 1 to 5")
        if (
            not isinstance(issue_tags, list)
            or any(tag not in ISSUE_TAGS for tag in issue_tags)
            or len(issue_tags) != len(set(issue_tags))
        ):
            return fail(f"{case_id}: issueTags contains an unknown or duplicate tag")
        if status == "fail" and not issue_tags:
            return fail(f"{case_id}: a failed review needs at least one issue tag")
        if not isinstance(notes, str) or not notes.strip():
            return fail(f"{case_id}: review notes are required")
        proof = packet_by_id.get(case_id)
        if not isinstance(proof, dict):
            return fail(f"{case_id}: packet proof identity is missing")
        _, agent_case = manifest_by_id[case_id]
        agent_case["human_review"] = {
            "status": status,
            "rating": score,
            "dimensions": dict(dimensions),
            "issue_tags": list(issue_tags),
            "reviewer": reviewer.strip(),
            "reviewed_at": reviewed_at,
            "notes": notes.strip(),
            "rating_source_contract": RATINGS_CONTRACT,
            "review_packet_digest_sha256": claimed_packet_digest,
            "machine_report_sha256": packet.get("machineReportSha256"),
            "before_proof_sha256": proof.get("beforeProofSha256"),
            "after_proof_sha256": proof.get("afterProofSha256"),
        }
    output_manifest.parent.mkdir(parents=True, exist_ok=True)
    write_json(output_manifest, manifest)
    pass_count = sum(
        1 for value in rating_cases.values() if value.get("status") == "pass"
    )
    fail_count = len(rating_cases) - pass_count
    print(f"reviewed_manifest={output_manifest}")
    print(f"human_pass={pass_count}")
    print(f"human_fail={fail_count}")
    print(f"packet_digest_sha256={claimed_packet_digest}")
    return 0


def build_calibration_profile(
    manifest_path: Path,
    packet_path: Path,
    report_path: Path,
    output_path: Path,
) -> int:
    manifest_path = manifest_path.resolve()
    packet_path = packet_path.resolve()
    report_path = report_path.resolve()
    output_path = output_path.resolve()
    if output_path.exists():
        return fail(f"output profile already exists: {output_path}")
    manifest = load_object(manifest_path)
    packet = load_object(packet_path)
    report = load_object(report_path)
    if packet.get("contract") != PACKET_CONTRACT:
        return fail(f"packet must use {PACKET_CONTRACT}")
    claimed_packet_digest = packet.get("packetDigestSha256")
    packet_without_digest = dict(packet)
    packet_without_digest.pop("packetDigestSha256", None)
    if claimed_packet_digest != value_sha256(packet_without_digest):
        return fail("review packet digest does not match its contents")
    if packet.get("machineReportSha256") != file_sha256(report_path):
        return fail("machine report does not match the reviewed proof run")

    manifest_by_id, case_order = manifest_cases(manifest)
    report_by_id = report_cases(report)
    packet_cases = packet.get("cases")
    if not isinstance(packet_cases, list):
        return fail("packet cases are missing")
    packet_by_id = {
        case.get("caseId"): case
        for case in packet_cases
        if isinstance(case, dict) and isinstance(case.get("caseId"), str)
    }
    expected_ids = set(case_order)
    if set(report_by_id) != expected_ids or set(packet_by_id) != expected_ids:
        return fail("manifest, packet, and report must contain exactly the same cases")

    reviewer = ""
    reviewed_at = ""
    dimension_scores: dict[str, list[int]] = {
        name: [] for name in RATING_DIMENSIONS
    }
    issue_counts = {name: 0 for name in ISSUE_TAGS}
    outcome_counts = {
        "agreementPass": 0,
        "falseConfidence": 0,
        "overConservative": 0,
        "agreementFail": 0,
    }
    cases: list[dict[str, Any]] = []
    threshold_signals: dict[str, dict[str, Any]] = {}

    for case_id in case_order:
        _, agent_case = manifest_by_id[case_id]
        human = agent_case.get("human_review")
        if not isinstance(human, dict):
            return fail(f"{case_id}: completed human_review is required")
        if human.get("rating_source_contract") != RATINGS_CONTRACT:
            return fail(f"{case_id}: review was not produced by {RATINGS_CONTRACT}")
        if human.get("review_packet_digest_sha256") != claimed_packet_digest:
            return fail(f"{case_id}: review belongs to a different packet")
        if human.get("machine_report_sha256") != packet.get("machineReportSha256"):
            return fail(f"{case_id}: review belongs to a different machine report")
        proof = packet_by_id[case_id]
        if (
            human.get("before_proof_sha256") != proof.get("beforeProofSha256")
            or human.get("after_proof_sha256") != proof.get("afterProofSha256")
        ):
            return fail(f"{case_id}: review proof identity does not match the packet")
        status = human.get("status")
        rating = human.get("rating")
        dimensions = human.get("dimensions")
        issue_tags = human.get("issue_tags")
        notes = human.get("notes")
        if status not in {"pass", "fail"}:
            return fail(f"{case_id}: completed human status is required")
        if not isinstance(rating, int) or isinstance(rating, bool) or not 1 <= rating <= 5:
            return fail(f"{case_id}: valid human rating is required")
        if not isinstance(dimensions, dict) or set(dimensions) != set(RATING_DIMENSIONS):
            return fail(f"{case_id}: photographic dimensions are incomplete")
        if any(
            not isinstance(value, int)
            or isinstance(value, bool)
            or not 1 <= value <= 5
            for value in dimensions.values()
        ):
            return fail(f"{case_id}: photographic dimensions are invalid")
        if (
            not isinstance(issue_tags, list)
            or any(tag not in ISSUE_TAGS for tag in issue_tags)
            or len(issue_tags) != len(set(issue_tags))
        ):
            return fail(f"{case_id}: issue tags are invalid")
        if status == "fail" and not issue_tags:
            return fail(f"{case_id}: failed review needs an issue tag")
        if not isinstance(notes, str) or not notes.strip():
            return fail(f"{case_id}: review notes are required")
        if not reviewer:
            reviewer = str(human.get("reviewer", "")).strip()
            reviewed_at = str(human.get("reviewed_at", "")).strip()
        if human.get("reviewer") != reviewer or human.get("reviewed_at") != reviewed_at:
            return fail("all cases must come from one signed photographer review session")

        for name, value in dimensions.items():
            dimension_scores[name].append(value)
        for tag in issue_tags:
            issue_counts[tag] += 1
        machine_pass = report_by_id[case_id].get("machinePass") is True
        if machine_pass and status == "pass":
            outcome = "agreement-pass"
            outcome_counts["agreementPass"] += 1
        elif machine_pass:
            outcome = "false-confidence"
            outcome_counts["falseConfidence"] += 1
        elif status == "pass":
            outcome = "over-conservative"
            outcome_counts["overConservative"] += 1
        else:
            outcome = "agreement-fail"
            outcome_counts["agreementFail"] += 1
        report_case = report_by_id[case_id]
        pixel_alignment = report_case.get("pixelIntentAlignment")
        checks = pixel_alignment.get("checks", []) if isinstance(pixel_alignment, dict) else []
        for check in checks:
            if not isinstance(check, dict):
                continue
            check_name = check.get("name")
            if not isinstance(check_name, str):
                continue
            for threshold_key in CHECK_THRESHOLD_KEYS.get(check_name, []):
                signal = threshold_signals.setdefault(
                    threshold_key,
                    {"observations": [], "falseConfidenceCount": 0, "overConservativeCount": 0},
                )
                signal["observations"].append(
                    {
                        "caseId": case_id,
                        "check": check_name,
                        "delta": check.get("delta"),
                        "threshold": check.get("threshold"),
                        "machineCheckPassed": check.get("passed") is True,
                        "humanStatus": status,
                        "humanRating": rating,
                    }
                )
                if outcome == "false-confidence":
                    signal["falseConfidenceCount"] += 1
                elif outcome == "over-conservative":
                    signal["overConservativeCount"] += 1
        retouch = report_case.get("retouchQuality")
        retouch_summary: dict[str, Any] | None = None
        if isinstance(retouch, dict) and retouch.get("available") is True:
            retouch_summary = {
                key: retouch.get(key)
                for key in (
                    "targetMeanRgbDelta",
                    "surroundingMeanRgbDelta",
                    "surroundingChangedFraction",
                    "surroundingGradientEnergyRatio",
                    "reconstructionGradientEnergyRatio",
                )
            }
            signal = threshold_signals.setdefault(
                "retouchQuality",
                {"observations": [], "falseConfidenceCount": 0, "overConservativeCount": 0},
            )
            signal["observations"].append(
                {"caseId": case_id, "humanStatus": status, "humanRating": rating, **retouch_summary}
            )
            if outcome == "false-confidence":
                signal["falseConfidenceCount"] += 1
            elif outcome == "over-conservative":
                signal["overConservativeCount"] += 1
        cases.append(
            {
                "caseId": case_id,
                "caseKind": packet_by_id[case_id].get("caseKind"),
                "machinePass": machine_pass,
                "humanStatus": status,
                "humanRating": rating,
                "dimensions": dict(dimensions),
                "issueTags": list(issue_tags),
                "outcome": outcome,
                "pixelIntentAlignment": pixel_alignment,
                "retouchQuality": retouch_summary,
            }
        )

    for signal in threshold_signals.values():
        if signal["falseConfidenceCount"] and signal["overConservativeCount"]:
            action = "collect-more-evidence"
            status = "conflicting-human-signal"
        elif signal["falseConfidenceCount"]:
            action = "tighten-and-rerun"
            status = "human-rejected-machine-pass"
        elif signal["overConservativeCount"]:
            action = "relax-and-rerun"
            status = "human-accepted-machine-fail"
        else:
            action = "hold-until-disagreement"
            status = "no-change-signal"
        signal["status"] = status
        signal["suggestedAction"] = action
        signal["automaticThresholdChangeAllowed"] = False

    dimension_means = {
        name: round(sum(values) / len(values), 3)
        for name, values in dimension_scores.items()
    }
    profile: dict[str, Any] = {
        "contract": CALIBRATION_CONTRACT,
        "version": 1,
        "status": "observed",
        "reviewer": reviewer,
        "reviewedAt": reviewed_at,
        "caseCount": len(cases),
        "packetDigestSha256": claimed_packet_digest,
        "sourceManifestSha256": packet.get("manifestSha256"),
        "reviewedManifestSha256": file_sha256(manifest_path),
        "machineReportSha256": file_sha256(report_path),
        "blindReview": True,
        "localOnly": True,
        "professionalReadinessClaimed": False,
        "automaticThresholdMutationAllowed": False,
        "freshCorpusRunRequiredAfterThresholdChange": True,
        "tasteSummary": {
            "dimensionMeans": dimension_means,
            "weakestDimensions": sorted(
                dimension_means, key=lambda name: (dimension_means[name], name)
            ),
            "issueTagCounts": {
                name: count for name, count in issue_counts.items() if count
            },
        },
        "outcomes": outcome_counts,
        "thresholdSignals": threshold_signals,
        "cases": cases,
    }
    profile["profileDigestSha256"] = value_sha256(profile)
    output_path.parent.mkdir(parents=True, exist_ok=True)
    write_json(output_path, profile)
    print(f"calibration_profile={output_path}")
    print(f"cases={len(cases)}")
    print(f"false_confidence={outcome_counts['falseConfidence']}")
    print(f"over_conservative={outcome_counts['overConservative']}")
    print(f"profile_digest_sha256={profile['profileDigestSha256']}")
    return 0


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    build = commands.add_parser("build", help="Build a standalone local review packet.")
    build.add_argument("--manifest", type=Path, required=True)
    build.add_argument("--report", type=Path, required=True)
    build.add_argument("--output", type=Path, required=True)
    apply = commands.add_parser("apply", help="Validate ratings and write a new reviewed manifest.")
    apply.add_argument("--manifest", type=Path, required=True)
    apply.add_argument("--packet", type=Path, required=True)
    apply.add_argument("--ratings", type=Path, required=True)
    apply.add_argument("--output-manifest", type=Path, required=True)
    profile = commands.add_parser(
        "profile", help="Build a local taste profile from a completed blind review."
    )
    profile.add_argument("--manifest", type=Path, required=True)
    profile.add_argument("--packet", type=Path, required=True)
    profile.add_argument("--report", type=Path, required=True)
    profile.add_argument("--output", type=Path, required=True)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    try:
        if args.command == "build":
            return build_packet(args.manifest, args.report, args.output)
        if args.command == "apply":
            return apply_ratings(
                args.manifest, args.packet, args.ratings, args.output_manifest
            )
        return build_calibration_profile(
            args.manifest, args.packet, args.report, args.output
        )
    except (OSError, ValueError, json.JSONDecodeError) as exc:
        return fail(str(exc))


if __name__ == "__main__":
    raise SystemExit(main())
