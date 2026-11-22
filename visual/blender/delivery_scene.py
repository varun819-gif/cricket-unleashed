# One complete delivery in 3D: a right-arm fast bowler (Pakistan) bowls to a
# right-handed batter (India), who drives it through the covers for four.
# The ball's path is NOT hand-animated: it is the exact path computed by our
# C++ ball physics (visual/data/delivery_cover_drive.json, from
# `cricket_sim --seed 5 --record`). The players' movements are keyframed so
# that the bowler's hand is at the physics release point at release time and
# the bat's middle is at the physics contact point at contact time.
#
# Run (Blender 4.x, no GPU needed):
#   blender -b -P visual/blender/delivery_scene.py -- --still broadcast 0.0 out.png
#   blender -b -P visual/blender/delivery_scene.py -- --anim broadcast out_dir/
# Cameras: broadcast, batter, bowler, side.
import json
import math
import os
import sys

import bpy
from mathutils import Matrix, Quaternion, Vector

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import cricketer as C  # noqa: E402

HERE = os.path.dirname(os.path.abspath(__file__))
DATA = json.load(open(os.path.join(HERE, "..", "data", "delivery_cover_drive.json")))
FPS = 25
T0, T1 = -1.6, 2.6  # seconds around release (t = 0)
TC = DATA["contactTime"]


def frame(t):
    return int(round((t - T0) * FPS)) + 1


# ------------------------------------------------------------------ materials

def node_mat(name):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    return m, m.node_tree.nodes, m.node_tree.links


def grass_material():
    m, n, l = node_mat("grass")
    b = n["Principled BSDF"]
    tc = n.new("ShaderNodeTexCoord")
    wave = n.new("ShaderNodeTexWave")  # mowing stripes, 6 m wide, along the pitch
    wave.wave_type = "BANDS"
    wave.bands_direction = "X"
    wave.inputs["Scale"].default_value = 0.0833
    wave.inputs["Distortion"].default_value = 0.0
    wave.wave_profile = "SAW"
    noise = n.new("ShaderNodeTexNoise")
    noise.inputs["Scale"].default_value = 180.0
    noise.inputs["Detail"].default_value = 8.0
    mix = n.new("ShaderNodeMix")
    mix.data_type = "RGBA"
    stripe = n.new("ShaderNodeMath")
    stripe.operation = "GREATER_THAN"
    stripe.inputs[1].default_value = 0.5
    l.new(tc.outputs["Object"], wave.inputs["Vector"])
    l.new(wave.outputs["Fac"], stripe.inputs[0])
    l.new(stripe.outputs[0], mix.inputs["Factor"])
    mix.inputs["A"].default_value = (0.10, 0.24, 0.05, 1)
    mix.inputs["B"].default_value = (0.14, 0.32, 0.07, 1)
    mix2 = n.new("ShaderNodeMix")
    mix2.data_type = "RGBA"
    mix2.blend_type = "MULTIPLY"
    mix2.inputs["Factor"].default_value = 0.35
    l.new(mix.outputs["Result"], mix2.inputs["A"])
    l.new(noise.outputs["Color"], mix2.inputs["B"])
    l.new(mix2.outputs["Result"], b.inputs["Base Color"])
    bump = n.new("ShaderNodeBump")
    bump.inputs["Strength"].default_value = 0.25
    l.new(noise.outputs["Fac"], bump.inputs["Height"])
    l.new(bump.outputs["Normal"], b.inputs["Normal"])
    b.inputs["Roughness"].default_value = 0.85
    return m


