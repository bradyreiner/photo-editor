#!/usr/bin/env python3
"""Original brush-score authoring and playback through the RUNNING Looksy app.

Geometry below plans where marks go; it never renders, imports or substitutes a
raster image. Every colored mark is a pressure stroke in photo.edit_composition.
"""
from __future__ import annotations

import argparse
from collections import defaultdict
import json
import math
from pathlib import Path
import random
import sys
import time

ROOT=Path(__file__).resolve().parents[3]
sys.path.insert(0,str(ROOT/'tools/character-animation'))
from looksy_client import Looksy

OUT=ROOT/'marketing/outputs/painterly-keyframe'
SESSION=OUT/'looksy-session/automation/session.json'
ASSET='board:b6603aa4-4759-45fb-aba8-506ad0fd77e6:56bdd62f-6b5e-4749-a9d1-0972a93f46d5'
R=random.Random(82913)


def rgb(color):return tuple(int(color[i:i+2],16) for i in [1,3,5])
def mix(a,b,t):
    a,b=rgb(a),rgb(b)
    return '#'+''.join(f'{round(x+(y-x)*max(0,min(1,t))):02x}' for x,y in zip(a,b))
def inside(x,y,poly):
    hit=False;j=len(poly)-1
    for i,(px,py) in enumerate(poly):
        qx,qy=poly[j]
        if (py>y)!=(qy>y) and x<(qx-px)*(y-py)/(qy-py)+px:hit=not hit
        j=i
    return hit


class Score:
    def __init__(self):self.layers=[];self.active=None;self.marks=0
    def layer(self,name):
        self.active=dict(name=name,groups={});self.layers.append(self.active)
    def mark(self,points,color,size,opacity=.92,material='dry_ink',texture=.19):
        size=round(size*2)/2
        color=color.lower()
        key=json.dumps([color,size,round(opacity,2),material,texture])
        groups=self.active['groups']
        if key not in groups:groups[key]=dict(color=color,size=size,opacity=opacity,
            material=material,texture=texture,marks=[])
        groups[key]['marks'].append([[round(x,2),round(y,2),round(p,3)] for x,y,p in points])
        self.marks+=1
    def stroke(self,points,color,size,opacity=.92,material='dry_ink'):
        self.mark([(x,y,1) for x,y in points],color,size,opacity,material)
    def poly(self,poly,dark,light,size=13,angle=-.4,grain=True):
        x0=min(x for x,y in poly);x1=max(x for x,y in poly)
        y0=min(y for x,y in poly);y1=max(y for x,y in poly)
        step=size*.52
        colors=[mix(dark,light,i/5) for i in range(6)]
        y=y0+step*.3
        row=0
        while y<=y1:
            x=x0+(row%2)*step*.5
            while x<=x1:
                xx=x+R.uniform(-.14,.14)*step;yy=y+R.uniform(-.14,.14)*step
                if inside(xx,yy,poly):
                    t=(xx-x0)/max(1,x1-x0)*.55+(1-(yy-y0)/max(1,y1-y0))*.25+.1
                    index=max(0,min(5,round((t+R.uniform(-.11,.11))*5)))
                    theta=angle+R.uniform(-.3,.3)
                    length=size*R.uniform(.15,.5)
                    dx=math.cos(theta)*length;dy=math.sin(theta)*length
                    # Keep coverage near the contour; surface-dependent edge
                    # cleanup is painted by subsequent contour/detail strokes.
                    self.mark([(xx-dx/2,yy-dy/2,.78),(xx+dx/2,yy+dy/2,1)],colors[index],size,
                              .95,'dry_ink' if grain else 'solid',.16 if grain else 0)
                x+=step
            y+=step;row+=1
    def oval(self,cx,cy,rx,ry,dark,light,size=10,angle=-.5):
        poly=[(cx+rx*math.cos(i*math.tau/64),cy+ry*math.sin(i*math.tau/64)) for i in range(64)]
        self.poly(poly,dark,light,size,angle)
    def dab(self,x,y,color,size,angle=0,length=1,opacity=.9):
        self.mark([(x,y,.85),(x+math.cos(angle)*length,y+math.sin(angle)*length,1)],color,size,opacity)
    def serial(self):
        return dict(schema='looksy-native-brush-score-v1',assetId=ASSET,width=1600,height=900,
                    description='After the Rain — original cinematic oil-painting study',
                    renderer='Running Looksy photo.edit_composition paint',markCount=self.marks,
                    layers=[dict(name=l['name'],groups=list(l['groups'].values())) for l in self.layers])


