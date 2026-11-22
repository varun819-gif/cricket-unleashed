# Visual phase 2: one complete delivery with five dressed players.
#
#   bowler (Pakistan, right-arm fast)  ->  batter (India, right-handed),
#   front-foot cover drive for FOUR; non-striker backing up, wicketkeeper
#   standing back, umpire signalling the boundary.
#
# The ball's path is the unchanged output of our C++ physics
# (visual/data/delivery_cover_drive.json). Movements are key poses (many
# more than phase 1) with smooth curves, foot roll, head and eye tracking.
# NOT motion capture: no suitable free cricket motion data with a clear
# licence was available (see visual/README.md).
#
#   blender -b -P visual/blender/scene_v2.py -- shot <name> <outdir> [WxH] [samples]
#   blender -b -P visual/blender/scene_v2.py -- still <camera> <t> <file> [WxH] [samples]
import json
import math
import os
import sys

import bpy
from mathutils import Matrix, Quaternion, Vector

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import cricketer as C  # noqa: E402
import equipment as E  # noqa: E402

DATA = json.load(open(os.path.join(HERE, "..", "data", "delivery_cover_drive.json")))
FPS = 100                 # key timebase (slow motion renders every frame, normal speed every 4th)
T0, T1 = -1.9, 4.8        # seconds around release (t = 0)
TC = DATA["contactTime"]  # bat meets ball (physics)
CP = Vector(DATA["contactPoint"])
RELEASE = Vector(DATA["flight"][0][1:])


def F(t):
    return int(round((t - T0) * FPS)) + 1


def ball_at(t):
    path = DATA["flight"] + DATA["afterShot"][1:]
    if t <= path[0][0]:
        return Vector(path[0][1:])
    for a, b in zip(path, path[1:]):
        if a[0] <= t <= b[0]:
            u = (t - a[0]) / max(1e-9, b[0] - a[0])
            return Vector(a[1:]).lerp(Vector(b[1:]), u)
    return Vector(path[-1][1:])


def lerp_keys(keys, t):
    """keys: sorted [(t, value)]; smoothstep between keys."""
    if t <= keys[0][0]:
        return keys[0][1]
    for (ta, va), (tb, vb) in zip(keys, keys[1:]):
        if ta <= t <= tb:
            u = (t - ta) / (tb - ta)
            u = u * u * (3 - 2 * u)
            if isinstance(va, (int, float)):
                return va + (vb - va) * u
            return Vector(va).lerp(Vector(vb), u)
    return keys[-1][1]


# ------------------------------------------------------------------ materials (ground)

def nodes(name):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    return m, m.node_tree.nodes, m.node_tree.links


def grass_material():
    m, n, l = nodes("grass")
    b = n["Principled BSDF"]
    tc = n.new("ShaderNodeTexCoord")
    stripes = n.new("ShaderNodeTexWave")
    stripes.bands_direction = "X"
    stripes.wave_profile = "SAW"
    stripes.inputs["Scale"].default_value = 0.0833
    gt = n.new("ShaderNodeMath")
    gt.operation = "GREATER_THAN"
    gt.inputs[1].default_value = 0.5
    l.new(tc.outputs["Object"], stripes.inputs["Vector"])
    l.new(stripes.outputs["Fac"], gt.inputs[0])
    big = n.new("ShaderNodeTexNoise")
    big.inputs["Scale"].default_value = 0.05
    fine = n.new("ShaderNodeTexNoise")
    fine.inputs["Scale"].default_value = 260.0
    fine.inputs["Detail"].default_value = 10.0
    col = n.new("ShaderNodeMix")
    col.data_type = "RGBA"
    l.new(gt.outputs[0], col.inputs["Factor"])
    col.inputs["A"].default_value = (0.045, 0.12, 0.022, 1)
    col.inputs["B"].default_value = (0.068, 0.165, 0.032, 1)
    patch = n.new("ShaderNodeMix")
    patch.data_type = "RGBA"
    patch.blend_type = "OVERLAY"
    patch.inputs["Factor"].default_value = 0.35
    l.new(col.outputs["Result"], patch.inputs["A"])
    l.new(big.outputs["Color"], patch.inputs["B"])
    blades = n.new("ShaderNodeMix")
    blades.data_type = "RGBA"
    blades.blend_type = "MULTIPLY"
    blades.inputs["Factor"].default_value = 0.4
    l.new(patch.outputs["Result"], blades.inputs["A"])
    l.new(fine.outputs["Color"], blades.inputs["B"])
    l.new(blades.outputs["Result"], b.inputs["Base Color"])
    bump = n.new("ShaderNodeBump")
    bump.inputs["Strength"].default_value = 0.35
    bump.inputs["Distance"].default_value = 0.003
    l.new(fine.outputs["Fac"], bump.inputs["Height"])
    l.new(bump.outputs["Normal"], b.inputs["Normal"])
    b.inputs["Roughness"].default_value = 0.8
    return m


