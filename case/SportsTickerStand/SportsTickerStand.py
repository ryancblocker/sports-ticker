"""Sports Ticker desk stand: screen pod + weighted base (Fusion 360 script).

Design (from the sketch):
  - Screen pod: rounded two-part shell around the CYD (ESP32-2432S028, 2-USB revision).
    Board is located by pins through its mounting holes (front half) and clamped by
    posts (back half); halves join with 4x M3 into heat-set inserts. A 7 mm bay on the
    USB side fits a low-profile 90 deg USB-C plug (5.1 mm from the port).
  - Mount: two dovetail rails on the pod back slide down into slots in the base's front
    face (tilted TILT deg back). A nub clicks into a dimple, then 2x M3 bolts from the back
    of the base go into heat-set inserts in the rails.
  - Base: tapered, rounded body; bottom edge rounded so it wraps under; four penny bores
    (10 stacked flat in each) behind a recessed lid that hooks in with two tabs and clicks shut
    with a snap arm; cable tunnel from behind the pod down to the back.

Run it from Fusion (Utilities > Scripts and Add-Ins) or via the Fusion MCP. It creates a
NEW design document each time. All sizes are in mm below; edit and re-run.

Coordinates: world Z up, X right (viewer's right), Y away from the viewer.
Pod-local frame (before tilting): board centre at origin, screen faces -Y, PCB back face at
Y=0, board long axis = X, short axis = Z.
"""
import math
import os

import adsk.core
import adsk.fusion

# ----------------------------------------------------------------------------- parameters
TILT = 12.0            # screen tilt back from vertical, deg
POD_LIFT = 38.0        # lowest point of the pod above the desk
WALL = 2.4             # pod shell wall
POD_IN_X = 50.0        # pod cavity half-width (board half 43 + plug bay)
POD_IN_Z = 26.5        # pod cavity half-height (board half 25 + room for the inner round-over)
POD_FRONT_IN = -5.2    # inner face of the bezel (display glass is at -5.1)
POD_BACK_IN = 10.0     # inner face of the pod back (parts reach +4.7; rest is cable room)
POD_CORNER_R = 9.0     # front-view corner radius
POD_EDGE_R = 4.5       # front/back perimeter round-over
WINDOW = (-31.0, 26.0, -21.5, 21.5)  # visible screen opening x0, x1, z0, z1 (board coords)
WINDOW_R = 1.5
WINDOW_CHAMFER = 1.0
HOLE_X, HOLE_Z = 39.0, 21.0          # board mounting holes (+/-)
SCREW_X, SCREW_Z = 46.7, 20.0        # pod shell screws (+/-), in the side bays
INSERT_D, INSERT_DEPTH = 3.6, 4.5    # pilot hole for M3 heat-set inserts 4.0 OD x 4 long
M3_CLEAR, M3_HEAD = 3.4, 6.0
BACK_BOSS_R = 4.1      # pod-back screw boss radius (wraps the head counterbore)

RAIL_X = 17.0          # rails at +/- this x
RAIL_Z = (-26.0, -6.0) # rail extent along the pod (pod-local z)
RAIL_ROOT, RAIL_TIP, RAIL_H = 8.0, 11.0, 3.0  # dovetail widths and height
RAIL_CLEAR = 0.2
BOLT_Z = -14.0         # base-to-pod bolts (pod-local z, at the rails)
BOLT_MEAT = 4.0        # base material between counterbore and slot floor
NUB_Z, NUB_R, NUB_H = -23.0, 1.0, 0.6

CABLE_HOLE = (20.0, 14.0, -15.0)     # pod back hole width, height, centre z
TUNNEL_W, TUNNEL_H, TUNNEL_DROP = 16.0, 10.0, 30.0  # cable tunnel, deg below horizontal

