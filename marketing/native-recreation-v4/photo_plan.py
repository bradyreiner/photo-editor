"""Native editable Photo chapter, compiled from the approved reference beats."""
from pathlib import Path
import json
import math
import re

from PIL import ImageFont
import opening_plan as d

ROOT = Path(__file__).resolve().parent
SOURCE = ROOT.parent/"animated-trailer/src/creative-launch"
FPS = 30
W, H = 1920, 1080
INK, PAPER, LIME = "#0b0d12", "#f1efdf", "#d7ff48"


def js_data(filename):
    text = (SOURCE/filename).read_text().split("=", 1)[1].strip().rstrip(";")
    text = re.sub(r"\b([A-Za-z_]\w*)\s*:", r'"\1":', text)
    return json.loads(re.sub(r",\s*([}\]])", r"\1", text))


def text(name, content, x, top, size, start, duration, family="Anton", color=PAPER,
         line_height=1.01, spacing=-1.8, width=None):
    if width is not None:
        font_path = d.ROOT.parent/"animated-trailer/public/fonts/rebuild"/(
            "SpaceGrotesk.ttf" if family == "Space Grotesk" else "Anton.ttf")
        font = ImageFont.truetype(str(font_path), round(size))
        lines = []
        for paragraph in content.split("\n"):
            line = ""
            for word in paragraph.split():
                candidate = (line+" "+word).strip()
                if line and font.getlength(candidate) > width:
                    lines.append(line);line = word
                else:line = candidate
            lines.append(line)
        content = "\n".join(lines)
    clip = d.title(name, content, d.track(name+" lane"), x,
                   top+len(content.split("\n"))*size*line_height/2,
                   size, family, color, spacing, start, duration)
    d.STEPS[-1]["values"]["values"]["lineHeight"] = line_height
    return clip


def shape(name, x, y, width, height, start, duration, color=INK,
          radius=0, stroke="#00000000", stroke_width=0):
    return d.add(name, "createTimelineGraphic", trackId=d.track(name+" lane"),
        shapeType="rounded-rectangle" if radius else "rectangle",
        startSeconds=start, durationSeconds=duration, values={
            "positionX": (x+width/2)/W, "positionY": (y+height/2)/H,
            "shapeWidth": width/W, "shapeHeight": height/H,
            "shapeCornerRadius": min(.5, radius/min(width,height)),
            "backgroundColor": color, "shapeStrokeColor": stroke,
            "shapeStrokeWidth": stroke_width/min(W,H)})


def image(name, source, x, y, width, height, start, duration, radius=0):
    clip = d.image(name, source, d.track(name+" lane"), start, duration)
    d.frame(name+" frame", clip, x, y, width, height, radius=radius/min(width,height))
    return clip


def mask(name, clip, x, y, width, height, radius=0):
    d.add(name, "setTimelineClipMask", clipId=clip, shape="rectangle",
          left=x/W, top=y/H, right=(x+width)/W, bottom=(y+height)/H,
          cornerRadius=radius/min(width,height))


def enter(name, clip, duration, at=0):
    d.animate(name+" opacity", clip, "opacity", 0, duration, lambda s:d.out(s,at,at+.55))
    d.animate(name+" lift", clip, "frameOffsetY", 0, duration,
              lambda s:4*45*(1-d.out(s,at,at+.75))/H)


def vector(name, commands, x, y, width, height, view_width, view_height, start, duration,
           stroke=PAPER, stroke_width=3, fill="#00000000", frames=None):
    parameters = {"viewBoxWidth": view_width, "viewBoxHeight": view_height,
                  "commands": commands, "stroke": stroke, "strokeWidth": stroke_width,
                  "fill": fill, "frames": frames or []}
    clip = d.add(name, "createTimelineGenerator", trackId=d.track(name+" lane"),
        generatorKind="vector-path", startSeconds=start, durationSeconds=duration,
        parameters={"vectorPath":parameters})
    d.frame(name+" frame", clip, x, y, width, height, fit="stretch")
    return clip


def poly(points, close=False):
    result=[{"verb":"M" if i==0 else "L","points":p} for i,p in enumerate(points)]
    if close:result.append({"verb":"Z","points":[]})
    return result


def caption(index, title, detail, start, duration):
    for name, clip, at in [
        ("label",text(f"photo-{index}-label","01 / PHOTO EDITOR",82,254,21,start,duration,
                      "Space Mono",LIME,1.2,1.8),0),
        ("title",text(f"photo-{index}-title",title,82,305,91,start,duration),.05),
        ("detail",text(f"photo-{index}-detail",detail,82,
                       305+len(title.split("\n"))*91*1.01+28,
                       33,start,duration,"Space Grotesk","#c9ced6",1.4,0,width=445),.2)]:
        enter(f"photo-{index}-"+name,clip,duration,at)


