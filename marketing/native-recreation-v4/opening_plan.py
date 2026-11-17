"""Author the reference opening as native media, titles and generators.

This file creates commands for author_native.py. It does not write Timeline
project internals or use exported reference frames as source footage.
"""
from pathlib import Path
import json

ROOT = Path(__file__).resolve().parent
MEDIA = ROOT.parent / "animated-trailer/public/media"
STEPS = []
WIDTH, HEIGHT = 1920, 1080


def add(step_id, action, **values):
    STEPS.append({"id": step_id, "commandId": "timeline.action." + action, "values": values})
    return {"result": step_id, "field": "createdClipId"}


def track(name, kind="graphics", role=""):
    add(name, "createTimelineTrack", kind=kind, name=name, audioRole=role)
    return {"result": name, "field": "trackId"}


def smooth(t, a, b):
    u = max(0, min(1, (t-a)/(b-a)))
    return u*u*u*(10+u*(-15+6*u))


def out(t, a, b):
    return 1-(1-max(0, min(1, (t-a)/(b-a))))**4


def animate(name, clip, parameter, start, end, function, fps=30):
    keys = [{"localSeconds": f/fps, "value": function(f/fps), "interpolation": "linear"}
            for f in range(round(start*fps), round(end*fps)+1)]
    # Constant runs need their two boundary keys, not hundreds of duplicates.
    compact = [k for i, k in enumerate(keys) if i in (0, len(keys)-1) or
               keys[i-1]["value"] != k["value"] or keys[i+1]["value"] != k["value"]]
    add(name, "setTimelineClipAnimation", clipId=clip,
        channels=[{"parameter": parameter, "keyframes": compact}])


def frame(name, clip, x=0, y=0, width=WIDTH, height=HEIGHT, fit="fill", opacity=1, radius=0):
    add(name, "setTimelineClipFrame", clipId=clip,
        offsetX=4*((x+width/2)/WIDTH-.5), offsetY=4*((y+height/2)/HEIGHT-.5),
        scale=1, rotationDegrees=0, perspectiveX=0, perspectiveY=0,
        opacity=opacity, objectFit=fit, width=width/WIDTH, height=height/HEIGHT,
        cornerRadius=radius, cropLeft=0, cropTop=0, cropRight=0, cropBottom=0)


def image(name, source, target, start=0, duration=12):
    return add(name, "createTimelineClip", trackId=target,
        assetId="native-trailer:"+source, filePath=str((MEDIA/source).resolve()),
        displayLabel=name, durationSeconds=duration, trimStartSeconds=0,
        trimEndSeconds=duration, startSeconds=start, posterSource="")


def title(name, text, target, x, y, size, family="Anton", color="#f1efdf", spacing=-1.8,
          start=0, duration=12, alignment="left"):
    values={"text": text, "fontFamily": family, "fontSize": size,
                "fontWeight": 400, "italic": False, "letterSpacing": spacing,
                "lineHeight": 1.01, "color": color, "backgroundColor": "#00000000",
                "alignment": alignment, "positionX": x/WIDTH, "positionY": y/HEIGHT}
    if name == "headline":
        clip = "702dc6df-102c-4eb9-a2e3-57fe51b2b730"
        add(name+"-timing", "setTimelineClipTiming", clipId=clip, trackId=target,
            startSeconds=start, durationSeconds=duration)
        add(name, "setTimelineClipTitle", clipId=clip, values=values)
        return clip
    return add(name, "createTimelineTitle", trackId=target, templateKey="headline",
        startSeconds=start, durationSeconds=duration, values=values)