BASE_BOT = (66.0, 55.0, 10.0)        # width, depth, corner radius at the desk
BASE_TOP = (52.0, 34.0, 8.0)         # width, depth, corner radius at the top
BASE_H = 62.0
BASE_TOP_R, BASE_BOT_R = 6.0, 4.0    # round-overs (bottom one makes the edge wrap under)
LID_HALF, LID_R = (26.0, 21.5), 6.5   # lid recess half sizes (x, y) and corner radius
LID_T, LID_CLEAR = 3.0, 0.2           # lid thickness (= recess depth), edge clearance
PENNY_BORE_D = 19.8                   # US penny is 19.05 mm; bores print a touch small
PENNY_STACK = 10                      # pennies per bore (1.52 mm each)
PENNY_PITCH = 21.4                    # bore centre spacing (1.6 mm webs), 2 x 2 grid
# lid retention: two hook tabs at -x slide into pockets, a snap arm at +x clicks over a ledge
TAB_Y, TAB_W, TAB_OUT = 11.0, 8.0, 2.9      # tab centres at +/-TAB_Y, width, reach past lid edge
TAB_Z = (1.6, 2.8)                          # tab thickness band (pocket is 0.2 bigger each way)
ARM_X, ARM_T, ARM_W, ARM_TOP = 23.2, 1.2, 8.0, 10.5   # snap arm inner face x, thickness, width, top z
BARB_OUT = 0.7                              # barb reach past the arm (0.4 snap past the slot wall)
SLOT_OUT_X = 24.7                           # slot outer wall x (ledge face)
LEDGE_Z = 8.25                              # top of the ledge the barb catches on

OUT_DIR = os.path.expanduser("~/projects/sports-ticker/case/stl")

# ----------------------------------------------------------------------------- helpers
app = adsk.core.Application.get()
cm = lambda v: v / 10.0
VI = lambda v: adsk.core.ValueInput.createByReal(v)
NEW = adsk.fusion.FeatureOperations.NewBodyFeatureOperation
JOIN = adsk.fusion.FeatureOperations.JoinFeatureOperation
CUT = adsk.fusion.FeatureOperations.CutFeatureOperation


def P(x, y, z):
    return adsk.core.Point3D.create(cm(x), cm(y), cm(z))


def V(x, y, z):
    return adsk.core.Vector3D.create(x, y, z)


def offset_plane(comp, base, dist_mm):
    pi = comp.constructionPlanes.createInput()
    pi.setByOffset(base, VI(cm(dist_mm)))
    return comp.constructionPlanes.add(pi)


def sk_pt(sk, x, y, z):
    return sk.modelToSketchSpace(P(x, y, z))


def poly(sk, pts):
    lines = sk.sketchCurves.sketchLines
    sp = [sk_pt(sk, *p) for p in pts]
    for i in range(len(sp)):
        lines.addByTwoPoints(sp[i], sp[(i + 1) % len(sp)])


def rrect(sk, c, u, v, a, b, r):
    """Rounded rectangle in model space: centre c, in-plane unit vectors u, v, half sizes a, b."""
    r = min(r, a - 0.01, b - 0.01)

    def at(du, dv):
        return sk_pt(sk, c[0] + u[0] * du + v[0] * dv, c[1] + u[1] * du + v[1] * dv, c[2] + u[2] * du + v[2] * dv)

    lines, arcs = sk.sketchCurves.sketchLines, sk.sketchCurves.sketchArcs
    k = r * (1 - 1 / math.sqrt(2))
    # corners: (+,+) (-,+) (-,-) (+,-); walk counter-clockwise
    lines.addByTwoPoints(at(a, -(b - r)), at(a, b - r))
    arcs.addByThreePoints(at(a, b - r), at(a - k, b - k), at(a - r, b))
    lines.addByTwoPoints(at(a - r, b), at(-(a - r), b))
    arcs.addByThreePoints(at(-(a - r), b), at(-(a - k), b - k), at(-a, b - r))
    lines.addByTwoPoints(at(-a, b - r), at(-a, -(b - r)))
    arcs.addByThreePoints(at(-a, -(b - r)), at(-(a - k), -(b - k)), at(-(a - r), -b))
    lines.addByTwoPoints(at(-(a - r), -b), at(a - r, -b))
    arcs.addByThreePoints(at(a - r, -b), at(a - k, -(b - k)), at(a, -(b - r)))


def circle(sk, c, r):
    sk.sketchCurves.sketchCircles.addByCenterRadius(sk_pt(sk, *c), cm(r))


def smallest_profile(sk):
    best = None
    for p in sk.profiles:
        a = p.areaProperties().area
        if best is None or a < best[0]:
            best = (a, p)
    return best[1]


def all_profiles(sk):
    oc = adsk.core.ObjectCollection.create()
    for p in sk.profiles:
        oc.add(p)
    return oc


