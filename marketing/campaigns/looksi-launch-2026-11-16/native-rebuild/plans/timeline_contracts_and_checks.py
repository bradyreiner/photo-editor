"""Read-only source/plan evidence; writes reports under this plans directory only."""
from pathlib import Path
import hashlib
import json
import wave

OUT = Path(__file__).resolve().parent
ROOT = OUT.parents[4]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def object_schema(properties, required=()):
    return dict(type="object", properties=properties, required=list(required), additionalProperties=False)


def number(low, high):
    return dict(type="number", minimum=low, maximum=high)


def string(values=None, **kwargs):
    return dict(type="string", **({"enum":values} if values else {}), **kwargs)


def write(name,obj):
    (OUT/name).write_text(json.dumps(obj,indent=2,ensure_ascii=False)+"\n")


def build_contracts():
    style = dict(text=string(maxLength=500), fontFamily=string(maxLength=120),
        fontSize=number(1,360), fontWeight=dict(type="integer",minimum=1,maximum=1000),
        italic=dict(type="boolean"),letterSpacing=number(-50,100),lineHeight=number(.5,3),
        color=string(),backgroundColor=string(),alignment=string(["left","center","right"]),
        positionX=number(-4,4),positionY=number(-4,4),safeMarginX=number(0,.5),safeMarginY=number(0,.5),
        shapeWidth=number(.000001,4),shapeHeight=number(.000001,4),shapeCornerRadius=number(0,.5),
        shapeStrokeColor=string(),shapeStrokeWidth=number(0,.05))
    duration=dict(startSeconds=number(0,21600),durationSeconds=number(.000001,3600))
    key=object_schema(dict(localSeconds=number(0,3600),value=dict(type="number"),
        interpolation=string(["hold","linear","ease-in","ease-out","ease-in-out","bezier"])),["localSeconds","value"])
    channel=object_schema(dict(parameter=string(),keyframes=dict(type="array",items=key,minItems=0,maxItems=20000)),["parameter","keyframes"])
    frame=dict(clipId=string(),offsetX=number(-4,4),offsetY=number(-4,4),scale=number(.000001,4),
        rotationDegrees=number(-180,180),perspectiveX=number(-.35,.35),perspectiveY=number(-.35,.35),
        opacity=number(0,1),objectFit=string(["fit","fill","stretch"]),width=number(.000001,4),
        height=number(.000001,4),cornerRadius=number(0,.5),cropLeft=number(0,1),cropTop=number(0,1),
        cropRight=number(0,1),cropBottom=number(0,1))
    clip=dict(trackId=string(),assetId=string(),filePath=string(),displayLabel=string(),
        durationSeconds=number(.000001,3600),trimStartSeconds=number(0,21600),
        trimEndSeconds=number(.000001,21600),startSeconds=number(0,21600),posterSource=string())
    schemas={
        "timeline.action.createTimelineTrack":object_schema(dict(kind=string(["video","graphics","audio"]),
            name=string(maxLength=48),audioRole=string(["","dialogue","voiceover","music","ambience","effects","commentary"])),["kind"]),
        "timeline.action.createTimelineTitle":object_schema(dict(trackId=string(),templateKey=string(
            ["basic","headline","lower-third","callout","quote","brand-card"]),**duration,values=object_schema(style)),
            ["trackId","templateKey","startSeconds","durationSeconds","values"]),
        "timeline.action.createTimelineGraphic":object_schema(dict(trackId=string(),shapeType=string(
            ["rectangle","rounded-rectangle","ellipse"]),**duration,values=object_schema(style)),
            ["trackId","shapeType","startSeconds","durationSeconds","values"]),
        "timeline.action.createTimelineClip":object_schema(clip,[k for k in clip if k!="posterSource"]),
        "timeline.action.setTimelineClipFrame":object_schema(frame,list(frame)),
        "timeline.action.setTimelineClipAnimation":object_schema(dict(clipId=string(),channels=dict(
            type="array",items=channel,minItems=1,maxItems=32)),["clipId","channels"]),
        "timeline.action.setTimelineClipBlendMode":object_schema(dict(clipId=string(),blendMode=string(
            ["normal","screen","soft-light"])),["clipId","blendMode"]),
        "timeline.action.saveTimeline":object_schema(dict(outputPath=string()),["outputPath"]),
    }
    sources=["docs/looksy-agent-interface.md","apps/desktop/src/photo_mcp_server.cpp",
        "apps/desktop/src/agent_timeline_commands.h","apps/desktop/src/timeline_document_controller.cpp",
        "apps/desktop/src/timeline_draft_lifecycle.cpp","marketing/native-recreation-v4/author_native.py"]
    examples=[]
    for name in ["looksy-native-portrait-in-progress-checkpoint-1.json","looksy-native-landscape-in-progress-checkpoint-5.json"]:
        path=ROOT/"marketing/native-recreation-v4/projects"/name
        d=json.loads(path.read_text())
        all_clips=[c for t in d["tracks"] for c in t.get("clips",[])]
        title=next((c for c in all_clips if c.get("mediaType")=="title" and c.get("titleText")),{})
        shape=next((c for c in all_clips if c.get("titleShapeType")),{})
        examples.append(dict(path=str(path.relative_to(ROOT)),sha256=sha(path),version=d["version"],
            sequenceFormat=d["sequenceFormat"],trackCount=len(d["tracks"]),clipCount=len(all_clips),
            rootKeys=list(d),sampleTrackFields=["trackId","kind","laneIndex","clips","muted","audioRole"],
            sampleTitle={k:title[k] for k in ["clipId","mediaType","titleText","titleFontFamily","titleFontSize",
                "titlePositionX","titlePositionY","startSeconds","durationSeconds","keyframes"] if k in title},
            sampleShape={k:shape[k] for k in ["clipId","mediaType","titleShapeType","titleShapeWidth",
                "titleShapeHeight","titleBackgroundColor","titlePositionX","titlePositionY"] if k in shape},
            portability="Historical source paths may be offline; not a ready-to-import campaign template."))
    out=dict(schema="looksy.native-timeline-source-contract-projection/v1",
        authority="Current working-tree source inspection only. Fetch app.command_schema live before mutation.",
        projectionScope="Exact generated-plan argument subset. Optional native fields outside this subset are intentionally omitted; this is not the complete live schema.",
        nativeSavedProjectVersion=8,sourceFiles=[dict(path=p,sha256=sha(ROOT/p)) for p in sources],
        valueSchemas=schemas,referenceNotation=dict(result="earlier plan step ID",field="trackId or createdClipId",
            resolution="Resolve author-side before app.invoke_command; never send reference object as a literal native ID."),
        nativeExamples=examples)
    write("timeline-source-contracts.json",out)
    return out