def pitch_material():
    m, n, l = node_mat("pitch")
    b = n["Principled BSDF"]
    noise = n.new("ShaderNodeTexNoise")
    noise.inputs["Scale"].default_value = 25.0
    noise.inputs["Detail"].default_value = 6.0
    ramp = n.new("ShaderNodeValToRGB")
    ramp.color_ramp.elements[0].color = (0.30, 0.27, 0.17, 1)
    ramp.color_ramp.elements[1].color = (0.43, 0.39, 0.25, 1)
    l.new(noise.outputs["Fac"], ramp.inputs["Fac"])
    l.new(ramp.outputs["Color"], b.inputs["Base Color"])
    bump = n.new("ShaderNodeBump")
    bump.inputs["Strength"].default_value = 0.08
    l.new(noise.outputs["Fac"], bump.inputs["Height"])
    l.new(bump.outputs["Normal"], b.inputs["Normal"])
    b.inputs["Roughness"].default_value = 0.8
    return m


def flat(name, color, rough=0.5, metal=0.0, emit=0.0):
    m, n, _ = node_mat(name)
    b = n["Principled BSDF"]
    b.inputs["Base Color"].default_value = (*color, 1)
    b.inputs["Roughness"].default_value = rough
    b.inputs["Metallic"].default_value = metal
    if emit:
        b.inputs["Emission Color"].default_value = (*color, 1)
        b.inputs["Emission Strength"].default_value = emit
    return m


# ------------------------------------------------------------------ helpers

def add_obj(name, mesh_fn, mat=None, **kw):
    mesh_fn(**kw)
    o = bpy.context.active_object
    o.name = name
    if mat:
        o.data.materials.append(mat)
    return o


def box(name, size, loc, mat, bevel=0.0):
    o = add_obj(name, bpy.ops.mesh.primitive_cube_add, mat, size=1, location=loc)
    o.scale = size
    bpy.ops.object.transform_apply(scale=True)
    if bevel:
        mod = o.modifiers.new("bevel", "BEVEL")
        mod.width = bevel
        mod.segments = 3
    return o


def cyl(name, r, h, loc, mat, rot=(0, 0, 0), verts=24):
    return add_obj(name, bpy.ops.mesh.primitive_cylinder_add, mat, radius=r, depth=h, location=loc, rotation=rot,
                   vertices=verts)


def bone_parent(obj, arm, bone):
    """Attach obj to a bone, keeping its current world position."""
    mw = obj.matrix_world.copy()
    obj.parent = arm
    obj.parent_type = "BONE"
    obj.parent_bone = bone
    obj.matrix_world = mw


def look_quat(direction, up=Vector((0, 0, 1))):
    return direction.to_track_quat("-Z", "Y") if abs(direction.normalized().dot(up)) > 0.99 else \
        direction.to_track_quat("-Z", "Y")


# ------------------------------------------------------------------ ground

def build_ground():
    mats = {"grass": grass_material(), "pitch": pitch_material(), "white": flat("white_paint", (0.9, 0.9, 0.88), 0.6),
            "stump": flat("stump", (0.85, 0.8, 0.65), 0.35), "rope": flat("rope", (0.8, 0.1, 0.1), 0.6)}
    bpy.ops.mesh.primitive_circle_add(vertices=128, radius=75, fill_type="NGON", location=(0, 10.06, 0))
    g = bpy.context.active_object
    g.name = "outfield"
    g.data.materials.append(mats["grass"])
    box("pitch", (3.05, 24.0, 0.01), (0, 10.06, 0.0), mats["pitch"])
    # Creases (Law 7): bowling crease through the stumps, popping crease 1.22 m
    # in front, return creases.
    for y0, sgn in ((0.0, 1), (20.12, -1)):
        box("bowling_crease", (2.64, 0.05, 0.002), (0, y0, 0.006), mats["white"])
        box("popping_crease", (3.66 * 1.4, 0.05, 0.002), (0, y0 + sgn * 1.22, 0.006), mats["white"])
        for x in (-1.32, 1.32):
            box("return_crease", (0.05, 2.44, 0.002), (x, y0 + sgn * 0.0, 0.006), mats["white"])
        # Stumps (Law 8): 71.1 cm high, the set 22.86 cm wide; two bails.
        for x in (-0.0968, 0.0, 0.0968):
            cyl("stump", 0.0175, 0.711, (x, y0, 0.3555), mats["stump"])
        for x in (-0.0484, 0.0484):
            cyl("bail", 0.007, 0.105, (x, y0, 0.718), mats["stump"], rot=(0, math.radians(90), 0), verts=12)
    bpy.ops.mesh.primitive_torus_add(major_radius=70, minor_radius=0.12, location=(0, 10.06, 0.12),
                                     major_segments=192, minor_segments=8)
    rope = bpy.context.active_object
    rope.name = "boundary_rope"
    rope.data.materials.append(mats["rope"])
    return mats