def plate(name, folder, prefix, count, progress, x, y, width, height, start, duration):
    # Each imported plate only lives while it contributes to the reference
    # blend. Adjacent source frames stay registered throughout the adjustment.
    values=[count*progress(f/FPS) for f in range(round(duration*FPS)+1)]
    for i in range(count+1):
        begin = 0 if i==0 else next((f for f,n in enumerate(values) if n>=i-1),len(values)-1)
        finish = next((f for f,n in enumerate(values) if n>=i+1),len(values)-1) if i<count else len(values)-1
        if finish<=begin:continue
        a,b=begin/FPS,finish/FPS
        clip=image(f"{name}-{i}",f"{folder}/{prefix}{i}.png",x,y,width,height,start+a,b-a,22)
        if i:
            d.animate(f"{name}-{i} blend",clip,"opacity",0,b-a,
                      lambda s,i=i,a=a:max(0,min(1,count*progress(s+a)-(i-1))))


def before_after(name, source, start, duration, finish, x=650,y=164,width=1100,height=733.7):
    before=image(name+" before",source,x,y,width,height,start,duration,22)
    mask(name+" window",before,x,y,width,height)
    show=lambda s:d.smooth(s,.15,.65)*(1-d.smooth(s,finish,finish+.6))
    d.animate(name+" wipe",before,"mask:right",0,duration,
              lambda s:(x+width*.38*show(s))/W)
    bar=shape(name+" divider",x-1,y,2,height,start,duration,PAPER)
    d.animate(name+" divider x",bar,"titlePositionX",0,duration,
              lambda s:(x+width*.38*show(s))/W)
    d.animate(name+" divider opacity",bar,"opacity",0,duration,show)
    for side,tx in [("BEFORE",x+22),("AFTER",x+width-100)]:
        label=text(name+" "+side,side,tx,y+22,16.5,start,duration,"Space Mono",PAPER,1.2,1.8)
        d.animate(name+" "+side+" opacity",label,"opacity",0,duration,show)


def curve_plot(start,duration):
    data=js_data("recipe-data.ts")
    x,y,scale=1327.6,507.2,1.1
    bg=shape("curve-panel",x,y,396,363,start,duration,"#f2111820",13.2)
    grid=[]
    for i in range(5):grid+=poly([[20+i*80,20],[20+i*80,280]])+poly([[20,20+i*65],[340,20+i*65]])
    vector("curve-grid",grid,x,y,396,363,360,330,start,duration,"#28bfccd3",1)
    vector("curve-diagonal",poly([[20,280],[340,20]]),x,y,396,363,360,330,start,duration,"#37aeb9c3",1)
    def state(s):
        a=d.smooth(s,.9,2.6);b=d.smooth(s,3.5,5.4)
        return a,b,(a+b)/2
    def coordinates(s):
        p=state(s)[2]*16;a=math.floor(p);b=min(16,a+1);f=p-a
        return [c for i,v in enumerate(data['x']) for c in
                [20+v*320,20+(1-(data['frames'][a][i]*(1-f)+data['frames'][b][i]*f))*260]]
    initial=coordinates(0)
    frames=[{"seconds":f/FPS,"coordinates":coordinates(f/FPS)} for f in range(round(duration*FPS)+1)]
    vector("curve-native-samples",poly([initial[i:i+2] for i in range(0,len(initial),2)]),
            x,y,396,363,360,330,start,duration,frames=frames)
    for name,px,py,delta,channel in [("shadow",.22,.22,-.06,0),("midtone",.55,.55,.05,1)]:
        dot=d.add("curve-"+name,"createTimelineGraphic",trackId=d.track("curve-"+name+" lane"),
                  shapeType="ellipse",startSeconds=start,durationSeconds=duration,
                  values={"positionX":(x+(20+px*320)*scale)/W,
                          "positionY":(y+(20+(1-py)*260)*scale)/H,
                          "shapeWidth":13.2/W,"shapeHeight":13.2/H,"backgroundColor":LIME})
        d.animate("curve-"+name+" move",dot,"titlePositionY",0,duration,
                  lambda s,py=py,delta=delta,channel=channel:(y+(20+(1-(py+delta*state(s)[channel]))*260)*scale)/H)
    text("curve-caption","MASTER / TWO EDITABLE POINTS",x+22,y+322,14.3,start,duration,"Space Mono",LIME,1.2,0)
    # Pointer stays a native closed path with native transform curves.
    pointer=vector("curve-pointer",poly([[0,0],[0,26],[7,20],[14,32],[20,29],[13,17],[24,17]],True),
                   x,y,26.4,35.2,24,32,start,duration,"#0d1723",2,"#ffffff")
    def cursor(s):
        a,b,_=state(s);p=(20+.22*320,20+(1-(.22-.06*a))*260)
        q=(20+.55*320,20+(1-(.55+.05*b))*260)
        u=0 if s<2.8 else d.smooth(s,2.8,3.45)
        return p[0]*(1-u)+q[0]*u,p[1]*(1-u)+q[1]*u
    d.animate("curve-pointer x",pointer,"frameOffsetX",0,duration,
              lambda s:4*((x+scale*cursor(s)[0]+13.2)/W-.5))
    d.animate("curve-pointer y",pointer,"frameOffsetY",0,duration,
              lambda s:4*((y+scale*cursor(s)[1]+17.6)/H-.5))
    d.animate("curve-pointer opacity",pointer,"opacity",0,duration,
              lambda s:d.out(s,.2,.6)*(1-d.smooth(s,5.9,6.4)))