def extrude(comp, prof, dist_mm, op, bodies=None, start_mm=None):
    """Extrude along the sketch plane normal; negative distance goes the other way."""
    ei = comp.features.extrudeFeatures.createInput(prof, op)
    d = adsk.fusion.DistanceExtentDefinition.create(VI(cm(abs(dist_mm))))
    dirn = adsk.fusion.ExtentDirections.PositiveExtentDirection if dist_mm > 0 else adsk.fusion.ExtentDirections.NegativeExtentDirection
    ei.setOneSideExtent(d, dirn)
    if start_mm is not None:
        ei.startExtent = adsk.fusion.OffsetStartDefinition.create(VI(cm(start_mm)))
    if bodies is not None:
        ei.participantBodies = bodies
    return comp.features.extrudeFeatures.add(ei)


def cyl_y(comp, x, z, r, y0, y1, op, bodies=None):
    """Cylinder along Y from y0 to y1 (component space == model space while building)."""
    pl = offset_plane(comp, comp.xZConstructionPlane, y0)  # XZ normal is +Y
    sk = comp.sketches.add(pl)
    circle(sk, (x, y0, z), r)
    return extrude(comp, smallest_profile(sk), y1 - y0, op, bodies)


def faces_where(body, test):
    return [f for f in body.faces if test(f)]


def planar_face(body, normal, coord_axis, coord_mm, tol=0.05):
    for f in body.faces:
        g = f.geometry
        if isinstance(g, adsk.core.Plane):
            n = g.normal  # plane geometry normal may point either way; match the axis only
            if abs(abs(n.x * normal[0] + n.y * normal[1] + n.z * normal[2]) - 1) < 1e-4:
                c = f.pointOnFace
                if abs(getattr(c, coord_axis) * 10 - coord_mm) < tol:
                    return f
    raise RuntimeError("face not found: n=%s %s=%s" % (normal, coord_axis, coord_mm))


def fillet(comp, edges, r_mm):
    oc = adsk.core.ObjectCollection.create()
    for e in edges:
        oc.add(e)
    fi = comp.features.filletFeatures.createInput()
    fi.addConstantRadiusEdgeSet(oc, VI(cm(r_mm)), True)
    return comp.features.filletFeatures.add(fi)


def combine(comp, target, tools, op, keep=False):
    oc = adsk.core.ObjectCollection.create()
    for t in tools:
        oc.add(t)
    ci = comp.features.combineFeatures.createInput(target, oc)
    ci.operation = op
    ci.isKeepToolBodies = keep
    return comp.features.combineFeatures.add(ci)


def pod_matrix():
    """Pod-local -> world: tilt back by TILT, lift so the lowest point is at POD_LIFT and the
    pod back plane passes through the base's front edge at the desk (world y=0, z=0)."""
    th = -math.radians(TILT)  # rotation about +X; negative leans the top toward +Y (back)
    s, c = math.sin(th), math.cos(th)
    y_back = POD_BACK_IN + WALL
    lowest = min(y * s + z * c for y in (POD_FRONT_IN - WALL, y_back) for z in (-(POD_IN_Z + WALL),))
    tz = POD_LIFT - lowest
    ty = -(y_back + tz * s) / c
    m = adsk.core.Matrix3D.create()
    m.setToRotation(th, V(1, 0, 0), P(0, 0, 0))
    m.translation = V(0, cm(ty), cm(tz))
    return m, (s, c, ty, tz)


def to_world(p, sc):
    s, c, ty, tz = sc
    x, y, z = p
    return (x, y * c - z * s + ty, y * s + z * c + tz)