def sky_and_sun(sun_elev=32, sun_rot=210):
    sc = bpy.context.scene
    w = bpy.data.worlds.new("sky")
    sc.world = w
    w.use_nodes = True
    n = w.node_tree.nodes
    sky = n.new("ShaderNodeTexSky")
    sky.sky_type = "NISHITA"
    sky.sun_elevation = math.radians(sun_elev)
    sky.sun_rotation = math.radians(sun_rot)
    sky.air_density = 1.2
    sky.dust_density = 2.0
    w.node_tree.links.new(sky.outputs["Color"], n["Background"].inputs["Color"])
    n["Background"].inputs["Strength"].default_value = 0.12
    sun = bpy.data.objects.new("sun", bpy.data.lights.new("sun", "SUN"))
    sc.collection.objects.link(sun)
    sun.data.energy = 3.0
    sun.data.angle = math.radians(1.5)
    sun.rotation_euler = (math.radians(90 - sun_elev), 0, math.radians(sun_rot - 90))


# ------------------------------------------------------------------ equipment

def make_bat(mats):
    """Bat: origin at the top of the handle, blade along local -Z, face along
    local -Y. Total length 96.5 cm, blade 10.8 cm wide (Law 5)."""
    wood = flat("willow", (0.86, 0.74, 0.52), 0.45)
    grip = flat("grip", (0.1, 0.1, 0.12), 0.8)
    handle = cyl("bat_handle", 0.016, 0.31, (0, 0, -0.155), grip, verts=16)
    blade = box("bat_blade", (0.108, 0.045, 0.655), (0, 0.008, -0.31 - 0.3275), wood, bevel=0.012)
    # Thicker spine on the back of the blade.
    spine = box("bat_spine", (0.05, 0.02, 0.45), (0, 0.035, -0.31 - 0.38), wood, bevel=0.009)
    bat = bpy.data.objects.new("bat", None)
    bpy.context.collection.objects.link(bat)
    for p in (handle, blade, spine):
        p.parent = bat
    bat.rotation_mode = "QUATERNION"
    return bat


def bat_pose(top, toe, face):
    """Rotation for the bat: handle top -> toe direction, face direction."""
    down = (Vector(toe) - Vector(top)).normalized()
    f = Vector(face)
    f = (f - f.dot(down) * down).normalized()
    # Local axes: -Z = down, -Y = face, X = Y x Z.
    z = -down
    y = -f
    x = y.cross(z)
    m = Matrix((x, y, z)).transposed()
    return m.to_quaternion()