def pitch_material():
    """Dry, rolled pitch: light brown with a faint green tinge, fine cracks,
    darker scuffed footmarks at both ends."""
    m, n, l = nodes("pitch")
    b = n["Principled BSDF"]
    tc = n.new("ShaderNodeTexCoord")
    noise = n.new("ShaderNodeTexNoise")
    noise.inputs["Scale"].default_value = 18.0
    noise.inputs["Detail"].default_value = 8.0
    ramp = n.new("ShaderNodeValToRGB")
    ramp.color_ramp.elements[0].color = (0.23, 0.205, 0.13, 1)
    ramp.color_ramp.elements[1].color = (0.35, 0.315, 0.205, 1)
    l.new(noise.outputs["Fac"], ramp.inputs["Fac"])
    vor = n.new("ShaderNodeTexVoronoi")
    vor.feature = "DISTANCE_TO_EDGE"
    vor.inputs["Scale"].default_value = 9.0
    l.new(tc.outputs["Object"], vor.inputs["Vector"])
    crack = n.new("ShaderNodeMath")
    crack.operation = "LESS_THAN"
    crack.inputs[1].default_value = 0.025
    l.new(vor.outputs["Distance"], crack.inputs[0])
    # footmarks: object y near each popping crease
    sep = n.new("ShaderNodeSeparateXYZ")
    l.new(tc.outputs["Object"], sep.inputs["Vector"])
    fm = []
    for yc in (-10.06 + 1.0, 10.06 - 1.0):  # object space: pitch centred on 0
        d = n.new("ShaderNodeMath")
        d.operation = "SUBTRACT"
        d.inputs[1].default_value = yc
        l.new(sep.outputs["Y"], d.inputs[0])
        a = n.new("ShaderNodeMath")
        a.operation = "ABSOLUTE"
        l.new(d.outputs[0], a.inputs[0])
        s = n.new("ShaderNodeMapRange")
        s.inputs["From Min"].default_value = 1.6
        s.inputs["From Max"].default_value = 0.2
        l.new(a.outputs[0], s.inputs["Value"])
        fm.append(s)
    fmax = n.new("ShaderNodeMath")
    fmax.operation = "MAXIMUM"
    l.new(fm[0].outputs["Result"], fmax.inputs[0])
    l.new(fm[1].outputs["Result"], fmax.inputs[1])
    fn = n.new("ShaderNodeTexNoise")
    fn.inputs["Scale"].default_value = 6.0
    fmask = n.new("ShaderNodeMath")
    fmask.operation = "MULTIPLY"
    l.new(fmax.outputs[0], fmask.inputs[0])
    l.new(fn.outputs["Fac"], fmask.inputs[1])
    worn = n.new("ShaderNodeMix")
    worn.data_type = "RGBA"
    l.new(fmask.outputs[0], worn.inputs["Factor"])
    l.new(ramp.outputs["Color"], worn.inputs["A"])
    worn.inputs["B"].default_value = (0.22, 0.19, 0.12, 1)
    cr = n.new("ShaderNodeMix")
    cr.data_type = "RGBA"
    l.new(crack.outputs[0], cr.inputs["Factor"])
    l.new(worn.outputs["Result"], cr.inputs["A"])
    cr.inputs["B"].default_value = (0.16, 0.14, 0.09, 1)
    crf = n.new("ShaderNodeMath")
    crf.operation = "MULTIPLY"
    crf.inputs[1].default_value = 0.35
    l.new(crack.outputs[0], crf.inputs[0])
    l.new(crf.outputs[0], cr.inputs["Factor"])
    l.new(cr.outputs["Result"], b.inputs["Base Color"])
    bump = n.new("ShaderNodeBump")
    bump.inputs["Strength"].default_value = 0.1
    l.new(noise.outputs["Fac"], bump.inputs["Height"])
    l.new(bump.outputs["Normal"], b.inputs["Normal"])
    b.inputs["Roughness"].default_value = 0.85
    return m


def flat(name, color, rough=0.5, emit=0.0):
    m, n, _ = nodes(name)
    b = n["Principled BSDF"]
    b.inputs["Base Color"].default_value = (*color, 1)
    b.inputs["Roughness"].default_value = rough
    if emit:
        b.inputs["Emission Color"].default_value = (*color, 1)
        b.inputs["Emission Strength"].default_value = emit
    return m


# ------------------------------------------------------------------ ground

def box(name, size, loc, material, rot=(0, 0, 0)):
    bpy.ops.mesh.primitive_cube_add(size=1, location=loc, rotation=rot)
    o = bpy.context.active_object
    o.name = name
    o.scale = size
    bpy.ops.object.transform_apply(scale=True)
    o.data.materials.append(material)
    return o


def build_ground():
    bpy.ops.mesh.primitive_circle_add(vertices=160, radius=90, fill_type="NGON", location=(0, 10.06, 0))
    g = bpy.context.active_object
    g.name = "outfield"
    g.data.materials.append(grass_material())
    box("pitch", (3.05, 22.6, 0.012), (0, 10.06, 0.0), pitch_material())
    white = flat("crease_paint", (0.92, 0.92, 0.9), 0.6)
    for y0, sg in ((0.0, 1), (20.12, -1)):
        box("bowling_crease", (2.64, 0.05, 0.003), (0, y0, 0.0065), white)
        box("popping_crease", (3.66 * 1.4, 0.05, 0.003), (0, y0 + sg * 1.22, 0.0065), white)
        for x in (-1.32, 1.32):
            box("return_crease", (0.05, 2.44, 0.003), (x, y0 + sg * 0.0, 0.0065), white)
        E.stumps_set(y0)
    # 30-yard circle (dotted discs) and the boundary rope
    disc = flat("circle_disc", (0.95, 0.95, 0.95), 0.6)
    for k in range(48):
        a = 2 * math.pi * k / 48
        r = 27.43
        bpy.ops.mesh.primitive_cylinder_add(radius=0.18, depth=0.01, location=(r * math.cos(a), 10.06 + r * math.sin(a) * 1.1, 0.005),
                                            vertices=12)
        bpy.context.active_object.data.materials.append(disc)
    bpy.ops.mesh.primitive_torus_add(major_radius=68, minor_radius=0.13, location=(0, 10.06, 0.13),
                                     major_segments=240, minor_segments=10)
    rope = bpy.context.active_object
    rope.name = "boundary_rope"
    rope.data.materials.append(flat("rope", (0.75, 0.08, 0.06), 0.65))
    advertising_boards()


ADS = [("CRICKET UNLEASHED", (0.02, 0.12, 0.45), (1, 0.8, 0.1)), ("PLAY ON YOUR PHONE", (0.6, 0.05, 0.05), (1, 1, 1)),
       ("T20  NIGHTS", (0.02, 0.3, 0.12), (1, 1, 1)), ("SIXER SPORTS", (0.95, 0.55, 0.05), (0.05, 0.05, 0.1)),
       ("CHAI TIME", (0.35, 0.15, 0.05), (1, 0.9, 0.7)), ("BLUE OCEAN TRAVEL", (0.05, 0.35, 0.65), (1, 1, 1))]


def advertising_boards():
    """Fictional sponsors only (no real brands). Boards just outside the rope."""
    r, n = 71.0, 72
    frame_m = flat("board_frame", (0.05, 0.05, 0.06), 0.5)
    mats = [(flat("ad_bg_%d" % i, bg, 0.4, emit=1.4), flat("ad_txt_%d" % i, fg, 0.4, emit=2.5))
            for i, (_, bg, fg) in enumerate(ADS)]
    for k in range(n):
        a = 2 * math.pi * (k + 0.5) / n
        cx, cy = r * math.cos(a), 10.06 + r * math.sin(a)
        rot = a - math.pi / 2  # board faces the centre
        i = k % len(ADS)
        board = box("ad_board", (2 * math.pi * r / n - 0.15, 0.08, 0.95), (cx, cy, 0.55),
                    mats[i][0], rot=(math.radians(-6), 0, rot + math.pi))
        board.rotation_euler = (0, math.radians(8), 0)
        board.rotation_euler = (math.radians(8) * 0, 0, rot)
        cu = bpy.data.curves.new("ad", "FONT")
        cu.body = ADS[i][0]
        cu.size = 0.48
        cu.align_x = "CENTER"
        cu.align_y = "CENTER"
        t = bpy.data.objects.new("ad_text", cu)
        bpy.context.collection.objects.link(t)
        t.data.materials.append(mats[i][1])
        inward = Vector((-math.cos(a), -math.sin(a), 0))
        t.location = Vector((cx, cy, 0.55)) + inward * 0.05
        t.rotation_euler = (math.radians(90), 0, rot)
        box("ad_base", (2 * math.pi * r / n - 0.1, 0.4, 0.06), (cx, cy, 0.03), frame_m, rot=(0, 0, rot))