# ----------------------------------------------------------------------------- parts
def build_pod(comp):
    y_front, y_back = POD_FRONT_IN - WALL, POD_BACK_IN + WALL
    ox, oz = POD_IN_X + WALL, POD_IN_Z + WALL
    # outer block
    pl = offset_plane(comp, comp.xZConstructionPlane, y_front)
    sk = comp.sketches.add(pl)
    rrect(sk, (0, y_front, 0), (1, 0, 0), (0, 0, 1), ox, oz, POD_CORNER_R)
    body = extrude(comp, sk.profiles.item(0), y_back - y_front, NEW).bodies.item(0)
    # round over front and back perimeters
    ff = planar_face(body, (0, -1, 0), "y", y_front)
    bf = planar_face(body, (0, 1, 0), "y", y_back)
    fillet(comp, list(ff.edges) + list(bf.edges), POD_EDGE_R)
    # hollow it
    oc = adsk.core.ObjectCollection.create()
    oc.add(body)
    si = comp.features.shellFeatures.createInput(oc, False)
    si.insideThickness = VI(cm(WALL))
    comp.features.shellFeatures.add(si)
    # split into front (bezel) and back at the PCB back plane
    split_pl = offset_plane(comp, comp.xZConstructionPlane, 0.0)
    spi = comp.features.splitBodyFeatures.createInput(body, split_pl, True)
    comp.features.splitBodyFeatures.add(spi)
    bodies = sorted([b for b in comp.bRepBodies], key=lambda b: b.boundingBox.minPoint.y)
    front, back = bodies[0], bodies[1]
    front.name, back.name = "Pod Front", "Pod Back"
    fb = adsk.core.ObjectCollection.create()
    fb.add(front)

    # screen window + chamfer
    sk = comp.sketches.add(offset_plane(comp, comp.xZConstructionPlane, y_front - 1))
    x0, x1, z0, z1 = WINDOW
    rrect(sk, ((x0 + x1) / 2, y_front - 1, (z0 + z1) / 2), (1, 0, 0), (0, 0, 1), (x1 - x0) / 2, (z1 - z0) / 2, WINDOW_R)
    extrude(comp, sk.profiles.item(0), WALL + 2, CUT, [front])
    wf = planar_face(front, (0, -1, 0), "y", y_front)
    win_edges = [e for e in wf.edges if abs(e.boundingBox.maxPoint.x * 10) < 40]
    ci = comp.features.chamferFeatures.createInput2()
    oc = adsk.core.ObjectCollection.create()
    for e in win_edges:
        oc.add(e)
    ci.chamferEdgeSets.addEqualDistanceChamferEdgeSet(oc, VI(cm(WINDOW_CHAMFER)), True)
    comp.features.chamferFeatures.add(ci)

    # board locating bosses + pins (front half)
    for sx in (-1, 1):
        for sz in (-1, 1):
            cyl_y(comp, sx * HOLE_X, sz * HOLE_Z, 3.0, POD_FRONT_IN - 0.5, -1.55, JOIN, [front])
            cyl_y(comp, sx * HOLE_X, sz * HOLE_Z, 1.4, -1.6, -0.5, JOIN, [front])
            # shell screw bosses with insert pilots
            cyl_y(comp, sx * SCREW_X, sz * SCREW_Z, 3.25, POD_FRONT_IN - 0.5, 0.0, JOIN, [front])
            cyl_y(comp, sx * SCREW_X, sz * SCREW_Z, INSERT_D / 2, -INSERT_DEPTH, 0.0, CUT, [front])

    # back half: clamp posts, screw bosses, counterbored holes
    for sx in (-1, 1):
        for sz in (-1, 1):
            cyl_y(comp, sx * HOLE_X, sz * HOLE_Z, 2.5, 0.05, POD_BACK_IN + 0.8, JOIN, [back])
            # 8.2 mm boss: leaves 1.1 mm walls around the 6 mm head counterbore (3.25 left 0.25,
            # too thin to slice) and still ~0.6 mm of skin at the rounded back corners
            cyl_y(comp, sx * SCREW_X, sz * SCREW_Z, BACK_BOSS_R, 0.0, POD_BACK_IN + 0.8, JOIN, [back])
            cyl_y(comp, sx * SCREW_X, sz * SCREW_Z, M3_CLEAR / 2, -0.1, y_back + 1, CUT, [back])
            cyl_y(comp, sx * SCREW_X, sz * SCREW_Z, M3_HEAD / 2, 4.0, y_back + 1, CUT, [back])

    # dovetail rails on the back, with insert pilots and the lock dimple
    for sx in (-1, 1):
        cx = sx * RAIL_X
        sk = comp.sketches.add(offset_plane(comp, comp.xYConstructionPlane, RAIL_Z[0]))
        r, t = RAIL_ROOT / 2, RAIL_TIP / 2
        poly(sk, [(cx - r, y_back - 1.0, RAIL_Z[0]), (cx + r, y_back - 1.0, RAIL_Z[0]), (cx + r, y_back, RAIL_Z[0]),
                  (cx + t, y_back + RAIL_H, RAIL_Z[0]), (cx - t, y_back + RAIL_H, RAIL_Z[0]), (cx - r, y_back, RAIL_Z[0])])
        extrude(comp, sk.profiles.item(0), RAIL_Z[1] - RAIL_Z[0], JOIN, [back])
        cyl_y(comp, cx, BOLT_Z, INSERT_D / 2, y_back + RAIL_H - INSERT_DEPTH, y_back + RAIL_H + 1, CUT, [back])
        cyl_y(comp, cx, NUB_Z, NUB_R + 0.2, y_back + RAIL_H - 0.8, y_back + RAIL_H + 1, CUT, [back])

    # cable hole through the back, low and centred (lines up with the base tunnel)
    w, h, cz = CABLE_HOLE
    sk = comp.sketches.add(offset_plane(comp, comp.xZConstructionPlane, POD_BACK_IN - 1))
    rrect(sk, (0, POD_BACK_IN - 1, cz), (1, 0, 0), (0, 0, 1), w / 2, h / 2, h / 2 - 0.01)
    extrude(comp, sk.profiles.item(0), WALL + 3, CUT, [back])
    return front, back


