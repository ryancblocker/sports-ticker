"""
Sports Ticker case for the ESP32-2432S028 ("Cheap Yellow Display").

Run in Fusion:  Utilities > Add-Ins > Scripts and Add-Ins > Scripts tab > "+" (or the
green plus) > pick this SportsTickerCase folder > Run.

Creates a NEW design with three bodies, then exports STLs next to this script (../stl):
  Bezel  - front frame with the screen window; the board drops in face-down
  Back   - rear shell; corner posts clamp the board, 4x M3 screws hold it together
  Stand  - tilted desk cradle the finished case sits in

Model orientation: looking down from +Z you are looking AT THE SCREEN.
  +X = right side of the screen, +Y = up, Z=0 = the back face of the case.

>>> Measure your board with calipers and fix the PARAMETERS below before printing. <<<
The defaults match common CYD boards, but revisions vary (window offset and USB side
especially). Edit, re-run (it makes a fresh design each time), print.
"""

import math
import os
import traceback

import adsk.core
import adsk.fusion

# =========================== PARAMETERS (millimeters) ===========================
# --- the board ---
PCB_L = 86.0            # board length (long side)
PCB_W = 50.0            # board width
PCB_T = 1.6             # board thickness
DISPLAY_STACK = 4.6     # front of PCB -> front of the display glass
BACK_CLEAR = 7.0        # room behind the PCB (ESP32 module, USB, SD slot)

# --- visible screen window (2.8" ILI9341 active area is ~57.6 x 43.2) ---
WIN_W = 59.0
WIN_H = 45.0
WIN_OFF_X = -5.0        # window center relative to board center, +X = right (as viewed)
WIN_OFF_Y = 0.0
WIN_R = 1.5             # window corner radius

# --- case ---
CLR = 0.3               # gap around the PCB
FRAME = 6.5             # solid frame width around the board (screws live here)
CORNER_R = 4.0          # outer corner radius
FRONT_T = 2.0           # front plate thickness
BACK_T = 2.0            # back plate thickness
POST = 5.0              # square corner posts that press on the PCB back

# --- screws (M3 x 12, self-tapping into the bezel) ---
SCREW_PILOT_D = 2.5     # use 4.0 for M3 heat-set inserts
PILOT_DEPTH = 6.5
SCREW_CLEAR_D = 3.4
SCREW_HEAD_D = 6.2
SCREW_HEAD_H = 3.0

# --- side cut-outs: (side, offset_along_edge, width, z_center_from_pcb_back, height)
#     side: "L"/"R" (short edges as viewed from the front), "T"/"B" (long edges)
#     z_center < 0 means behind the PCB (USB/SD sit on the back of the board)
SIDE_CUTOUTS = [
    ("L", 0.0, 13.0, -2.6, 7.5),   # micro-USB / USB-C
]

# --- back vents + engraving ---
VENTS = True
VENT_COUNT = 7
VENT_W = 2.2
VENT_LEN = 18.0
VENT_PITCH = 5.0
VENT_Y = 10.0           # vent block center (+Y = up)
ENGRAVE_TEXT = "HAPPY BIRTHDAY!"   # "" to skip
ENGRAVE_H = 5.0
ENGRAVE_DEPTH = 0.6
ENGRAVE_Y = -14.0

# --- desk stand ---
STAND = True
STAND_TILT = 15.0       # degrees back from vertical
STAND_WIDTH = 60.0
STAND_LIP_H = 6.0       # front lip height (keep below the screen window)
STAND_LIP_T = 3.0
STAND_REST_H = 34.0
STAND_REST_T = 3.0
STAND_SEAT_T = 3.0
STAND_FOOT = 22.0       # extra foot behind the backrest (stability)
STAND_FIT = 0.6         # slack in the slot for the case

EXPORT_STL = True
# ===============================================================================


def cm(v):
    """Fusion's API works in centimeters."""
    return v / 10.0


