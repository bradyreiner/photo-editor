#!/usr/bin/env python3
"""Serve an actual cleanup export in a pinned, independent local PlayCanvas viewer.

Requires a separately installed playcanvas@2.23.1 npm package. No remote assets
or telemetry are used. Browser actions publish native-resolution PNGs and import
receipts into the new output folder; this does not certify Octane compatibility.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import re
import shutil
import struct
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from urllib.parse import urlparse, parse_qs


def digest(path):
    with path.open('rb') as source:
        return hashlib.file_digest(source, 'sha256').hexdigest()


def inspect_ply(path):
    with path.open('rb') as source:
        header, properties, count = bytearray(), [], None
        while True:
            line = source.readline(4096)
            header += line
            if not line or len(header) > 65536:
                raise ValueError('Invalid PLY header')
            words = line.decode('ascii').strip().split()
            if words[:1] == ['element']:
                if len(words) != 3 or words[1] != 'vertex' or count is not None:
                    raise ValueError('Expected one vertex element')
                count = int(words[2])
            if words[:1] == ['property']:
                if len(words) != 3 or words[1] != 'float' or not re.fullmatch(r'[A-Za-z_][A-Za-z0-9_]*', words[2]):
                    raise ValueError('Expected scalar float properties')
                properties.append(words[2])
            if words == ['end_header']:
                break
        if b'format binary_little_endian 1.0\n' not in header or count is None or not 1 <= count <= 2000000 or \
                len(set(properties)) != len(properties) or not 14 <= len(properties) <= 512:
            raise ValueError('Unsupported Gaussian PLY')
        offset, row_bytes = len(header), 4 * len(properties)
        if path.stat().st_size != offset + count * row_bytes:
            raise ValueError('PLY payload size differs from its header')
        samples = []
        for index in sorted({0, count // 2, count - 1}):
            source.seek(offset + index * row_bytes)
            values = struct.unpack('<' + 'f' * len(properties), source.read(row_bytes))
            if not all(math.isfinite(value) for value in values):
                raise ValueError('Nonfinite sample')
            samples.append({'index': index, 'values': dict(zip(properties, values))})
    required = {'x','y','z','opacity',*(f'scale_{i}' for i in range(3)),*(f'rot_{i}' for i in range(4)),*(f'f_dc_{i}' for i in range(3))}
    rest = {p for p in properties if p.startswith('f_rest_')}
    degree = {0:0,9:1,24:2,45:3}.get(len(rest))
    if not required.issubset(properties) or degree is None or rest != {f'f_rest_{i}' for i in range(len(rest))}:
        raise ValueError('Expected standard degree 0 through 3 Gaussian properties')
    return {'count': count, 'properties': properties, 'samples': samples, 'shDegree': degree}


HTML = '''<!doctype html><html lang="en"><meta charset="utf-8">
<title>Gaussian export · independent renderer check</title>
<style>body{margin:0;background:#141619;color:#edf0f4;font:15px system-ui}header{padding:18px 24px}h1{font-size:20px;margin:0 0 8px}p{color:#b6bec9;margin:8px 0}button{font:inherit;padding:9px 15px;margin:8px 8px 0 0;background:#303843;color:white;border:1px solid #647182;border-radius:6px}button:disabled{opacity:.5}canvas{display:block;width:min(100%,1559px);height:auto;margin:auto}#status{font-variant-numeric:tabular-nums}</style>
<header><h1>Gaussian export · independent renderer</h1><p id="status">Loading the exported PLY…</p>
<button id="reset" disabled>Capture camera</button><button id="left" disabled>Left 10°</button><button id="right" disabled>Right 10°</button><button id="capture" disabled>Save current frame</button>
<p>Local PlayCanvas 2.23.1 · Gaussian PLY and camera metadata only · no upload</p></header>
<canvas id="view"></canvas><script src="engine/playcanvas.min.js"></script><script src="viewer.js"></script></html>'''

JS = r'''const status = document.getElementById('status');
const reportError = error => { status.textContent = 'FAILED: ' + String(error?.stack || error); };
window.addEventListener('error', event => reportError(event.error || event.message));
window.addEventListener('unhandledrejection', event => reportError(event.reason));
(async () => {
 const input = await (await fetch('input.json')).json();
 const cameraData = input.camera, canvas = document.getElementById('view');
 canvas.width = cameraData.width; canvas.height = cameraData.height;
 const app = new pc.Application(canvas, {graphicsDeviceOptions:{antialias:false,alpha:false,preserveDrawingBuffer:true}});
 app.setCanvasResolution(pc.RESOLUTION_FIXED, cameraData.width, cameraData.height);
 app.scene.gsplat.useTonemap = false; app.scene.gsplat.useFog = false;
 const camera = new pc.Entity('Exported capture camera');
 camera.addComponent('camera',{clearColor:new pc.Color(0,0,0),nearClip:0.01,farClip:1000,
  fov:2*Math.atan(cameraData.height/(2*cameraData.fy))*180/Math.PI,toneMapping:pc.TONEMAP_NONE});
 app.root.addChild(camera);
 const up = new pc.Vec3(...cameraData.down).mulScalar(-1);
 const target = new pc.Vec3(...cameraData.position).add(new pc.Vec3(...cameraData.forward).mulScalar(4));
 function resetCamera() {
  const r=cameraData.right, d=cameraData.down, f=cameraData.forward;
  const matrix = new pc.Mat4().set([r[0],r[1],r[2],0,-d[0],-d[1],-d[2],0,-f[0],-f[1],-f[2],0,0,0,0,1]);
  camera.setRotation(new pc.Quat().setFromMat4(matrix)); camera.setPosition(...cameraData.position);
 }
 resetCamera(); app.start();
 const asset = new pc.Asset('Actual Looksy cleanup export','gsplat',{url:'assets/scene.ply'},{reorder:false});
 app.assets.add(asset);
 await new Promise((resolve,reject) => { asset.once('load',resolve); asset.once('error',reject); app.assets.load(asset); });
 const data = asset.resource.gsplatData;
 let checked = 0;
 for (const sample of input.ply.samples) for (const [property,expected] of Object.entries(sample.values)) {
  const values=data.getProp(property);
  if (!values || values[sample.index] !== expected) throw new Error('External parser changed '+property+' at row '+sample.index);
  checked++;
 }
 if (data.numSplats !== input.ply.count || data.shBands !== input.ply.shDegree) throw new Error('Imported point count or SH degree differs');
 const entity = new pc.Entity('Exported Gaussian scene'); entity.addComponent('gsplat',{asset}); app.root.addChild(entity);
 const run = String(Date.now()); let frameNumber=0, saved=0;
 app.on('postrender',()=>frameNumber++);
 const frames = async count => {const end=frameNumber+count; while(frameNumber<end) await new Promise(requestAnimationFrame);};
 const pending = new Set();
 async function capture(stage) {
  if(pending.size) return; pending.add(stage);
  try {
   status.textContent='Rendering '+stage+'…'; await frames(45);
   const blob=await new Promise(resolve=>canvas.toBlob(resolve,'image/png'));
   if(!blob) throw new Error('Canvas PNG readback failed');
   const name=stage+'-'+run+'-'+(++saved);
   const imageResponse=await fetch('capture?name='+encodeURIComponent(name),{method:'POST',headers:{'Content-Type':'image/png'},body:blob});
   if(!imageResponse.ok) throw new Error('PNG publication failed '+imageResponse.status);
   const png=await imageResponse.json();
   const receipt={passed:true,stage,engine:pc.version,modelSha256:input.modelSha256,pointCount:data.numSplats,
    shDegree:data.shBands,checkedScalarValues:checked,propertiesPerCheckedRow:input.ply.properties.length,
    sourceRowsReordered:false,width:canvas.width,height:canvas.height,cameraPosition:camera.getPosition().toArray(),
    cameraRotation:camera.getRotation().toArray(),originalCamera:cameraData,png,
    graphicsDevice:app.graphicsDevice.deviceType,userAgent:navigator.userAgent,
    scope:'Independent PlayCanvas import, sampled exact decoded attributes and visible native-resolution rendering; not Octane compatibility or renderer pixel parity'};
   const response=await fetch('receipt?name='+encodeURIComponent(name),{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(receipt)});
   if(!response.ok) throw new Error('Receipt publication failed '+response.status);
   status.textContent='Ready · '+data.numSplats.toLocaleString()+' splats · SH '+data.shBands+' · '+checked+' decoded values verified · saved '+stage;
  } finally {pending.delete(stage);}
 }
 function orbit(degrees) {
  const offset=camera.getPosition().clone().sub(target), rotation=new pc.Quat().setFromAxisAngle(up,degrees);
  const moved=rotation.transformVector(offset).add(target); camera.setPosition(moved); camera.lookAt(target,up);
 }
 document.getElementById('reset').onclick=()=>{resetCamera();capture('reset').catch(reportError);};
 document.getElementById('left').onclick=()=>{orbit(10);capture('left').catch(reportError);};
 document.getElementById('right').onclick=()=>{orbit(-10);capture('right').catch(reportError);};
 document.getElementById('capture').onclick=()=>capture('manual').catch(reportError);
 for(const button of document.querySelectorAll('button')) button.disabled=false;
 await capture('initial');
})().catch(reportError);
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--ply', type=Path, required=True)
    parser.add_argument('--receipt', type=Path, required=True)
    parser.add_argument('--engine-root', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--port', type=int, default=0)
    args = parser.parse_args()
    args.ply, args.receipt, args.engine_root = [p.resolve(strict=True) for p in (args.ply,args.receipt,args.engine_root)]
    if not args.output.is_absolute() or args.output.exists() or not 0 <= args.port <= 65535 or args.receipt.stat().st_size > 4*1024*1024:
        raise ValueError('Use a new absolute output directory and bounded receipt/port')
    export = json.loads(args.receipt.read_text())
    model_sha = digest(args.ply)
    if export.get('exportedArtifactSha256') != model_sha:
        raise ValueError('Export identity differs from its receipt')
    ply = inspect_ply(args.ply)
    if ply['count'] != export['visibleCount']:
        raise ValueError('Exported count differs from receipt')
    camera = export['currentCamera']
    for key in ('position','right','down','forward'):
        if len(camera[key]) != 3 or not all(isinstance(x,(int,float)) and math.isfinite(x) for x in camera[key]):
            raise ValueError('Invalid camera vectors')
    if not all(isinstance(camera[k],(int,float)) and math.isfinite(camera[k]) and camera[k] > 0 for k in ('width','height','fx','fy')) or \
            camera['width'] > 4096 or camera['height'] > 4096 or camera['fx'] != camera['fy'] or \
            camera['cx'] != camera['width']/2 or camera['cy'] != camera['height']/2:
        raise ValueError('This interoperability fixture requires a centered square-pixel pinhole camera')
    package = json.loads((args.engine_root/'package.json').read_text())
    if package.get('name') != 'playcanvas' or package.get('version') != '2.23.1':
        raise ValueError('Use the pinned independent engine version 2.23.1')
    args.output.mkdir()
    for folder in ('assets','engine','evidence'):
        (args.output/folder).mkdir()
    shutil.copyfile(args.ply,args.output/'assets/scene.ply')
    shutil.copyfile(args.engine_root/'build/playcanvas.min.js',args.output/'engine/playcanvas.min.js')
    shutil.copyfile(args.engine_root/'LICENSE',args.output/'engine/LICENSE')
    (args.output/'index.html').write_text(HTML)
    (args.output/'viewer.js').write_text(JS)
    inputs={'modelSha256':model_sha,'ply':ply,'camera':camera,'sourcePly':str(args.ply),'exportReceiptSha256':digest(args.receipt),
            'engineVersion':'2.23.1','engineBundleSha256':digest(args.output/'engine/playcanvas.min.js')}
    (args.output/'input.json').write_text(json.dumps(inputs,indent=2)+'\n')
    output=args.output

    class Handler(SimpleHTTPRequestHandler):
        def __init__(self,*a,**kw):
            super().__init__(*a,directory=str(output),**kw)

        def do_POST(self):
            parsed=urlparse(self.path)
            name=parse_qs(parsed.query).get('name',[''])[0]
            if self.headers.get('Origin') != f'http://127.0.0.1:{self.server.server_port}':
                self.send_error(403);return
            length=int(self.headers.get('Content-Length','0'))
            if not re.fullmatch(r'[a-z]+-[0-9]+-[0-9]+',name) or parsed.path not in ('/capture','/receipt') or not 0 < length <= 16*1024*1024:
                self.send_error(400);return
            body=self.rfile.read(length)
            if len(body) != length:
                self.send_error(400);return
            suffix='.png' if parsed.path=='/capture' else '.json'
            target=output/'evidence'/(name+suffix)
            if suffix=='.png' and body[:8] != b'\x89PNG\r\n\x1a\n':
                self.send_error(400);return
            if suffix=='.json':
                data=json.loads(body)
                if data.get('modelSha256') != model_sha or data.get('pointCount') != ply['count']:
                    self.send_error(400);return
            try:
                with target.open('xb') as file:
                    file.write(body)
            except FileExistsError:
                self.send_error(409);return
            if suffix=='.json':
                (output/'latest.json').write_bytes(body)
            response=json.dumps({'file':target.name,'sha256':digest(target)}).encode()
            self.send_response(200);self.send_header('Content-Type','application/json');self.send_header('Content-Length',str(len(response)));self.end_headers();self.wfile.write(response)

    server=ThreadingHTTPServer(('127.0.0.1',args.port),Handler)
    url=f'http://127.0.0.1:{server.server_port}/'
    (output/'server.json').write_text(json.dumps({'url':url,'modelSha256':model_sha,'engineBundleSha256':inputs['engineBundleSha256']},indent=2)+'\n')
    print(url,flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        server.server_close()


if __name__ == '__main__':
    main()
