#!/usr/bin/env python3
"""Pack the stand STLs into one slicer-ready 3MF, each part laid flat in print orientation.

    python3 case/make_test_print_3mf.py

Reads case/stl/*.stl (exported by SportsTickerStand.py in their assembled positions) and
writes test-print/SportsTickerFinal.3mf. Keep the pod constants in sync with
SportsTickerStand.py (they undo the pod's tilt).
"""
import math
import os
import struct
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
STL_DIR = os.path.join(ROOT, "case", "stl")
OUT = os.path.join(ROOT, "test-print", "SportsTickerFinal.3mf")

# same values as SportsTickerStand.py
TILT, POD_LIFT, WALL = 12.0, 38.0, 2.4
POD_IN_Z, POD_FRONT_IN, POD_BACK_IN = 26.5, -5.2, 10.0
PLATE = 256.0   # Bambu Lab X1C plate (group is 178 x 124, so it fits 180 mm beds too)
GAP = 8.0


def pod_world_to_local():
    th = -math.radians(TILT)
    s, c = math.sin(th), math.cos(th)
    y_back = POD_BACK_IN + WALL
    lowest = min(y * s + (-(POD_IN_Z + WALL)) * c for y in (POD_FRONT_IN - WALL, y_back))
    tz = POD_LIFT - lowest
    ty = -(y_back + tz * s) / c

    def f(p):
        x, y, z = p[0], p[1] - ty, p[2] - tz
        return (x, y * c + z * s, -y * s + z * c)  # inverse rotation
    return f


def read_stl(path):
    d = open(path, "rb").read()
    n = struct.unpack("<I", d[80:84])[0]
    tris = []
    for i in range(n):
        v = struct.unpack("<12f", d[84 + i * 50:84 + i * 50 + 48])
        tris.append((v[3:6], v[6:9], v[9:12]))
    return tris


def transform(tris, f):
    return [tuple(f(p) for p in t) for t in tris]


def bbox(tris):
    pts = [p for t in tris for p in t]
    return [min(p[i] for p in pts) for i in range(3)], [max(p[i] for p in pts) for i in range(3)]


def place(tris, x0, y0):
    mn, _ = bbox(tris)
    return transform(tris, lambda p: (p[0] - mn[0] + x0, p[1] - mn[1] + y0, p[2] - mn[2]))


def mesh_xml(tris, oid, name):
    idx, verts, faces = {}, [], []
    for t in tris:
        ids = []
        for p in t:
            k = (round(p[0], 4), round(p[1], 4), round(p[2], 4))
            if k not in idx:
                idx[k] = len(verts)
                verts.append(k)
            ids.append(idx[k])
        if len(set(ids)) == 3:
            faces.append(ids)
    vx = "".join('<vertex x="%.4f" y="%.4f" z="%.4f"/>' % v for v in verts)
    fx = "".join('<triangle v1="%d" v2="%d" v3="%d"/>' % tuple(f) for f in faces)
    return '<object id="%d" name="%s" type="model"><mesh><vertices>%s</vertices><triangles>%s</triangles></mesh></object>' % (
        oid, name, vx, fx), len(verts), len(faces)


def base_only():
    """Reprint of just the base + lid, side by side on the plate."""
    parts = [("Base (upright)", read_stl(os.path.join(STL_DIR, "SportsTicker_Base.stl"))),
             ("Penny Lid", read_stl(os.path.join(STL_DIR, "SportsTicker_PennyLid.stl")))]
    sizes = [[bbox(t)[1][i] - bbox(t)[0][i] for i in range(3)] for _, t in parts]
    w = sizes[0][0] + GAP + sizes[1][0]
    h = max(sizes[0][1], sizes[1][1])
    ox, oy = (PLATE - w) / 2, (PLATE - h) / 2
    spots = [(ox, oy), (ox + sizes[0][0] + GAP, oy)]
    write_3mf([(n, place(t, *s)) for (n, t), s in zip(parts, spots)],
              os.path.join(ROOT, "test-print", "SportsTicker_BaseAndLid.3mf"), "Sports Ticker stand - base + lid")