def build():
    base = track("00 / Ink background")
    add("ink", "createTimelineGraphic", trackId=base, shapeType="rectangle",
        startSeconds=0, durationSeconds=12, values={"positionX": .5, "positionY": .5,
            "shapeWidth": 1, "shapeHeight": 1, "backgroundColor": "#0b0d12"})
    background = track("01 / Additive light")
    light = add("light", "createTimelineGenerator", trackId=background, generatorKind="desktop-light",
        startSeconds=0, durationSeconds=12,
        parameters={"timeOffsetSeconds": 0, "timeScale": 1, "gather": 0})
    frame("light-opacity", light, opacity=.7)
    animate("light-gather", light, "generator:gather", 0, 12, lambda t: 1-smooth(t, 1, 4))
    hero_track = track("02 / Camera photograph")
    hero = image("hero", "creative-polish-v2/curve/curve-16.png", hero_track)
    frame("hero-frame", hero, x=480, width=1440)
    animate("hero-opacity", hero, "opacity", 0, 12,
            lambda t: out(t, 2, 3.1)*(1-smooth(t, 8, 9.6)))
    animate("hero-scale", hero, "frameScale", 2, 8,
            lambda t: 1.08-.08*smooth(t, 2, 8))
    fade_track = track("03 / Photograph fade")
    fade = add("hero-fade", "createTimelineGenerator", trackId=fade_track,
        generatorKind="linear-gradient", startSeconds=0, durationSeconds=12,
        parameters={"angleDegrees": 90, "stops": [
            {"position": .08, "color": "#080c13", "opacity": 1},
            {"position": .30, "color": "#080c13", "opacity": 232/255},
            {"position": .80, "color": "#080c13", "opacity": 0}]})
    animate("fade-opacity", fade, "opacity", 0, 12,
            lambda t: out(t, 2, 3.1)*(1-smooth(t, 8, 9.6)))
    branding = track("04 / Wordmark")
    logo = image("wordmark", "looksy-logo-dark.png", branding)
    frame("wordmark-frame", logo, x=90, y=126, width=480, height=480*454/1795, fit="fit")
    animate("wordmark-opacity", logo, "opacity", 0, 12, lambda t: 1-smooth(t, 8, 9.6))
    copy = track("05 / Introduction typography")
    category = title("category", "PHOTO + VIDEO / DESKTOP", copy, 90, 96, 21,
                     "Space Mono", "#d7ff48", 1.8)
    headline = title("headline", "One shoot.\nEvery deliverable.", track("06 / Headline"),
                     90, 456.45, 145)
    description = title("description", "From camera RAW to finished photos,\nlayered designs and films. In one app.",
                        track("07 / Description"), 90, 694, 38, "Space Grotesk", spacing=0)
    for name, clip in [("headline", headline), ("description", description)]:
        animate(name+"-opacity", clip, "opacity", 0, 12,
                lambda t: out(t, 2, 3.1)*(1-smooth(t, 8, 9.6)))
        animate(name+"-lift", clip, "frameOffsetY", 0, 4,
                lambda t: 4*45*(1-out(t, 2, 3.1))/HEIGHT)
    animate("category-opacity", category, "opacity", 0, 12, lambda t: 1-smooth(t, 8, 9.6))
    x = 95
    for i, word in enumerate(["EDIT", "DESIGN", "CUT", "DELIVER"]):
        w, h = len(word)*19*.6+36, 49
        pill = add("pill-"+word, "createTimelineGraphic", trackId=track("Pill / "+word),
            shapeType="rounded-rectangle", startSeconds=0, durationSeconds=12,
            values={"positionX": (x+w/2)/WIDTH, "positionY": (880+h/2)/HEIGHT,
                    "shapeWidth": w/WIDTH, "shapeHeight": h/HEIGHT,
                    "shapeCornerRadius": .5, "backgroundColor": "#bd0b0d12",
                    "shapeStrokeColor": "#55c4c8ce", "shapeStrokeWidth": 1/1080})
        text = title("pill-text-"+word, word, track("Pill text / "+word), x+w/2, 880+h/2,
                     19, "Space Mono", "#d7ff48" if i == 0 else "#f1efdf", 0, alignment="center")
        for name, clip in [("pill-"+word, pill), ("pill-text-"+word, text)]:
            animate(name+"-opacity", clip, "opacity", 0, 12,
                    lambda t: out(t, 5, 6)*(1-smooth(t, 8, 9.6)))
        x += w+12
    desktop_track = track("08 / Native desktop")
    desktop = image("desktop", "desktop-launch/raw-editor.png", desktop_track)
    frame("desktop-frame", desktop, x=285, y=245, width=1350, height=790, fit="fit", radius=.016)
    animate("desktop-opacity", desktop, "opacity", 8, 9.6, lambda t: smooth(t, 8, 9.6))
    desktop_title_track = track("09 / Desktop introduction")
    desktop_label = title("desktop-label", "MEET LOOKSY", desktop_title_track,
                          85, 90, 21, "Space Mono", "#d7ff48", 1.8)
    desktop_title = title("desktop-title", "Your work. From start to finish.",
                          track("10 / Desktop headline"), 85, 174, 104)
    for name, clip in [("desktop-label", desktop_label), ("desktop-title", desktop_title)]:
        animate(name+"-opacity", clip, "opacity", 0, 12, lambda t: smooth(t, 8, 9.6))
    footer = track("11 / Footer")
    title("footer-left", "FOR PROFESSIONALS. FOR THE WORK YOU MAKE FOR YOURSELF.",
          footer, 82, 1048, 17, "Space Mono", "#aeb6c3", 1)
    title("footer-right", "LOOKSY / DESKTOP", track("12 / Footer right"), 1838, 1048, 17,
          "Space Mono", "#aeb6c3", 1, alignment="right")
    return {"reference": "landscape.mp4", "chapter": [0, 12], "steps": STEPS,
            "unresolvedParity": ["desktop circular reveal", "exact browser/native type metrics"]}


if __name__ == "__main__":
    path = ROOT/"opening-landscape.commands.json"
    path.write_text(json.dumps(build(), indent=2)+"\n")
    print(path, len(STEPS), "native commands")
