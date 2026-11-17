"""Original character and environment artwork for the KIN film studies."""
from __future__ import annotations
import math
import random
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "tools/character-animation"))
from scene import Scene, aim_arm, blinks, gradient, keys, wave

INK = "#243d41"
CREAM = "#f5e8c9"
TEAL = "#426c68"
RED = "#ba5845"
SKIN = "#d6a17c"


def shadow(s, x, y, width, parent="camera"):
    s.ellipse(-width/2, -10, width, 20, "#350d292c", parent=parent, x=x, y=y)


def arm(s, root, name, shoulder, robot=False, sleeve="#d4a75d"):
    lengths = (42, 38) if robot else (53, 47)
    width = 15 if robot else 19
    upper = s.group(parent=root, name=name+"-upper", x=shoulder[0], y=shoulder[1], rotation=0)
    if robot:
        s.ellipse(-11, -11, 22, 22, "#41666a", parent=upper)
        s.rect(-width/2, 0, width, lengths[0]+6, "#c6be9f", radius=6, parent=upper,
               stroke="#536b69", strokeWidth=2)
        for y in [12, 21, 30]:
            s.line(-6, y, 6, y, "#9b9f89", 2, parent=upper)
    else:
        s.rect(-width/2-2, -5, width+4, lengths[0]+15, sleeve, radius=11, parent=upper)
        s.path(f"M-9 3 Q-14 25 -7 {lengths[0]}", stroke="#9a783e", sw=2, opacity=0.5, parent=upper)
    lower = s.group(parent=upper, name=name+"-lower", y=lengths[0], rotation=0)
    if robot:
        s.ellipse(-12, -12, 24, 24, "#52736e", parent=lower)
        s.ellipse(-7, -7, 14, 14, "#d0cfb3", parent=lower)
        s.rect(-8, 0, 16, lengths[1], "#d8cfad", radius=5, parent=lower)
        s.rect(-10, lengths[1]-12, 20, 11, RED, radius=2, parent=lower)
    else:
        s.rect(-width/2, -2, width, lengths[1]-3, sleeve, radius=8, parent=lower)
        s.rect(-11, lengths[1]-10, 22, 10, "#c39651", radius=3, parent=lower)
    hand = s.group(parent=lower, name=name+"-hand", y=lengths[1])
    if robot:
        s.rect(-12, -2, 24, 23, "#e6dabc", radius=9, parent=hand, stroke="#63817a", strokeWidth=2)
        s.path("M-10 10 Q-20 15 -17 24 M10 10 Q19 15 16 24", stroke="#e6dabc", sw=8, parent=hand)
        s.line(-7, 8, 7, 8, "#b7ae91", 2, parent=hand)
    else:
        s.path("M-8 -2 Q-17 5 -12 17 Q-10 26 1 25 Q13 24 12 14 L7 1 Z", SKIN, parent=hand)
        s.path("M9 7 Q20 0 21 8 Q19 15 11 16", SKIN, parent=hand)
        s.path("M-5 15 L-4 20 M1 15 L2 21 M7 15 L7 20", stroke="#b77c62", sw=1.2, parent=hand)
    return dict(upper=upper, lower=lower, hand=hand, shoulder=shoulder, lengths=lengths)