def exposure_control(start, duration):
    sx,sy,k=778.7,925.2,2.31
    u=lambda s:d.smooth(s,.7,4.5)
    tool=lambda s:d.out(s,0,.45)*(1-d.smooth(s,duration-.35,duration))
    parts=[]
    parts.append(shape("exposure-panel",754.5,901,880,140,start,duration,"#111a24",13.2))
    label=text("exposure-label","Exposure",sx+6*k,sy+6*k,13*k,start,duration,
               "Helvetica Neue","#e2e2e2",1.2,0)
    parts.append(label)
    rail_x,rail_y,rail_w=sx+110*k,sy+18.5*k,120*k
    parts.append(shape("exposure-rail",rail_x,rail_y,rail_w,3*k,start,duration,"#4d585858",2*k))
    tick_commands=[]
    for i in range(11):
        tick_commands+=poly([[i*119/10,0],[i*119/10,4 if i%5==0 else 2]])
    ticks=vector("exposure-ticks",tick_commands,rail_x,rail_y-4*k,120*k,7*k,
                 120,7,start,duration,"#47b5b5b5",1)
    parts.append(ticks)
    # The fill is a true animated native path, not a disconnected overlay.
    fill_frames=[]
    for f in range(round(duration*FPS)+1):
        s=f/FPS;right=60+120*.14*u(s)
        fill_frames.append({"seconds":s,"coordinates":[60,0,right,0,right,3,60,3]})
    fill=vector("exposure-fill",poly([[60,0],[60,0],[60,3],[60,3]],True),
                rail_x,rail_y,120*k,3*k,120,3,start,duration,
                "#00000000",0,"#d1c4c4c4",fill_frames)
    parts.append(fill)
    centre=lambda s:sx+(110+(.5+.14*u(s))*104+8)*k
    thumb=d.add("exposure-thumb","createTimelineGraphic",trackId=d.track("Exposure thumb lane"),
                shapeType="ellipse",startSeconds=start,durationSeconds=duration,
                values={"positionX":centre(0)/W,"positionY":(sy+20*k)/H,
                        "shapeWidth":16*k/W,"shapeHeight":16*k/H,
                        "backgroundColor":"#f3f3f3","shapeStrokeColor":"#e6c4c4c4",
                        "shapeStrokeWidth":k/min(W,H)})
    d.animate("exposure-thumb x",thumb,"titlePositionX",0,duration,lambda s:centre(s)/W)
    parts.append(thumb)
    pointer=vector("exposure-cursor",poly([[2,1],[2,20],[7,15],[11,24],[15,22],[11,13],[18,13]],True),
                   sx,sy,20*k,26*k,20,26,start,duration,"#111111",1.2,"#ffffff")
    d.animate("exposure-cursor x",pointer,"frameOffsetX",0,duration,
              lambda s:4*((centre(s)+(3+10)*k)/W-.5))
    d.animate("exposure-cursor y",pointer,"frameOffsetY",0,duration,
              lambda s:4*((sy+(25+13)*k+19.8*(1-d.out(s,0,.45)))/H-.5))
    d.animate("exposure-cursor opacity",pointer,"opacity",0,duration,
              lambda s:tool(s) if .7<s<4.5 else 0)
    values=[f"+{1.4*u(f/FPS):.2f} EV" for f in range(round(duration*FPS))]
    lane=d.track("Exposure value / exact frame text")
    a=0
    while a<len(values):
        b=a+1
        while b<len(values) and values[b]==values[a]:b+=1
        title=d.add(f"exposure-value-{a}","createTimelineTitle",trackId=lane,
            templateKey="headline",startSeconds=start+a/FPS,durationSeconds=(b-a)/FPS,
            values={"text":values[a],"fontFamily":"Menlo","fontSize":13*k,
                    "fontWeight":400,"italic":False,"letterSpacing":0,"lineHeight":1.2,
                    "color":"#f3f3f3","backgroundColor":"#00000000","alignment":"right",
                    "positionX":(sx+(238+80+8)*k)/W,"positionY":(sy+20*k)/H})
        d.animate(f"exposure-value-{a} opacity",title,"opacity",0,(b-a)/FPS,
                  lambda s,a=a:tool(s+a/FPS))
        d.animate(f"exposure-value-{a} lift",title,"frameOffsetY",0,(b-a)/FPS,
                  lambda s,a=a:4*19.8*(1-d.out(s+a/FPS,0,.45))/H)
        a=b
    for i,clip in enumerate(parts):
        base_y=next((step["values"]["offsetY"] for step in reversed(d.STEPS)
                     if step["commandId"].endswith(".setTimelineClipFrame") and
                     step["values"].get("clipId")==clip),0)
        d.animate(f"exposure-part-{i} opacity",clip,"opacity",0,duration,tool)
        d.animate(f"exposure-part-{i} lift",clip,"frameOffsetY",0,duration,
                  lambda s,base_y=base_y:base_y+4*19.8*(1-d.out(s,0,.45))/H)