def author():
    s=Score()
    s.layer('01 · Atmosphere / warm light through rain')
    # Opaque native-brush ground: broad wet strokes, then broken atmospheric color.
    for y in range(-45,960,30):
        c=mix('#a5b5b6','#dfc9a0',min(1,y/600)) if y<550 else mix('#afb1a3','#697b83',(y-550)/400)
        s.stroke([(-80,y),(1680,y)],c,95,1,'solid')
    for i in range(1350):
        x=R.uniform(0,1600);y=R.uniform(0,520)
        t=(y/520)*.6+max(0,1-abs(x-1080)/700)*.35
        c=mix('#9eafb5','#f0d4a0',round(t*7)/7)
        s.dab(x,y,c,R.choice([13,19,25]),R.uniform(-.2,.2),R.uniform(15,42),.42)
    # The sun is veiled by moist air, a paint mass rather than a glowing disk.
    s.oval(1110,205,112,57,'#d9c69e','#eed9ab',30)
    for i in range(140):
        x=R.gauss(1080,165);y=R.gauss(215,62)
        s.dab(x,y,R.choice(['#d6c4a5','#e5cca3','#e9d4b2']),R.choice([14,22,30]),.02,30,.4)

    s.layer('02 · Distant park / foliage and pond')
    s.poly([(-30,400),(180,352),(330,380),(480,341),(710,366),(840,331),(1030,371),(1240,335),(1640,378),(1640,653),(-30,650)],
           '#778d81','#a7ab86',25)
    # Distant tree silhouettes resolve into a handful of upright strokes.
    for x in [70,205,355,470,780,875,1040,1200,1370,1530]:
        y=R.uniform(350,410);h=R.uniform(140,240)
        s.stroke([(x+10,y+160),(x,y),(x+R.uniform(-20,20),y-h*.45)],'#75887c',R.choice([7,11,15]),.65)
        for i in range(40):
            xx=R.gauss(x,53);yy=R.gauss(y-h*.38,64)
            s.dab(xx,yy,R.choice(['#8e9b82','#7e927e','#a8ab84','#a0a88b']),R.choice([13,19,25]),-.5,15,.75)
    s.poly([(-30,525),(180,494),(350,514),(580,485),(880,520),(1040,538),(990,640),(550,666),(-30,641)],
           '#8da5a2','#c0c4a7',22,0)
    for i in range(750):
        x=R.uniform(0,1120);y=R.uniform(525,650)
        if inside(x,y,[(-30,525),(180,494),(350,514),(580,485),(880,520),(1040,538),(990,640),(550,666),(-30,641)]):
            c=R.choice(['#8ca7a5','#b2bca4','#c6c5a5','#d5cba6','#7d9d99'])
            s.dab(x,y,c,R.choice([4,7,11]),R.uniform(-.08,.08),R.uniform(9,32),.72)
    s.poly([(1080,505),(1360,476),(1630,503),(1640,728),(1035,686),(984,604)],'#6e8a6d','#acb184',24)

    s.layer('03 · Wet path / reflected sky')
    s.poly([(-25,664),(255,614),(519,643),(832,617),(1110,651),(1630,592),(1640,940),(-25,940)],
           '#73858a','#c1baa7',30,.02)
    for i in range(1400):
        x=R.uniform(-10,1610);y=R.uniform(651,925)
        colors=['#83959a','#9ba8a5','#aab1a5','#b7b4a1','#c9bfa7','#778d95','#a8a7a2']
        s.dab(x,y,R.choice(colors),R.choice([6,10,16]),R.uniform(-.06,.06),R.uniform(7,45),.62)
    for i in range(150):
        y=R.uniform(668,900);x=R.gauss(1110+(y-650)*.2,70+(y-650)*.23)
        s.dab(x,y,R.choice(['#d3c19b','#d9c8a4','#e0cda7','#bdb69f']),R.choice([5,8,12]),0,R.uniform(7,37),.68)
    # Footing and shadows share the plane of the path.
    s.oval(658,831,106,15,'#566f75','#819391',10,0)
    s.oval(1011,770,124,18,'#506a72','#80928c',10,0)

    s.layer('04 · Near trees / atmospheric depth')
    for poly,dark,light in [
        ([(78,-30),(123,-30),(153,350),(169,671),(137,718),(114,617),(110,313)],'#394f54','#7d8670'),
        ([(1501,-30),(1546,-30),(1528,353),(1560,753),(1519,790),(1497,629),(1509,303)],'#354f51','#6e8068')]:
        s.poly(poly,dark,light,18,1.45)
    for path in [[(139,380),(209,294),(270,181),(306,92)],[(144,292),(68,171),(-20,117)],
                 [(1519,390),(1429,264),(1365,110)],[(1516,260),(1610,146)]]:
        s.stroke(path,'#53685f',19,.9)
        s.stroke([(x-4,y) for x,y in path],'#8b9478',4,.55)
    for i in range(1450):
        x=R.choice([R.gauss(172,175),R.gauss(1450,173)]);y=R.gauss(190,150)
        if y>510 or x<0 or x>1600:continue
        s.dab(x,y,R.choice(['#657d64','#78906b','#91a174','#a5ae7f','#a8ae82','#536f61']),
              R.choice([8,14,21]),R.uniform(-1.0,-.1),R.uniform(4,20),.87)
    # Thin reflected trunks, interrupted by water movement.
    for x in [134,202,350,481,790,868]:
        for y in range(546,643,13):
            s.dab(x+(y-546)*.15+R.uniform(-7,7),y,'#82968a',5,0,R.uniform(5,19),.4)

    s.layer('05 · Figure / anatomical block-in and coat')
    # Full adult proportions: an 80px head on a roughly 570px standing figure.
    s.poly([(592,639),(622,641),(617,713),(603,803),(580,809),(580,736)],'#3d4f55','#72807c',11,1.52)
    s.poly([(640,641),(670,633),(668,704),(687,800),(662,810),(639,731)],'#3c5057','#78847d',11,1.3)
    s.poly([(581,797),(604,798),(611,817),(645,826),(647,836),(582,839),(575,830)],'#303f49','#637371',7,.1)
    s.poly([(662,796),(687,797),(697,819),(725,828),(726,836),(663,840),(657,828)],'#303f49','#64746e',7,.1)
    s.stroke([(581,834),(644,834)],'#adab94',2.4,.78)
    s.stroke([(664,835),(725,833)],'#aaa88f',2.4,.78)
    # Back sleeve, then torso, then near sleeve: physically readable overlap.
    s.poly([(578,394),(597,398),(612,441),(601,488),(612,518),(591,532),(570,501),(559,449)],
           '#77745d','#b1a27a',13,1.35)
    s.poly([(592,383),(627,364),(663,375),(691,403),(695,456),(681,514),(687,583),(704,650),
            (650,670),(575,661),(558,623),(573,546),(562,484),(562,426)],'#7b785f','#d0b688',17,1.32)
    # Large cloth planes and lapels; edges are later restated with narrow paint.
    s.poly([(627,373),(644,378),(622,427),(609,472),(611,537),(603,636),(582,638),(593,501),(589,432)],
           '#9c9270','#d5bd8e',10,1.4)
    s.poly([(656,380),(678,397),(658,442),(633,459),(647,415)],'#aa9e78','#dfc89a',9,1.1)
    s.poly([(645,475),(670,459),(662,549),(681,628),(650,654),(628,637)],'#88866a','#b2a27b',12,1.4)
    s.poly([(680,399),(702,412),(724,461),(766,487),(760,515),(705,491),(681,457),(667,427)],
           '#8e8668','#c7af81',12,.5)
    # Sleeve cuffs and the rolled seam communicate thickness.
    s.stroke([(747,484),(763,496),(757,516),(743,511)],'#aaa07a',7,.95)
    for line in [[(579,483),(590,522),(583,562)],[(616,439),(620,475),(615,515)],
                 [(643,482),(638,536),(655,602)],[(685,437),(703,462),(718,478)],
                 [(591,607),(578,643)],[(679,582),(692,639)]]:
        s.stroke(line,'#727762',2.8,.66)
    s.stroke([(614,454),(613,532),(606,624)],'#d8c093',2,.75)
    for x,y in [(625,470),(623,510),(621,550)]:
        s.dab(x,y,'#6a6b56',5,0,1,.92)
        s.dab(x+1,y-1,'#b5aa83',1.7,0,1,.85)
    # A real scarf wraps the neck rather than serving as a character outline.
    s.poly([(615,354),(644,350),(671,368),(658,391),(626,391),(608,377)],'#784f4d','#b27e65',10,.2)
    s.poly([(633,381),(652,389),(647,449),(636,484),(621,475),(628,434)],'#815650','#bd8b6d',9,1.5)
    s.stroke([(624,372),(650,378),(665,369)],'#c19474',3,.7)
    s.stroke([(632,401),(630,440),(625,470)],'#c09174',2,.75)

    s.layer('06 · Portrait / hands and the offered cup')
    s.poly([(635,331),(660,331),(660,365),(647,377),(628,361)],'#9c7768','#d6af89',8,1.3)
    # Hair mass, observed face planes, and subdued cool side shadow.
    s.oval(638,301,35,48,'#453d38','#887153',9,1.1)
    s.oval(610,308,18,21,'#46413b','#786a51',8,1)
    face=[(641,275),(656,274),(670,286),(675,301),(674,309),(684,318),(679,323),
          (677,337),(666,349),(650,345),(637,334),(633,313),(633,289)]
    s.poly(face,'#a48071','#d9b78f',6.5,1.0)
    s.poly([(646,280),(659,281),(669,289),(670,302),(656,303),(645,293)],'#bb9980','#dfc09a',4.5,.3)
    s.poly([(654,311),(669,306),(674,316),(676,330),(664,337),(653,327)],'#c3997e','#e1b994',4.5,.5)
    s.poly([(636,299),(645,300),(650,318),(648,337),(640,330),(635,318)],'#8f7770','#b5957d',4.5,1.4)
    # Temple, cheekbone, nose and jaw have distinct planar values.
    s.stroke([(667,301),(670,309),(680,318),(675,319)],'#e0be98',3.5,.9)
    s.stroke([(677,321),(674,323)],'#9a7264',2,.85)
    s.stroke([(659,326),(668,327),(675,326)],'#98685f',2.4,.86)
    s.stroke([(662,329),(669,331),(674,328)],'#ba8b73',2,.78)
    s.stroke([(660,341),(666,345),(672,339)],'#dfb996',2,.72)
    # One near eye and a foreshortened far eye; restrained detail at full-shot scale.
    s.stroke([(648,300),(655,297),(661,299)],'#68534a',2.2,.91)
    s.stroke([(651,304),(656,303),(661,305)],'#56483f',1.7,.93)
    s.dab(657,305,'#3e4541',2.4,0,.5,.97)
    s.dab(658,304,'#e7cba5',1,0,.2,.9)
    s.stroke([(670,299),(673,301)],'#766050',1.5,.82)
    s.stroke([(671,305),(673,306)],'#655547',1.2,.82)
    s.oval(633,314,6,11,'#a4816f','#c7a483',3.4,1.2)
    s.stroke([(634,310),(630,313),(633,319)],'#8b6e61',1.8,.8)
    s.poly([(615,279),(634,257),(654,259),(667,271),(669,283),(654,279),(645,287),(633,302),(624,308),(619,295)],
           '#4e443c','#8c7456',7,.8)
    for line in [[(622,278),(638,265),(653,267)],[(623,289),(635,275),(648,272)],
                 [(621,300),(630,284),(640,278)],[(607,300),(610,292),(619,294)]]:
        s.stroke(line,'#aa8c63',1.7,.68)
    s.stroke([(665,271),(672,287),(673,297)],'#6c5846',2,.68)
    # Near hand is modeled as palm + opposed thumb and folded finger planes.
    s.poly([(761,495),(775,493),(784,496),(789,502),(800,503),(800,509),(788,512),
            (782,519),(771,519),(762,512)],'#9d7b6a','#d1aa84',4.4,.35)
    s.stroke([(775,498),(784,500),(790,505)],'#e3bd92',2.4,.86)
    s.stroke([(780,510),(789,509),(796,507)],'#aa7b67',1.6,.8)
    s.stroke([(775,514),(783,514)],'#a47d67',1.4,.8)
    # Porcelain cup: a modest curved volume with a visible ellipse at its rim.
    s.poly([(793,477),(819,478),(818,506),(811,516),(800,513),(795,506)],'#a7aa9d','#e2d7ba',5,1.3)
    s.oval(806,477,14,4,'#8c8d7c','#c2ba9f',3.5,0)
    s.stroke([(795,477),(806,474),(820,478)],'#ece1c4',1.8,.95)
    s.stroke([(820,485),(827,486),(829,497),(821,502)],'#c8c3aa',3,.92)
    s.stroke([(807,464),(803,451),(809,438)],'#cfc9b0',2,.35)

    s.layer('07 · Machine / painted metal and believable construction')
    # Wheels first; cast shadows tie the object to the same ground as the woman.
    for cx,cy in [(927,748),(1035,744)]:
        s.oval(cx,cy,23,29,'#263e48','#5c7274',7,1.4)
        s.oval(cx+2,cy,11,17,'#5b7274','#a1aaa0',5,1.4)
        s.oval(cx+3,cy,5,7,'#647a7a','#c0c1a9',3,1)
    s.poly([(908,649),(1014,638),(1055,658),(1061,721),(1029,746),(917,745)],'#617a7d','#a9b0a2',13,.9)
    s.poly([(908,649),(1014,638),(1037,658),(925,670)],'#9fa99a','#d6c9a7',10,.08)
    s.poly([(925,670),(1037,658),(1035,731),(924,741)],'#9aab9f','#d2c5a2',11,1.3)
    s.poly([(1037,658),(1055,658),(1061,721),(1035,731)],'#5b777a','#8d9d96',8,1.45)
    s.poly([(937,690),(1005,683),(1009,718),(939,725)],'#7a9390','#a8b4a2',7,.1)
    s.stroke([(934,679),(1028,669)],'#e4d7b4',2.5,.9)
    s.stroke([(925,737),(1033,727)],'#687e79',3,.87)
    for xx,yy in [(935,677),(1026,669),(937,731),(1027,723)]:
        s.dab(xx,yy,'#5b7474',3.5,0,1,.95)
        s.dab(xx+1,yy-1,'#c9c9af',1.5,0,1,.9)
    for i in range(5):
        s.stroke([(949+i*10,696),(950+i*10,709)],'#58767a',2.3,.87)
    # Pan-tilt neck and head cast a small shadow onto the upper chassis.
    s.poly([(960,610),(982,607),(991,645),(963,651)],'#526e76','#a7b3a2',8,1.2)
    s.oval(973,611,17,13,'#476572','#b3bda7',6,0)
    s.stroke([(958,616),(984,612)],'#d0c9aa',2,.85)
    s.poly([(927,548),(988,535),(1015,557),(1014,596),(946,614),(924,588)],'#7c9690','#c8c2a6',10,.3)
    s.poly([(927,548),(988,535),(1015,557),(947,570)],'#b5bbaa','#e2d2ad',8,.1)
    s.poly([(947,570),(1015,557),(1014,596),(948,613)],'#879e96','#b7bdaa',8,1.3)
    s.poly([(927,548),(947,570),(948,613),(924,588)],'#526e75','#a0afa0',6,1.2)
    # Lens cylinder points up and toward the offered cup, rather than a cartoon face.
    s.oval(932,578,18,27,'#314d5a','#a4b7ab',6,1.3)
    s.oval(928,575,13,21,'#213f51','#5f858a',4,1.3)
    s.oval(925,572,7,12,'#294d60','#7daca7',3,1.1)
    s.stroke([(921,562),(925,559),(930,561)],'#d4e0c5',1.8,.95)
    s.dab(929,567,'#bfddcc',2.3,0,1,.9)
    s.stroke([(968,581),(997,575)],'#637f7b',1.6,.7)
    s.dab(997,589,'#d6b071',4,0,1,.9)
    s.stroke([(1000,545),(1011,516),(1004,500)],'#718c87',3,.92)
    s.dab(1004,499,'#d0b27a',5,0,1,.9)
    # A small utility arm holds a wildflower: a counter-offer to the coffee.
    s.oval(924,679,10,13,'#3d606a','#b7bba5',4,1)
    s.stroke([(919,676),(881,635),(862,580)],'#526e76',9,.95)
    s.stroke([(919,673),(884,632),(865,579)],'#b6c0ab',3,.9)
    s.oval(881,634,7,8,'#4d6e76','#a8b7a4',3.5,1)
    s.stroke([(860,581),(851,573),(849,558)],'#789084',4,.95)
    s.stroke([(865,580),(869,569),(859,559)],'#b1bba3',3.5,.92)
    s.stroke([(853,570),(847,533),(836,505)],'#758f69',2,.95)
    s.stroke([(847,539),(836,534),(830,525)],'#8d9f71',3,.9)
    for i in range(8):
        a=i*math.tau/8
        s.stroke([(836+math.cos(a)*3,504+math.sin(a)*3),
                  (836+math.cos(a)*10,504+math.sin(a)*9)],'#e0d6b7',4.5,.94)
    s.dab(836,504,'#bc9b56',5,0,.5,.98)
    s.dab(835,503,'#ead19b',2,0,.5,.98)

    s.layer('08 · Finishing / broken light and wet ground')
    # Sparse finishing marks rather than a uniform grunge overlay.
    for i in range(500):
        x=R.uniform(0,1600);y=R.uniform(710,920)
        if 535<x<1090 and y<845:continue
        s.dab(x,y,R.choice(['#a9b2a4','#c0c4ac','#d4cbae','#6c8587']),R.choice([3,5,7]),0,R.uniform(5,24),.7)
    for i in range(110):
        x=R.choice([R.uniform(0,360),R.uniform(1260,1600)]);y=R.uniform(735,910)
        s.stroke([(x,y),(x+R.uniform(-12,12),y-R.uniform(11,55))],
                 R.choice(['#697f64','#879569','#acb07d']),R.choice([1.5,2.5,3.5]),.84)
    for i in range(27):
        x=R.uniform(90,1500);y=R.uniform(570,880)
        if 555<x<1100:continue
        s.stroke([(x,y),(x-8,y+1),(x-12,y+3)],'#d0c9ac',1.2,.6)
    return s.serial()