def batter_kit(arm, mats):
    pad = flat("pads", (0.95, 0.95, 0.93), 0.55)
    glove = flat("gloves", (0.95, 0.95, 0.95), 0.6)
    helmet_m = flat("helmet", (0.03, 0.12, 0.45), 0.25)
    grille_m = flat("grille", (0.7, 0.7, 0.72), 0.3, metal=0.9)
    pb = arm.pose.bones
    M = arm.matrix_world
    for side in ("L", "R"):
        knee = M @ pb["lowerleg01." + side].head
        ankle = M @ pb["foot." + side].head
        mid = (knee + ankle) / 2
        length = (knee - ankle).length + 0.12
        p = box("pad_" + side, (0.15, 0.10, length), mid + Vector((0, -0.06, 0.04)), pad, bevel=0.03)
        p.rotation_euler = (Vector((0, 0, 1)).rotation_difference(knee - ankle)).to_euler()
        bone_parent(p, arm, "lowerleg01." + side)
        hand = M @ pb["wrist." + side].tail
        g = add_obj("glove_" + side, bpy.ops.mesh.primitive_uv_sphere_add, glove, radius=0.06, location=hand)
        g.scale = (1.0, 0.8, 1.35)
        bone_parent(g, arm, "wrist." + side)
    head = M @ pb["head"].head
    top = M @ pb["head"].tail
    centre = head + (top - head) * 0.55
    h = add_obj("helmet", bpy.ops.mesh.primitive_uv_sphere_add, helmet_m, radius=0.135, location=centre)
    h.scale = (1.0, 1.08, 0.95)
    bone_parent(h, arm, "head")
    fwd = (M.to_3x3() @ Vector((0, -1, 0))).normalized()
    for k in range(5):
        z = -0.02 - 0.03 * k
        bar = cyl("grille", 0.004, 0.2, centre + fwd * 0.15 + Vector((0, 0, z)), grille_m, verts=8)
        bar.rotation_euler = (0, math.radians(90), math.atan2(fwd.y, fwd.x) + math.radians(90))
        bone_parent(bar, arm, "head")


def bowler_kit(arm, mats):
    cap_m = flat("cap", (0.0, 0.35, 0.12), 0.7)
    glass = flat("glasses", (0.02, 0.02, 0.02), 0.05, metal=0.6)
    pb = arm.pose.bones
    M = arm.matrix_world
    head = M @ pb["head"].head
    top = M @ pb["head"].tail
    centre = head + (top - head) * 0.62
    cap = add_obj("cap", bpy.ops.mesh.primitive_uv_sphere_add, cap_m, radius=0.112, location=centre)
    cap.scale = (1.0, 1.1, 0.8)
    bone_parent(cap, arm, "head")
    fwd = (M.to_3x3() @ Vector((0, -1, 0))).normalized()
    brim = box("cap_brim", (0.16, 0.10, 0.008), centre + fwd * 0.13 + Vector((0, 0, -0.03)), cap_m, bevel=0.004)
    bone_parent(brim, arm, "head")
    eyes = [M @ pb[b].head for b in ("eye.L", "eye.R") if b in pb]
    if len(eyes) == 2:
        mid = (eyes[0] + eyes[1]) / 2
        g = box("sunglasses", (0.14, 0.012, 0.035), mid + fwd * 0.02, glass, bevel=0.006)
        bone_parent(g, arm, "head")


def add_eyes(arm):
    eye_m = flat("eye", (0.9, 0.88, 0.85), 0.1)
    iris = flat("iris", (0.08, 0.05, 0.03), 0.1)
    pb = arm.pose.bones
    M = arm.matrix_world
    fwd = (M.to_3x3() @ Vector((0, -1, 0))).normalized()
    for b in ("eye.L", "eye.R"):
        if b not in pb:
            continue
        c = M @ pb[b].head
        e = add_obj("eyeball", bpy.ops.mesh.primitive_uv_sphere_add, eye_m, radius=0.0115, location=c)
        bone_parent(e, arm, "head")
        i = add_obj("iris", bpy.ops.mesh.primitive_uv_sphere_add, iris, radius=0.0055, location=c + fwd * 0.009)
        bone_parent(i, arm, "head")


# ------------------------------------------------------------------ rig control