class Builder:
    def __init__(self):
        self.tbm = adsk.fusion.TemporaryBRepManager.get()

    # --- primitives (all inputs in mm) ---
    def box(self, x0, x1, y0, y1, z0, z1):
        cx, cy, cz = (x0 + x1) / 2, (y0 + y1) / 2, (z0 + z1) / 2
        obb = adsk.core.OrientedBoundingBox3D.create(
            adsk.core.Point3D.create(cm(cx), cm(cy), cm(cz)),
            adsk.core.Vector3D.create(1, 0, 0),
            adsk.core.Vector3D.create(0, 1, 0),
            cm(abs(x1 - x0)), cm(abs(y1 - y0)), cm(abs(z1 - z0)))
        return self.tbm.createBox(obb)

    def cyl(self, cx, cy, z0, z1, d):
        return self.tbm.createCylinderOrCone(
            adsk.core.Point3D.create(cm(cx), cm(cy), cm(z0)), cm(d / 2),
            adsk.core.Point3D.create(cm(cx), cm(cy), cm(z1)), cm(d / 2))

    def rrect(self, cx, cy, w, h, r, z0, z1):
        """Rounded-rectangle prism."""
        r = max(0.01, min(r, w / 2 - 0.01, h / 2 - 0.01))
        b = self.box(cx - w / 2, cx + w / 2, cy - h / 2 + r, cy + h / 2 - r, z0, z1)
        self.add(b, self.box(cx - w / 2 + r, cx + w / 2 - r, cy - h / 2, cy + h / 2, z0, z1))
        for sx in (-1, 1):
            for sy in (-1, 1):
                self.add(b, self.cyl(cx + sx * (w / 2 - r), cy + sy * (h / 2 - r), z0, z1, 2 * r))
        return b

    def slot(self, cx, cy, length, width, z0, z1, vertical=True):
        """Stadium-shaped slot."""
        r = width / 2
        if vertical:
            b = self.box(cx - r, cx + r, cy - length / 2 + r, cy + length / 2 - r, z0, z1)
            self.add(b, self.cyl(cx, cy - length / 2 + r, z0, z1, width))
            self.add(b, self.cyl(cx, cy + length / 2 - r, z0, z1, width))
        else:
            b = self.box(cx - length / 2 + r, cx + length / 2 - r, cy - r, cy + r, z0, z1)
            self.add(b, self.cyl(cx - length / 2 + r, cy, z0, z1, width))
            self.add(b, self.cyl(cx + length / 2 - r, cy, z0, z1, width))
        return b

    # --- booleans (modify target in place) ---
    def add(self, target, tool):
        if not self.tbm.booleanOperation(target, tool, adsk.fusion.BooleanTypes.UnionBooleanType):
            raise RuntimeError("union failed")
        return target

    def cut(self, target, tool):
        if not self.tbm.booleanOperation(target, tool, adsk.fusion.BooleanTypes.DifferenceBooleanType):
            raise RuntimeError("cut failed")
        return target

    def keep(self, target, tool):
        if not self.tbm.booleanOperation(target, tool, adsk.fusion.BooleanTypes.IntersectionBooleanType):
            raise RuntimeError("intersect failed")
        return target

    def copy(self, body):
        return self.tbm.copy(body)

    def rotate_x(self, body, deg):
        m = adsk.core.Matrix3D.create()
        m.setToRotation(math.radians(deg), adsk.core.Vector3D.create(1, 0, 0), adsk.core.Point3D.create(0, 0, 0))
        self.tbm.transform(body, m)

    def move(self, body, dx, dy, dz):
        m = adsk.core.Matrix3D.create()
        m.translation = adsk.core.Vector3D.create(cm(dx), cm(dy), cm(dz))
        self.tbm.transform(body, m)