def execute(score,limit=None,resume_empty_first_layer=False,resume=False):
    client=Looksy(SESSION)
    inspect=lambda:client.call('photo.inspect_composition',dict(assetId=ASSET))
    before=inspect()
    if not before.get('ok') or before.get('width')!=1600 or before.get('height')!=900:
        raise RuntimeError('The exact admitted painting canvas is not open')
    progress_path=OUT/'paint-progress.json'
    saved=json.loads(progress_path.read_text()) if progress_path.exists() else None
    if saved and not resume:raise RuntimeError('Existing painting progress found; inspect before replaying')
    adopted=None
    if before.get('layers') and not saved:
        if resume_empty_first_layer and len(before['layers'])==1 and before['layers'][0].get('kind')=='pixel':
            adopted=before['layers'][0]['id']
        else:raise RuntimeError('Fresh painting requires the verified empty document')
    if saved and (saved['assetId']!=ASSET or any(x['layerId'] not in {l['id'] for l in before['layers']} for x in saved['layers'])):
        raise RuntimeError('Native painting recovery identity changed')
    calls=saved['calls'] if saved else 0;layers=saved['layers'] if saved else []
    def wait():
        deadline=time.monotonic()+60
        while True:
            d=inspect()
            if not d.get('ok'):raise RuntimeError(d)
            if d.get('paintError'):raise RuntimeError(d['paintError'])
            if not d.get('paintBusy'):return d
            if time.monotonic()>deadline:raise TimeoutError('Native paint worker did not finish')
            time.sleep(.06)
    for li,layer in enumerate(score['layers']):
        if saved and li<saved['layer']:continue
        if limit is not None and li>=limit:break
        recovering=saved and li==saved['layer']
        if recovering:
            wait()
            lid=layers[li]['layerId']
        elif li==0 and adopted:
            lid=adopted
        else:
            created=client.call('photo.edit_composition',dict(assetId=ASSET,operation='add_pixel_layer',values={}))
            if not created.get('ok'):raise RuntimeError(created)
            lid=created.get('createdLayerId')
        if not lid:raise RuntimeError('Native creation did not return a stable paint layer ID')
        if not recovering:
            layers.append(dict(name=layer['name'],layerId=lid))
            progress_path.write_text(json.dumps(dict(assetId=ASSET,layers=layers,calls=calls,layer=li,group=-1)))
        print(layer['name'],flush=True)
        for gi,group in enumerate(layer['groups']):
            if recovering and gi<=saved['group']:continue
            points=[]
            chunks=[]
            for mark in group['marks']:
                # Zero-flow travel lets independent oil marks share one native
                # stroke without drawing connecting lines across the canvas.
                segment=[dict(x=mark[0][0],y=mark[0][1],pressure=0)]
                segment += [dict(x=x,y=y,pressure=p) for x,y,p in mark]
                segment += [dict(x=mark[-1][0],y=mark[-1][1],pressure=0)]
                if len(points)+len(segment)>3500:chunks.append(points);points=[]
                points+=segment
            if points:chunks.append(points)
            for chunk in chunks:
                for point in chunk:
                    point['x']=min(1599,max(0,point['x']))
                    point['y']=min(899,max(0,point['y']))
                values=dict(points=chunk,color=group['color'],size=group['size'],hardness=.72,
                    opacity=group['opacity'],flow=1,spacing=.17,pressureSize=True,pressureFlow=True,
                    textureMaterial=group['material'],textureStrength=group['texture'],
                    textureScale=3.5,textureSeed=1129)
                result=client.call('photo.edit_composition',dict(assetId=ASSET,operation='paint',layerId=lid,values=values))
                if not result.get('ok'):raise RuntimeError(result)
                calls+=1
                progress_path.write_text(json.dumps(dict(assetId=ASSET,layers=layers,calls=calls,layer=li,group=gi)))
                wait()
            if gi%20==0:print(f'  {gi+1}/{len(layer["groups"])} color/brush groups, {calls} native paint commits',flush=True)
    final=wait()
    (OUT/'paint-inspection.json').write_text(json.dumps(final,indent=2))
    (OUT/'native-paint-receipt.json').write_text(json.dumps(dict(assetId=ASSET,canvas=[1600,900],
        nativePaintCommits=calls,authoredMarks=score['markCount'],layers=layers,
        source='Original authored brush points; no imported image, SVG or generated image',
        app='Running Looksy Release',paintBusy=final.get('paintBusy'),paintError=final.get('paintError')),indent=2))
    print('Native painting complete',flush=True)


if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--execute',action='store_true');parser.add_argument('--limit',type=int)
    parser.add_argument('--resume-empty-first-layer',action='store_true')
    parser.add_argument('--resume',action='store_true')
    args=parser.parse_args()
    score=author()
    OUT.mkdir(parents=True,exist_ok=True)
    (OUT/'brush-score.json').write_text(json.dumps(score,separators=(',',':')))
    print(f'Authored {score["markCount"]} painted marks in {sum(len(x["groups"]) for x in score["layers"])} native brush groups',flush=True)
    if args.execute:execute(score,args.limit,args.resume_empty_first_layer,args.resume)
