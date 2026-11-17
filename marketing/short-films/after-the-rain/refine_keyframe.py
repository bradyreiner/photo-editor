"""Append a deliberate focal-definition pass with native Looksy brushes."""
import json
from pathlib import Path
import sys
import time

from paint_keyframe import ASSET,OUT,SESSION,Score,Looksy


def refine():
    s=Score();s.layer('09 · Focal definition / painted anatomy and metal')
    def stroke(points,color,size=2.5,opacity=.8):s.stroke(points,color,size,opacity)
    # Subdued foreground glazes concentrate the light on the exchange.
    s.mark([(60,120,1),(90,420,1),(138,720,1),(135,880,1)],'#384d53',120,.16,'solid',0)
    s.mark([(1535,100,1),(1516,430,1),(1540,725,1),(1565,885,1)],'#344e52',120,.2,'solid',0)
    # Follow the planes of the face, rather than drawing a uniform contour.
    stroke([(644,279),(653,278),(661,282)],'#e2c29e',3.7,.83)
    stroke([(668,290),(671,300),(670,307)],'#b78f77',2.6,.8)
    stroke([(670,309),(678,317),(675,319)],'#e0b592',2.2,.9)
    stroke([(677,321),(674,322)],'#77564b',1.5,.8)
    stroke([(649,299),(655,297),(660,299)],'#4e453b',1.8,.86)
    stroke([(651,305),(656,304),(660,306)],'#4c443b',1.4,.9)
    stroke([(657,306),(658,308)],'#373c37',1.6,.97)
    stroke([(657,304),(658,304)],'#eed7b5',1,.95)
    stroke([(659,326),(666,327),(673,325)],'#885a51',1.7,.82)
    stroke([(661,330),(668,331),(673,328)],'#ca9b7e',1.8,.77)
    stroke([(651,340),(663,346),(670,342)],'#b18a71',2,.64)
    stroke([(642,332),(639,321),(640,307)],'#8c7465',3.2,.66)
    stroke([(638,347),(639,359),(649,367)],'#ccaa86',3.7,.83)
    # Broken dark folds and light planes give the coat a stitched, worn body.
    stroke([(572,426),(570,461),(578,487)],'#63695b',4.5,.73)
    stroke([(579,497),(588,522),(581,550)],'#74765e',3.4,.74)
    stroke([(601,399),(608,417),(613,430)],'#e0c28d',4,.8)
    stroke([(626,396),(616,432),(613,454)],'#c8b081',3.5,.85)
    stroke([(656,397),(644,420),(636,441)],'#dfc294',4,.88)
    stroke([(625,452),(623,493),(620,540),(614,590),(610,634)],'#6c6d56',2.5,.73)
    stroke([(629,454),(627,498),(624,538)],'#d9bd8d',2.8,.8)
    stroke([(647,485),(642,524),(649,565)],'#686d5b',3.7,.64)
    stroke([(653,547),(668,593),(680,633)],'#bca779',5,.8)
    stroke([(580,649),(611,656),(650,657),(687,646)],'#7c7b5f',3,.75)
    stroke([(686,417),(699,444),(710,464)],'#d7b987',4,.8)
    stroke([(700,464),(723,484),(745,496)],'#73745c',4,.73)
    stroke([(709,461),(729,477),(751,488)],'#d8b888',3,.82)
    stroke([(749,489),(760,497),(756,511)],'#c8b18a',3.4,.9)
    # Warm palm planes, with small, irregular finger separations.
    stroke([(766,499),(775,499),(784,504)],'#d5ab87',4.3,.84)
    stroke([(766,511),(775,516),(783,514)],'#8f6d5a',1.8,.77)
    stroke([(781,506),(790,505),(796,507)],'#cba282',2,.85)
    stroke([(778,510),(786,509)],'#895f51',1.4,.7)
    stroke([(795,480),(798,501),(803,510)],'#e3d5b5',2.6,.94)
    stroke([(796,476),(806,474),(819,478)],'#f0e0bd',1.8,.96)
    # Lens housing: the dark underside and narrow specular edge define volume.
    stroke([(928,548),(944,567),(947,609)],'#526a70',3.1,.88)
    stroke([(949,570),(1012,558)],'#d7c9a9',3.2,.94)
    stroke([(951,610),(1009,595)],'#5d7779',3.4,.88)
    stroke([(929,552),(919,568),(919,583),(926,596)],'#365661',3.8,.95)
    stroke([(925,554),(917,568),(918,581)],'#a7c0b7',2.1,.9)
    stroke([(924,568),(923,576),(925,583)],'#355f6e',3,.94)
    stroke([(923,565),(926,563)],'#d3e0c8',2,.96)
    stroke([(967,615),(969,640)],'#617c7a',5,.88)
    stroke([(979,616),(986,639)],'#c2c8b0',3,.9)
    # Metal chassis seams remain subordinate to the human/robot eye line.
    stroke([(913,652),(927,667),(1035,657)],'#d8ccb0',3,.92)
    stroke([(925,671),(926,736),(1032,727)],'#637e7c',3,.85)
    stroke([(1038,662),(1051,662),(1055,717),(1038,729)],'#506d74',3,.85)
    stroke([(935,681),(1018,673)],'#d4c7a7',2,.84)
    stroke([(943,720),(1003,714)],'#5c7a7c',2,.83)
    stroke([(922,676),(881,634),(862,579)],'#4c6a73',6,.87)
    stroke([(920,674),(884,633),(865,580)],'#c4c7ab',2.6,.92)
    stroke([(850,568),(847,536),(837,506)],'#587951',1.5,.95)
    # Dark accents at contact points keep both figures on the path.
    stroke([(586,837),(622,839),(644,835)],'#465e62',3,.85)
    stroke([(667,838),(704,840),(726,836)],'#435d62',3,.85)
    stroke([(915,772),(947,774)],'#435f65',4,.84)
    stroke([(1021,771),(1048,770)],'#435f65',4,.84)
    return s.serial()