class Rig:
    """IK handles for one player: feet, hands, knee/elbow poles, look target."""

    def __init__(self, arm, name):
        self.arm = arm
        self.t = {}
        for k in ("foot.L", "foot.R", "hand.L", "hand.R"):
            self.t[k] = C.empty(name + "_" + k, (0, 0, 0))
        pole = {}
        for k, loc in (("knee.L", (0.15, -1.5, 0.55)), ("knee.R", (-0.15, -1.5, 0.55)),
                       ("elbow.L", (0.6, 0.8, 1.0)), ("elbow.R", (-0.6, 0.8, 1.0))):
            e = C.empty(name + "_" + k, loc)
            e.parent = arm
            pole[k] = e
        self.pole = pole
        C.ik(arm, "lowerleg02.L", self.t["foot.L"], pole["knee.L"], 4)
        C.ik(arm, "lowerleg02.R", self.t["foot.R"], pole["knee.R"], 4)
        C.ik(arm, "lowerarm02.L", self.t["hand.L"], pole["elbow.L"], 4)
        C.ik(arm, "lowerarm02.R", self.t["hand.R"], pole["elbow.R"], 4)
        self.look = C.empty(name + "_look", (0, 0, 1.6))
        c = arm.pose.bones["head"].constraints.new("DAMPED_TRACK")
        c.target = self.look
        c.track_axis = "TRACK_Y"
        c.influence = 0.0  # head turning: to be tuned (bone axes)

    def key(self, t, root, facing, feet=None, hands=None, look=None, spine=(0, 0, 0)):
        f = frame(t)
        a = self.arm
        a.location = root
        a.rotation_euler = (0, 0, math.radians(facing))
        a.keyframe_insert("location", frame=f)
        a.keyframe_insert("rotation_euler", frame=f)
        for b in ("spine03", "spine02"):
            C.rot(a, b, *[s / 2 for s in spine])
            a.pose.bones[b].keyframe_insert("rotation_euler", frame=f)
        for k, v in (feet or {}).items():
            self.t["foot." + k].location = v
            self.t["foot." + k].keyframe_insert("location", frame=f)
        for k, v in (hands or {}).items():
            self.t["hand." + k].location = v
            self.t["hand." + k].keyframe_insert("location", frame=f)
        if look:
            self.look.location = look
            self.look.keyframe_insert("location", frame=f)


# ------------------------------------------------------------------ the players

def build_players(mats):
    batter, barm = C.build_human(
        "batter", [("asian-male-young", 1.0), ("universal-male-young-maxmuscle-averageweight", 0.35),
                   ("male-young-averagemuscle-averageweight-maxheight", 0.1)],
        skin=(0.42, 0.26, 0.16), shirt=(0.04, 0.22, 0.72), trousers=(0.04, 0.18, 0.55), shoes=(0.92, 0.92, 0.92))
    bowler, warm = C.build_human(
        "bowler", [("asian-male-young", 1.0), ("universal-male-young-maxmuscle-averageweight", 0.55),
                   ("male-young-averagemuscle-averageweight-maxheight", 0.45)],
        skin=(0.38, 0.24, 0.15), shirt=(0.0, 0.33, 0.12), trousers=(0.0, 0.27, 0.10), shoes=(0.92, 0.92, 0.92))
    for arm in (barm, warm):
        add_eyes(arm)
    batter_kit(barm, mats)
    bowler_kit(warm, mats)
    return Rig(barm, "bat"), Rig(warm, "bowl")