def build():
    d.STEPS.clear()
    shape("photo-ink",0,0,W,H,12,50)
    light=d.add("photo-light","createTimelineGenerator",trackId=d.track("Photo light lane"),
        generatorKind="desktop-light",startSeconds=12,durationSeconds=50,
        parameters={"timeOffsetSeconds":12,"timeScale":1,"gather":0,"opacity":.3})
    copies=[("Start with the\ncamera original.","RAW photo editing. Your camera data, with room to refine."),
            ("Find your\nexposure.","Recover the frame. Shape the light. Keep the original."),
            ("Protect the highlights.\nOpen the shadows.","Refine the bright and dark regions independently."),
            ("Color, from\nthe ground up.","A custom color engine, from RAW development to color-managed output."),
            ("Shape the\nwhole image.","Move a shadow point. Lift the midtones. The photograph follows.")]
    edges=[0,4,10,15,20,27]
    for i,(headline,detail) in enumerate(copies):
        begin,end=edges[i],edges[i+1];duration=end-begin;start=12+begin
        if i==0:
            image("raw-camera-original","studio/raw-colour/0-0.png",650,164,1100,733.7,start,duration,22)
        else:
            progress=(lambda s:(d.smooth(s,.9,2.6)+d.smooth(s,3.5,5.4))/2) if i==4 else (
                lambda s,i=i:d.smooth(s,.7,4.5 if i==1 else 3.8))
            plate("photo-adjust-"+str(i),"creative-polish-v2/curve" if i==4 else "studio/raw-colour",
                  "curve-" if i==4 else f"{i-1}-",16 if i==4 else 8,progress,
                  650,164,1100,733.7,start,duration)
            before_after("photo-compare-"+str(i),"creative-polish-v2/curve/curve-0.png" if i==4 else
                         f"studio/raw-colour/{i-1}-0.png",start,duration,5.8 if i==4 else 4.9 if i==1 else 4.1)
        text("photo-source-"+str(i),"IMG_4969.CR2 / 6024 × 4020",650,120,18.7,start,duration,"Space Mono",LIME,1.2,1.8)
        if i==1:exposure_control(start,duration)
        if i==4:curve_plot(start,duration)
        caption(i,headline,detail,start,duration)
    # The detailed pixel, orange-mix and delivery beats are appended separately
    # so they cannot silently regress to an unrelated image montage.
    return {"reference":"landscape.mp4","chapter":[12,39],"steps":d.STEPS,
            "unresolvedParity":["exposure thumb press/release and reset icon","highlight/shadow pills","color-engine rail",
                                 "initial photo perspective","phase tool entrance/exit motion",
                                 "pixel/detail, orange mix and native delivery 39–62"]}


if __name__=="__main__":
    plan=build();path=ROOT/"photo-landscape.commands.json"
    path.write_text(json.dumps(plan,indent=2)+"\n");print(path,len(plan['steps']),"native commands")