def build_case(B):
    L_in, W_in = PCB_L + 2 * CLR, PCB_W + 2 * CLR
    L_out, W_out = L_in + 2 * FRAME, W_in + 2 * FRAME
    z_pcb_back = BACK_T + BACK_CLEAR                 # back shell / bezel split plane
    z_glass = z_pcb_back + PCB_T + DISPLAY_STACK     # inner face of the front plate
    z_front = z_glass + FRONT_T
    screw_x = L_in / 2 + FRAME / 2
    screw_y = W_in / 2 + FRAME / 2
    corners = [(sx * screw_x, sy * screw_y) for sx in (-1, 1) for sy in (-1, 1)]

    # ---------------- bezel ----------------
    bezel = B.rrect(0, 0, L_out, W_out, CORNER_R, z_pcb_back, z_front)
    B.cut(bezel, B.box(-L_in / 2, L_in / 2, -W_in / 2, W_in / 2, z_pcb_back - 1, z_glass))
    B.cut(bezel, B.rrect(WIN_OFF_X, WIN_OFF_Y, WIN_W, WIN_H, WIN_R, z_glass - 1, z_front + 1))
    for (x, y) in corners:
        B.cut(bezel, B.cyl(x, y, z_pcb_back - 1, z_pcb_back + PILOT_DEPTH, SCREW_PILOT_D))

    # ---------------- back shell ----------------
    back = B.rrect(0, 0, L_out, W_out, CORNER_R, 0, z_pcb_back)
    B.cut(back, B.box(-L_in / 2, L_in / 2, -W_in / 2, W_in / 2, BACK_T, z_pcb_back + 1))
    # corner posts that clamp the PCB against the bezel
    for sx in (-1, 1):
        for sy in (-1, 1):
            x_edge, y_edge = sx * L_in / 2, sy * W_in / 2
            B.add(back, B.box(x_edge, x_edge - sx * POST, y_edge, y_edge - sy * POST, BACK_T - 0.1, z_pcb_back))
    for (x, y) in corners:
        B.cut(back, B.cyl(x, y, -1, z_pcb_back + 1, SCREW_CLEAR_D))
        B.cut(back, B.cyl(x, y, -1, SCREW_HEAD_H, SCREW_HEAD_D))
    if VENTS:
        x0 = -(VENT_COUNT - 1) * VENT_PITCH / 2
        for i in range(VENT_COUNT):
            B.cut(back, B.slot(x0 + i * VENT_PITCH, VENT_Y, VENT_LEN, VENT_W, -1, BACK_T + 1))

    # ---------------- side cut-outs (through both halves) ----------------
    for (side, off, w, zc, h) in SIDE_CUTOUTS:
        z0, z1 = z_pcb_back + zc - h / 2, z_pcb_back + zc + h / 2
        if side in ("L", "R"):
            s = -1 if side == "L" else 1
            xa, xb = s * (L_in / 2 - 0.5), s * (L_out / 2 + 1)
            tool = B.box(min(xa, xb), max(xa, xb), off - w / 2, off + w / 2, z0, z1)
        else:
            s = -1 if side == "B" else 1
            ya, yb = s * (W_in / 2 - 0.5), s * (W_out / 2 + 1)
            tool = B.box(off - w / 2, off + w / 2, min(ya, yb), max(ya, yb), z0, z1)
        B.cut(bezel, B.copy(tool))
        B.cut(back, tool)

    dims = dict(L_out=L_out, W_out=W_out, depth=z_front, split=z_pcb_back)
    return bezel, back, dims


def build_stand(B, case_depth, case_w):
    """Cradle profile is built upright in a (w=depth, h=height) frame, then tilted back."""
    D = case_depth + STAND_FIT
    deep = 60.0
    hw = STAND_WIDTH / 2
    # Note: model Y = w (depth, viewer at -Y), model Z = h (height)
    s = B.box(-hw, hw, -STAND_LIP_T, D + STAND_REST_T, -deep, 0)                       # seat block
    B.add(s, B.box(-hw, hw, -STAND_LIP_T, 0, -deep, STAND_LIP_H))                      # front lip
    B.add(s, B.box(-hw, hw, D, D + STAND_REST_T, -deep, STAND_REST_H))                 # backrest
    # gentle relief so the case's bottom edge doesn't bind in the corner
    B.cut(s, B.box(-hw - 1, hw + 1, -0.01, 1.0, -1.0, 0.01))
    B.rotate_x(s, -STAND_TILT)  # tilt the top toward +Y (away from the viewer)

    t = math.radians(STAND_TILT)
    def rot(w, h):
        return (w * math.cos(t) + h * math.sin(t), -w * math.sin(t) + h * math.cos(t))
    back_u, back_v = rot(D + STAND_REST_T, -STAND_SEAT_T)
    front_u, _ = rot(-STAND_LIP_T, -STAND_SEAT_T)
    table = back_v  # thinnest point of the seat sets the table plane
    B.keep(s, B.box(-hw - 1, hw + 1, -200, 200, table, 200))
    # foot behind the backrest for stability
    B.add(s, B.box(-hw, hw, back_u - 2, back_u + STAND_FOOT, table, table + STAND_SEAT_T))
    # sit it on Z=0 beside the case
    B.move(s, 0, -(case_w / 2 + 25 + (back_u + STAND_FOOT)), -table)
    return s