if __name__=='__main__':
    score=refine();(OUT/'definition-brush-score.json').write_text(json.dumps(score,separators=(',',':')))
    client=Looksy(SESSION)
    inspected=client.call('photo.inspect_composition',dict(assetId=ASSET))
    original=json.loads((OUT/'native-paint-receipt.json').read_text())
    if inspected.get('paintBusy') or inspected.get('paintError') or len(inspected.get('layers',[]))!=8:
        raise RuntimeError('The exact completed eight-layer painting is required')
    if {x['id'] for x in inspected['layers']}!={x['layerId'] for x in original['layers']}:
        raise RuntimeError('Native paint layer identity changed')
    created=client.call('photo.edit_composition',dict(assetId=ASSET,operation='add_pixel_layer',values={}))
    if not created.get('ok'):raise RuntimeError(created)
    lid=created['createdLayerId'];count=0
    for g in score['layers'][0]['groups']:
        pts=[]
        for mark in g['marks']:
            pts+=[dict(x=mark[0][0],y=mark[0][1],pressure=0)]
            pts+=[dict(x=x,y=y,pressure=p) for x,y,p in mark]
            pts+=[dict(x=mark[-1][0],y=mark[-1][1],pressure=0)]
        response=client.call('photo.edit_composition',dict(assetId=ASSET,layerId=lid,operation='paint',values=dict(
            points=pts,color=g['color'],size=g['size'],hardness=.65,opacity=g['opacity'],flow=1,
            pressureSize=True,pressureFlow=True,spacing=.15,textureMaterial=g['material'],
            textureStrength=g['texture'],textureScale=3,textureSeed=1129)))
        if not response.get('ok'):raise RuntimeError(response)
        while True:
            state=client.call('photo.inspect_composition',dict(assetId=ASSET))
            if state.get('paintError'):raise RuntimeError(state['paintError'])
            if not state.get('paintBusy'):break
            time.sleep(.1)
        count+=1
        if count%10==0:print(f'{count}/{len(score["layers"][0]["groups"])} native definition strokes',flush=True)
    original['definitionPaintCommits']=count
    original['layers'].append(dict(name=score['layers'][0]['name'],layerId=lid))
    original['paintBusy']=False;original['paintError']=''
    (OUT/'native-paint-receipt.json').write_text(json.dumps(original,indent=2))
    print('Native definition pass complete',flush=True)
