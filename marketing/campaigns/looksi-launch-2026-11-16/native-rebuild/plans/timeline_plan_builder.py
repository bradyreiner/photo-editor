"""Write planning-only native command recipes. Does not connect to or modify Looksy.

These are inputs to native authoring, never finished native projects or renders.
UNBOUND resource/task IDs deliberately fail the existing author's destination guard.
"""
from pathlib import Path
import hashlib
import json

ROOT = Path(__file__).resolve().parents[5]
OUT = Path(__file__).resolve().parent
V2 = ROOT / "marketing/campaigns/looksi-launch-2026-11-16/v2"
W, H, FPS = 1080, 1920, 30
INK, WHITE = "#08090B", "#F4F3EC"


def write(name, obj):
    (OUT / name).write_text(json.dumps(obj, indent=2, ensure_ascii=False) + "\n")


class Plan:
    def __init__(self, number, title, duration):
        self.number, self.title, self.duration = number, title, duration
        self.steps, self.scenes, self.assets = [], [], {}
        # Graphics lanes sit above media lanes regardless of creation order.
        # A native shape on an ordinary low video lane makes a real backdrop
        # without hiding the source photos/video on later media lanes.
        self.background = self.track("ink", "video")
        self.shape("ink", self.background, 0, duration, "rectangle", .5, .5, 1, 1, INK)

    def add(self, step_name, action, **values):
        step_id = f"c{self.number:02d}-{step_name}"
        assert all(s["id"] != step_id for s in self.steps), step_id
        self.steps.append(dict(id=step_id, commandId="timeline.action." + action, values=values))
        return {"result": step_id, "field": "trackId" if action == "createTimelineTrack" else "createdClipId"}

    def track(self, name, kind="graphics", role=""):
        return self.add("track-" + name, "createTimelineTrack", kind=kind,
                        name=name[:48], audioRole=role)

    def shape(self, name, track, start, duration, shape, x, y, width, height, fill,
              stroke="#00000000", stroke_width=0, radius=0):
        return self.add(name, "createTimelineGraphic", trackId=track, shapeType=shape,
                        startSeconds=start, durationSeconds=duration, values={
                            "positionX": x, "positionY": y, "shapeWidth": width,
                            "shapeHeight": height, "backgroundColor": fill,
                            "shapeCornerRadius": radius, "shapeStrokeColor": stroke,
                            "shapeStrokeWidth": stroke_width})

    def text(self, name, text, start, duration, y=.24, size=88, x=.08,
             family="Space Grotesk", color=WHITE, weight=600, align="left", animate=True):
        track = self.track(name)
        clip = self.add(name, "createTimelineTitle", trackId=track, templateKey="headline",
                        startSeconds=start, durationSeconds=duration, values={
                            "text": text, "fontFamily": family, "fontSize": size,
                            "fontWeight": weight, "italic": False, "letterSpacing": -1,
                            "lineHeight": 1.04, "color": color, "backgroundColor": "#00000000",
                            "alignment": align, "positionX": x, "positionY": y,
                            "safeMarginX": .065, "safeMarginY": .07})
        if animate:
            self.animate(name, clip, opacity=[(0, 0), (.3, 1), (duration-.25, 1), (duration, 0)],
                         titlePositionY=[(0, y+.025), (.4, y)])
        return clip

    def animate(self, name, clip, **channels):
        return self.add(name + "-animation", "setTimelineClipAnimation", clipId=clip,
                        channels=[dict(parameter=parameter, keyframes=[
                            dict(localSeconds=round(round(t*FPS)/FPS, 9), value=value, interpolation="ease-in-out")
                            for t, value in keys]) for parameter, keys in channels.items()])

    def frame(self, name, clip, x=.5, y=.53, width=.84, height=.45, fit="fit", opacity=1):
        return self.add(name + "-frame", "setTimelineClipFrame", clipId=clip,
                        offsetX=4*(x-.5), offsetY=4*(y-.5), scale=1, rotationDegrees=0,
                        perspectiveX=0, perspectiveY=0, opacity=opacity, objectFit=fit,
                        width=width, height=height, cornerRadius=0,
                        cropLeft=0, cropTop=0, cropRight=1, cropBottom=1)

    def media(self, name, relative, start, duration, source_duration=None, source_in=0,
              x=.5, y=.53, width=.84, height=.45, fit="fit", kind="video"):
        path = (V2 / relative).resolve()
        assert path.is_file(), path
        self.assets[str(path)] = dict(path=str(path), bytes=path.stat().st_size,
            sha256=hashlib.sha256(path.read_bytes()).hexdigest())
        track = self.track(name, kind, "music" if kind == "audio" else "")
        clip = self.add(name, "createTimelineClip", trackId=track,
                        assetId=f"native-rebuild:c{self.number:02d}:{name}", filePath=str(path),
                        displayLabel=name, durationSeconds=source_duration or duration,
                        trimStartSeconds=source_in, trimEndSeconds=source_in+duration,
                        startSeconds=start, posterSource="")
        if kind != "audio":
            self.frame(name, clip, x, y, width, height, fit)
        return clip

    def scene(self, start, end, purpose, native_objects, acceptance):
        self.scenes.append(dict(startSeconds=start, endSeconds=end, purpose=purpose,
                                nativeObjects=native_objects, acceptance=acceptance))

    def score(self, relative):
        clip = self.media("source-score", relative, 0, self.duration, kind="audio")
        # Native gain automation retains a genuinely editable mix decision.
        self.animate("source-score", clip, audioGainDb=[(0,-60),(.25,-8),
                     (self.duration-.65,-8),(self.duration,-60)])

    def end(self, start, cta):
        d = self.duration-start
        self.text("builder-disclosure", "I’m building LOOKSi.", start, d, .29, 80)
        self.text("product-category", "Photo + video.", start+.3, d-.3, .42, 82)
        self.text("maker-line", "For people who care\nhow it looks.", start+.6, d-.6, .56, 64)
        self.text("cta", cta, start+.9, d-.9, .76, 65, color="#00FF96")

    def save(self):
        target = ROOT / f"marketing/campaigns/looksi-launch-2026-11-16/native-rebuild/campaign{self.number:02d}/projects/campaign{self.number:02d}-native-v1.json"
        self.add("save-native-project-copy", "saveTimeline", outputPath=str(target))
        data = dict(schema="looksy.native-authoring-command-plan/v1", executionState="planning-only-unbound",
            resourceId="UNBOUND", taskId="UNBOUND", title=self.title,
            authoringOrigin="Execute typed commands inside Looksy; this JSON is not an imported project.",
            sequence=dict(width=W,height=H,frameRate=FPS,audioSampleRate=48000),
            colorSpace="Rec.709", dataLevels="Legal", durationSeconds=self.duration,
            nativeProjectSchemaVersionObserved=8,
            nativeCreation=dict(tool="timeline.create_draft", arguments=dict(settings=dict(
                timelineName=f"LOOKSi C{self.number:02d} — {self.title}", width=W, height=H,
                frameRate=FPS,audioSampleRate=48000,colorSpace="Rec.709",dataLevels="Legal",trackLayout="creator_social"))),
            bindingRequired=["Fresh native resourceId", "Current admitted taskId", "Current startingDocumentHash",
                "Canonical existing outputDirectory admitted to task", "Live available command schemas"],
            admissionActions=[dict(kind="text",summary="Create editable campaign titles and native graphic shapes."),
                dict(kind="layers",summary="Create and arrange native Timeline tracks and clips."),
                dict(kind="media",summary="Import only the listed raw/native source assets as individual clips."),
                dict(kind="framing",summary="Frame sources and animate native position, scale and opacity."),
                dict(kind="audio",summary="Place admitted audio source clips and author gain envelopes in Timeline."),
                dict(kind="delivery",summary="Save readback-verified native project and prepare native H.264 export.")],
            sourceAssets=list(self.assets.values()), scenePlan=self.scenes, steps=self.steps,
            completionStatus="Not authored in Looksy; no native deliverable or export exists from this plan.",
            reviewRequirements=["Inspect native title/shape text and source identities", "Verify track compositing order in native preview",
                "Native preview and visible proof at every cut", "Readback-verified project save", "Native delivery preflight",
                "Native export receipt reaches completed with output integrity", "Full decode/phone-size/audio review", "Human creative approval pending"])
        write(f"timeline-campaign{self.number:02d}.commands.json",data)
        return data


