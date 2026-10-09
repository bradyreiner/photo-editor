#!/usr/bin/env python3
"""Build a self-contained local review page from real campaign render artifacts."""
import hashlib
import html
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


def main():
    cards = []
    inventory = []
    for manifest_path in sorted((ROOT / "campaigns").glob("*/campaign.json")):
        campaign = json.loads(manifest_path.read_text())
        campaign_id = campaign["campaign_id"]
        videos = sorted(
            (p for p in (ROOT / "build" / campaign_id).rglob("*.mp4") if p.with_suffix(".json").exists()),
            key=lambda p: (p.parent.name != "9x16", "final" not in p.parts, p.as_posix()),
        )
        options = []
        for path in videos:
            rel = path.relative_to(ROOT).as_posix()
            parts = path.relative_to(ROOT / "build" / campaign_id).parts
            record = json.loads(path.with_suffix(".json").read_text())
            size_label = "full-size" if parts[1] == "final" else "preview"
            label = f"{parts[0]} / {size_label} / {parts[2]} / {record.get("mode", "draft")}"
            poster = path.with_name(path.stem.rsplit("-", 1)[0] + "-thumbnail.png").relative_to(ROOT).as_posix()
            options.append(f'<option value="{html.escape(rel)}" data-poster="{html.escape(poster)}">{html.escape(label)}</option>')
            inventory.append({"campaign": campaign_id, "path": rel,
                              "bytes": path.stat().st_size, "sha256": hashlib.sha256(path.read_bytes()).hexdigest()})
        first = videos[0].relative_to(ROOT).as_posix() if videos else ""
        first_poster = videos[0].with_name(videos[0].stem.rsplit("-", 1)[0] + "-thumbnail.png").relative_to(ROOT).as_posix() if videos else ""
        video = f'<video controls preload="metadata" playsinline poster="{html.escape(first_poster)}" src="{html.escape(first)}"></video>' if first else '<p class="empty">Render this campaign to see footage here.</p>'
        cards.append(f'''<article class="campaign" id="{campaign_id}">
<div class="section-label">CAMPAIGN / {len(cards)+1:02}</div>
<h2>{html.escape(campaign['title'])}</h2>
<div class="film">{video}</div>
<label>Version <select onchange="switchFilm(this)">{''.join(options)}</select></label>
<p class="caption">Review drafts · source imagery and development interface previews. Actual product execution and founder takes are pending.</p>
<div class="links"><a href="campaigns/{campaign_id}.md">Scripts & platform copy ↗</a><a class="download" href="{html.escape(first)}" download>Download selected video ↓</a></div>
</article>''')
    sources_path = ROOT / "media/sources.json"
    sources = json.loads(sources_path.read_text()) if sources_path.exists() else {}
    raw_assets = sources.get("assets", sources.get("sources", [])) if isinstance(sources, dict) else sources
    rows = []
    for asset in raw_assets:
        title = asset.get("title", asset.get("id", "Source"))
        source = asset.get("source_page", asset.get("source_url", ""))
        rows.append(f'<tr><td>{html.escape(str(title))}</td><td>{html.escape(str(asset.get("license", "See ledger")))}</td><td>{html.escape(str(asset.get("rights_status", "Review required")))}</td><td><a href="{html.escape(str(source))}">Source ↗</a></td></tr>')
    page = '''<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1"><title>Looksy — Campaign studio</title>
<style>
:root{color-scheme:dark;--bg:#080a0e;--fg:#f5f4f0;--muted:#a2aab8;--line:#2a303b;--coral:#ff6254;--cyan:#67ddeb}*{box-sizing:border-box}body{margin:0;background:var(--bg);color:var(--fg);font-family:Inter,-apple-system,BlinkMacSystemFont,Arial,sans-serif;line-height:1.5}a{color:inherit;text-decoration:none;border-bottom:1px solid #4a5361}a:hover{color:var(--cyan)}header{display:flex;align-items:center;justify-content:space-between;padding:24px 5vw;border-bottom:1px solid var(--line)}header img{width:136px;height:auto}nav{display:flex;gap:24px;font-size:13px}main{max-width:1460px;margin:auto;padding:0 5vw 60px}.hero{position:relative;min-height:510px;padding:80px 0;display:flex;align-items:center;border-bottom:1px solid var(--line);overflow:hidden}.hero:before{content:"";position:absolute;inset:0 0 0 43%;background:linear-gradient(90deg,var(--bg),transparent 40%),linear-gradient(0deg,var(--bg),transparent 50%),url('media/generated/alpine-blue-hour.png') center 55%/cover;opacity:.8;z-index:-1}.section-label{font:11px ui-monospace,monospace;letter-spacing:2px;color:var(--cyan)}h1{font-size:clamp(46px,7vw,88px);line-height:1.02;letter-spacing:-.065em;max-width:740px;margin:24px 0}h1 em{font-style:normal;color:var(--coral)}.hero p{max-width:460px;font-size:17px;color:var(--muted)}.status{display:inline-block;padding:8px 12px;margin-top:12px;border-left:2px solid var(--coral);background:#1b1213;font-size:12px;color:#ffd0c9}.grid{display:grid;grid-template-columns:repeat(2,minmax(0,1fr));gap:40px;margin:48px 0}.campaign{min-width:0}h2{font-size:30px;line-height:1.1;letter-spacing:-1px;min-height:66px;margin:12px 0 24px}.film{display:flex;justify-content:center;align-items:center;background:#11151c;border:1px solid var(--line);height:480px;padding:12px}.film video{max-width:100%;max-height:100%;display:block}label{display:flex;align-items:center;gap:14px;margin:16px 0;font-size:12px;color:var(--muted)}select{flex:1;min-width:0;background:#141923;color:var(--fg);padding:10px;border:1px solid #374150;border-radius:3px}.caption{font-size:12px;color:var(--muted);min-height:40px}.links{display:flex;gap:18px;flex-wrap:wrap;font-size:12px}.workflow{border-top:1px solid var(--line);padding-top:40px}.workflow h2{min-height:0}.steps{display:grid;grid-template-columns:repeat(4,1fr);gap:26px;margin:32px 0}.steps strong{display:block;font-size:15px;margin-bottom:8px}.steps p{font-size:13px;color:var(--muted);margin:0}.steps span{display:block;color:var(--coral);font:12px ui-monospace,monospace;margin-bottom:10px}code{font-size:12px;overflow-wrap:anywhere}.command{background:#121721;padding:20px;border-left:2px solid var(--cyan)}table{width:100%;font-size:12px;border-collapse:collapse;margin:22px 0}th,td{text-align:left;padding:12px 8px;border-bottom:1px solid var(--line)}th{color:var(--muted)}footer{font-size:12px;color:var(--muted);margin-top:50px;display:flex;justify-content:space-between;gap:20px}.empty{color:var(--muted)}@media(max-width:850px){.grid{grid-template-columns:1fr}.hero{min-height:440px}.hero:before{inset:0;opacity:.35}.steps{grid-template-columns:repeat(2,1fr)}.film{height:420px}nav{gap:12px}h2{min-height:0}table{display:block;overflow:auto}}@media(prefers-reduced-motion:reduce){*{scroll-behavior:auto}}
</style></head><body><header><img src="media/looksy-wordmark.png" alt="Looksy"><nav><a href="strategy.md">Strategy</a><a href="experiments.csv">Experiments</a><a href="rights-guide.md">Media rights</a></nav></header><main>
<section class="hero"><div><div class="section-label">INDEPENDENT CREATIVE POWER / CAMPAIGN STUDIO</div><h1>The creative<br>future is <em>yours.</em></h1><p>Two campaign directions. Twelve shootable stories. A repeatable local production system with room to experiment.</p><div class="status">INTERNAL REVIEW · PRODUCT CAPTURES PENDING</div></div></section>
<section class="grid">''' + "".join(cards) + '''</section><section class="workflow"><div class="section-label">THE PRODUCTION LOOP</div><h2>One capture session.<br>A whole batch of stories.</h2><div class="steps"><div><span>01 / COLLECT</span><strong>Build a source set</strong><p>Original and licensed media, with a saved source and rights record.</p></div><div><span>02 / CAPTURE</span><strong>Record the real work</strong><p>Source, actions, human revision, and the finished export. Keep an uncut master.</p></div><div><span>03 / EXPERIMENT</span><strong>Change the opening</strong><p>Result, founder, and challenge hooks over the same core story.</p></div><div><span>04 / REVIEW</span><strong>Render, watch, learn</strong><p>Check readability, claims and rights; then track outcomes by platform.</p></div></div><div class="command"><code>.venv/bin/python tools/queue.py --campaign all --variant baseline --size preview --mode draft<br>.venv/bin/python tools/build_review.py</code></div><p class="caption">Commands run from marketing/. Full-size drafts use --size final; public-ready rendering separately uses --mode final and requires cleared assets and verified product proof.</p></section>
<section class="workflow"><div class="section-label">SOURCE LIBRARY</div><h2>Know where the media came from.</h2><table><thead><tr><th>Asset</th><th>License</th><th>Use status</th><th>Reference</th></tr></thead><tbody>''' + "".join(rows) + '''</tbody></table><div class="links"><a href="media/generated/PROMPT.md">Generated image prompt ↗</a><a href="media/sources.json">Source ledger ↗</a><a href="evidence/current-capture-status.md">Capture status ↗</a><a href="README.md">Production instructions ↗</a></div></section><footer><span>Looksy / Your ideas. Your machine. Your direction.</span><span>Local review library · nothing published</span></footer></main>
<script>function switchFilm(select){const card=select.closest('article');const video=card.querySelector('video');if(!video)return;video.pause();video.src=select.value;video.poster=select.selectedOptions[0].dataset.poster;video.load();card.querySelector('.download').href=select.value;}</script></body></html>'''
    (ROOT / "review.html").write_text(page, encoding="utf-8")
    (ROOT / "evidence/render-inventory.json").write_text(json.dumps(inventory, indent=2) + "\n")
    print(f"{ROOT / 'review.html'} ({len(inventory)} real video files)")


if __name__ == "__main__":
    main()