def build_board_proxy(comp):
    """Simple keep-out model of the CYD from the measured Havenview CAD (board coords:
    x long, y short, z out of the screen). Pod-local: X=x, Y=-z, Z=y."""
    boxes = [  # x0, x1, y0, y1, z0, z1
        (-43, 43, -25, 25, 0, 1.5),            # PCB
        (-34.5, 34.5, -25, 25, 1.5, 5.0),      # display module
        (-32, 27, -22.5, 22.5, 5.0, 5.1),      # glass
        (-41, -38, -17, -11, 1.5, 2.0),        # front sensor
        (-36, -17.6, -9, 9, -3.0, 0),          # ESP32 module
        (-43, -36, -9, 9, -1.0, 0),            # antenna
        (38.1, 43.6, -13.2, -5.4, -2.5, 0),    # micro-USB
        (37.5, 42, 4, 14, -3.5, 0),            # second port / connector
        (-16.8, -6.8, -24.9, -20.6, -3.5, 0),  # JST
        (-32.8, -22.8, -24.9, -20.6, -3.5, 0), # JST
        (-2.6, 12.4, -24.9, -9.9, -1.8, 0),    # SD slot
        (11.4, 19, 20.6, 23.8, -4.6, 0),       # connector
        (-35.4, -27.9, 17.2, 23.2, -4.7, 0),   # buttons
        (-15, -10, -16.5, -11.5, -1.5, 0),     # misc
        (43.6, 48.7, 3, 15, -3.5, 1.5),        # ASSUMED: 90 deg USB-C plug, 5.1 mm out
    ]
    pcb = None
    for i, (x0, x1, y0, y1, z0, z1) in enumerate(boxes):
        # pod-local: X=x, Y=-z, Z=y ; sketch on an XZ-parallel plane at Y=-z1, extrude to Y=-z0
        sk = comp.sketches.add(offset_plane(comp, comp.xZConstructionPlane, -z1))
        poly(sk, [(x0, -z1, y0), (x1, -z1, y0), (x1, -z1, y1), (x0, -z1, y1)])
        b = extrude(comp, sk.profiles.item(0), z1 - z0, NEW).bodies.item(0)
        if i == 0:
            pcb = b
        else:
            combine(comp, pcb, [b], JOIN)
    for sx in (-1, 1):
        for sz in (-1, 1):
            cyl_y(comp, sx * HOLE_X, sz * HOLE_Z, 1.6, -1.6, 0.1, CUT, [pcb])
    pcb.name = "CYD keep-out"
    for i, b in enumerate([b for b in comp.bRepBodies if b != pcb]):
        b.name = "CYD keep-out %d" % (i + 2)
    return pcb