def engrave(root, back_body):
    """Mirror-text cut into the back face (reads correctly from behind)."""
    sk = root.sketches.add(root.xYConstructionPlane)
    sk.name = "Engraving"
    w = cm(80)
    ti = sk.sketchTexts.createInput2(ENGRAVE_TEXT, cm(ENGRAVE_H))
    ti.setAsMultiLine(adsk.core.Point3D.create(-w / 2, cm(ENGRAVE_Y - ENGRAVE_H), 0),
                      adsk.core.Point3D.create(w / 2, cm(ENGRAVE_Y + ENGRAVE_H), 0),
                      adsk.core.HorizontalAlignments.CenterHorizontalAlignment,
                      adsk.core.VerticalAlignments.MiddleVerticalAlignment, 0)
    ti.fontName = "Arial Black"
    txt = sk.sketchTexts.add(ti)
    try:
        txt.isHorizontalFlip = True  # we're writing on the face that points down (-Z)
    except Exception:
        pass
    ext = root.features.extrudeFeatures.createInput(txt, adsk.fusion.FeatureOperations.CutFeatureOperation)
    ext.setDistanceExtent(False, adsk.core.ValueInput.createByReal(cm(ENGRAVE_DEPTH)))
    ext.participantBodies = [back_body]
    root.features.extrudeFeatures.add(ext)


def run(context):
    ui = None
    try:
        app = adsk.core.Application.get()
        ui = app.userInterface
        doc = app.documents.add(adsk.core.DocumentTypes.FusionDesignDocumentType)
        design = adsk.fusion.Design.cast(app.activeProduct)
        design.designType = adsk.fusion.DesignTypes.DirectDesignType  # lets us add B-Rep bodies directly
        root = design.rootComponent

        B = Builder()
        bezel_t, back_t, dims = build_case(B)
        bodies = {}
        bodies["Bezel"] = root.bRepBodies.add(bezel_t)
        bodies["Back"] = root.bRepBodies.add(back_t)
        if STAND:
            bodies["Stand"] = root.bRepBodies.add(build_stand(B, dims["depth"], dims["W_out"]))
        for name, body in bodies.items():
            body.name = name

        notes = []
        if ENGRAVE_TEXT:
            try:
                engrave(root, bodies["Back"])
            except Exception:
                notes.append("Engraving skipped (text feature failed): " + traceback.format_exc().splitlines()[-1])

        try:
            doc.name = "Sports Ticker Case"
        except Exception:
            pass
        app.activeViewport.fit()

        if EXPORT_STL:
            out = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))), "stl")
            os.makedirs(out, exist_ok=True)
            em = design.exportManager
            for name, body in bodies.items():
                opts = em.createSTLExportOptions(body, os.path.join(out, "sports_ticker_%s.stl" % name.lower()))
                opts.meshRefinement = adsk.fusion.MeshRefinementSettings.MeshRefinementHigh
                em.execute(opts)
            notes.append("STLs exported to:\n" + out)

        ui.messageBox(
            "Sports Ticker case built.\n\nOutside: %.1f x %.1f x %.1f mm (split at %.1f mm)\n\n%s\n\n"
            "Print Bezel face-down, Back face-down, Stand on its base. Hardware: 4x M3x12 screws."
            % (dims["L_out"], dims["W_out"], dims["depth"], dims["split"], "\n".join(notes)),
            "Sports Ticker")
    except Exception:
        if ui:
            ui.messageBox("Failed:\n{}".format(traceback.format_exc()))
