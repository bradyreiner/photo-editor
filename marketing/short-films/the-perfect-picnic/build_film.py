#!/usr/bin/env python3
"""Author all shots and original sound for The Perfect Picnic."""
from __future__ import annotations

import json
import math
import random
import sys
import wave as wav
from pathlib import Path

HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[2]
sys.path.insert(0,str(ROOT/"tools/character-animation"))
from scene import Scene, aim_arm, blinks, gradient, keys, wave
from art import CREAM, INK, RED, TEAL, foreground, landscape, person, robot

OUTPUT=ROOT/"marketing/outputs/the-perfect-picnic"
OUTPUT.mkdir(parents=True,exist_ok=True)
SHOTS=[]


def begin(start,end,title,cam=(800,450,1),cam_end=None,rain=False):
    s=Scene(end-start)
    s.camera(cam,cam_end)
    landscape(s,rain=rain)
    SHOTS.append(dict(start=start,end=end,title=title,scene=None))
    return s


def end(s):
    foreground(s)
    SHOTS[-1]["scene"]=s.finish()


def blanket(s,wet=False):
    s.path("M460 689 Q723 655 1072 689 L1170 779 Q760 801 378 768 Z","#a46652" if wet else "#c78168")
    for i in range(9):
        x=465+i*70
        s.path(f"M{x} 685 L{x-63} 779",stroke="#e4b18b",sw=10,opacity=.55)
    for i in range(4):
        y=696+i*22
        s.path(f"M{459-i*24} {y} Q760 {y+20} {1080+i*22} {y+4}",stroke="#e6b891",sw=7,opacity=.6)
    for i in range(13):
        x=400+i*55
        s.line(x,774,x-9,787,"#d7ac88",2)
    basket=s.group(x=978,y=684)
    s.path("M-48 -52 L53 -52 L45 9 Q0 28 -42 8 Z","#af8255",parent=basket)
    s.path("M-29 -53 Q-34 -107 4 -110 Q42 -108 33 -53",stroke="#93724e",sw=9,parent=basket)
    s.line(-47,-51,51,-51,"#d0aa71",6,parent=basket)
    for y in [-35,-21,-7,7]:
        s.line(-43,y,44,y,"#d0a26c",3,parent=basket)
    for x in [-25,-5,15,35]:
        s.line(x,-47,x,10,"#926f4b",2,parent=basket)


def cake(s,x=795,y=700,scale=1,name="cake"):
    g=s.group(x=x,y=y,scaleX=scale,scaleY=scale,name=name)
    s.ellipse(-58,-9,116,21,"#40605a",parent=g,opacity=.25)
    s.ellipse(-53,-14,106,25,"#e9dfbd",parent=g)
    s.path("M-28 -50 L31 -50 L24 -6 Q0 6 -23 -6 Z","#b78855",parent=g)
    for xx in [-15,-4,7,18]:
        s.line(xx,-43,xx*.8,-8,"#d9b27a",2,parent=g)
    s.path("M-35 -49 Q-42 -61 -26 -68 Q-28 -82 -10 -82 Q-1 -100 9 -81 Q30 -81 27 -65 Q44 -62 35 -49 Q9 -42 -35 -49 Z",
           "#f3dfb4",parent=g)
    s.ellipse(-3,-90,14,14,"#b94b41",parent=g)
    s.path("M3 -89 Q7 -96 15 -94",stroke="#5b805b",sw=3,parent=g)
    return g


def strawberry(s,x,y,parent="camera",name=None):
    g=s.group(x=x,y=y,parent=parent,name=name)
    s.path("M-12 -10 Q-25 -12 -18 9 L-1 27 Q17 12 20 -3 Q20 -18 5 -10 Z","#ba5145",parent=g)
    s.path("M-13 -12 L-4 -23 L1 -14 L10 -23 L12 -12 L21 -12 L10 -5 L0 -10 L-8 -4 Z","#5c8155",parent=g)
    for xx,yy in [(-9,1),(4,0),(10,8),(-4,13),(0,20)]:
        s.ellipse(xx,yy,2,3,"#f3c78a",parent=g)
    return g