def build_base(comp, M, sc):
    bw, bd, br = BASE_BOT
    tw, td, tr = BASE_TOP
    t12 = math.tan(math.radians(TILT))
    yf_top = BASE_H * t12
    sk0 = comp.sketches.add(comp.xYConstructionPlane)
    rrect(sk0, (0, bd / 2, 0), (1, 0, 0), (0, 1, 0), bw / 2, bd / 2, br)
    sk1 = comp.sketches.add(offset_plane(comp, comp.xYConstructionPlane, BASE_H))
    rrect(sk1, (0, yf_top + td / 2, BASE_H), (1, 0, 0), (0, 1, 0), tw / 2, td / 2, tr)
    li = comp.features.loftFeatures.createInput(NEW)
    li.loftSections.add(sk0.profiles.item(0))
    li.loftSections.add(sk1.profiles.item(0))
    base = comp.features.loftFeatures.add(li).bodies.item(0)
    base.name = "Base"
    top = planar_face(base, (0, 0, 1), "z", BASE_H)
    fillet(comp, list(top.edges), BASE_TOP_R)
    bot = planar_face(base, (0, 0, -1), "z", 0.0)
    fillet(comp, list(bot.edges), BASE_BOT_R)

    # ---- cutters built in pod-local coordinates, then moved into place with the pod matrix
    y_back = POD_BACK_IN + WALL
    cutters, nubs = [], []
    for sx in (-1, 1):
        cx = sx * RAIL_X
        r, t = RAIL_ROOT / 2 + RAIL_CLEAR, RAIL_TIP / 2 + RAIL_CLEAR
        floor = y_back + RAIL_H + RAIL_CLEAR
        sk = comp.sketches.add(offset_plane(comp, comp.xYConstructionPlane, RAIL_Z[0]))
        poly(sk, [(cx - r, y_back - 2, RAIL_Z[0]), (cx + r, y_back - 2, RAIL_Z[0]), (cx + r, y_back, RAIL_Z[0]),
                  (cx + t, floor, RAIL_Z[0]), (cx - t, floor, RAIL_Z[0]), (cx - r, y_back, RAIL_Z[0])])
        cutters.append(extrude(comp, sk.profiles.item(0), 70, NEW).bodies.item(0))
        cutters.append(cyl_y(comp, cx, BOLT_Z, M3_CLEAR / 2, y_back - 1, y_back + 80, NEW).bodies.item(0))
        cutters.append(cyl_y(comp, cx, BOLT_Z, M3_HEAD / 2 + 0.25, floor + BOLT_MEAT, y_back + 80, NEW).bodies.item(0))
        nubs.append(cyl_y(comp, cx, NUB_Z, NUB_R, floor - NUB_H, floor + 0.5, NEW).bodies.item(0))
    # cable tunnel: from the pod's cable hole, sloping down toward the back
    s, c, ty, tz = sc
    dl = math.radians(TUNNEL_DROP)
    dw = (0.0, math.cos(dl), -math.sin(dl))                    # world direction (back, down)
    th = -math.radians(TILT)                                   # world -> pod-local: rotate by -th
    dly = dw[1] * math.cos(-th) - dw[2] * math.sin(-th)
    dlz = dw[1] * math.sin(-th) + dw[2] * math.cos(-th)
    gamma = math.atan2(-dlz, dly)                              # angle of the axis below pod +Y
    ang_pl = None
    for sign in (1, -1):
        pi = comp.constructionPlanes.createInput()
        pi.setByAngle(comp.xConstructionAxis, VI(sign * gamma), comp.xZConstructionPlane)
        cand = comp.constructionPlanes.add(pi)
        n = cand.geometry.normal
        if abs(n.y * dly + n.z * dlz) > 0.999:
            ang_pl = cand
            nsign = 1 if (n.y * dly + n.z * dlz) > 0 else -1
            break
        cand.deleteMe()
    E = (0.0, y_back, CABLE_HOLE[2])
    n = ang_pl.geometry.normal
    off = E[1] * n.y + E[2] * n.z  # mm: the angled plane contains the X axis
    tpl = offset_plane(comp, ang_pl, off)
    sk = comp.sketches.add(tpl)
    v = (0.0, -n.z, n.y)  # in-plane, perpendicular to X
    rrect(sk, E, (1, 0, 0), v, TUNNEL_W / 2, TUNNEL_H / 2, TUNNEL_H / 2 - 0.01)
    ei = comp.features.extrudeFeatures.createInput(sk.profiles.item(0), NEW)
    ei.setSymmetricExtent(VI(cm(70)), False)  # 70 mm each side of the plane
    cutters.append(comp.features.extrudeFeatures.add(ei).bodies.item(0))

    oc = adsk.core.ObjectCollection.create()
    for b in cutters + nubs:
        oc.add(b)
    mi = comp.features.moveFeatures.createInput2(oc)
    mi.defineAsFreeMove(M)
    comp.features.moveFeatures.add(mi)
    combine(comp, base, cutters, CUT)
    combine(comp, base, nubs, JOIN)

    # ---- lid recess, four penny bores, tab pockets, snap slot, pry notch (all from the bottom)
    yc = bd / 2
    sk = comp.sketches.add(comp.xYConstructionPlane)
    rrect(sk, (0, yc, 0), (1, 0, 0), (0, 1, 0), LID_HALF[0], LID_HALF[1], LID_R)
    extrude(comp, sk.profiles.item(0), LID_T, CUT, [base])
    vol0 = base.volume
    bore_h = PENNY_STACK * 1.52 + 0.6
    sk = comp.sketches.add(offset_plane(comp, comp.xYConstructionPlane, LID_T - 0.01))
    for sx in (-1, 1):
        for sy in (-1, 1):
            circle(sk, (sx * PENNY_PITCH / 2, yc + sy * PENNY_PITCH / 2, LID_T - 0.01), PENNY_BORE_D / 2)
    extrude(comp, all_profiles(sk), bore_h + 0.01, CUT, [base])
    removed = (vol0 - base.volume) * 1000.0
    expect = 4 * math.pi * (PENNY_BORE_D / 2) ** 2 * bore_h
    print("Penny bores: removed %.0f mm3, expected %.0f mm3 (equal = bores hit nothing else)" % (removed, expect))
    a = LID_HALF[0]
    for sy in (-1, 1):  # hook-tab pockets in the -x wall
        box(comp, -(a + TAB_OUT + 0.4), -(a - 0.2), yc + sy * TAB_Y - TAB_W / 2 - 0.3, yc + sy * TAB_Y + TAB_W / 2 + 0.3,
            TAB_Z[0] - 0.2, LID_T + 0.1, CUT, [base])
    # snap slot (arm flexes toward -x) and the window above the ledge for the barb
    box(comp, ARM_X - 1.2, SLOT_OUT_X, yc - ARM_W / 2 - 0.4, yc + ARM_W / 2 + 0.4, LID_T - 0.1, ARM_TOP + 0.4, CUT, [base])
    box(comp, SLOT_OUT_X - 0.1, ARM_X + ARM_T + BARB_OUT + 0.4, yc - ARM_W / 2 - 0.4, yc + ARM_W / 2 + 0.4,
        LEDGE_Z, ARM_TOP + 0.4, CUT, [base])
    # pry notch at the +x edge: a coin or fingernail levers the lid out
    box(comp, a - 0.5, a + 2.0, yc - 3.0, yc + 3.0, -0.1, 1.6, CUT, [base])
    return base