def person(s, name, x, floor=714, age="child", scale=1, walk=False, mood="happy"):
    elder = age == "elder"
    adult = age != "child"
    body_scale = 1.18 if adult else 0.95
    shadow(s, x, floor+4, 140*scale)
    root = s.group(name=name, x=x, y=floor, scaleX=scale, scaleY=scale)
    legs = []
    for side, px in [("back", -19), ("front", 18)]:
        leg = s.group(parent=root, name=f"{name}-{side}-leg", x=px, y=-95,
                      rotation=wave(s.duration, 0, 20 if walk else 0.2, 1.15, 0 if side=="back" else math.pi))
        s.rect(-12, -4, 24, 72, "#52635c" if elder else "#566b65", radius=11, parent=leg)
        foot = s.group(parent=leg, y=64, rotation=wave(s.duration, 0, 12 if walk else 0, 1.15, 2 if side=="back" else 5))
        s.path("M-13 -2 L12 -2 L15 18 Q32 22 30 31 L-14 31 Z", "#2a4b50", parent=foot)
        s.path("M-12 27 L29 27 L29 33 L-13 33 Z", "#d3bd8b", parent=foot)
        s.line(4, 8, 12, 8, "#7b9a91", 2, parent=foot)
        legs.append(leg)
    torso = s.group(parent=root, name=name+"-torso", y=wave(s.duration, 0, 3 if walk else 1.1, 0.575 if walk else 4.2),
                     rotation=wave(s.duration, 2 if elder else 0, 1.3 if walk else 0.5, 1.15 if walk else 5.6))
    back_arm = arm(s, torso, name+"-back-arm", (-40, -181), sleeve="#b98d49")
    s.path("M-43 -205 Q-59 -174 -54 -108 L-64 -78 Q-4 -58 58 -80 L47 -182 Q40 -211 23 -218 Z",
           "#d3a359", parent=torso,
           gradient=dict(type="linear", coordinates=[-55,-140,55,-140], stops=[[0,"#b38d52"],[0.45,"#e6ba70"],[1,"#ce9f58"]]))
    s.path("M-2 -210 Q-7 -153 0 -78", stroke="#aa7e41", sw=2, parent=torso)
    for yy in [-171,-143,-115]:
        s.ellipse(8, yy, 5, 5, "#83643e", parent=torso)
    s.path("M22 -127 L42 -130 L44 -103 Q35 -97 23 -103 Z", "#c69951", stroke="#b98b49", sw=1.5, parent=torso)
    s.rect(-15,-233,33,33,SKIN,radius=8,parent=torso)
    head = s.group(parent=torso, name=name+"-head", y=-257,
                   rotation=wave(s.duration, -4 if elder else -2, 1.2, 5.3))
    hair = "#d9d6c2" if elder else "#654538"
    s.ellipse(-49,-70,89,120,hair,parent=head)
    if not elder:
        s.ellipse(-59,-41,29,91,hair,parent=head, rotation=-15)
        s.ellipse(-72,-49,32,38,hair,parent=head)
    else:
        s.ellipse(-64,-43,48,48,"#b7c0ae",parent=head)
        s.path("M-63 -27 Q-50 -43 -36 -32 M-64 -19 Q-49 -35 -36 -24",stroke="#d9d9c5",sw=2,parent=head)
    s.path("M-39 -45 Q-15 -67 18 -48 Q44 -36 44 -3 Q52 4 45 12 L34 15 Q29 48 4 51 Q-25 53 -35 25 Q-49 14 -39 -45 Z",
           SKIN,parent=head,
           gradient=dict(type="linear",coordinates=[-40,-20,45,25],stops=[[0,"#c78d6c"],[0.65,"#e7b38c"],[1,"#d5a17b"]]))
    s.ellipse(-45,-2,18,27,"#d39a77",parent=head)
    s.path("M-40 4 Q-32 0 -34 15",stroke="#b88065",sw=2,parent=head)
    s.path("M-44 -34 Q-50 -66 -19 -76 Q12 -85 35 -58 L46 -38 Q18 -36 2 -56 Q-10 -37 -37 -31 Z",hair,parent=head)
    s.path("M-38 -48 Q-25 -69 -8 -66 M-27 -38 Q-6 -55 8 -60",stroke="#eef0d7" if elder else "#8d6750",sw=3,parent=head,opacity=.6)
    eyes = s.group(parent=head,name=name+"-eyes",y=-4)
    for xx in [-8,24]:
        s.ellipse(xx-5,-8,9,13,"#3c3830",parent=eyes)
        s.ellipse(xx-3,-6,2.8,3.5,"#fff0ce",parent=eyes)
    s.path("M-15 -19 Q-8 -24 0 -21 M17 -22 Q23 -25 31 -20",stroke=hair,sw=3,parent=head)
    if elder:
        for xx in [-8,25]:
            s.ellipse(xx-15,-17,31,30,"#00000000",stroke="#58665a",strokeWidth=2.6,parent=head)
        s.path("M7 -4 Q9 -8 10 -4 M-24 -7 L-37 -10",stroke="#58665a",sw=2.4,parent=head)
        s.path("M-22 20 L-15 22 M28 23 L34 20 M-3 38 Q7 42 16 36",stroke="#b7846a",sw=1.4,parent=head)
    s.path("M14 -1 Q22 10 15 12",stroke="#b77e62",sw=2,parent=head)
    s.ellipse(20,12,17,8,"#36bb6b5a",parent=head)
    mouth = s.path("M1 25 Q13 35 24 23",stroke="#88564c",sw=2.5,parent=head,name=name+"-mouth")
    if mood=="sad":
        s.set(mouth,scaleY=-.5,y=39)
    scarf = s.group(parent=torso,name=name+"-scarf",x=-8,y=-216)
    s.path("M-30 -9 Q7 3 40 -10 L38 7 Q5 20 -28 7 Z",RED,parent=scarf)
    s.path("M-17 3 Q-29 36 -10 63 L6 58 Q-7 34 -1 6 Z","#a9493d",parent=scarf,
           rotation=wave(s.duration,-5,5 if walk else 1.5,1.9))
    s.path("M-11 48 L1 44 M-10 53 L3 49",stroke="#d98664",sw=2,parent=scarf)
    front_arm = arm(s,torso,name+"-front-arm",(40,-185))
    for a,phase in [(back_arm,0),(front_arm,math.pi)]:
        s.set(a["upper"],rotation=wave(s.duration, -8,24 if walk else 1,1.15,phase))
        s.set(a["lower"],rotation=wave(s.duration, -12,9 if walk else .8,1.15,phase))
    blinks(s,eyes,[1.7,5.1,8.9,12.2])
    if elder:
        s.set(root,scaleY=scale*body_scale)
    return dict(root=root,torso=torso,head=head,eyes=eyes,mouth=mouth,back=back_arm,front=front_arm,legs=legs)