def sky_and_sun(elev=30.0, rot=215.0):
    sc = bpy.context.scene
    w = bpy.data.worlds.new("sky")
    sc.world = w
    w.use_nodes = True
    n = w.node_tree.nodes
    sky = n.new("ShaderNodeTexSky")
    sky.sky_type = "NISHITA"
    sky.sun_elevation = math.radians(elev)
    sky.sun_rotation = math.radians(rot)
    sky.air_density = 1.0
    sky.dust_density = 0.8
    sky.ozone_density = 1.0
    w.node_tree.links.new(sky.outputs["Color"], n["Background"].inputs["Color"])
    n["Background"].inputs["Strength"].default_value = 0.11
    sun = bpy.data.objects.new("sun", bpy.data.lights.new("sun", "SUN"))
    sc.collection.objects.link(sun)
    sun.data.energy = 3.6
    sun.data.angle = math.radians(0.8)
    sun.data.color = (1.0, 0.95, 0.88)
    sun.rotation_euler = (math.radians(90 - elev), 0, math.radians(rot - 90))


# ------------------------------------------------------------------ rig control

def rest_rot(arm, bone):
    return arm.data.bones[bone].matrix_local.to_3x3()


class Rig:
    """IK feet (with foot roll), IK hands, knee/elbow poles, spine, head and
    eye tracking. All keys are in world space; facing 0 = looking down -y."""

    def __init__(self, player, tag, elbow_back=0.8):
        self.p = player
        a = self.arm = player.arm
        self.tag = tag
        self.tgt, self.rot = {}, {}
        for k in ("foot.L", "foot.R", "hand.L", "hand.R"):
            self.tgt[k] = C.empty(tag + "_" + k, (0, 0, 0))
        self.pole = {}
        for k, loc in (("knee.L", (0.12, -1.6, 0.55)), ("knee.R", (-0.12, -1.6, 0.55)),
                       ("elbow.L", (0.7, elbow_back, 1.0)), ("elbow.R", (-0.7, elbow_back, 1.0))):
            e = C.empty(tag + "_" + k, loc)
            e.parent = a
            self.pole[k] = e
        C.ik(a, "lowerleg02.L", self.tgt["foot.L"], self.pole["knee.L"], 4)
        C.ik(a, "lowerleg02.R", self.tgt["foot.R"], self.pole["knee.R"], 4)
        C.ik(a, "lowerarm02.L", self.tgt["hand.L"], self.pole["elbow.L"], 4)
        C.ik(a, "lowerarm02.R", self.tgt["hand.R"], self.pole["elbow.R"], 4)
        for s in ("L", "R"):
            e = C.empty(tag + "_footrot." + s, (0, 0, 0))
            e.rotation_mode = "QUATERNION"
            c = a.pose.bones["foot." + s].constraints.new("COPY_ROTATION")
            c.target = e
            self.rot["foot." + s] = e
        self.rest_foot = {s: rest_rot(a, "foot." + s) for s in ("L", "R")}
        self.look = C.empty(tag + "_look", (0, 0, 1.6))
        c = a.pose.bones["head"].constraints.new("DAMPED_TRACK")
        c.target = self.look
        c.track_axis = "TRACK_Z"  # the head bone's Z axis points out of the face
        c.influence = 0.75
        c = a.pose.bones["neck02"].constraints.new("DAMPED_TRACK")
        c.target = self.look
        c.track_axis = "TRACK_Z"
        c.influence = 0.25
        self.eye_objs = []

    def track_eyes(self, eye_objs):
        for e in eye_objs:
            c = e.constraints.new("DAMPED_TRACK")
            c.target = self.look
            c.track_axis = "TRACK_NEGATIVE_Y"
        self.eye_objs = eye_objs

    def root(self, t, loc, facing):
        a = self.arm
        a.location = loc
        a.rotation_euler = (0, 0, math.radians(facing))
        a.keyframe_insert("location", frame=F(t))
        a.keyframe_insert("rotation_euler", frame=F(t))

    def foot(self, t, side, loc, yaw, pitch=0.0):
        e = self.tgt["foot." + side]
        e.location = loc
        e.keyframe_insert("location", frame=F(t))
        r = self.rot["foot." + side]
        q = (Matrix.Rotation(math.radians(yaw), 3, "Z") @ Matrix.Rotation(math.radians(pitch), 3, "X")
             @ self.rest_foot[side]).to_quaternion()
        r.rotation_quaternion = q
        r.keyframe_insert("rotation_quaternion", frame=F(t))

    def hand(self, t, side, loc):
        e = self.tgt["hand." + side]
        e.location = loc
        e.keyframe_insert("location", frame=F(t))

    def spine(self, t, lean=0.0, twist=0.0, side=0.0):
        for b, w in (("spine03", 0.35), ("spine02", 0.35), ("spine01", 0.3)):
            C.rot(self.arm, b, lean * w, twist * w, side * w)
            self.arm.pose.bones[b].keyframe_insert("rotation_euler", frame=F(t))

    def look_at(self, t, loc):
        self.look.location = loc
        self.look.keyframe_insert("location", frame=F(t))


# ------------------------------------------------------------------ players

INDIA = dict(shirt=(0.03, 0.20, 0.70), trim=(0.98, 0.50, 0.06), trousers=(0.03, 0.17, 0.55))
PAK = dict(shirt=(0.0, 0.30, 0.11), trim=(0.65, 0.88, 0.30), trousers=(0.0, 0.24, 0.09))