def box(comp, x0, x1, y0, y1, z0, z1, op, bodies=None):
    sk = comp.sketches.add(offset_plane(comp, comp.xYConstructionPlane, z0))
    poly(sk, [(x0, y0, z0), (x1, y0, z0), (x1, y1, z0), (x0, y1, z0)])
    return extrude(comp, sk.profiles.item(0), z1 - z0, op, bodies)


def build_lid(comp):
    bw, bd, br = BASE_BOT
    yc = bd / 2
    a = LID_HALF[0] - LID_CLEAR
    sk = comp.sketches.add(comp.xYConstructionPlane)
    rrect(sk, (0, yc, 0), (1, 0, 0), (0, 1, 0), a, LID_HALF[1] - LID_CLEAR, LID_R - LID_CLEAR)
    lid = extrude(comp, sk.profiles.item(0), LID_T, NEW).bodies.item(0)
    lid.name = "Penny Lid"
    # hook tabs (-x): hinge in first, then swing the +x end up until the snap clicks
    for sy in (-1, 1):
        box(comp, -(LID_HALF[0] + TAB_OUT), -(a - 0.5), yc + sy * TAB_Y - TAB_W / 2, yc + sy * TAB_Y + TAB_W / 2,
            TAB_Z[0], TAB_Z[1], JOIN, [lid])
    # snap arm (+x) rising from the lid's inner face
    box(comp, ARM_X, ARM_X + ARM_T, yc - ARM_W / 2, yc + ARM_W / 2, LID_T - 0.5, ARM_TOP, JOIN, [lid])
    # barb: 30 deg catch face underneath (pries out), long ramp on top (clicks in)
    xo, xb = ARM_X + ARM_T - 0.05, ARM_X + ARM_T + BARB_OUT
    y0 = yc - ARM_W / 2
    sk = comp.sketches.add(offset_plane(comp, comp.xZConstructionPlane, y0))  # XZ normal is +Y
    poly(sk, [(xo, y0, LEDGE_Z - 0.05), (xb, y0, LEDGE_Z + 0.35), (xb, y0, LEDGE_Z + 0.95), (xo, y0, ARM_TOP)])
    extrude(comp, sk.profiles.item(0), ARM_W, JOIN, [lid])
    return lid