def umbrella(s,x,y,size=1,name="umbrella",parent="camera"):
    g=s.group(x=x,y=y,scaleX=size,scaleY=size,parent=parent,name=name)
    s.path("M0 0 L0 100 Q0 121 18 115 Q25 112 23 102",stroke="#897a55",sw=5,parent=g)
    s.path("M-92 0 Q-78 -57 0 -65 Q79 -57 92 0 Q64 -17 46 0 Q23 -17 0 0 Q-22 -17 -46 0 Q-68 -17 -92 0 Z",
           "#d8ac58",parent=g)
    s.path("M0 -65 Q-31 -39 -46 0 M0 -65 L0 0 M0 -65 Q32 -40 46 0",stroke="#b88c44",sw=2,parent=g)
    s.path("M-80 -6 Q-62 -49 -11 -56",stroke="#f3d284",sw=3,parent=g)
    s.line(0,-64,0,-74,"#897a55",4,parent=g)
    return g


def puddle(s,x=800,y=730,width=420,name=None):
    g=s.group(x=x,y=y,name=name)
    s.ellipse(-width/2,-19,width,46,"#729a98",parent=g)
    s.ellipse(-width/2+15,-14,width-30,29,"#adc6b7",parent=g)
    s.path(f"M{-width*.38} 2 Q0 -12 {width*.38} 2",stroke="#dce0be",sw=2,parent=g)
    return g


def splash(s,x,y,time,scale=1,huge=False,name="splash"):
    # Authored silhouettes and droplets; each has an anticipation-free impulse,
    # a ballistic arc and a settling phase. Rendered geometry, not a clip overlay.
    g=s.group(x=x,y=y,name=name,opacity=keys((0,0),(time-.04,0),(time,1),(time+.55,1),(time+.9,0)))
    s.ellipse(-100*scale,-8,200*scale,19,"#00000000",stroke="#daead5",strokeWidth=3,parent=g,
              scaleX=keys((0,.1),(time,.1),(time+.8,2.8)),
              scaleY=keys((0,.1),(time,.1),(time+.8,1.6)))
    wave_shape="M-160 0 Q-110 -6 -138 -82 Q-92 -53 -95 -125 Q-48 -57 -60 -184 Q-10 -119 0 -235 Q19 -116 57 -179 Q52 -78 104 -128 Q82 -42 151 -81 Q115 -12 170 0 Z"
    burst=s.path(wave_shape,"#9ac9c2",stroke="#d2e5ce",sw=2,parent=g,
                 scaleX=keys((0,.05),(time,.05),(time+.18,scale),(time+.62,scale*1.5)),
                 scaleY=keys((0,.01),(time,.01),(time+.17,scale),(time+.62,.02)))
    rng=random.Random(98 if huge else 22)
    for i in range(30 if huge else 15):
        dx=rng.uniform(-230,230)*scale
        rise=rng.uniform(70,300)*scale
        r=rng.uniform(3,7)*scale
        s.ellipse(-r,-r,r*2,r*3,"#b4dcd0",parent=g,
                  x=keys((0,0),(time,0),(time+.6,dx)),
                  y=keys((0,0),(time,0),(time+.23,-rise),(time+.66,30),easing="linear"),
                  rotation=rng.uniform(-30,30))
    return burst


def look(s,rig,poses):
    s.set(rig["head"],rotation=keys(*poses))