def make_players():
    P = {}
    P["batter"] = C.build_player("batter", dict(
        targets=[("asian-male-young", 1.0), ("universal-male-young-maxmuscle-averageweight", 0.35),
                 ("head_head-oval", 0.5), ("nose_nose-trans-up", 0.4)],
        skin=(0.42, 0.26, 0.16), sleeves="short", gloves="bat",
        hair={"length": 0.008, "color": 0.95, "count": 4500}, **INDIA))
    P["nonstriker"] = C.build_player("nonstriker", dict(
        targets=[("asian-male-young", 0.7), ("african-male-young", 0.3), ("head_head-square", 0.6),
                 ("chin_chin-width-incr", 0.5), ("universal-male-young-averagemuscle-maxweight", 0.25)],
        skin=(0.32, 0.19, 0.11), sleeves="short", gloves="bat",
        hair={"length": 0.01, "color": 1.0, "count": 4000}, **INDIA))
    P["bowler"] = C.build_player("bowler", dict(
        targets=[("caucasian-male-young", 0.45), ("asian-male-young", 0.55),
                 ("universal-male-young-maxmuscle-averageweight", 0.55),
                 ("male-young-averagemuscle-averageweight-maxheight", 0.45), ("head_head-round", 0.3),
                 ("nose_nose-scale-vert-incr", 0.5)],
        skin=(0.46, 0.31, 0.21), sleeves="long",
        hair={"length": 0.014, "color": 0.9, "count": 5000, "volume": 0.008}, **PAK))
    P["keeper"] = C.build_player("keeper", dict(
        targets=[("asian-male-young", 1.0), ("male-young-averagemuscle-averageweight-minheight", 0.4),
                 ("universal-male-young-averagemuscle-maxweight", 0.3), ("head_head-fat-incr", 0.3)],
        skin=(0.40, 0.25, 0.16), sleeves="long", gloves="keeper", glove_color=(0.95, 0.95, 0.92),
        hair={"length": 0.01, "color": 0.95, "count": 4000}, **PAK))
    P["umpire"] = C.build_player("umpire", dict(
        targets=[("african-male-young", 0.6), ("caucasian-male-young", 0.4),
                 ("universal-male-young-averagemuscle-maxweight", 0.45), ("head_head-square", 0.3),
                 ("mouth_mouth-scale-horiz-incr", 0.4)],
        skin=(0.24, 0.14, 0.08), sleeves="long", shirt=(0.40, 0.58, 0.85), trim=(0.1, 0.15, 0.3),
        trousers=(0.03, 0.03, 0.04), shoes=(0.05, 0.05, 0.05),
        hair={"length": 0.006, "color": 1.0, "count": 3000}))
    eyes = {k: E.eyes(p) for k, p in P.items()}
    for k in ("batter", "nonstriker"):
        E.helmet(P[k])
        E.leg_pad(P[k], "L")
        E.leg_pad(P[k], "R")
        E.shoe_soles(P[k])
    E.thigh_guard(P["batter"], "L")
    E.number_on_back(P["batter"], "18")
    E.number_on_back(P["nonstriker"], "7")
    E.cap(P["bowler"], PAK["shirt"], "bowler_cap")
    E.sunglasses(P["bowler"])
    E.shoe_soles(P["bowler"])
    E.number_on_back(P["bowler"], "45")
    E.cap(P["keeper"], PAK["shirt"], "keeper_cap")
    E.leg_pad(P["keeper"], "L", keeper=True)
    E.leg_pad(P["keeper"], "R", keeper=True)
    E.shoe_soles(P["keeper"])
    E.umpire_hat(P["umpire"])
    E.shoe_soles(P["umpire"], spikes=False)
    rigs = {}
    for k, p in P.items():
        rigs[k] = Rig(p, k, elbow_back=0.8 if k != "batter" else 0.6)
        rigs[k].track_eyes(eyes[k])
    for side in ("L", "R"):
        C.curl_fingers(P["batter"].arm, side, 65)
        C.curl_fingers(P["nonstriker"].arm, "R", 65)
        C.curl_fingers(P["keeper"].arm, side, 25)
    C.curl_fingers(P["bowler"].arm, "R", 35)
    return P, rigs


# ------------------------------------------------------------------ bat helpers

def bat_quat(top, toe, face):
    down = (Vector(toe) - Vector(top)).normalized()
    f = Vector(face)
    f = (f - f.dot(down) * down).normalized()
    z, y = -down, -f
    x = y.cross(z)
    return Matrix((x, y, z)).transposed().to_quaternion()


def key_bat(bat, t, top, d, face):
    bat.location = top
    bat.rotation_quaternion = bat_quat(top, Vector(top) + Vector(d) * 0.965, face)
    bat.keyframe_insert("location", frame=F(t))
    bat.keyframe_insert("rotation_quaternion", frame=F(t))


def hands_on_bat(rig, bat, one_hand=False):
    for k, d in (("L", 0.05), ("R", 0.14)):
        if one_hand and k == "L":
            continue
        h = rig.tgt["hand." + k]
        h.animation_data_clear()
        h.parent = bat
        h.location = (0, 0, -d)


# ------------------------------------------------------------------ animation: bowler

