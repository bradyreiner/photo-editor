#!/usr/bin/env python3
"""Render a branded motion-direction study, not a product demonstration.

Run with marketing/.venv/bin/python marketing/concepts/optical-motion/render_study.py
Outputs stay in the ignored marketing/renders directory. No paid services required.
"""
from pathlib import Path
import json
import math
import subprocess

from PIL import Image, ImageDraw, ImageFont, ImageOps

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / "renders/optical-motion"
W, H, FPS, SECONDS = 1080, 1920, 60, 12
INK, PAPER = "#050505", "#F3F3F3"
RGB = ("#FF3B30", "#30D158", "#3282FF")
FONT = "/System/Library/Fonts/Supplemental/Arial Bold.ttf"
REGULAR = "/System/Library/Fonts/Supplemental/Arial.ttf"
PHOTO = Image.open(ROOT / "media/generated/alpine-blue-hour.png").convert("RGB")
LOGO = Image.open(ROOT / "media/looksy-wordmark.png").convert("RGBA")
FONTS = {}


def font(size, bold=True):
    key = (size, bold)
    if key not in FONTS:
        FONTS[key] = ImageFont.truetype(FONT if bold else REGULAR, size)
    return FONTS[key]


def ease(value):
    x = max(0, min(1, value))
    return 1 - (1 - x) ** 4


def text(frame, copy, xy, size, fill=PAPER, bold=True):
    ImageDraw.Draw(frame).text(xy, copy, font=font(size, bold), fill=fill, anchor="lt", stroke_width=0)


def reveal(frame, copy, x, y, size, progress, fill=PAPER):
    """Type enters through a fixed baseline mask, with a short eased travel."""
    layer = Image.new("RGBA", (W, size + 42))
    shift = round((1 - ease(progress)) * (size + 35))
    ImageDraw.Draw(layer).text((x, shift), copy, font=font(size), fill=fill, anchor="lt")
    frame.paste(layer, (0, y), layer)


def picture(frame, box, zoom=1.0, source=PHOTO):
    x, y, width, height = map(round, box)
    if width < 1 or height < 1:
        return
    base = ImageOps.fit(source, (round(width * zoom), round(height * zoom)), method=Image.Resampling.LANCZOS)
    dx, dy = (base.width - width) // 2, (base.height - height) // 2
    frame.paste(base.crop((dx, dy, dx + width, dy + height)), (x, y))


def header(frame, number, label):
    text(frame, f"LOOKSY   /   {label}", (84, 178), 25, bold=False)
    text(frame, number, (930, 178), 25, bold=False)


def footer(frame, t):
    draw = ImageDraw.Draw(frame)
    draw.line((84, 1658, 996, 1658), fill="#4A4A4A", width=1)
    draw.rectangle((84, 1656, 84 + round(912 * t / SECONDS), 1659), fill=PAPER)
    text(frame, "MOTION STUDY / SOURCE IMAGERY", (84, 1692), 24, bold=False)