def film():
    # 1. A clear, affectionate character contrast before the weather changes.
    s=begin(0,6,"A very important picnic",(800,470,1.04),(810,470,1.09))
    blanket(s)
    nell=person(s,"nell",575,scale=1)
    pip=robot(s,"pip",1035,scale=.77)
    cake(s)
    board=s.group(parent=pip["back"]["hand"],x=0,y=-5,rotation=-8,name="the-very-important-checklist")
    s.rect(-26,-8,52,67,"#8d7757",radius=4,parent=board)
    s.rect(-22,-4,44,58,"#ede0ba",radius=2,parent=board)
    s.rect(-10,-11,20,8,"#78927e",radius=2,parent=board)
    for yy,tt in [(8,.7),(24,2.2),(40,4.1)]:
        s.rect(-16,yy-4,9,9,"#00000000",stroke="#6b8a74",strokeWidth=1,parent=board)
        s.line(-2,yy,16,yy,"#9aab8b",1,parent=board)
        s.path(f"M-15 {yy} L-11 {yy+3} L-5 {yy-4}",stroke="#537b64",sw=2,parent=board,
               trim=keys((0,0),(tt,0),(tt+.35,1)))
    aim_arm(s,pip,"front",[(0,60,-117),(.7,-16,-110),(1.3,60,-117),(2.2,-16,-90),(2.9,60,-117),
                           (4.1,-16,-75),(4.8,60,-117),(6,60,-117)],bend=-1)
    look(s,pip,[(0,12),(1.8,22),(2.7,0),(3.6,20),(5.4,-5)])
    look(s,nell,[(0,3),(2.5,7),(4,0),(5.5,-6)])
    s.set(nell["legs"][1],rotation=wave(6,0,14,1.9))
    aim_arm(s,nell,"front",[(0,64,-120),(2.2,64,-120),(3.1,87,-102),(3.8,64,-120),(6,64,-120)])
    end(s)

    # 2. Reaction timing holds after the theft, then after the offer.
    s=begin(6,11,"The strawberry",(824,505,1.65),(824,500,1.68))
    blanket(s)
    nell=person(s,"nell",650,scale=.95)
    pip=robot(s,"pip",970,scale=.77)
    cake(s,810,700,.8)
    berry=strawberry(s,1,7,parent=nell["front"]["hand"],name="strawberry")
    s.set(berry,scaleX=.65,scaleY=.65,opacity=keys((0,1),(4.55,1),(4.7,0)))
    aim_arm(s,nell,"front",[(0,65,-124),(.6,65,-124),(1.4,122,-175),(2.5,122,-175),
                            (3.1,122,-175),(4.4,20,-228),(5,20,-228)])
    look(s,pip,[(0,12),(1.8,12),(2.3,-15),(3.2,-7),(4,-7),(4.6,5)])
    look(s,nell,[(0,3),(2,-8),(3.2,4),(4.5,-10)])
    end(s)

    # 3. The weather gives them different ideas.
    s=begin(11,16,"One drop. Two different reactions.",(830,470,1.3),(830,460,1.34),rain=True)
    blanket(s,True); cake(s)
    nell=person(s,"nell",620,scale=.98)
    pip=robot(s,"pip",1010,scale=.77)
    look(s,pip,[(0,0),(.7,-12),(1.5,-19),(3,-19),(3.4,14),(4.8,14)])
    look(s,nell,[(0,0),(1.5,-7),(3.3,-12),(5,-4)])
    aim_arm(s,nell,"front",[(0,60,-115),(1.5,60,-115),(2.4,129,-194),(4,129,-194)])
    end(s)

    # 4. The escalating umbrella gag has a setup, escalation and clean release.
    s=begin(16,23,"The emergency solution",(970,480,1.08),(970,480,1.08),rain=True)
    blanket(s,True); cake(s)
    nell=person(s,"nell",585,scale=.98)
    pip=robot(s,"pip",1020,scale=.77)
    aim_arm(s,pip,"front",[(0,60,-110),(.6,70,-194),(2,70,-194),(3.2,70,-194),(4.5,100,-180),(5.7,60,-100)],bend=-1)
    u=umbrella(s,1068,536,.25)
    s.set(u,scaleX=keys((0,.05),(.6,.25),(1.4,.25),(2.1,1.85),(3.4,1.85),(5.2,1.4),(7,1.2)),
          scaleY=keys((0,.05),(.6,.25),(1.4,.25),(2.1,1.85),(3.4,1.85),(5.2,1.4),(7,1.2)),
          x=keys((0,1068),(2.1,1031),(3.2,1031),(4.3,1141),(5.5,1510),(7,1930)),
          y=keys((0,536),(2.1,353),(3.2,353),(4.3,353),(5.5,60),(7,-280)),
          rotation=keys((0,0),(3.2,0),(3.6,12),(4.2,-15),(5,38),(6.7,64)))
    s.set(pip["root"],x=keys((0,1020),(3.2,1020),(4.3,1130),(5,1140),(5.5,1100),(7,1100)),
          rotation=keys((0,0),(3.2,0),(4.2,15),(5,9),(5.5,-4),(6.2,0)))
    look(s,nell,[(0,0),(2.1,-11),(3.6,-16),(4.5,-22),(5.5,-13),(7,-5)])
    look(s,pip,[(0,-15),(3,-12),(4.5,-22),(5.6,-25),(6.8,17)])
    end(s)

    # 5. The invitation is a gesture, not a title explaining the story.
    s=begin(23,28,"An alternative plan",(870,490,1.2),(835,490,1.25),rain=True)
    blanket(s,True); cake(s,760,700,.8)
    puddle(s,850,735,450)
    nell=person(s,"nell",610,scale=.98)
    pip=robot(s,"pip",1060,scale=.77,mood="sad")
    look(s,pip,[(0,18),(1.6,19),(2.7,4),(4,-10)])
    look(s,nell,[(0,8),(1.5,16),(2.5,-2),(4,-6)])
    s.set(nell["root"],x=keys((0,610),(1.6,610),(2.5,730),(5,730)))
    s.set(nell["legs"][1],rotation=keys((0,0),(1.8,-20),(2.5,0),(5,0)))
    aim_arm(s,nell,"front",[(0,65,-120),(2.8,65,-120),(3.8,130,-168),(5,130,-168)])
    splash(s,750,740,2.5,.24,name="first-small-splash")
    end(s)

    # 6. Each jump gets anticipation, travel, landing and a held reaction.
    s=begin(28,35,"A better kind of afternoon",(865,490,1.15),(865,480,1.18),rain=True)
    puddle(s,800,741,650)
    nell=person(s,"nell",720,scale=.98)
    pip=robot(s,"pip",1080,scale=.77)
    s.set(nell["root"],y=keys((0,714),(.5,730),(.9,605),(1.3,590),(1.75,714),(2,728),(2.35,714),
                                  (3.5,714),(3.9,728),(4.3,590),(4.6,580),(5.05,714),(5.3,729),(5.7,714),(7,714)),
          scaleY=keys((0,.98),(.5,.89),(.9,1.04),(1.3,1.03),(1.75,.98),(2,.89),(2.35,.98),
                      (3.5,.98),(3.9,.89),(4.3,1.04),(4.6,1.03),(5.05,.98),(5.3,.89),(5.7,.98),(7,.98)))
    for a in [nell["front"],nell["back"]]:
        s.set(a["upper"],rotation=keys((0,-10),(.9,-114),(1.5,-110),(2.3,-10),(3.6,-10),(4.3,-112),(4.8,-110),(5.7,-10),(7,-10)))
    look(s,pip,[(0,0),(.8,-15),(1.5,-15),(2.2,9),(3.5,-5),(4.5,-16),(5.6,7),(6.6,-7)])
    s.set(pip["root"],rotation=keys((0,0),(1.75,0),(1.88,8),(2.4,0),(7,0)))
    splash(s,720,742,1.75,.7,name="nell-splash-a")
    splash(s,720,742,5.05,.85,name="nell-splash-b")
    end(s)

    # 7. Pip's huge leap repeats Nell's action, with his own overengineering.
    s=begin(35,42,"Pip commits",(840,465,1.12),(840,465,1.12),rain=True)
    puddle(s,850,742,720)
    nell=person(s,"nell",700,scale=.98)
    pip=robot(s,"pip",1010,scale=.77)
    s.set(pip["root"],y=keys((0,714),(.8,722),(1.3,714),(1.7,744),(2.3,744),(2.65,510),(3,475),(3.5,714),(3.7,738),(4,714),(5,714),(5.35,680),(5.65,714),(7,714)),
          scaleY=keys((0,.77),(.8,.7),(1.3,.77),(1.7,.62),(2.3,.62),(2.65,.88),(3,.85),(3.5,.77),(3.7,.65),(4,.77),(7,.77)),
          rotation=keys((0,0),(1.7,-5),(2.3,-5),(2.65,-12),(3,8),(3.5,0),(7,0)))
    for a in [pip["front"],pip["back"]]:
        s.set(a["upper"],rotation=keys((0,8),(1.7,35),(2.3,35),(2.65,-130),(3,-135),(3.5,-20),(4.1,10),(7,10)))
    look(s,nell,[(0,-6),(1.7,6),(2.7,-20),(3.4,-8),(3.55,14),(4.4,14),(4.8,-12),(7,-10)])
    s.set(nell["torso"],rotation=keys((0,0),(3.5,0),(3.6,-8),(4.3,-8),(4.7,0),(5,-4),(5.3,0),(5.6,-4),(5.9,0),(6.2,-3),(6.6,0)))
    s.set(nell["mouth"],opacity=keys((0,1),(3.49,1),(3.5,0),(4.4,0),(4.5,1)))
    s.ellipse(7,22,14,19,"#88564c",parent=nell["head"],opacity=keys((0,0),(3.49,0),(3.5,1),(4.4,1),(4.5,0)))
    s.set(nell["eyes"],scaleY=keys((0,1),(3.49,1),(3.5,1.3),(4.4,1.3),(4.8,.4),(5.1,.7),(5.4,.4),(5.8,.7),(6.2,.4),(7,.7)))
    splash(s,1010,744,3.5,1.65,True,name="the-big-splash")
    end(s)

    # 8. A foreground gag closes both earlier setups while the friends play.
    s=begin(42,48,"Wonderfully unplanned",(810,480,1.06),(810,480,.96),rain=True)
    puddle(s,1000,736,720)
    nell=person(s,"nell",960,scale=.78)
    pip=robot(s,"pip",1190,scale=.62)
    for rig,base,delay,sc in [(nell,714,0,.78),(pip,714,.6,.62)]:
        ps=[(0,base)]
        for t in [1+delay,3+delay]:
            ps.extend([(t,base),(t+.3,base-70),(t+.65,base),(t+.8,base+8),(t+1,base)])
        s.set(rig["root"],y=keys(*ps))
    splash(s,960,738,1.65,.5,name="shared-splash-1")
    splash(s,1190,738,2.25,.45,name="shared-splash-2")
    splash(s,960,738,3.65,.5,name="shared-splash-3")
    splash(s,1190,738,4.25,.45,name="shared-splash-4")
    s.ellipse(428,774,310,38,"#748a72")
    cake(s,570,770,1.15)
    umbrella(s,575,639,.74,name="cupcake-emergency-umbrella")
    s.text("THE PERFECT PICNIC",195,170,850,50,size=19,font="Avenir Next",tracking=5,fill="#f7e9cb")
    end(s)

    # 9. A calm end card gives the joke and the promise room to land.
    s=Scene(6)
    s.camera()
    SHOTS.append(dict(start=48,end=54,title="KIN — For the wonderfully unplanned.",scene=None))
    s.rect(-20,-20,1640,940,"#f0e6cb")
    s.text("KIN",300,310,1000,185,size=150,font="Avenir Next",bold=True,tracking=18,
           fill="#315653",center=True,opacity=keys((0,0),(.6,1)))
    s.text("For the wonderfully unplanned.",250,510,1100,60,size=34,font="Georgia",fill="#53716a",center=True,
           opacity=keys((0,0),(.8,0),(1.6,1)))
    s.text("A FICTIONAL COMPANION ROBOT BRAND",300,719,1000,35,size=12,font="Avenir Next",tracking=3,
           fill="#8d9c87",center=True,opacity=.8)
    u=umbrella(s,800,243,.47)
    s.set(u,scaleX=keys((0,0),(.25,.04),(.55,.52),(.8,.47)),
          scaleY=keys((0,0),(.25,.04),(.55,.52),(.8,.47)),rotation=keys((0,-8),(.55,5),(1.1,0)))
    SHOTS[-1]["scene"]=s.finish(bars=False,vignette=False)