def animate_batter(rig, bat):
    """Right-handed batter: stance, backlift, front-foot stride, cover drive
    meeting the ball at the physics contact point, follow-through."""
    cp = Vector(DATA["contactPoint"])
    face_cover = Vector((0.75, 0.6, 0)).normalized()  # bat face towards extra cover
    bowler = (0.3, 19.5, 1.9)
    poses = [
        # t, root (pelvis), facing, left foot, right foot, bat top, bat toe, bat face
        (T0, (-0.16, 0.98, -0.07), 90, (-0.05, 1.18, 0.09), (-0.05, 0.72, 0.09),
         (0.02, 0.92, 0.72), (0.12, 0.98, 0.02), (1, 0, 0)),
        (-0.45, (-0.16, 0.98, -0.07), 90, (-0.05, 1.18, 0.09), (-0.05, 0.72, 0.09),
         (0.02, 0.92, 0.72), (0.12, 0.98, 0.02), (1, 0, 0)),
        # backlift as the bowler gathers
        (-0.12, (-0.16, 0.95, -0.06), 92, (-0.03, 1.22, 0.09), (-0.05, 0.72, 0.09),
         (-0.02, 0.78, 1.05), (-0.05, 0.35, 1.45), (1, 0, 0)),
        # front foot moving out to the pitch of the ball
        (0.20, (-0.05, 1.55, -0.12), 105, (0.10, 1.95, 0.16), (-0.05, 0.74, 0.09),
         (0.0, 1.30, 1.10), (-0.05, 0.85, 1.55), (0.9, 0.3, 0)),
        # contact: bat face to cover, the middle of the blade on the ball
        (TC, (0.04, 1.92, -0.20), 115, (0.16, 2.28, 0.09), (-0.08, 0.85, 0.09),
         (cp.x - 0.10, cp.y - 0.12, cp.z + 0.70), (cp.x + 0.03, cp.y + 0.10, cp.z - 0.24), tuple(face_cover)),
        # follow-through, bat high over the left shoulder
        (TC + 0.28, (0.04, 1.95, -0.14), 125, (0.16, 2.28, 0.09), (-0.05, 1.00, 0.12),
         (0.30, 2.05, 1.55), (0.0, 2.15, 2.30), (0.3, 0.9, 0.2)),
        (T1, (0.04, 1.95, -0.06), 130, (0.16, 2.28, 0.09), (0.0, 1.10, 0.09),
         (0.32, 2.00, 1.50), (0.02, 2.05, 2.28), (0.3, 0.9, 0.2)),
    ]
    for t, root, facing, lf, rf, top, toe, face in poses:
        rig.key(t, root, facing, feet={"L": lf, "R": rf}, look=bowler if t < TC + 0.1 else (30, 45, 1.0))
        q = bat_pose(top, toe, face)
        bat.location = top
        bat.rotation_quaternion = q
        f = frame(t)
        bat.keyframe_insert("location", frame=f)
        bat.keyframe_insert("rotation_quaternion", frame=f)
    # Hands on the handle: top hand (left) at the top, bottom hand 9 cm lower.
    for k, d in (("L", 0.05), ("R", 0.14)):
        h = rig.t["hand." + k]
        h.parent = bat
        h.location = (0, 0, -d)
        h.animation_data_clear()