def build():
    plans=[]
    p=Plan(1,"I wanted to make this.",32)
    p.text("hook","I wanted\nto make this.",0,5,.30,108)
    p.scene(0,5,"A direct maker letter, carried by native type.",["editable headline"],"No founder filming placeholder; disclosure is first person.")
    p.media("mountain", "media/sources/nepal-mountain-working-copy.jpg",5,9,y=.56,height=.43)
    p.text("why","A place for the images\nI want to make.",5,9,.19,67)
    p.scene(5,14,"Real source photograph and maker motive.",["photo clip","two-line title"],"Photo comes from listed source, with no app output claim.")
    p.media("native-photo-after","media/exports/finish/nepal-mountain-working-copy-full-resolution-2.png",14,8,y=.53,height=.46)
    p.text("still-building","Still building.\nStill looking closely.",14,8,.19,70)
    p.text("native-date","Native photo export · 16 Nov 2026",14,8,.82,35,family="Space Mono",weight=400)
    p.scene(14,22,"An actual native photo export as a source insert.",["verified native PNG clip","editable date label"],"Use only the distinct -2 finish file, not failed first export.")
    p.text("invitation","Come shape\nwhat comes next.",22,4,.35,87)
    p.end(26,"What would you make?")
    p.scene(22,32,"Maker invitation and category disclosure.",["editable titles"],"No release, pricing, speed or shipping promise.")
    p.score("campaign01/audio/01-original-letter-bed.wav");plans.append(p.save())

    p=Plan(2,"NEPAL. In three forms.",42)
    p.text("hook","NEPAL.\nIn three forms.",0,4,.29,104)
    p.media("photo-treatment","media/exports/finish/nepal-mountain-working-copy-full-resolution-2.png",4,7)
    p.text("photo-label","01 / A photo treatment",4,7,.18,58)
    p.text("photo-date","Native photo output · 16 Nov 2026",4,7,.82,33,family="Space Mono",weight=400)
    p.scene(0,11,"Open the native-output casebook, then show the real photo treatment.",["titles","native photo source clip"],"This is a mixed-session casebook, not a new continuous live walkthrough.")
    p.media("native-archival-study","media/archive/native-timeline-review-2026-11-02.mp4",11,12,source_duration=12,y=.53,height=.34)
    p.text("study-label","02 / A 12-second\nmotion study",11,12,.18,64)
    p.text("study-date","Native Timeline export · 2 Nov 2026",11,12,.82,33,family="Space Mono",weight=400)
    p.scene(11,23,"Play the actual historical native motion study as one source insert.",["12-second archival output clip","date title"],"Keep source visible in a frame; do not represent this insert as the whole new authored film.")
    for i in range(6):
        p.media(f"board-{i+1}",f"media/boards/nepal-near-far-{i+1:02d}.png",23+2*i,2,y=.53,height=.43)
    p.text("board-label","03 / A story\nin six boards",23,12,.17,67)
    p.text("board-date","Native board exports · 16 Nov 2026",23,12,.83,33,family="Space Mono",weight=400)
    p.scene(23,35,"Turn through six actual native board output inserts.",["six individually timed board PNG clips","editable labels"],"Film inserts do not establish that a new storyboard/carousel native document is authored.")
    p.end(35,"Which form would you try?")
    p.scene(35,42,"Close the casebook with an invitation.",["editable title clips"],"Native output proof scope and historical dates stay legible.")
    p.score("campaign02/audio/original-score.wav");plans.append(p.save())

    p=Plan(3,"Keep the gold. Keep the quiet blues.",33)
    p.text("hook","Keep the gold.\nKeep the quiet blues.",0,4,.29,89)
    p.media("baseline","media/exports/baseline/nepal-mountain-working-copy-full-resolution.png",4,8)
    p.text("baseline-label","The starting point.",4,8,.18,70)
    p.scene(0,12,"Set the photographic aim and hold the actual baseline.",["editable title","native baseline PNG clip"],"No fake slider UI or before/after animation pretending to be live.")
    p.media("finish","media/exports/finish/nepal-mountain-working-copy-full-resolution-2.png",12,14)
    p.text("recipe","5900 K\nContrast +12\nExposure +0.15 EV",12,8,.19,61,family="Space Mono",weight=400)
    p.text("combined","Three settings, together.",20,6,.18,67)
    p.text("recipe-scope","Native photo exports · 16 Nov 2026",12,14,.83,33,family="Space Mono",weight=400)
    p.scene(12,26,"Hold the real combined finish long enough to compare and read.",["native finish clip","recipe titles"],"These three changes were combined; do not imply isolated effects, recorded slider changes or Undo proof.")
    p.end(26,"What would you keep?")
    p.scene(26,33,"Ask for taste, not software purchase.",["editable disclosure and CTA"],"No release or price assertion.")
    p.score("campaign03/audio/original-score.wav");plans.append(p.save())

    p=Plan(4,"The last tweak. Again.",24)
    p.text("hook","The last tweak.",0,3,.30,103)
    p.text("again","Again.",1.5,1.5,.47,115,family="Instrument Serif",weight=400,color="#00FF96")
    p.scene(0,3,"A confidently held hook, then a second thought.",["two separate native title clips"],"No implied version-management product capability.")
    for i,label in enumerate(["final.jpg","final_final.jpg","final_final_2.jpg","actually_final.jpg"]):
        p.text(f"revision-{i}",label,3+i*.5,5-i*.5,.28+i*.075,43,x=.11+i*.025,
               family="Space Mono",weight=400,color=WHITE if i<3 else "#00FF96")
    p.text("question","One more thing…",6,2,.70,62,family="Instrument Serif",weight=400)
    p.scene(3,8,"File-name pile becomes relatable creative indecision.",["four independent text clips with native entrance keys"],"Filenames are graphic storytelling, not a fake app file browser.")
    # Rebuild the poster itself from native editable primitives, rather than importing the prior rendered plate.
    for index,(color,cx) in enumerate([("#FF2222",.40),("#00FF55",.50),("#2255FF",.60)]):
        track=p.track(f"poster-color-{index}")
        c=p.shape(f"poster-color-{index}",track,8,10,"ellipse",cx,.51,.30,.23,color)
        p.add(f"poster-color-{index}-blend","setTimelineClipBlendMode",clipId=c,blendMode="screen")
        p.animate(f"poster-color-{index}",c,frameScale=[(0,.94),(2,1.05),(4,.98),(7,1),(10,1)])
    p.text("poster-type","A LITTLE\nMORE EYE.",8,10,.51,83,x=.5,align="center",family="Archivo Black")
    p.text("art-disclosure","Original campaign graphic",8,10,.77,36,family="Space Mono",weight=400)
    p.text("choice-1","A bit bigger?",8,2,.18,66)
    p.text("choice-2","Or a little quieter?",10,2,.18,66)
    p.text("choice-3","Move it back.",12,2,.18,66)
    p.text("choice-4","There.",14,4,.18,79,family="Instrument Serif",weight=400)
    # Native pointer cross uses ordinary editable shapes, not an external cursor overlay.
    ct=p.track("pointer-cross")
    ch=p.shape("pointer-horizontal",ct,8,6,"rectangle",.78,.62,.065,.003,WHITE)
    cv=p.shape("pointer-vertical",ct,8,6,"rectangle",.78,.62,.005,.037,WHITE)
    for name,c in [("pointer-h",ch),("pointer-v",cv)]:
        p.animate(name,c,frameOffsetX=[(0,0),(2,-.12),(4,.05),(6,0)],frameOffsetY=[(0,0),(2,-.10),(4,.06),(6,0)])
    p.scene(8,18,"Native poster colors and type explore scale; a pointer cross follows considered alternatives, then rests.",["three screen-blended native ellipses","editable poster title","four choice titles","two pointer shapes"],"No accidental clipping. Poster explicitly an original campaign graphic, not Looksy app output or feature proof.")
    p.end(18,"What’s your\n“last tweak”?")
    p.scene(18,24,"A breath and a larger closing question.",["editable maker disclosure and CTA"],"All copy fits a phone preview; no face shoot or fake UI prerequisite.")
    p.score("campaign04/audio/original-sound-design.wav");plans.append(p.save())

    p=Plan(5,"Make room for your eye.",36)
    p.text("hook","Make room\nfor your eye.",0,4,.29,106,family="Instrument Serif",weight=400)
    p.scene(0,4,"An unhurried typographic opening.",["editable native title"],"Hold an invitation rather than making a product-performance claim.")
    photo_scenes=[("flowers","IMG_4870.jpg",4,6,"For the color you noticed."),
                  ("boats","IMG_4969.jpg",10,6,"For the quieter frame."),
                  ("dawn","IMG_5077.jpg",16,6,"For the light you stayed for.")]
    for name,path,start,dur,copy in photo_scenes:
        c=p.media(name,"media/sources/"+path,start,dur,y=.54,height=.51)
        p.animate(name,c,frameScale=[(0,1),(dur,1.045)])
        p.text(name+"-line",copy,start,dur,.17,62,family="Instrument Serif",weight=400)
        p.scene(start,start+dur,copy,["source photograph clip","slow native scale keys","editable type"],"Keep subject and horizon clear; no synthesized photographic detail or fake app output.")
    p.media("native-product-moment","media/exports/finish/nepal-mountain-working-copy-full-resolution-2.png",22,7)
    p.text("native-line","Still looking.\nStill making.",22,7,.18,77)
    p.text("native-date","Native photo output · 16 Nov 2026",22,7,.82,34,family="Space Mono",weight=400)
    p.scene(22,29,"A real dated native product output anchors the manifesto.",["verified native photo source","editable date title"],"Output proof does not imply current-session recording or broader workflow qualification.")
    p.end(29,"What catches your eye?")
    p.scene(29,36,"Simple maker disclosure and open invitation.",["editable type"],"No feature, speed, price, release or availability promise.")
    p.score("campaign05/audio/original-score.wav");plans.append(p.save())
    manifest=dict(schema="looksy.native-timeline-plan-set/v1",status="Planning only; native creation not run",
        priorV2Status="External references; do not count as native authored or native-complete assets.",
        campaignCount=len(plans),plannedFilms=[dict(campaign=f"{i+1:02d}",title=p["title"],
            seconds=p["durationSeconds"],commands=len(p["steps"]),sourceAssets=len(p["sourceAssets"]),
            plan=f"timeline-campaign{i+1:02d}.commands.json") for i,p in enumerate(plans)])
    write("timeline-plan-set.json",manifest)
    return plans


if __name__ == "__main__":
    build()
