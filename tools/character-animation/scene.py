"""Author native Looksy rig scenes. No raster, browser or video dependencies."""
from __future__ import annotations

import math
import re


def keys(*pairs, easing="smooth"):
    return [[float(t), float(v), easing] for t, v in pairs]


def wave(duration, center=0, amplitude=1, period=4, phase=0, step=0.25):
    times = [min(i * step, duration) for i in range(math.ceil(duration / step) + 1)]
    return keys(*[(t, center + amplitude * math.sin(t * math.tau / period + phase)) for t in times])


def path_commands(source):
    tokens = re.findall(r"[MLCQZ]|[-+]?(?:\d*\.\d+|\d+)(?:[eE][-+]?\d+)?", source)
    counts = dict(M=2, L=2, C=6, Q=4, Z=0)
    output, i = [], 0
    while i < len(tokens):
        verb = tokens[i]
        count = counts[verb]
        output.append([verb, *[float(x) for x in tokens[i+1:i+1+count]]])
        i += count+1
    return output


class Scene:
    def __init__(self, duration, width=1600, height=900):
        self.duration = duration
        self.data = dict(schema="looksy-rig-scene-v1", width=width, height=height,
                         paperOpacity=0.09, nodes=[])
        self.by_id = {}
        self.index = 0

    def node(self, kind="group", parent="camera", name=None, **properties):
        self.index += 1
        name = name or f"n{self.index}"
        if name in self.by_id:
            raise ValueError(f"Duplicate node: {name}")
        data = dict(id=name, kind=kind, **properties)
        if parent:
            data["parent"] = parent
        self.data["nodes"].append(data)
        self.by_id[name] = data
        return name

    def group(self, **kw):
        return self.node(**kw)

    def rect(self, left, top, w, h, fill, radius=0, **kw):
        return self.node("rect", rect=[left, top, w, h], fill=fill, radius=radius, **kw)

    def ellipse(self, left, top, w, h, fill, **kw):
        return self.node("ellipse", rect=[left, top, w, h], fill=fill, **kw)

    def path(self, source, fill="#00000000", stroke="#00000000", sw=1, **kw):
        sw = kw.pop("strokeWidth", sw)
        return self.node("path", commands=path_commands(source), fill=fill,
                         stroke=stroke, strokeWidth=sw, **kw)

    def line(self, x1, y1, x2, y2, stroke, sw=1, **kw):
        return self.path(f"M{x1} {y1} L{x2} {y2}", stroke=stroke, sw=sw, **kw)

    def text(self, text, x, y, w, h, size=24, fill="#efe3cd", **kw):
        return self.node("text", rect=[x, y, w, h], text=text, fontSize=size, fill=fill, **kw)

    def set(self, node, **kw):
        self.by_id[node].update(kw)

    def camera(self, start=(800, 450, 1), end=None):
        end = end or start
        x, y, scale = start
        ex, ey, es = end
        return self.group(parent=None, name="camera",
                          x=keys((0, 800-x*scale), (self.duration, 800-ex*es)),
                          y=keys((0, 450-y*scale), (self.duration, 450-ey*es)),
                          scaleX=keys((0, scale), (self.duration, es)),
                          scaleY=keys((0, scale), (self.duration, es)))

    def finish(self, bars=True, vignette=True):
        if vignette:
            self.rect(0, 0, 1600, 900, "#00000000", parent=None,
                      gradient=dict(type="radial", coordinates=[800, 430, 930],
                                    stops=[[0, "#000b1820"], [0.5, "#000b1820"], [1, "#990b1820"]]))
        if bars:
            self.rect(0, 0, 1600, 68, "#0d191c", parent=None)
            self.rect(0, 832, 1600, 68, "#0d191c", parent=None)
        return self.data


def gradient(top, bottom, y1=0, y2=900):
    return dict(type="linear", coordinates=[0, y1, 0, y2], stops=[[0, top], [1, bottom]])


def two_bone(dx, dy, first, second, bend=1):
    distance = min(max(math.hypot(dx, dy), abs(first-second)+0.01), first+second-0.01)
    angle = math.acos(max(-1, min(1, (first*first+distance*distance-second*second)/(2*first*distance))))
    upper = math.atan2(dy, dx) - bend*angle
    elbow = bend * (math.pi-math.acos(max(-1, min(1, (first*first+second*second-distance*distance)/(2*first*second)))))
    return math.degrees(upper)-90, math.degrees(elbow)


def aim_arm(scene, rig, side, poses, bend=1):
    arm = rig[side]
    a, b, hand = [], [], []
    sx, sy = arm["shoulder"]
    for t, x, y in poses:
        upper, elbow = two_bone(x-sx, y-sy, arm["lengths"][0], arm["lengths"][1], bend)
        a.append((t, upper))
        b.append((t, elbow))
        hand.append((t, -upper-elbow))
    scene.set(arm["upper"], rotation=keys(*a))
    scene.set(arm["lower"], rotation=keys(*b))
    scene.set(arm["hand"], rotation=keys(*hand))


def blinks(scene, eyes, times):
    values = [(0, 1)]
    for t in times:
        if t+0.16 >= scene.duration:
            continue
        values += [(t, 1), (t+0.065, 0.08), (t+0.16, 1)]
    values += [(scene.duration, 1)]
    scene.set(eyes, scaleY=keys(*values))