def animate_bowler(rig, ball):
    """Right-arm fast bowler. Run-up strides (foot plants, pelvis bob and
    sway, arm swing), bound, side-on back-foot landing, braced front leg,
    bowling arm over the top to the physics release point, follow-through
    across the body, run-off to the off side."""
    v = 6.6
    period = 0.31
    x_run = 0.34
    last_plant = -0.62           # left foot, take-off for the bound
    y_last = 22.35
    plants = []                  # (t, side, y)
    side = "L"
    t = last_plant
    y = y_last
    while t > T0 - period:
        plants.append((t, side, y))
        t -= period
        y += v * period
        side = "R" if side == "L" else "L"
    plants.reverse()
    fx = {"L": x_run + 0.09, "R": x_run - 0.09}
    # feet in the run-up
    for (tp, s, yp) in plants:
        rig.foot(tp, s, (fx[s], yp, 0.085), 0, 0)
        rig.foot(tp + 0.10, s, (fx[s], yp, 0.085), 0, -10)
        if tp + 0.62 <= last_plant + 0.01 or (s == "R" and tp + 0.62 < -0.2):
            rig.foot(tp + 0.15, s, (fx[s], yp - 0.05, 0.13), 0, -45)            # toe-off
            rig.foot(tp + 0.30, s, (fx[s], yp - 1.3, 0.38), 0, -70)             # heel kicks up behind
            rig.foot(tp + 0.48, s, (fx[s], yp - 3.0, 0.30), 0, 10)              # knee drives forward
    # pelvis, facing, spine, arms in the run-up
    tt = plants[0][0]
    while tt <= last_plant:
        phase = ((tt - plants[0][0]) % period) / period
        y = y_last + v * (last_plant - tt)
        z = -0.035 + 0.045 * math.sin(2 * math.pi * (phase - 0.25))
        k = int(round((tt - plants[0][0]) / period))
        lead = plants[min(k, len(plants) - 1)][1]
        sway = 7 if lead == "L" else -7
        rig.root(tt, (x_run, y, z), sway * 0.5)
        rig.spine(tt, lean=12, twist=-sway)
        swing = 0.33 * math.cos(2 * math.pi * (tt - plants[0][0]) / (2 * period))
        rig.hand(tt, "L", (x_run + 0.27, y - swing, 1.12 + 0.12 * max(0, swing) / 0.33))
        rig.hand(tt, "R", (x_run - 0.27, y + swing, 1.12 + 0.12 * max(0, -swing) / 0.33))
        rig.look_at(tt, (0, 1.0, 1.5))
        tt += 0.05
    # bound
    rig.foot(-0.55, "L", (x_run + 0.08, 22.25, 0.14), 0, -50)
    rig.foot(-0.42, "L", (x_run + 0.12, 21.25, 0.62), -20, 20)   # front knee drives up
    rig.foot(-0.42, "R", (x_run - 0.05, 22.05, 0.30), -10, -60)
    rig.root(-0.42, (x_run, 21.25, 0.16), -35)
    rig.spine(-0.42, lean=0, twist=-10)
    rig.hand(-0.45, "L", (x_run + 0.20, 21.15, 1.62))
    rig.hand(-0.45, "R", (x_run - 0.05, 21.35, 1.55))
    # back (right) foot lands side-on; front arm high
    rig.foot(-0.25, "R", (0.24, 20.45, 0.085), -90, 0)
    rig.foot(-0.25, "L", (0.30, 19.85, 0.42), -60, 10)
    rig.root(-0.25, (0.30, 20.55, -0.06), -82)
    rig.spine(-0.25, lean=-8, twist=-8, side=-6)
    rig.hand(-0.25, "L", (0.32, 19.85, 2.12))
    # front (left) foot lands behind the popping crease, leg braced
    rig.foot(-0.08, "L", (0.10, 19.10, 0.085), -25, 0)
    rig.root(-0.08, (0.25, 19.80, -0.12), -55)
    rig.spine(-0.08, lean=5, twist=-14, side=-4)
    rig.hand(-0.08, "L", (0.55, 19.60, 1.45))
    rig.foot(0.0, "L", (0.10, 19.10, 0.085), -25, 0)
    rig.root(0.0, (0.20, 19.10, 0.03), -20)
    rig.spine(0.0, lean=22, twist=6)
    rig.hand(0.0, "L", (0.62, 19.75, 1.00))
    # bowling arm: low behind -> up -> over the top -> release -> across to the left hip
    arm_path = [(-0.25, (0.50, 21.15, 0.95)), (-0.17, (0.62, 20.95, 1.35)), (-0.10, (0.55, 20.55, 1.95)),
                (-0.05, (0.42, 19.95, 2.30)), (0.0, tuple(RELEASE)), (0.06, (0.36, 18.35, 1.55)),
                (0.15, (0.66, 18.65, 0.88)), (0.30, (0.62, 18.30, 0.95))]
    for tk, pos in arm_path:
        rig.hand(tk, "R", pos)
    # back leg: toe drags, then swings through; follow-through and run-off
    rig.foot(-0.04, "R", (0.24, 20.40, 0.10), -90, -25)
    rig.foot(0.02, "R", (0.24, 20.25, 0.14), -70, -60)
    rig.foot(0.14, "R", (0.30, 19.30, 0.40), -20, -30)
    rig.foot(0.30, "R", (0.40, 18.05, 0.085), 0, 0)
    rig.root(0.14, (0.24, 18.62, -0.08), 10)
    rig.spine(0.14, lean=42, twist=18)
    rig.foot(0.30, "L", (0.10, 19.10, 0.10), -10, -35)
    rig.root(0.32, (0.36, 18.10, -0.06), 25)
    rig.spine(0.32, lean=30, twist=10)
    run_off = [(0.30, "R", (0.40, 18.05)), (0.60, "L", (0.72, 16.75)), (0.92, "R", (1.00, 15.70)),
               (1.28, "L", (1.22, 14.95)), (1.70, "R", (1.38, 14.50)), (2.10, "L", (1.50, 14.35))]
    for (ta, sa, pa), (tb, sb, pb) in zip(run_off, run_off[1:]):
        rig.foot(tb, sb, (pb[0], pb[1], 0.085), 15, 0)
        rig.foot(ta + 0.12, sb, (pb[0] - 0.05, pb[1] + 1.0, 0.30), 15, -50)
    for tr, pos, fc, lean in ((0.62, (0.62, 16.95, -0.05), 20, 20), (0.95, (0.95, 15.90, -0.04), 15, 12),
                              (1.30, (1.18, 15.10, -0.02), 25, 8), (1.75, (1.36, 14.60, 0.0), 40, 5),
                              (T1, (1.45, 14.45, 0.0), 55, 4)):
        rig.root(tr, pos, fc)
        rig.spine(tr, lean=lean)
    for tr, l_, r_ in ((0.62, (0.95, 17.2, 1.05), (0.35, 16.7, 1.0)), (0.95, (1.25, 15.6, 1.0), (0.7, 16.1, 1.05)),
                       (1.40, (1.50, 15.2, 1.0), (1.0, 14.9, 1.0)), (T1, (1.62, 14.55, 0.95), (1.20, 14.40, 0.95))):
        rig.hand(tr, "L", l_)
        rig.hand(tr, "R", r_)
    # head: the batter until release, then the ball
    for tk in (-0.6, -0.3, 0.0):
        rig.look_at(tk, (0, 1.2, 1.4))
    for k in range(0, 29):
        tk = 0.1 * k
        rig.look_at(tk + 0.1, ball_at(tk + 0.1))
    # ball in the hand until release (follows the hand target), then physics
    for f in range(F(T0), F(T1) + 1):
        tk = T0 + (f - 1) / FPS
        if tk < 0:
            continue
        ball.location = ball_at(tk)
        ball.keyframe_insert("location", frame=f)
    hand_tgt = rig.tgt["hand.R"]
    c = ball.constraints.new("COPY_LOCATION")
    c.target = hand_tgt
    c.influence = 1.0
    c.keyframe_insert("influence", frame=F(-0.01))
    c.influence = 0.0
    c.keyframe_insert("influence", frame=F(0.0))


# ------------------------------------------------------------------ animation: batter