def sound():
    import numpy as np
    rate=48000
    duration=54
    audio=np.zeros((duration*rate,2),np.float64)
    rng=np.random.default_rng(8831)

    def add(start,samples,volume=.1,pan=0):
        pos=round(start*rate)
        if pos<0:return
        n=min(len(samples),len(audio)-pos)
        if n<=0:return
        gains=np.array([math.sqrt((1-pan)/2),math.sqrt((1+pan)/2)])
        audio[pos:pos+n]+=samples[:n,None]*gains*volume

    def note(time,midi,length=2.6,volume=.12,pan=0):
        t=np.arange(round(length*rate))/rate
        freq=440*2**((midi-69)/12)
        strike=(1-np.exp(-t*220))*np.exp(-t/1.2)
        sample=np.zeros_like(t)
        for h in range(1,7):
            sample+=np.sin(math.tau*freq*(h+0.0007*h*h)*t)*np.exp(-t*h*.35)/h**1.8
        sample*=strike
        sample+=.03*rng.normal(size=len(t))*np.exp(-t*90)
        add(time,sample,volume,pan)

    # Original pentatonic piano theme in D, with rests for the gags.
    melody=[(0.6,74), (1.35,78),(2.1,81),(3.1,78),(4.2,76),(5,74),
            (6.5,78),(7.25,81),(8.5,83),(9.8,81),
            (11.4,78),(12.5,76),(14,74),
            (16.2,74),(17,78),(18,81),(19,86),(20.1,88),(21.4,86),(22.2,81),
            (24.5,74),(26.4,78),
            (28.1,74),(28.8,78),(29.5,81),(30.2,83),(31.3,81),(32.2,78),(33.1,76),(34.2,78),
            (35.3,74),(36,74),(36.7,78),(37.4,81),(38,86),
            (40.2,78),(40.8,81),(41.4,86),
            (42,83),(42.8,81),(43.6,78),(44.4,81),(45.2,86),(46.2,83),(47.2,81),
            (48.8,78),(49.5,81),(50.5,74)]
    for t,m in melody:
        note(t,m,volume=.11 if t<35 else .14,pan=.18)
    for start,notes in [(0,[50,57,62]),(6,[47,54,59]),(11,[43,50,55]),(16,[45,52,57]),
                        (23,[47,54,59]),(28,[50,57,62]),(31.5,[43,50,55]),(35,[45,52,57]),
                        (40.2,[43,50,55]),(44,[45,52,57]),(48,[50,57,62])]:
        for i,m in enumerate(notes):note(start+i*.2,m,length=4.5,volume=.065,pan=-.25)
    # Rain has filtered body and individual droplets, not white noise at full gain.
    n=round(37*rate)
    noise=rng.normal(size=n)
    smooth=np.convolve(noise,np.ones(12)/12,mode="same")
    envelope=np.minimum(1,np.arange(n)/(rate*2))*np.minimum(1,(n-np.arange(n))/(rate*1.3))
    add(11,smooth*envelope,.045,-.2)
    for time in [11.2,11.9,12.6]:
        t=np.arange(round(.12*rate))/rate
        add(time,np.sin(math.tau*(850*t-1100*t*t))*np.exp(-t*38),.1,.1)
    def chirp(time):
        for delay,m in [(0,86),(.17,93)]:
            t=np.arange(round(.22*rate))/rate
            f=440*2**((m-69)/12)
            add(time+delay,np.sin(math.tau*f*t)*np.sin(np.minimum(1,t/.015)*math.pi/2)*np.exp(-t*15),.065,.25)
    for time in [2,8.1,16.2,34.2,41.1,49]:chirp(time)
    # Fabric pop and upward-moving wind for the umbrella.
    t=np.arange(round(1.8*rate))/rate
    whoosh=rng.normal(size=len(t))*.45
    whoosh=np.convolve(whoosh,np.ones(25)/25,mode="same")
    add(19.3,whoosh*np.sin(math.pi*t/1.8)**2,.3,.4)
    # Splash impulses: low water body and a longer, fine droplet tail.
    for time,amp in [(25.5,.1),(29.75,.23),(33.05,.25),(38.5,.48),(43.65,.15),(44.25,.14),(45.65,.15),(46.25,.14)]:
        t=np.arange(round(.95*rate))/rate
        n=rng.normal(size=len(t))
        low=np.convolve(n,np.ones(28)/28,mode="same")
        spl=(low*2*np.exp(-t*5)+n*.12*np.exp(-t*8))*(1-np.exp(-t*150))
        add(time,spl,amp,.2)
    # Small spring/servo glissando for the robot's anticipation and leap.
    t=np.arange(round(.36*rate))/rate
    add(37.3,np.sin(math.tau*(180*t+780*t*t))*np.sin(math.pi*t/.36)*.4,.12,.25)
    # Gentle two-tap play/laughter rhythm, represented instrumentally.
    for time in [10.0,40.4,40.7,41.0]:note(time,90,length=.25,volume=.045,pan=-.3)
    # A quiet open-air bed and short stereo room echoes unify synthesized stems.
    t=np.arange(len(audio))/rate
    air=np.convolve(rng.normal(size=len(audio)),np.ones(32)/32,mode="same")
    audio[:,0]+=air*.005;audio[:,1]+=air*.004
    dry=audio.copy()
    for lag,gain in [(0.071,.11),(.137,.075),(.211,.05)]:
        offset=round(lag*rate)
        audio[offset:,0]+=dry[:-offset,1]*gain
        audio[offset:,1]+=dry[:-offset,0]*gain
    fade=np.minimum(1,t/.15)*np.minimum(1,(54-t)/1.8)
    audio*=fade[:,None]
    peak=np.max(np.abs(audio))
    if peak>.89:audio*=.89/peak
    with wav.open(str(OUTPUT/"original-score-and-foley.wav"),"wb") as f:
        f.setnchannels(2);f.setsampwidth(2);f.setframerate(rate)
        f.writeframes((np.clip(audio,-1,1)*32767).astype("<i2").tobytes())


if __name__=="__main__":
    film()
    data=dict(schema="looksy-character-film-v1",title="The Perfect Picnic",fps=24,duration=54,shots=SHOTS,
              characters=["Nell — a playful girl","Pip — an earnest companion robot"],
              brand=dict(name="KIN",fictional=True,tagline="For the wonderfully unplanned."))
    (OUTPUT/"film.json").write_text(json.dumps(data,separators=(",",":")))
    # Direct inputs for the existing native vector-path generator. Legacy M is
    # retained for discoverability by older schema readers; scene owns drawing.
    commands=[]
    for index,shot in enumerate(SHOTS):
        commands.append(dict(startSeconds=shot["start"],durationSeconds=shot["end"]-shot["start"],
            label=shot["title"],generatorKind="vector-path",parameters=dict(vectorPath=dict(
                viewBoxWidth=1600,viewBoxHeight=900,commands=[dict(verb="M",points=[0,0])],scene=shot["scene"]))))
    (OUTPUT/"native-generator-shots.json").write_text(json.dumps(commands,separators=(",",":")))
    sound()
    print(f"Authored {len(SHOTS)} shots, 54 seconds, {sum(len(x['scene']['nodes']) for x in SHOTS)} nodes: {OUTPUT}")
