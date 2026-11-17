#!/usr/bin/env python3
"""Render the native film and a review contact sheet without temporary frame dumps."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys

HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[2]
OUT=ROOT/"marketing/outputs/the-perfect-picnic"
BINARY=ROOT/"build/care-comes-back/rig-render"
ENV=dict(os.environ,QT_QPA_PLATFORM="offscreen")


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument("--width",type=int,default=1600)
    parser.add_argument("--height",type=int,default=900)
    parser.add_argument("--stills-only",action="store_true")
    args=parser.parse_args()
    from PIL import Image,ImageDraw,ImageFont
    film=OUT/"film.json"
    times=[2,8.8,13.5,18.7,21,26.5,29.2,33.2,36.9,38.5,39.5,44.5,50.5]
    thumb_w,thumb_h=480,270
    sheet=Image.new("RGB",(thumb_w*3,(thumb_h+40)*5),(19,34,37))
    drawer=ImageDraw.Draw(sheet)
    font=ImageFont.truetype("/System/Library/Fonts/Supplemental/Arial.ttf",17)
    for i,t in enumerate(times):
        path=OUT/f"frame-{t:05.1f}.png"
        subprocess.run([str(BINARY),str(film),str(args.width),str(args.height),str(t),str(path)],env=ENV,check=True)
        frame=Image.open(path).convert("RGB")
        frame.thumbnail((thumb_w,thumb_h))
        x=(i%3)*thumb_w;y=(i//3)*(thumb_h+40)
        sheet.paste(frame,(x,y))
        drawer.text((x+12,y+thumb_h+10),f"{t:04.1f}s",font=font,fill=(232,224,197))
    sheet.save(OUT/"contact-sheet.jpg",quality=92)
    if args.stills_only:return
    film_data=json.loads(film.read_text())
    fps=film_data["fps"]
    video=OUT/"the-perfect-picnic.mp4"
    staged=OUT/"the-perfect-picnic.rendering.mp4"
    with (OUT/"render.log").open("w") as log:
        renderer=subprocess.Popen([str(BINARY),str(film),str(args.width),str(args.height),"0","raw"],env=ENV,stdout=subprocess.PIPE,stderr=log)
        encoder=subprocess.Popen(["ffmpeg","-hide_banner","-loglevel","warning","-y",
            "-f","rawvideo","-pixel_format","rgba","-video_size",f"{args.width}x{args.height}",
            "-framerate",str(fps),"-i","pipe:0","-i",str(OUT/"original-score-and-foley.wav"),
            "-c:v","libx264","-preset","medium","-crf","17","-pix_fmt","yuv420p",
            "-af","volume=5dB","-c:a","aac","-b:a","256k","-movflags","+faststart","-t",str(film_data["duration"]),
            str(staged)],stdin=renderer.stdout,stderr=log)
        renderer.stdout.close()
        encoder_code=encoder.wait()
        renderer_code=renderer.wait()
    if renderer_code or encoder_code:
        raise RuntimeError(f"Native renderer={renderer_code}, encoder={encoder_code}; see {OUT/'render.log'}")
    staged.replace(video)
    probe=json.loads(subprocess.check_output(["ffprobe","-v","error","-show_streams","-show_format","-of","json",str(video)]))
    def sha(path):return hashlib.sha256(path.read_bytes()).hexdigest()
    receipt=dict(title=film_data["title"],video=str(video),renderer="native Qt hierarchical vector rig",
                 source_sha256=sha(film),video_sha256=sha(video),probe=probe,
                 checks=dict(render_complete=True,full_motion_human_review=False,
                             native_app_reopen_qualified=False),
                 artifacts=dict(contact_sheet=str(OUT/"contact-sheet.jpg"),script=str(HERE/"SCREENPLAY.md")))
    (OUT/"receipt.json").write_text(json.dumps(receipt,indent=2))
    (OUT/"review.html").write_text('''<!doctype html><meta charset="utf-8"><title>The Perfect Picnic — KIN</title>
<style>body{margin:0;background:#132225;color:#eee4c8;font:18px Georgia}main{max-width:1200px;margin:48px auto;padding:24px}h1{font-weight:400;font-size:46px}p{color:#acbdb0;line-height:1.6}video,img{width:100%;border-radius:12px}a{color:#edbd77}details{margin-top:32px}</style>
<main><p>KIN · AN ORIGINAL ANIMATED COMMERCIAL</p><h1>The Perfect Picnic</h1>
<video controls preload="metadata" poster="frame-002.0.png"><source src="the-perfect-picnic.mp4" type="video/mp4"></video>
<p>Nell and her very conscientious little robot set out to have the perfect afternoon.<br>Then the weather has a better idea.</p>
<p>54 seconds · original vector characters · native articulated animation · original synthesized score and foley</p>
<p><a href="film.json">Editable shot and rig data</a> · <a href="native-generator-shots.json">Native timeline generator inputs</a> · <a href="receipt.json">Render receipt</a></p>
<details><summary>Shot contact sheet</summary><img src="contact-sheet.jpg"></details></main>''')
    print(f"Rendered {video}",flush=True)


if __name__=="__main__":main()