def robot(s,name,x,floor=714,scale=1,walk=False,mood="happy",weathered=False):
    shadow(s,x,floor+5,130*scale)
    root=s.group(name=name,x=x,y=floor,scaleX=scale,scaleY=scale)
    for side,px in [("back",-24),("front",24)]:
        leg=s.group(parent=root,name=f"{name}-{side}-leg",x=px,y=-55,
                    rotation=wave(s.duration,0,25 if walk else .2,.85,0 if side=="front" else math.pi))
        s.rect(-10,-2,20,38,"#8d9e8a",radius=7,parent=leg)
        s.line(-7,12,7,12,"#506f69",3,parent=leg)
        s.path("M-16 28 Q-5 24 12 27 L27 39 Q28 48 17 49 L-17 49 Z","#e5d6b1",parent=leg,stroke="#637a6d",strokeWidth=2)
        s.path("M-16 45 L24 45",stroke="#667b6e",sw=4,parent=leg)
    torso=s.group(parent=root,name=name+"-torso",y=wave(s.duration,0,3 if walk else 1,.425 if walk else 3.7),
                  rotation=wave(s.duration,0,2 if walk else .5,.85 if walk else 4.7))
    back=arm(s,torso,name+"-back-arm",(-45,-141),True)
    s.rect(-47,-174,94,124,"#e5d5ad",radius=25,parent=torso,stroke="#708273",strokeWidth=2,
           gradient=dict(type="linear",coordinates=[-47,-100,47,-100],stops=[[0,"#bdbb9b"],[.48,"#efe3bf"],[1,"#d7c8a2"]]))
    s.rect(-30,-139,60,59,"#cfc5a3",radius=9,parent=torso)
    s.ellipse(-10,-121,20,20,"#7e9a86",parent=torso)
    s.ellipse(-5,-116,10,10,"#dbe8ac",parent=torso)
    s.line(-23,-89,23,-89,"#a9a78a",2,parent=torso)
    for xx in [-36,36]:
        s.ellipse(xx-3,-72,6,6,"#88967f",parent=torso)
    head=s.group(parent=torso,name=name+"-head",y=-204,rotation=wave(s.duration,3,2,4.8))
    antenna=s.group(parent=head,x=0,y=-49,rotation=wave(s.duration,5,8 if walk else 3,1.8))
    s.path("M0 0 Q-3 -15 7 -26",stroke="#8da18d",sw=5,parent=antenna)
    s.ellipse(1,-35,15,15,"#dd925c",parent=antenna)
    s.ellipse(5,-32,5,5,"#ffcf84",parent=antenna)
    for xx in [-70,61]:
        s.rect(xx,-22,13,38,"#b2b49a",radius=6,parent=head)
        s.line(xx+5,-11,xx+5,5,"#6d8e80",2,parent=head)
    s.rect(-67,-52,134,106,"#eadfbf",radius=28,parent=head,stroke="#6e8778",strokeWidth=2,
           gradient=dict(type="linear",coordinates=[0,-52,0,54],stops=[[0,"#f8edcf"],[.6,"#e5d9b8"],[1,"#c3bf9e"]]))
    s.path("M-49 -38 Q-15 -48 34 -39",stroke="#fff4d9",sw=4,parent=head)
    s.rect(-56,-32,112,70,"#304f52",radius=20,parent=head,
           gradient=dict(type="linear",coordinates=[0,-32,0,38],stops=[[0,"#233e46"],[1,"#476d65"]]))
    s.path("M-44 -19 Q-8 -30 34 -22",stroke="#93b2a0",sw=2,parent=head,opacity=.27)
    eyes=s.group(parent=head,name=name+"-eyes",y=2)
    for xx in [-23,23]:
        s.ellipse(xx-16,-19,32,37,"#1ce8efb3",parent=eyes)
        s.rect(xx-6,-12,12,24,"#d6ecae",radius=6,parent=eyes)
        s.rect(xx-4,-11,4,9,"#fff2c8",radius=2,parent=eyes)
    if mood=="sad":
        s.set(eyes,scaleY=.5,rotation=-8)
    s.path("M-5 24 Q0 28 5 24",stroke="#b0cf9e",sw=2,parent=head)
    s.line(-38,45,-22,45,"#f7ebcb",2,parent=head)
    if weathered:
        s.path("M40 -44 L47 -40 M48 39 L43 44 M-42 43 L-36 45",stroke="#a7a68a",sw=2,parent=head)
        s.rect(27, -164, 12, 11, RED, radius=1, parent=torso,rotation=-8)
    front=arm(s,torso,name+"-front-arm",(45,-141),True)
    for a,phase in [(back,0),(front,math.pi)]:
        s.set(a["upper"],rotation=wave(s.duration,8,20 if walk else 1,.85,phase))
        s.set(a["lower"],rotation=wave(s.duration,-16,10 if walk else 1,.85,phase))
    blinks(s,eyes,[2.2,6.7,10.5])
    return dict(root=root,torso=torso,head=head,eyes=eyes,front=front,back=back,antenna=antenna)


