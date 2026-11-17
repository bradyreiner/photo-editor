#!/usr/bin/env python3
"""Run isolated Remotion-bundled headless Chromium against the CampaignPoster SVG filter.

This uses only local file URLs and a fresh temporary browser profile. It captures
three browser raster stages (source path, feTurbulence alpha, displacement result)
for dash offsets 1.0, 0.5, and 0.0. No Remotion composition render is launched.
"""
from __future__ import annotations

import argparse
import base64
import json
import os
import re
import subprocess
import tempfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
DEFAULT_CHROME = ROOT / "marketing/animated-trailer/node_modules/.remotion/chrome-headless-shell/mac-arm64/chrome-headless-shell-mac-arm64/chrome-headless-shell"
REGION = (-350.0, 236.5, 1680.0, 882.0)


def svg_data_url(svg: str) -> str:
    return "data:image/svg+xml;base64," + base64.b64encode(svg.encode()).decode()


def page_html() -> str:
    x, y, width, height = REGION
    dash_states = (1.0, 0.5, 0.0)
    source_svgs: list[str] = []
    for index, dash_offset in enumerate(dash_states):
        source_svgs.append(f'''<svg xmlns="http://www.w3.org/2000/svg" width="{width:g}" height="{height:g}" viewBox="{x:g} {y:g} {width:g} {height:g}">
          <path d="M-120 955L1100 400" stroke="#ee6d33" stroke-width="180" fill="none" stroke-linecap="round" pathLength="1" stroke-dasharray="1" stroke-dashoffset="{dash_offset:g}"/>
        </svg>''')
    map_svg = f'''<svg xmlns="http://www.w3.org/2000/svg" width="{width:g}" height="{height:g}" viewBox="{x:g} {y:g} {width:g} {height:g}">
      <defs><filter id="noise" filterUnits="userSpaceOnUse" primitiveUnits="userSpaceOnUse" x="{x:g}" y="{y:g}" width="{width:g}" height="{height:g}">
        <feTurbulence type="fractalNoise" baseFrequency=".07 .2" numOctaves="2" seed="8"/>
      </filter></defs>
      <rect x="{x:g}" y="{y:g}" width="{width:g}" height="{height:g}" fill="#fff" filter="url(#noise)"/>
    </svg>'''
    # The original filter's SVG defaults: filter region is 10% outset from
    # the stroked path bounds, and primitive coordinates are user space.
    source_urls = [svg_data_url(s) for s in source_svgs]
    map_url = svg_data_url(map_svg)
    payload = json.dumps({"sources": source_urls, "map": map_url, "region": REGION})
    return f'''<!doctype html><html><head><meta charset="utf-8"><style>
      html,body{{margin:0;padding:0;background:transparent}} #payload{{white-space:pre-wrap}}
    </style></head><body><pre id="payload">waiting</pre><script>
    const config={payload};
    function load(url){{return new Promise((resolve,reject)=>{{const im=new Image();im.onload=()=>resolve(im);im.onerror=reject;im.src=url;}});}}
    function raster(url){{return load(url).then(im=>{{const c=document.createElement('canvas');c.width=im.naturalWidth;c.height=im.naturalHeight;const g=c.getContext('2d',{{willReadFrequently:true}});g.drawImage(im,0,0);return {{png:c.toDataURL('image/png'),width:c.width,height:c.height}};}});}}
    function filteredSvg(sourceUrl){{const [x,y,w,h]=config.region;const svg=`<svg xmlns="http://www.w3.org/2000/svg" width="${{w}}" height="${{h}}" viewBox="${{x}} ${{y}} ${{w}} ${{h}}"><defs><filter id="f" filterUnits="userSpaceOnUse" primitiveUnits="userSpaceOnUse" x="${{x}}" y="${{y}}" width="${{w}}" height="${{h}}"><feTurbulence type="fractalNoise" baseFrequency=".07 .2" numOctaves="2" seed="8" result="noise"/><feDisplacementMap in="SourceGraphic" in2="noise" scale="8"/></filter></defs><image href="${{sourceUrl}}" x="${{x}}" y="${{y}}" width="${{w}}" height="${{h}}" filter="url(#f)"/></svg>`;return 'data:image/svg+xml;base64,'+btoa(unescape(encodeURIComponent(svg)));}}
    Promise.all([raster(config.map),...config.sources.map(raster)]).then(async ([map,...sources])=>{{
      const results=[];for(let i=0;i<sources.length;i++){{results.push({{dashOffset:[1,.5,0][i],source:sources[i],filtered:await raster(filteredSvg(sources[i].png))}});}}
      document.getElementById('payload').textContent=JSON.stringify({{map,results}});
    }}).catch(e=>{{document.getElementById('payload').textContent='ERROR:'+String(e&&e.stack||e);}});
    </script></body></html>'''


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--chrome", type=Path, default=DEFAULT_CHROME)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    chrome = args.chrome.resolve()
    if not chrome.is_file():
        raise SystemExit(f"Chromium executable missing: {chrome}")
    args.output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="looksy-svg-browser-") as temp:
        temp_path = Path(temp)
        html_path = temp_path / "oracle.html"
        profile = temp_path / "profile"
        html_path.write_text(page_html())
        command = [
            str(chrome), "--headless", "--no-sandbox", "--disable-gpu",
            "--disable-background-networking", "--disable-component-update",
            "--disable-sync", "--no-first-run", "--hide-scrollbars",
            "--force-device-scale-factor=1", "--window-size=1680,882",
            "--virtual-time-budget=10000", f"--user-data-dir={profile}",
            f"file://{html_path}", "--dump-dom",
        ]
        result = subprocess.run(command, capture_output=True, text=True, timeout=45)
        if result.returncode != 0:
            raise SystemExit(f"Chromium failed ({result.returncode}):\n{result.stderr[-3000:]}")
        match = re.search(r'<pre id="payload">(.*?)</pre>', result.stdout, re.S)
        if not match:
            raise SystemExit(f"Chromium did not return the fixture payload:\n{result.stdout[-2000:]}\n{result.stderr[-1000:]}")
        payload = match.group(1)
        if payload.startswith("ERROR:"):
            raise SystemExit(payload)
        data = json.loads(payload)
        def save_png(data_url: str, path: Path) -> None:
            header, encoded = data_url.split(",", 1)
            if header != "data:image/png;base64":
                raise ValueError(f"unexpected browser raster type: {header}")
            path.write_bytes(base64.b64decode(encoded))
        map_path = args.output / "chromium-feTurbulence-alpha.png"
        save_png(data["map"]["png"], map_path)
        metadata = {"browser": result.stdout.split("<title>", 1)[1].split("</title>", 1)[0] if "<title>" in result.stdout else "Chromium Headless Shell", "region": REGION, "map": {"path": map_path.name, "width": data["map"]["width"], "height": data["map"]["height"]}, "dashStates": []}
        for index, item in enumerate(data["results"]):
            stem = f"campaign-poster-dash-{item['dashOffset']:g}"
            source_path = args.output / f"{stem}-source.png"
            filtered_path = args.output / f"{stem}-filtered.png"
            save_png(item["source"]["png"], source_path)
            save_png(item["filtered"]["png"], filtered_path)
            metadata["dashStates"].append({"dashOffset": item["dashOffset"], "source": source_path.name, "filtered": filtered_path.name, "width": item["source"]["width"], "height": item["source"]["height"]})
        (args.output / "browser-oracle.json").write_text(json.dumps(metadata, indent=2) + "\n")
        print(json.dumps(metadata, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
