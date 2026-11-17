#!/usr/bin/env python3
"""Build a local review page for stored native renders, without external services."""
import json
import sys
from pathlib import Path

root = Path(sys.argv[1]).resolve()
study = json.loads((root / "study.json").read_text())
data = json.dumps(study).replace("<", "\\u003c")
page = r'''<!doctype html>
<html lang="en"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Looksy · Film study</title>
<style>
:root{color-scheme:dark;font:15px/1.5 -apple-system,BlinkMacSystemFont,Segoe UI,sans-serif;background:#151515;color:#e6e3de}
*{box-sizing:border-box}body{margin:0}main{max-width:1400px;margin:auto;padding:38px 32px 60px}
header{display:flex;justify-content:space-between;gap:24px;align-items:end;border-bottom:1px solid #3d3b36;padding-bottom:24px}
.eyebrow{font-size:11px;letter-spacing:.18em;text-transform:uppercase;color:#c6b28c}h1{font:42px/1.1 Georgia,serif;margin:8px 0 0}
a{color:#d7c49d}button,select,.link{font:inherit;border:1px solid #4c4942;border-radius:7px;background:#222;color:inherit;padding:9px 14px;text-decoration:none;cursor:pointer}
button:hover,a.link:hover{border-color:#c6b28c}button[aria-pressed=true]{border-color:#c6b28c;background:#3d372b;color:#f3e3c2}
nav{display:grid;grid-template-columns:repeat(4,1fr);gap:12px;margin:24px 0}nav button{padding:0;text-align:left;overflow:hidden;min-width:0}
nav img{display:block;width:100%;height:100px;object-fit:cover;opacity:.9}nav span{display:block;padding:10px 12px;font-size:13px}
.meta,.controls,.section-heading{display:flex;align-items:center;gap:12px;flex-wrap:wrap}.meta{justify-content:space-between;margin:22px 0 14px}
h2{font:28px/1.2 Georgia,serif;margin:0}.muted{color:#aaa69d;font-size:13px}.controls{margin:12px 0}.controls label{display:flex;gap:8px;align-items:center}
.viewer{max-height:68vh;min-height:200px;overflow:auto;background:#080808;border:1px solid #48443c;border-radius:8px}
.canvas{position:relative;width:100%;aspect-ratio:1}.canvas img{position:absolute;inset:0;width:100%;height:100%;max-width:none}
#before{clip-path:inset(0 50% 0 0)}#split{position:absolute;top:0;bottom:0;left:50%;border-left:1px solid #f0dcb3;pointer-events:none}
.range{display:grid;grid-template-columns:80px 1fr 150px;gap:14px;align-items:center;margin:10px 0 28px;font-size:12px;color:#c9c2b5}.range input{width:100%;accent-color:#d4bd8e}
.detail-frame{overflow:auto;height:430px;border:1px solid #48443c;background:#080808;border-radius:8px;margin-top:12px}.detail-frame img{display:block;max-width:none}
.section-heading{justify-content:space-between;margin-top:28px}.notes{display:grid;grid-template-columns:1fr 1fr;gap:28px;border-top:1px solid #3d3b36;margin-top:32px;padding-top:18px}
.notes p{margin:7px 0}.error{color:#ffb5a8}footer{margin-top:28px;font-size:12px;color:#aaa69d}
@media(max-width:750px){main{padding:24px 14px}h1{font-size:34px}header{display:block}header>a{display:inline-block;margin-top:18px}nav{grid-template-columns:1fr 1fr}.notes{grid-template-columns:1fr}.range{grid-template-columns:60px 1fr 100px}}
</style>
<main>
<header><div><div class="eyebrow">Looksy / Native capture studies</div><h1>Depth, light & film</h1><p class="muted">Stored native renders · 8K originals · controlled film and aperture comparisons</p></div><a class="link" href="README.md">Open scene instructions</a></header>
<nav id="scenes" aria-label="Scene studies"></nav>
<div class="meta"><div><h2 id="title"></h2><div class="muted" id="metadata"></div></div><a class="link" id="original" target="_blank">Open full-resolution original ↗</a></div>
<div class="controls"><label><span id="mode-label">Film variant</span><select id="variant" aria-label="Comparison variant"></select></label><label><input id="pixels" type="checkbox">100% pixel view</label><span class="muted" id="domain"></span></div>
<div class="viewer" id="viewer"><div class="canvas" id="canvas"><img id="after" alt="Processed native photograph"><img id="before" alt="Reference native photograph"><div id="split"></div></div></div>
<div class="range"><span id="left-label">No film</span><input id="wipe" type="range" min="0" max="100" value="50" aria-label="Reference versus selected variant"><span id="right-label">Grain + halation</span></div>
<div class="section-heading"><div><h2>Original detail</h2><div class="muted">Unresampled crops from the full-resolution output. Scroll to inspect.</div></div><select id="detail" aria-label="Full-resolution detail"></select></div>
<div class="detail-frame"><img id="detail-image" alt="Unresampled native detail"></div>
<div class="notes"><div><div class="eyebrow">What stays fixed</div><p id="fixed"></p><p class="muted">Film comparisons keep pose, optics, exposure and grading fixed. The sampling study keeps the film recipe and seed fixed.</p></div><div><div class="eyebrow">Qualification</div><p class="muted">67 MP film test: 16 versus 64 tiles, maximum error 1 code value in 13 pixels. Physical-distance grain correlation: 0.349 at 8K, 0.374 at 4K.</p><p class="muted">Captured lighting and scan detail remain limiting. Ideal thin lens; compound glass, diffraction and measured film-stock/scanner profiles remain unfinished.</p></div></div>
<p id="error" class="error" role="status"></p><footer>These controls compare saved images; open the included projects in Looksy to orbit, change focus, aperture, zoom and film parameters in 3D. Sources and licenses are in README.md.</footer>
</main>
<script>
const study=__STUDY_DATA__;
const $=id=>document.getElementById(id);
let current=null,buttons=[];
function fail(event){$('error').textContent='An image could not load: '+event.target.getAttribute('src')}
for(const id of ['before','after','detail-image'])$(id).addEventListener('error',fail);
function wipe(){const value=Number($('wipe').value);$('before').style.clipPath=`inset(0 ${100-value}% 0 0)`;$('split').style.left=value+'%'}
function sizeView(){const img=$('after'),canvas=$('canvas');if(!img.naturalWidth)return;const aspect=img.naturalWidth/img.naturalHeight;const width=$('pixels').checked?img.naturalWidth:Math.min($('viewer').clientWidth-2,(window.innerHeight*.68-2)*aspect);canvas.style.width=width+'px';canvas.style.margin=$('pixels').checked?'0':'0 auto';canvas.style.aspectRatio=String(aspect);if($('pixels').checked){$('viewer').scrollLeft=(canvas.clientWidth-$('viewer').clientWidth)/2;$('viewer').scrollTop=(canvas.clientHeight-$('viewer').clientHeight)/2}}
function variant(){const v=$('variant').selectedOptions[0];$('after').src=v.value;$('right-label').textContent=v.textContent;wipe()}
function detail(){const i=Number($('detail').value);$('detail-image').src=current.details[i].image}
function choose(c){current=c;$('error').textContent='';$('title').textContent=c.title;$('metadata').textContent=`Original: ${c.width.toLocaleString()} × ${c.height.toLocaleString()} · ${(c.points/1e6).toFixed(2)}M Gaussians · ${c.samples} samples · ${c.renderSeconds.toFixed(1)} s export`;$('original').href=c.image;
const p=c.compareCamera||c.camera;$('fixed').textContent=`Displayed comparison: ${p.focalLengthMm} mm · f/${p.fNumber} · focus ${p.focusDistanceM.toFixed(3)} m · authored scene scale. Generic physical-size grain and RGB halation.`;
$('variant').replaceChildren();let options;
if(c.versions){options=c.versions;$('before').src=c.reference;$('left-label').textContent='256 samples';$('mode-label').textContent='Aperture samples';$('domain').textContent='2K diagnostic · film and seed fixed'}else{options=[{label:'Grain + halation',image:c.compare+'/film-combined.png'},{label:'Grain only',image:c.compare+'/grain-only.png'},{label:'Halation only',image:c.compare+'/halation-only.png'}];$('before').src=c.compare+'/no-film.png';$('left-label').textContent='No film';$('mode-label').textContent='Film variant';$('domain').textContent=(c.compareResolution||'4K')+' comparison · optics fixed'}
for(const option of options){const item=document.createElement('option');item.value=option.image;item.textContent=option.label;$('variant').append(item)}
$('pixels').checked=false;variant();$('detail').replaceChildren();c.details.forEach((d,i)=>{const option=document.createElement('option');option.value=i;option.textContent=d.name;$('detail').append(option)});detail();buttons.forEach((b,i)=>b.setAttribute('aria-pressed',String(study.cases[i]===c)))}
study.cases.forEach(c=>{const b=document.createElement('button'),img=document.createElement('img'),label=document.createElement('span');img.src=c.preview||'previews/'+c.id+'.png';img.alt='';label.textContent=c.title;b.append(img,label);b.addEventListener('click',()=>choose(c));$('scenes').append(b);buttons.push(b)});
$('wipe').addEventListener('input',wipe);$('variant').addEventListener('change',variant);$('pixels').addEventListener('change',sizeView);$('detail').addEventListener('change',detail);$('after').addEventListener('load',sizeView);window.addEventListener('resize',sizeView);choose(study.cases[0]);
</script></html>'''
(root / "index.html").write_text(page.replace("__STUDY_DATA__", data))
print(root / "index.html")