def tree(s,x,y,scale=1,season="spring",seed=4,depth=1):
    rng=random.Random(seed)
    g=s.group(x=x,y=y,scaleX=scale,scaleY=scale)
    leaf="#719880" if season=="spring" else "#b77d52" if season=="autumn" else "#4c7f72"
    s.path("M-18 0 Q-13 -110 -22 -215 L-58 -273 L-47 -277 L-8 -232 L8 -310 L23 -309 L12 -206 Q20 -100 25 0 Z","#42625a",parent=g)
    s.path("M-1 -16 Q-9 -126 -2 -213 M5 -174 L-14 -199",stroke="#7b8970",sw=3,parent=g)
    if season!="winter":
        for i in range(16):
            xx=rng.uniform(-108,100)
            yy=rng.uniform(-356,-239)
            s.ellipse(xx-45,yy-37,90,74,leaf,parent=g,opacity=.85)
        for i in range(40):
            xx=rng.uniform(-117,121)
            yy=rng.uniform(-353,-229)
            s.path(f"M{xx} {yy} Q{xx+12} {yy-15} {xx+20} {yy-4} Q{xx+11} {yy+5} {xx} {yy} Z",
                   "#9eaf86" if season!="autumn" else "#e0ab67",parent=g,opacity=.6)
        if season=="spring":
            for i in range(20):
                xx=rng.uniform(-110,110); yy=rng.uniform(-350,-235)
                s.ellipse(xx,yy,10,8,"#d4b2a0",parent=g,opacity=.85)