def write_3mf(placed, out, title):
    objs, items = [], []
    for i, (name, t) in enumerate(placed):
        xml, nv, nf = mesh_xml(t, i + 1, name)
        objs.append(xml)
        items.append('<item objectid="%d"/>' % (i + 1))
    model = ('<?xml version="1.0" encoding="UTF-8"?>'
             '<model unit="millimeter" xml:lang="en-US" xmlns="http://schemas.microsoft.com/3dmanufacturing/core/2015/02">'
             '<metadata name="Title">%s</metadata><resources>%s</resources><build>%s</build></model>') % (
        title, "".join(objs), "".join(items))
    ctypes = ('<?xml version="1.0" encoding="UTF-8"?>'
              '<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">'
              '<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>'
              '<Default Extension="model" ContentType="application/vnd.ms-package.3dmanufacturing-3dmodel+xml"/></Types>')
    rels = ('<?xml version="1.0" encoding="UTF-8"?>'
            '<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">'
            '<Relationship Target="/3D/3dmodel.model" Id="rel0" '
            'Type="http://schemas.microsoft.com/3dmanufacturing/2013/01/3dmodel"/></Relationships>')
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
        z.writestr("[Content_Types].xml", ctypes)
        z.writestr("_rels/.rels", rels)
        z.writestr("3D/3dmodel.model", model)
    print("->", out)


def main():
    to_local = pod_world_to_local()
    # pod parts: back to pod-local, then rotate so pod -Y (screen side) points down: (x,y,z)->(x,-z,y)
    lay_pod = lambda p: (lambda q: (q[0], -q[2], q[1]))(to_local(p))
    parts = [
        ("Pod Front (face down)", transform(read_stl(os.path.join(STL_DIR, "SportsTicker_PodFront.stl")), lay_pod)),
        ("Pod Back (rails up)", transform(read_stl(os.path.join(STL_DIR, "SportsTicker_PodBack.stl")), lay_pod)),
        ("Base (upright)", read_stl(os.path.join(STL_DIR, "SportsTicker_Base.stl"))),
        ("Penny Lid", read_stl(os.path.join(STL_DIR, "SportsTicker_PennyLid.stl"))),
    ]
    sizes = []
    for name, t in parts:
        mn, mx = bbox(t)
        sizes.append([mx[i] - mn[i] for i in range(3)])
    # layout: pods stacked on the left, base + lid stacked on the right
    w_left = max(sizes[0][0], sizes[1][0])
    w = w_left + GAP + max(sizes[2][0], sizes[3][0])
    h = max(sizes[0][1] + GAP + sizes[1][1], sizes[2][1] + GAP + sizes[3][1])
    ox, oy = (PLATE - w) / 2, (PLATE - h) / 2
    spots = [(ox, oy), (ox, oy + sizes[0][1] + GAP), (ox + w_left + GAP, oy), (ox + w_left + GAP, oy + sizes[2][1] + GAP)]
    objs, items = [], []
    for i, ((name, t), (x, y)) in enumerate(zip(parts, spots)):
        placed = place(t, x, y)
        xml, nv, nf = mesh_xml(placed, i + 1, name)
        objs.append(xml)
        items.append('<item objectid="%d"/>' % (i + 1))
        print("%-22s %6.1f x %6.1f x %5.1f mm  at (%.0f, %.0f)  %d tris" % (name, *sizes[i], x, y, nf))
    model = ('<?xml version="1.0" encoding="UTF-8"?>'
             '<model unit="millimeter" xml:lang="en-US" xmlns="http://schemas.microsoft.com/3dmanufacturing/core/2015/02">'
             '<metadata name="Title">Sports Ticker stand - final</metadata>'
             '<resources>%s</resources><build>%s</build></model>') % ("".join(objs), "".join(items))
    ctypes = ('<?xml version="1.0" encoding="UTF-8"?>'
              '<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">'
              '<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>'
              '<Default Extension="model" ContentType="application/vnd.ms-package.3dmanufacturing-3dmodel+xml"/></Types>')
    rels = ('<?xml version="1.0" encoding="UTF-8"?>'
            '<Relationships xmlns="http://schemas.openxmlformats.org/package/2006/relationships">'
            '<Relationship Target="/3D/3dmodel.model" Id="rel0" '
            'Type="http://schemas.microsoft.com/3dmanufacturing/2013/01/3dmodel"/></Relationships>')
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with zipfile.ZipFile(OUT, "w", zipfile.ZIP_DEFLATED) as z:
        z.writestr("[Content_Types].xml", ctypes)
        z.writestr("_rels/.rels", rels)
        z.writestr("3D/3dmodel.model", model)
    print("plate group %.0f x %.0f mm on a %.0f mm bed -> %s" % (w, h, PLATE, OUT))


if __name__ == "__main__":
    main()
    base_only()