def animate_bowler(rig):
    """Right-arm fast bowler: last strides of the run-up, bound, back-foot
    landing side-on, front foot behind the popping crease, release at the
    physics release point, follow-through."""
    rel = Vector(DATA["flight"][0][1:])  # (0.3, 18.7, 2.15) at t = 0
    batter = (0, 1.0, 1.0)
    keys = [
        # t, root, facing, L foot, R foot, L hand, R hand (bowling hand)
        (T0, (0.30, 28.6, -0.02), 0, (0.40, 28.2, 0.30), (0.20, 29.4, 0.09), (0.6, 28.4, 1.2), (0.1, 28.9, 0.95)),
        (-1.35, (0.30, 27.0, 0.0), 0, (0.40, 26.9, 0.09), (0.20, 27.6, 0.35), (0.6, 27.3, 1.0), (0.1, 26.6, 1.25)),
        (-1.10, (0.30, 25.4, -0.02), 0, (0.40, 25.8, 0.32), (0.20, 25.0, 0.09), (0.6, 25.0, 1.25), (0.1, 25.8, 0.95)),
        (-0.85, (0.30, 23.8, 0.0), 0, (0.40, 23.5, 0.09), (0.20, 24.3, 0.35), (0.6, 24.0, 1.0), (0.1, 23.4, 1.25)),
        # bound: both feet off, arms gather high
        (-0.55, (0.30, 22.1, 0.25), -40, (0.42, 21.8, 0.45), (0.20, 22.4, 0.30), (0.55, 21.6, 2.05), (0.15, 21.9, 1.55)),
        # back (right) foot lands side-on
        (-0.25, (0.28, 20.55, -0.05), -85, (0.25, 19.9, 0.40), (0.18, 20.45, 0.09), (0.20, 19.6, 2.10), (0.55, 21.1, 1.05)),
        # front (left) foot lands behind the popping crease; arm coming over
        (-0.08, (0.25, 19.70, -0.12), -60, (0.08, 19.05, 0.09), (0.20, 20.45, 0.12), (0.45, 19.4, 0.95), (0.55, 20.3, 2.05)),
        # release: bowling hand exactly at the physics release point
        (0.0, (0.20, 19.10, 0.02), -25, (0.08, 19.05, 0.09), (0.25, 20.15, 0.25), (0.55, 19.5, 0.85), tuple(rel)),
        # follow-through: arm sweeps across, back leg comes through
        (0.25, (0.12, 18.75, -0.12), 25, (0.08, 19.05, 0.12), (0.05, 18.20, 0.35), (0.60, 19.3, 0.9), (-0.25, 18.4, 0.75)),
        (0.70, (0.30, 17.40, -0.02), 30, (0.45, 17.0, 0.30), (0.20, 17.7, 0.09), (0.6, 17.5, 1.0), (0.0, 17.3, 1.0)),
        (1.30, (0.70, 16.20, 0.0), 20, (0.85, 16.0, 0.09), (0.55, 16.5, 0.30), (1.0, 16.4, 1.1), (0.4, 16.0, 1.1)),
        (T1, (1.20, 15.60, 0.0), 10, (1.35, 15.5, 0.09), (1.05, 15.8, 0.09), (1.5, 15.7, 1.0), (0.9, 15.5, 1.0)),
    ]
    lean = {-0.08: (-5, 0, 0), 0.0: (-12, 0, 0), 0.25: (-25, 0, 0)}
    for t, root, facing, lf, rf, lh, rh in keys:
        rig.key(t, root, facing, feet={"L": lf, "R": rf}, hands={"L": lh, "R": rh}, look=batter,
                spine=lean.get(t, (0, 0, 0)))
    # The bowling arm stays straight over the top: elbow pole behind.
    rig.pole["elbow.R"].location = (-0.2, 1.0, 2.2)


def make_ball():
    leather = flat("ball_leather", (0.55, 0.03, 0.03), 0.35)
    ball = add_obj("ball", bpy.ops.mesh.primitive_uv_sphere_add, leather, radius=0.036, location=(0, 0, 0),
                   segments=32, ring_count=16)
    bpy.ops.object.shade_smooth()
    return ball


def animate_ball(ball, bowler_rig):
    # In the hand until release, then the physics path sample by sample.
    path = DATA["flight"] + DATA["afterShot"][1:]
    hand = bowler_rig.arm.pose.bones["wrist.R"]
    for f in range(1, frame(T1) + 1):
        t = T0 + (f - 1) / FPS
        if t < 0:
            continue
        # interpolate the physics samples at this time
        p = path[-1][1:]
        for a, b in zip(path, path[1:]):
            if a[0] <= t <= b[0]:
                u = (t - a[0]) / max(1e-9, b[0] - a[0])
                p = [a[i] + u * (b[i] - a[i]) for i in (1, 2, 3)]
                break
        ball.location = p
        ball.keyframe_insert("location", frame=f)
    # Before release the ball rides in the bowling hand.
    c = ball.constraints.new("CHILD_OF")
    c.target = bowler_rig.arm
    c.subtarget = "wrist.R"
    c.set_inverse_pending = False
    c.inverse_matrix = Matrix.Identity(4)
    c.influence = 1.0
    c.keyframe_insert("influence", frame=frame(-0.04))
    c.influence = 0.0
    c.keyframe_insert("influence", frame=frame(0.0))
    del hand


def ball_trail(mats):
    """A faint line along the physics path (for the broadcast still)."""
    path = [Vector(p[1:]) for p in DATA["flight"] + DATA["afterShot"][1:40]]
    curve = bpy.data.curves.new("trail", "CURVE")
    curve.dimensions = "3D"
    curve.bevel_depth = 0.012
    sp = curve.splines.new("POLY")
    sp.points.add(len(path) - 1)
    for i, p in enumerate(path):
        sp.points[i].co = (p.x, p.y, p.z, 1)
    o = bpy.data.objects.new("ball_trail", curve)
    bpy.context.collection.objects.link(o)
    o.data.materials.append(flat("trail", (1.0, 0.85, 0.2), 0.5, emit=2.0))
    return o