def animate_batter(rig, bat):
    """Right-handed batter: still head in the stance, small trigger movement,
    high backlift, front foot to the pitch of the ball with the weight going
    forward, bat coming down straight to meet the ball (exact physics contact
    point and time), high follow-through, balance held, head following the
    ball to the boundary."""
    bowler_head = (0.3, 19.6, 1.9)
    feet = [  # t, side, pos, yaw, pitch
        (T0, "L", (-0.06, 1.17, 0.085), 90, 0), (T0, "R", (-0.06, 0.74, 0.085), 90, 0),
        (-0.25, "R", (-0.06, 0.74, 0.085), 90, 0), (-0.10, "R", (-0.01, 0.70, 0.085), 85, 0),
        (-0.02, "L", (-0.03, 1.20, 0.085), 92, 0), (0.06, "L", (-0.02, 1.32, 0.13), 100, -25),
        (0.16, "L", (0.06, 1.78, 0.22), 110, 5), (0.27, "L", (0.16, 2.28, 0.085), 122, 0),
        (0.32, "R", (-0.02, 0.72, 0.085), 85, 0), (TC, "R", (-0.05, 0.82, 0.12), 95, -35),
        (TC + 0.22, "R", (-0.02, 1.02, 0.13), 110, -55), (2.0, "R", (-0.02, 1.02, 0.12), 110, -50),
        (T1, "R", (0.04, 1.30, 0.085), 115, 0), (T1, "L", (0.16, 2.28, 0.085), 122, 0),
    ]
    for t, s, pos, yaw, pitch in feet:
        rig.foot(t, s, pos, yaw, pitch)
    roots = [(T0, (-0.17, 0.97, -0.075), 90), (-0.25, (-0.17, 0.97, -0.075), 90), (-0.10, (-0.13, 0.95, -0.08), 92),
             (0.05, (-0.12, 1.02, -0.08), 95), (0.16, (-0.07, 1.40, -0.11), 100), (0.27, (-0.01, 1.76, -0.17), 108),
             (TC, (0.05, 1.92, -0.20), 115), (TC + 0.18, (0.07, 2.0, -0.15), 122), (1.2, (0.07, 1.98, -0.10), 124),
             (T1, (0.08, 1.85, -0.03), 120)]
    for t, loc, fc in roots:
        rig.root(t, loc, fc)
    for t, lean, tw in ((T0, 14, 0), (-0.25, 14, 0), (0.05, 12, -4), (0.27, 22, 2), (TC, 27, 8),
                        (TC + 0.25, 10, 22), (1.2, 8, 24), (T1, 6, 14)):
        rig.spine(t, lean=lean, twist=tw)
    for t in (T0, -0.8, -0.3, 0.0, 0.25):
        rig.look_at(t, bowler_head if t < 0 else ball_at(t + 0.05))
    for k in range(0, 24):
        t = TC - 0.15 + 0.1 * k
        rig.look_at(t, ball_at(min(t + 0.05, 2.5)) + Vector((0, 0, 0.3 if t > 0.8 else 0.0)))
    # the bat: grounded, tap, backlift, downswing, contact, follow-through
    swing_dir = Vector((0.45, 0.89, 0.0)).normalized()          # towards extra cover
    d_c = Vector((0.13, 0.22, -0.94)).normalized()              # blade direction at contact
    top_c = CP - d_c * 0.80                                     # middle of the blade on the ball
    face_c = Vector((0.75, 0.6, 0.0)).normalized()
    lat = swing_dir.cross(Vector((0, 0, 1))).normalized()

    def bat_dir(theta):
        th = math.radians(theta)
        return (-math.cos(th) * Vector((0, 0, 1)) - math.sin(th) * swing_dir).normalized()

    def face_for(d, blend=0.0):
        f = lat.cross(d).normalized()
        return f.lerp(face_c, blend).normalized()

    grounded = (Vector((0.03, 0.93, 0.74)), (Vector((0.10, 0.80, 0.02)) - Vector((0.03, 0.93, 0.74))).normalized())
    for t in (T0, -0.80, -0.62):
        key_bat(bat, t, grounded[0], grounded[1], (1, 0, 0))
    key_bat(bat, -0.71, grounded[0] + Vector((0, 0, 0.05)), grounded[1], (1, 0, 0))  # tap
    swing = [  # t, hands (top of handle), theta (deg), face blend
        (-0.30, (0.02, 0.92, 0.80), 25, 0.0), (-0.12, (0.0, 0.84, 1.00), 110, 0.0), (0.05, (-0.02, 0.80, 1.08), 150, 0.0),
        (0.20, (-0.02, 0.92, 1.12), 142, 0.0), (0.30, (0.00, 1.32, 1.10), 95, 0.2), (0.37, (0.04, 1.72, 0.97), 38, 0.6),
    ]
    for t, hands, th, bl in swing:
        d = bat_dir(th)
        key_bat(bat, t, Vector(hands), d, face_for(d, bl))
    key_bat(bat, TC, top_c, d_c, face_c)
    for t, hands, th in ((TC + 0.06, (0.16, 2.24, 0.88), -70), (TC + 0.14, (0.22, 2.24, 1.25), -130),
                         (TC + 0.28, (0.25, 1.98, 1.52), -215), (1.4, (0.24, 1.96, 1.50), -222),
                         (T1, (0.30, 1.85, 0.95), -20)):
        d = bat_dir(th)
        key_bat(bat, t, Vector(hands), d, face_for(d, 0.3))


# ------------------------------------------------------------------ other players

def animate_nonstriker(rig, bat):
    """Backs up as the bowler delivers, watches the drive, jogs a few steps,
    pulls up when it is clearly four."""
    x = -1.05
    steps = [(T0, "L", (x + 0.1, 20.15)), (T0, "R", (x - 0.1, 20.45)), (-0.30, "L", (x + 0.1, 20.15)),
             (-0.12, "R", (x - 0.1, 19.55)), (0.12, "L", (x + 0.1, 18.95)), (0.36, "R", (x - 0.1, 18.40)),
             (0.62, "L", (x + 0.1, 18.10)), (0.95, "R", (x - 0.1, 17.10)), (1.25, "L", (x + 0.1, 16.30)),
             (1.60, "R", (x - 0.05, 16.10)), (T1, "L", (x + 0.12, 16.20))]
    prev = {}
    for t, s, (px, py) in steps:
        if s in prev and t - prev[s][0] > 0.15:
            pt, (qx, qy) = prev[s]
            rig.foot(t - 0.14, s, ((px + qx) / 2, (py + qy) / 2, 0.24), 0, -30)
        rig.foot(t, s, (px, py, 0.085), 0, 0)
        prev[s] = (t, (px, py))
    for t, y, z, fc in ((T0, 20.30, -0.04, 0), (-0.3, 20.30, -0.04, 0), (0.0, 19.70, -0.03, 0), (0.36, 18.70, -0.03, 0),
                        (0.62, 18.30, -0.04, 10), (0.95, 17.40, -0.03, 5), (1.25, 16.70, -0.03, 10),
                        (1.60, 16.25, -0.02, 30), (T1, 16.20, -0.02, 40)):
        rig.root(t, (x, y, z), fc)
        rig.spine(t, lean=10 if t < 1.6 else 4)
    # bat in the right hand, the left hand swinging
    for t, top, toe in ((T0, (x - 0.25, 19.6, 0.80), (x - 0.25, 18.95, 0.02)),
                        (-0.12, (x - 0.25, 19.5, 0.82), (x - 0.25, 18.85, 0.05)),
                        (0.36, (x - 0.28, 18.4, 0.85), (x - 0.30, 17.6, 0.25)),
                        (0.95, (x - 0.28, 17.2, 0.88), (x - 0.30, 16.4, 0.30)),
                        (1.60, (x - 0.30, 16.15, 0.82), (x - 0.35, 15.6, 0.10)),
                        (T1, (x - 0.30, 16.2, 0.80), (x - 0.35, 15.65, 0.05))):
        key_bat(bat, t, Vector(top), (Vector(toe) - Vector(top)).normalized(), (0, -1, 0))
    for t, y in ((T0, 20.4), (0.0, 19.9), (0.36, 18.9), (0.95, 17.6), (1.6, 16.4), (T1, 16.4)):
        rig.hand(t, "L", (x + 0.30, y, 1.0))
    for t in (T0, -0.2):
        rig.look_at(t, (0.3, 20.0, 1.5))
    for k in range(0, 26):
        t = 0.1 * k
        rig.look_at(t, ball_at(t))