# ----------------------------------------------------------------------------- main
def run(_context):
    # close a half-built document left by a previous failed run of this script
    try:
        ad = app.activeDocument
        d0 = adsk.fusion.Design.cast(ad.products.itemByProductType("DesignProductType"))
        if d0 and not ad.isSaved and any(o.component.name == "Screen Pod" for o in d0.rootComponent.occurrences):
            ad.close(False)
    except Exception:
        pass
    doc = app.documents.add(adsk.core.DocumentTypes.FusionDesignDocumentType)
    design = adsk.fusion.Design.cast(app.activeProduct)
    design.designType = adsk.fusion.DesignTypes.ParametricDesignType
    root = design.rootComponent
    ident = adsk.core.Matrix3D.create()
    M, sc = pod_matrix()

    pod_occ = root.occurrences.addNewComponent(ident)
    pod_occ.component.name = "Screen Pod"
    front, back = build_pod(pod_occ.component)

    cyd_occ = root.occurrences.addNewComponent(ident)
    cyd_occ.component.name = "CYD board (reference)"
    build_board_proxy(cyd_occ.component)

    base_occ = root.occurrences.addNewComponent(ident)
    base_occ.component.name = "Stand Base"
    base = build_base(base_occ.component, M, sc)

    lid_occ = root.occurrences.addNewComponent(ident)
    lid_occ.component.name = "Penny Lid"
    lid = build_lid(lid_occ.component)

    # tilt the pod and the board into place (a Move feature in each component; setting the
    # occurrence transform gets reverted by the position snapshot in a parametric design)
    for occ in (pod_occ, cyd_occ):
        comp = occ.component
        oc = adsk.core.ObjectCollection.create()
        for bd_ in comp.bRepBodies:
            oc.add(bd_)
        mi = comp.features.moveFeatures.createInput2(oc)
        mi.defineAsFreeMove(M)
        comp.features.moveFeatures.add(mi)

    # report
    def bb(o):
        b = None
        for bd_ in o.bRepBodies:
            if b is None:
                b = bd_.boundingBox.copy()
            else:
                b.combine(bd_.boundingBox)
        return "%.1f x %.1f x %.1f mm" % ((b.maxPoint.x - b.minPoint.x) * 10, (b.maxPoint.y - b.minPoint.y) * 10,
                                         (b.maxPoint.z - b.minPoint.z) * 10)

    print("Pod:", bb(pod_occ), " Base:", bb(base_occ))
    b = root.boundingBox
    print("Overall: %.1f W x %.1f D x %.1f H mm" % ((b.maxPoint.x - b.minPoint.x) * 10, (b.maxPoint.y - b.minPoint.y) * 10,
                                                 b.maxPoint.z * 10))
    for o in (pod_occ, base_occ, lid_occ, cyd_occ):
        for bd_ in o.bRepBodies:
            print("  %-22s %-14s vol %.1f cm3" % (o.component.name, bd_.name, bd_.volume))

    # interference check
    oc = adsk.core.ObjectCollection.create()
    for o in (pod_occ, base_occ, lid_occ, cyd_occ):
        for bd_ in o.bRepBodies:
            oc.add(bd_)
    ii = design.createInterferenceInput(oc)
    ii.areCoincidentFacesIncluded = False
    res = design.analyzeInterference(ii)
    print("Interferences:", res.count)
    for r in res:
        print("  %s <-> %s : %.4f cm3" % (r.entityOne.name, r.entityTwo.name, r.interferenceBody.volume))

    # STL export
    os.makedirs(OUT_DIR, exist_ok=True)
    em = design.exportManager
    for body, name in ((front, "PodFront"), (back, "PodBack"), (base, "Base"), (lid, "PennyLid")):
        proxy = None
        for o in root.allOccurrences:
            for bd_ in o.bRepBodies:
                if bd_.name == body.name:
                    proxy = bd_
        opts = em.createSTLExportOptions(proxy, os.path.join(OUT_DIR, "SportsTicker_%s.stl" % name))
        opts.meshRefinement = adsk.fusion.MeshRefinementSettings.MeshRefinementHigh
        em.execute(opts)
    print("STLs ->", OUT_DIR)
    app.activeViewport.fit()