def render(t):
    frame = Image.new("RGB", (W, H), INK)
    draw = ImageDraw.Draw(frame)
    if t < 2.7:
        header(frame, "01", "THE IDEA")
        for i, word in enumerate(("YOUR", "NEXT", "IDEA.")):
            reveal(frame, word, 76, 385 + i * 176, 166, (t - i * 0.14) / 0.48)
        p = ease((t - 0.5) / 0.65)
        picture(frame, (84 + 1000 * (1 - p), 1025, 912, 500), 1.06 + t * 0.012)
        for i, color in enumerate(RGB):
            draw.rectangle((84 + i * 44, 1572, 110 + i * 44, 1578), fill=color)
    elif t < 5.7:
        u = t - 2.7
        header(frame, "02", "THE FEELING")
        reveal(frame, "MAKE IT", 76, 340, 155, u / 0.42)
        reveal(frame, "YOURS.", 76, 502, 155, (u - 0.12) / 0.42)
        p = ease(u / 0.6)
        picture(frame, (84, 775 + 140 * (1 - p), 912, 760), 1.10 - min(u, 3) * 0.015)
        # Precise photographic crop brackets: a visual transition motif.
        for x, y, sx, sy in ((110, 802, 1, 1), (970, 802, -1, 1), (110, 1509, 1, -1), (970, 1509, -1, -1)):
            draw.line((x, y, x + 34 * sx, y), fill=PAPER, width=3)
            draw.line((x, y, x, y + 34 * sy), fill=PAPER, width=3)
    elif t < 8.7:
        u = t - 5.7
        header(frame, "03", "THE POSSIBILITIES")
        for i, word in enumerate(("PHOTO.", "DESIGN.", "MOTION.")):
            reveal(frame, word, 76, 350 + i * 160, 143, (u - i * 0.17) / 0.42)
        # Source-image panels illustrate layout and motion; they are not app UI.
        for i in range(3):
            p = ease((u - 0.22 - i * 0.08) / 0.58)
            x, y = 84 + i * 308, 1010 + i * 46 + 220 * (1 - p)
            picture(frame, (x, y, 286, 450), 1.0 + i * 0.18 + u * 0.012)
            draw.rectangle((x, y + 464, x + 286, y + 468), fill=RGB[i])
    else:
        u = t - 8.7
        header(frame, "04", "YOUR DIRECTION")
        p = ease(u / 0.65)
        # RGB points converge into one registered light signature.
        for i, color in enumerate(RGB):
            angle = i * math.tau / 3 - math.pi / 2
            radius = 180 * (1 - p) + 18
            x, y = 540 + math.cos(angle) * radius, 518 + math.sin(angle) * radius
            draw.ellipse((x - 13, y - 13, x + 13, y + 13), fill=color)
        logo_width = 850
        logo = LOGO.resize((logo_width, round(LOGO.height * logo_width / LOGO.width)), Image.Resampling.LANCZOS)
        frame.paste(logo, (115, round(715 + (1 - p) * 55)), logo)
        reveal(frame, "YOUR IDEAS.", 84, 1112, 75, (u - 0.25) / 0.5)
        reveal(frame, "IN FOCUS.", 84, 1204, 75, (u - 0.38) / 0.5)
        text(frame, "A VISUAL DIRECTION FOR THE REEL SERIES", (84, 1465), 26, bold=False)
    footer(frame, t)
    return frame


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    silent = OUT / "study-silent.mp4"
    movie = OUT / "looksy-optical-motion-study.mp4"
    encoder = subprocess.Popen([
        "ffmpeg", "-hide_banner", "-loglevel", "error", "-y", "-f", "rawvideo",
        "-pixel_format", "rgb24", "-video_size", f"{W}x{H}", "-framerate", str(FPS),
        "-i", "-", "-an", "-c:v", "libx264", "-preset", "fast", "-crf", "18",
        "-pix_fmt", "yuv420p", "-vf", "scale=out_color_matrix=bt709",
        "-color_primaries", "bt709", "-color_trc", "bt709", "-colorspace", "bt709",
        "-x264-params", "colorprim=bt709:transfer=bt709:colormatrix=bt709",
        "-movflags", "+faststart", str(silent),
    ], stdin=subprocess.PIPE)
    samples = []
    sample_frames = {75, 225, 405, 615}
    for n in range(FPS * SECONDS):
        frame = render(n / FPS)
        encoder.stdin.write(frame.tobytes())
        if n in sample_frames:
            frame.save(OUT / f"frame-{n:03}.jpg", quality=94)
            samples.append(frame.resize((270, 480), Image.Resampling.LANCZOS))
        if n % FPS == 0:
            print(f"Rendered {n // FPS}/{SECONDS}s", flush=True)
    encoder.stdin.close()
    if encoder.wait() != 0:
        raise RuntimeError("Video encoder failed")
    subprocess.run([
        "ffmpeg", "-hide_banner", "-loglevel", "error", "-y", "-i", str(silent),
        "-i", str(ROOT / "media/audio/optical-pulse.wav"), "-t", str(SECONDS),
        "-c:v", "copy", "-c:a", "aac", "-b:a", "192k", "-af",
        "volume=16dB,afade=t=in:d=0.1,afade=t=out:st=11.4:d=0.6", "-movflags", "+faststart", str(movie),
    ], check=True)
    silent.unlink()
    sheet = Image.new("RGB", (1080, 480), INK)
    for i, sample in enumerate(samples):
        sheet.paste(sample, (i * 270, 0))
    sheet.save(OUT / "contact-sheet.jpg", quality=95)
    (OUT / "provenance.json").write_text(json.dumps({
        "status": "concept motion study; not a completed product showcase",
        "source_image": "media/generated/alpine-blue-hour.png",
        "source_origin": "existing generated image; not a Looksy editing result",
        "brand": "media/looksy-wordmark.png",
        "audio": "media/audio/optical-pulse.wav",
        "audio_origin": "existing original synthesized composition",
        "app_footage": False,
        "video": {"width": W, "height": H, "fps": FPS, "duration_s": SECONDS},
    }, indent=2) + "\n")
    print(movie, flush=True)


if __name__ == "__main__":
    main()