def landscape(s,season="spring",evening=False,rain=False):
    sky1,sky2=("#264b58","#91aaa0") if evening else ("#9db9b1","#eee1be")
    s.rect(-1000,-800,3600,2300,sky1,gradient=gradient(sky1,sky2,-100,600))
    if not rain:
        s.ellipse(1130,130,110,110,"#d9d7ae",opacity=.7)
        s.ellipse(1090,90,190,190,"#00000000",gradient=dict(type="radial",coordinates=[1185,185,95],
                  stops=[[0,"#55fff0ba"],[1,"#00fff0ba"]]))
    for source,color in [("M-700 520 Q-100 210 520 450 Q1050 160 1850 430 L2500 1100 L-700 1100 Z","#718e80"),
                         ("M-500 600 Q50 350 630 530 Q1170 260 2300 550 L2400 1100 L-500 1100 Z","#537b6a")]:
        s.path(source,color)
    for i,x in enumerate([-190,20,240,1350,1550,1760]):
        tree(s,x,610,.65,season,seed=i+20)
    s.path("M-500 680 Q200 530 800 650 Q1190 535 2300 650 L2300 1200 L-500 1200 Z","#8c9e76" if season!="winter" else "#bbc7b6")
    s.path("M-600 740 Q200 605 1000 684 Q1600 605 2300 695 L2400 900 L-600 1000 Z","#c3b58f" if season!="winter" else "#d7d4bc")
    s.path("M-300 724 Q300 659 850 704 Q1370 654 2000 705",stroke="#e3d1a6",sw=4,opacity=.6)
    for i in range(28):
        x=(i*127)%1800-100; y=740+(i*37)%130
        s.path(f"M{x} {y} Q{x+19} {y-5} {x+37} {y+1}",stroke="#a9a381",sw=2,opacity=.6)
    # The opening and closing crossing are visually identical landmarks.
    tree(s,1210,676,1.5,season,seed=71)
    for x in [200,430,660,890]:
        s.rect(x,520,11,150,"#7d8f76",radius=4)
    s.path("M100 556 Q550 531 1010 555 M100 605 Q550 580 1010 604",stroke="#879780",sw=9)
    for x in [1010,1125]:
        s.rect(x,521,14,153,"#405f56",radius=4)
        s.ellipse(x-4,513,22,16,"#a8ad8c")
    s.line(1016,544,1131,544,"#405f56",7)
    s.line(1016,601,1131,601,"#405f56",7)
    if rain:
        rng=random.Random(41)
        for i in range(95):
            x=rng.uniform(-100,1750); y=rng.uniform(60,900)
            n=s.line(0,0,-10,29,"#8ab9c7bf",1.3,x=x,y=y,opacity=.65)
            vals=[]
            period=.9+rng.random()*.7
            for j in range(math.ceil(s.duration/period)+1):
                t=j*period
                if t>s.duration: break
                vals.extend([(t,y-250),(min(t+period-.005,s.duration),y+400)])
            if vals[-1][0]<s.duration: vals.append((s.duration,y))
            s.set(n,y=keys(*vals,easing="linear"))
    rng=random.Random(17)
    for i in range(35):
        x=rng.uniform(-100,1800); y=rng.uniform(720,880)
        s.path(f"M{x} {y} L{x-5} {y-14} M{x} {y} L{x+8} {y-11}",stroke="#617b5a",sw=2,opacity=.6)


def foreground(s):
    s.path("M-100 870 Q100 760 300 845 L440 920 L-100 920 Z","#294f4b",opacity=.85)
    s.path("M1220 920 Q1390 770 1700 815 L1700 950 Z","#294f4b",opacity=.85)


def workshop(s):
    s.rect(-600,-500,2800,1800,"#496f69",gradient=gradient("#254c52","#82917a",0,900))
    for x in range(-100,1800,160):
        s.line(x,-100,x,1000,"#315c59",2,opacity=.4)
    s.rect(130,130,380,420,"#c3c3a3",radius=140)
    s.rect(146,146,348,390,"#96afb0",radius=127)
    s.path("M150 440 Q330 300 492 420 L492 530 L150 530 Z","#658e80")
    s.line(320,145,320,533,"#c3c3a3",10)
    s.line(150,350,492,350,"#c3c3a3",10)
    s.rect(830,185,440,18,"#a68b62",radius=3)
    for i in range(6):
        s.rect(856+i*62,118-(i%3)*20,40,65+(i%3)*20,["#9e6d4b","#a7ac88","#cebd8b"][i%3],radius=5)
    s.line(1130,215,1130,340,"#8ba18b",4)
    s.path("M1093 328 L1167 328 L1187 351 L1073 351 Z","#c6b680")
    s.path("M1090 351 L920 703 L1370 703 L1169 351 Z","#19ffe3a0")
    for x in [580,740]:
        s.line(x,330,x,420,"#9bb19b",5)
        s.ellipse(x-10,419,20,28,"#00000000",stroke="#b8c1a0",strokeWidth=4)
    s.rect(140,715,1320,145,"#765c48",radius=8)
    s.rect(100,690,1400,35,"#b18a5b",radius=8)
    s.line(130,697,1470,697,"#dcc08b",3)
    s.path("M180 740 Q560 750 1150 733 M230 810 Q750 791 1320 803",stroke="#906e50",sw=2,opacity=.6)


def lamp(s,x,y):
    s.ellipse(x-45,y-9,90,18,"#49645b")
    s.line(x,y-12,x-20,y-120,"#4a6158",10)
    s.line(x-20,y-120,x+40,y-185,"#a3ac89",9)
    s.path(f"M{x+10} {y-185} Q{x+48} {y-232} {x+84} {y-184} Z","#d8b56b")
    s.ellipse(x+20,y-193,55,14,"#f4dc9b")