# ------------------------------------------------------------------ cameras

CAMS = {
    # name: (location, look at, focal length mm)
    "broadcast": ((0.5, 64.0, 10.0), (0.0, 8.5, 0.6), 125),
    "batter": ((4.2, 4.6, 1.25), (0.0, 1.5, 0.95), 60),
    "bowler": ((-4.6, 16.0, 1.6), (0.25, 19.8, 1.4), 55),
    "side": ((-16.0, 10.0, 2.2), (0.0, 10.0, 1.0), 28),
    "drive": ((6.5, -4.5, 1.5), (0.3, 3.0, 0.8), 45),
}


def set_camera(name):
    loc, at, lens = CAMS[name]
    cam = bpy.data.objects.new("cam_" + name, bpy.data.cameras.new("cam_" + name))
    bpy.context.scene.collection.objects.link(cam)
    cam.location = loc
    d = Vector(at) - Vector(loc)
    cam.rotation_euler = d.to_track_quat("-Z", "Y").to_euler()
    cam.data.lens = lens
    cam.data.clip_end = 500
    bpy.context.scene.camera = cam
    return cam


def setup_render(width=1280, height=720, samples=48):
    sc = bpy.context.scene
    sc.render.engine = "CYCLES"
    sc.cycles.device = "CPU"
    sc.cycles.samples = samples
    sc.cycles.use_denoising = False
    sc.cycles.use_adaptive_sampling = True
    sc.render.resolution_x = width
    sc.render.resolution_y = height
    sc.render.fps = FPS
    sc.frame_start = 1
    sc.frame_end = frame(T1)
    sc.view_settings.exposure = -0.6
    sc.view_settings.view_transform = "AgX" if "AgX" in [v.identifier for v in
        type(sc.view_settings).bl_rna.properties["view_transform"].enum_items] else "Filmic"


def build(trail=False):
    bpy.ops.wm.read_factory_settings(use_empty=True)
    mats = build_ground()
    sky_and_sun()
    bat_rig, bowl_rig = build_players(mats)
    bat = make_bat(mats)
    animate_batter(bat_rig, bat)
    animate_bowler(bowl_rig)
    ball = make_ball()
    animate_ball(ball, bowl_rig)
    if trail:
        ball_trail(mats)
    setup_render()


def main():
    lo = os.environ.get("CU_LOWRES")
    argv = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    mode = argv[0] if argv else "--still"
    if mode == "--still":
        cam, t, out = argv[1], float(argv[2]), argv[3]
        build(trail=len(argv) > 4 and argv[4] == "trail")
        set_camera(cam)
        if lo:
            sc = bpy.context.scene
            sc.render.resolution_x, sc.render.resolution_y, sc.cycles.samples = 640, 360, 12
        bpy.context.scene.frame_set(frame(t))
        bpy.context.scene.render.filepath = out
        bpy.ops.render.render(write_still=True)
    elif mode == "--anim":
        cam, out = argv[1], argv[2]
        build()
        set_camera(cam)
        sc = bpy.context.scene
        if len(argv) > 3:
            sc.render.resolution_x, sc.render.resolution_y = (int(v) for v in argv[3].split("x"))
        if len(argv) > 4:
            sc.cycles.samples = int(argv[4])
        rng = os.environ.get("CU_RANGE")  # e.g. "-0.3,1.0" seconds
        if rng:
            a, b = (float(v) for v in rng.split(","))
            sc.frame_start, sc.frame_end = frame(a), frame(b)
        sc.render.filepath = os.path.join(out, "f_")
        bpy.ops.render.render(animation=True)
    elif mode == "--save":
        build()
        bpy.ops.wm.save_as_mainfile(filepath=argv[1])


if __name__ == "__main__":
    main()