def animate_keeper(rig):
    """Standing back to the fast bowler: crouch, rise with the ball, gloves
    together, then stand and turn to watch the drive."""
    x, y = 0.35, -15.0
    for s, dx in (("L", 0.30), ("R", -0.30)):
        rig.foot(T0, s, (x + dx, y, 0.085), 180 + (15 if s == "L" else -15), 0)
        rig.foot(0.50, s, (x + dx, y, 0.085), 180 + (15 if s == "L" else -15), 0)
    rig.foot(0.80, "L", (x + 0.45, y + 0.15, 0.085), 150, 0)
    rig.foot(1.05, "R", (x + 0.10, y + 0.20, 0.085), 150, 0)
    rig.foot(T1, "L", (x + 0.45, y + 0.15, 0.085), 150, 0)
    rig.foot(T1, "R", (x + 0.10, y + 0.20, 0.085), 150, 0)
    for t, z, lean in ((T0, -0.42, 32), (-0.15, -0.42, 32), (0.15, -0.30, 25), (0.40, -0.18, 20), (0.75, -0.05, 8),
                       (T1, -0.02, 4)):
        rig.root(t, (x, y, z), 180 if t < 0.6 else 150)
        rig.spine(t, lean=lean)
    for t, lz, h in ((T0, 0.50, 0.12), (-0.15, 0.50, 0.12), (0.30, 0.95, 0.06), (0.55, 1.05, 0.08)):
        rig.hand(t, "L", (x + h, y + 0.40, lz))
        rig.hand(t, "R", (x - h, y + 0.40, lz))
    rig.hand(1.0, "L", (x + 0.40, y + 0.10, 0.95))
    rig.hand(1.0, "R", (x - 0.25, y + 0.20, 0.95))
    for t in (T0, 0.0, 0.3):
        rig.look_at(t, ball_at(max(0, t)) if t >= 0 else (0.3, 19.0, 2.0))
    for k in range(4, 27):
        t = 0.1 * k
        rig.look_at(t, ball_at(t))


def animate_umpire(rig):
    """Behind the stumps at the bowler's end, slightly crouched, hands on the
    thighs; stands, watches the ball, then signals four (arm waved across)."""
    x, y = -0.08, 22.35
    for s, dx in (("L", 0.16), ("R", -0.16)):
        rig.foot(T0, s, (x + dx, y, 0.085), 0, 0)
        rig.foot(T1, s, (x + dx, y, 0.085), 0, 0)
    for t, z, lean in ((T0, -0.06, 20), (0.0, -0.06, 22), (0.6, -0.01, 5), (T1, 0.0, 3)):
        rig.root(t, (x, y, z), 0)
        rig.spine(t, lean=lean)
    for t in (T0, 0.5):
        rig.hand(t, "L", (x + 0.17, y - 0.12, 0.72))
        rig.hand(t, "R", (x - 0.17, y - 0.12, 0.72))
    for tt in (1.0, 3.6):
        rig.hand(tt, "L", (x + 0.22, y - 0.05, 0.9))
        rig.hand(tt, "R", (x - 0.22, y - 0.05, 0.9))
    # four: right arm waved across the body, several times (after the ball
    # reaches the rope at about 4.2 s)
    t = 3.75
    for k in range(8):
        rig.hand(t + 0.15 * k, "R", (x + (0.32 if k % 2 else -0.48), y - 0.38, 1.36))
    rig.hand(T1, "R", (x - 0.22, y - 0.05, 0.9))
    rig.hand(T1, "L", (x + 0.22, y - 0.05, 0.9))
    for t in (T0, 0.0):
        rig.look_at(t, (0, 1.0, 1.0))
    for k in range(1, 16):
        rig.look_at(0.1 * k, ball_at(0.1 * k))
    rig.look_at(3.6, (0.0, 0.0, 1.5))
    rig.look_at(T1, (0.0, 0.0, 1.5))


# ------------------------------------------------------------------ scene

def make_ball():
    m, n, l = nodes("ball_leather")
    b = n["Principled BSDF"]
    b.inputs["Base Color"].default_value = (0.95, 0.93, 0.9, 1)  # white ball (T20)
    b.inputs["Roughness"].default_value = 0.35
    b.inputs["Coat Weight"].default_value = 0.4
    bpy.ops.mesh.primitive_uv_sphere_add(radius=0.036, segments=32, ring_count=16)
    ball = bpy.context.active_object
    ball.name = "ball"
    bpy.ops.object.shade_smooth()
    ball.data.materials.append(m)
    seam = flat("seam", (0.15, 0.1, 0.08), 0.6)
    bpy.ops.mesh.primitive_torus_add(major_radius=0.0362, minor_radius=0.0016, major_segments=48, minor_segments=6)
    s = bpy.context.active_object
    s.data.materials.append(seam)
    s.parent = ball
    return ball


CAMS = {
    "broadcast": dict(loc=(0.6, 62.0, 9.8), at=(0.0, 9.0, 0.6), lens=115),
    "bowler_side": dict(loc=(4.6, 0.0, 1.7), track="bowler", lens=55),
    "batter_front": dict(loc=(4.6, 5.2, 1.15), at=(0.05, 1.75, 0.95), lens=62),
    "chase": dict(loc=(0, 0, 0), track="chase", lens=35),
    "umpire": dict(loc=(-1.3, 16.8, 1.6), at=(-0.08, 22.3, 1.3), lens=55),
    "hero": dict(loc=(2.8, 3.8, 1.0), at=(0.05, 1.9, 1.05), lens=50),
}