def schema_check(value,schema,path="values"):
    # Validate the selected source-derived contract after replacing symbolic references with dummy IDs.
    typ=schema.get("type")
    if typ=="object":
        assert isinstance(value,dict),(path,"object")
        assert set(schema.get("required",[]))<=set(value),(path,"missing required")
        assert not set(value)-set(schema["properties"]),(path,"unexpected fields",set(value)-set(schema["properties"]))
        for k,v in value.items(): schema_check(v,schema["properties"][k],path+"."+k)
    elif typ=="array":
        assert isinstance(value,list),path
        assert schema.get("minItems",0)<=len(value)<=schema.get("maxItems",float("inf")),path
        for i,v in enumerate(value): schema_check(v,schema["items"],f"{path}[{i}]")
    elif typ=="string": assert isinstance(value,str),path
    elif typ=="boolean": assert isinstance(value,bool),path
    elif typ in ("integer","number"):
        assert not isinstance(value,bool) and isinstance(value,int if typ=="integer" else (int,float)),path
        assert schema.get("minimum",float("-inf"))<=value<=schema.get("maximum",float("inf")),path
    if "enum" in schema: assert value in schema["enum"],(path,value)
    if isinstance(value,str): assert len(value)<=schema.get("maxLength",float("inf")),path


def check(contracts):
    results=[]
    for path in sorted(OUT.glob("timeline-campaign*.commands.json")):
        p=json.loads(path.read_text());seen={};durations={};sources={a["path"]:a for a in p["sourceAssets"]}
        assert p["resourceId"]==p["taskId"]=="UNBOUND"
        assert p["executionState"]=="planning-only-unbound"
        assert p["steps"][0]["values"]["kind"]=="video", "Full-frame ink must not obscure media from a top graphics lane"
        def resolve(v):
            if isinstance(v,dict) and set(v)=={"result","field"}:
                assert v["result"] in seen,(path,v)
                expected="trackId" if seen[v["result"]]["commandId"].endswith("createTimelineTrack") else "createdClipId"
                assert v["field"]==expected,(path,v)
                return "resolved-native-id"
            if isinstance(v,dict): return {k:resolve(x) for k,x in v.items()}
            if isinstance(v,list): return [resolve(x) for x in v]
            return v
        for step in p["steps"]:
            assert step["id"] not in seen
            v=step["values"];schema_check(resolve(v),contracts["valueSchemas"][step["commandId"]])
            if "durationSeconds" in v:
                length=v.get("trimEndSeconds",v["durationSeconds"])-v.get("trimStartSeconds",0)
                assert 1/30<=length<=p["durationSeconds"]
                assert 0<=v.get("startSeconds",0)<=p["durationSeconds"]-length+1e-6
                durations[step["id"]]=length
            if step["commandId"].endswith("createTimelineClip"):
                a=sources[v["filePath"]];fp=Path(a["path"])
                assert fp.is_file() and str(fp.resolve())==a["path"] and not fp.is_symlink()
                assert a["sha256"]==sha(fp) and a["bytes"]==fp.stat().st_size
                assert v["trimEndSeconds"]<=v["durationSeconds"]
                assert not any(part in fp.parts for part in ["videos","carousels"]),"External finished composition excluded"
                assert fp.name!="nepal-mountain-working-copy-full-resolution.png" or fp.parent.name=="baseline"
                if fp.suffix==".wav":
                    with wave.open(str(fp)) as w: assert v["trimEndSeconds"]<=w.getnframes()/w.getframerate()+1e-6
            if step["commandId"].endswith("setTimelineClipAnimation"):
                length=durations[v["clipId"]["result"]]
                for channel in v["channels"]:
                    times=[k["localSeconds"] for k in channel["keyframes"]]
                    assert times==sorted(set(times)) and all(0<=t<=length for t in times)
                    assert all(abs(t*30-round(t*30))<1e-4 for t in times)
            seen[step["id"]]=step
        assert p["steps"][-1]["commandId"]=="timeline.action.saveTimeline"
        output=Path(p["steps"][-1]["values"]["outputPath"])
        assert "native-rebuild" in output.parts and output.suffix==".json" and not output.exists()
        assert p["scenePlan"][0]["startSeconds"]==0 and p["scenePlan"][-1]["endSeconds"]==p["durationSeconds"]
        assert all(a["endSeconds"]==b["startSeconds"] for a,b in zip(p["scenePlan"],p["scenePlan"][1:]))
        assert not any("export" in s["commandId"] for s in p["steps"])
        results.append(dict(plan=path.name,status="pass-source-and-plan-lint-only",commands=len(seen),
                            sourceAssets=len(sources),durationSeconds=p["durationSeconds"]))
    report=dict(schema="looksy.native-plan-lint/v1",status="passed",nativeExecutionPerformed=False,
        checks=["Unbound destination guards present", "Unique stable plan step IDs",
                "References point only backward and select correct returned ID field",
                "Selected source schema properties, enums, types and bounds",
                "Clip bounds within sequence and native minimum one-frame duration",
                "Canonical source files and exact bytes/SHA-256",
                "No finished external campaign video/carousel import; failed first finish excluded",
                "Audio source length covers native trim", "Ordered unique local keyframes within clip on 30fps clock",
                "Native save is last, fresh and under native-rebuild", "Scene coverage starts/ends on film bounds",
                "Full-frame backdrop uses a low ordinary video lane, not an above-media graphics lane",
                "Export kept outside authoring mutation recipe"],
        plans=results,limitations=["No live schema discovery, native invocation, native preview, save or render was performed.",
            "These checks do not prove font resolution, compositing, photographic color, rendered quality, native editability or human approval."])
    write("timeline-plan-lint.json",report)
    print(json.dumps(report,indent=2))


if __name__=="__main__":
    check(build_contracts())