def make_camera(name, P, ball):
    c = CAMS[name]
    cam = bpy.data.objects.new("cam_" + name, bpy.data.cameras.new("cam_" + name))
    bpy.context.scene.collection.objects.link(cam)
    cam.data.lens = c["lens"]
    cam.data.clip_end = 600
    cam.location = c["loc"]
    if c.get("track") == "bowler":
        # a tracking camera beside the run-up, level with the bowler
        for t in [T0 + 0.05 * k for k in range(int((T1 - T0) / 0.05) + 1)]:
            P["bowler"].arm.animation_data.action.fcurves  # ensure animated
        for t, y in ((T0, 28.0), (-0.6, 22.0), (-0.25, 20.3), (0.0, 19.4), (0.3, 18.6), (1.0, 17.0)):
            cam.location = (c["loc"][0], y - 4.2, c["loc"][2])
            cam.keyframe_insert("location", frame=F(t))
        tgt = C.empty("cam_target_bowler", (0, 0, 0))
        con = tgt.constraints.new("COPY_LOCATION")
        con.target = P["bowler"].arm
        con2 = tgt.constraints.new("TRANSFORM")
        tgt.location = (0, 0, 1.25)
        tr = cam.constraints.new("TRACK_TO")  # keeps the horizon level
        tr.target = tgt
        tr.track_axis = "TRACK_NEGATIVE_Z"
        tr.up_axis = "UP_Y"
        tgt.constraints.remove(con2)
        con.use_offset = True
    elif c.get("track") == "chase":
        # 'ball cam': runs a few metres behind the ball to the rope
        tgt = C.empty("cam_target_ball", (0, 0, 0))
        con = tgt.constraints.new("COPY_LOCATION")
        con.target = ball
        for k in range(0, 100):
            t = TC + 0.05 * k
            p, q = ball_at(t), ball_at(max(TC, t - 0.25))
            d = Vector((p.x - q.x, p.y - q.y, 0))
            d = d.normalized() if d.length > 1e-3 else Vector((0.55, 0.83, 0))
            cam.location = p - d * 5.5 + Vector((0, 0, 1.4))
            cam.keyframe_insert("location", frame=F(t))
        tr = cam.constraints.new("TRACK_TO")
        tr.target = tgt
        tr.track_axis = "TRACK_NEGATIVE_Z"
        tr.up_axis = "UP_Y"
    else:
        d = Vector(c["at"]) - Vector(c["loc"])
        cam.rotation_euler = d.to_track_quat("-Z", "Y").to_euler()
    bpy.context.scene.camera = cam
    if name in ("batter_front", "hero", "umpire", "bowler_side"):
        cam.data.dof.use_dof = True
        cam.data.dof.aperture_fstop = 4.0
        cam.data.dof.focus_distance = (Vector(c["loc"]) - Vector(c.get("at", (0.3, 19.5, 1.2)))).length
    return cam


def setup_render(w=960, h=540, samples=24):
    sc = bpy.context.scene
    sc.render.engine = "CYCLES"
    sc.cycles.device = "CPU"
    sc.cycles.samples = samples
    sc.cycles.use_denoising = False
    sc.cycles.use_adaptive_sampling = True
    sc.cycles.max_bounces = 6
    sc.render.resolution_x, sc.render.resolution_y = w, h
    sc.render.fps = 25
    sc.frame_start, sc.frame_end = 1, F(T1)
    sc.view_settings.view_transform = "AgX"
    sc.view_settings.look = "AgX - Punchy"
    sc.view_settings.exposure = -0.45
    sc.render.film_transparent = False


def build():
    bpy.ops.wm.read_factory_settings(use_empty=True)
    build_ground()
    sky_and_sun()
    P, R = make_players()
    bat = E.bat_object()
    ns_bat = E.bat_object()
    ns_bat.name = "nonstriker_bat"
    ball = make_ball()
    animate_bowler(R["bowler"], ball)
    animate_batter(R["batter"], bat)
    hands_on_bat(R["batter"], bat)
    animate_nonstriker(R["nonstriker"], ns_bat)
    hands_on_bat(R["nonstriker"], ns_bat, one_hand=True)
    animate_keeper(R["keeper"])
    animate_umpire(R["umpire"])
    # smooth curves everywhere (auto-clamped Bezier: no overshoot)
    for ob in bpy.data.objects:
        ad = ob.animation_data
        if ad and ad.action:
            for fc in ad.action.fcurves:
                for kp in fc.keyframe_points:
                    kp.interpolation = "BEZIER"
                    kp.handle_left_type = kp.handle_right_type = "AUTO_CLAMPED"
    return P, R, ball


SHOTS = {
    # name: (camera, t_start, t_end, speed)   speed 0.25 = 4x slow motion
    "1_broadcast": ("broadcast", -1.70, -0.30, 1.0),
    "2_bowler_slowmo": ("bowler_side", -0.42, 0.14, 0.5),
    "3_drive_slowmo": ("batter_front", 0.10, 0.80, 0.25),
    "4_chase": ("chase", 2.30, 4.25, 1.0),
    "5_umpire": ("umpire", 3.55, 4.75, 1.0),
}


def main():
    argv = sys.argv[sys.argv.index("--") + 1:]
    mode = argv[0]
    if mode == "shot":
        name, out = argv[1], argv[2]
        cam_name, ta, tb, speed = SHOTS[name]
        P, R, ball = build()
        make_camera(cam_name, P, ball)
        w, h = (int(v) for v in (argv[3] if len(argv) > 3 else "960x540").split("x"))
        setup_render(w, h, int(argv[4]) if len(argv) > 4 else 24)
        os.makedirs(out, exist_ok=True)
        step = speed * FPS / 25.0
        k, f = 0, float(F(ta))
        sc = bpy.context.scene
        while f <= F(tb) + 1e-6:
            sc.frame_set(int(round(f)))
            sc.render.filepath = os.path.join(out, "%s_%04d.png" % (name, k))
            bpy.ops.render.render(write_still=True)
            k += 1
            f += step
    elif mode == "still":
        cam_name, t, out = argv[1], float(argv[2]), argv[3]
        P, R, ball = build()
        make_camera(cam_name, P, ball)
        w, h = (int(v) for v in (argv[4] if len(argv) > 4 else "1280x720").split("x"))
        setup_render(w, h, int(argv[5]) if len(argv) > 5 else 48)
        bpy.context.scene.frame_set(F(t))
        bpy.context.scene.render.filepath = out
        bpy.ops.render.render(write_still=True)
    elif mode == "portrait":
        # close-up of one player's head at time t, from the front
        who, t, out = argv[1], float(argv[2]), argv[3]
        P, R, ball = build()
        sc = bpy.context.scene
        sc.frame_set(F(t))
        dg = bpy.context.evaluated_depsgraph_get()
        arm = P[who].arm.evaluated_get(dg)
        hb = arm.pose.bones["head"]
        head = arm.matrix_world @ ((hb.head + hb.tail) / 2)
        fwd = (arm.matrix_world.to_3x3() @ hb.matrix.to_3x3() @ Vector((0, 0, 1))).normalized()
        side = fwd.cross(Vector((0, 0, 1))).normalized()
        cam = bpy.data.objects.new("cam_portrait", bpy.data.cameras.new("cam_portrait"))
        sc.collection.objects.link(cam)
        cam.location = head + fwd * 0.75 + side * 0.25 + Vector((0, 0, 0.04))
        cam.rotation_euler = (head - cam.location).to_track_quat("-Z", "Y").to_euler()
        cam.data.lens = 70
        cam.data.dof.use_dof = True
        cam.data.dof.focus_distance = (head - cam.location).length
        cam.data.dof.aperture_fstop = 2.8
        sc.camera = cam
        w, h = (int(v) for v in (argv[4] if len(argv) > 4 else "800x800").split("x"))
        setup_render(w, h, int(argv[5]) if len(argv) > 5 else 48)
        sc.render.filepath = out
        bpy.ops.render.render(write_still=True)
    elif mode == "save":
        build()
        bpy.ops.wm.save_as_mainfile(filepath=argv[1])


if __name__ == "__main__":
    main()
