# Cricket equipment modelled in code (all ours, free): helmet with grille and
# peak, bat (shaped blade with edges, spine and bow, oval handle with grip),
# leg pads with vertical bolsters, knee roll and straps, thigh guard, shoe
# soles with spikes, stumps with spikes and bail grooves, bails, caps, umpire
# hat, sunglasses and eyes. Sizes follow the Laws of Cricket where they set
# them (bat, ball, stumps, bails).
#
# Everything is built around a player in the rest pose (facing -y) and then
# fixed to the right bone, so it moves with the player.
import math

import bmesh
import bpy
from mathutils import Matrix, Vector


# ------------------------------------------------------------------ helpers

def mat(name, color, rough=0.5, metal=0.0, coat=0.0):
    m = bpy.data.materials.get(name)
    if m:
        return m
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    b = m.node_tree.nodes["Principled BSDF"]
    b.inputs["Base Color"].default_value = (*color, 1)
    b.inputs["Roughness"].default_value = rough
    b.inputs["Metallic"].default_value = metal
    if coat:
        b.inputs["Coat Weight"].default_value = coat
    return m


def mesh_obj(name, bm, material=None, smooth=True, parent=None):
    me = bpy.data.meshes.new(name)
    bm.to_mesh(me)
    bm.free()
    if smooth:
        for p in me.polygons:
            p.use_smooth = True
    ob = bpy.data.objects.new(name, me)
    bpy.context.collection.objects.link(ob)
    if material:
        me.materials.append(material)
    if parent:
        ob.parent = parent
    return ob


def bone_attach(obj, arm, bone):
    mw = obj.matrix_world.copy()
    obj.parent = arm
    obj.parent_type = "BONE"
    obj.parent_bone = bone
    obj.matrix_world = mw


def loft(rings, cap_ends=True):
    """rings: list of lists of Vectors (same count). Returns a bmesh."""
    bm = bmesh.new()
    vs = [[bm.verts.new(p) for p in r] for r in rings]
    n = len(rings[0])
    for a, b in zip(vs, vs[1:]):
        for i in range(n):
            bm.faces.new((a[i], a[(i + 1) % n], b[(i + 1) % n], b[i]))
    if cap_ends:
        bm.faces.new(list(reversed(vs[0])))
        bm.faces.new(vs[-1])
    bm.normal_update()
    return bm


def lathe(profile, segments=24, axis="Z"):
    """profile: [(radius, height)] from bottom to top -> solid of revolution."""
    rings = []
    for r, h in profile:
        ring = []
        for k in range(segments):
            a = 2 * math.pi * k / segments
            x, y = r * math.cos(a), r * math.sin(a)
            ring.append(Vector((x, y, h)) if axis == "Z" else Vector((h, x, y)))
        rings.append(ring)
    bm = loft(rings, cap_ends=True)
    return bm


def tube_curve(name, points, radius, material, parent=None):
    cu = bpy.data.curves.new(name, "CURVE")
    cu.dimensions = "3D"
    cu.bevel_depth = radius
    cu.bevel_resolution = 3
    sp = cu.splines.new("POLY")
    sp.points.add(len(points) - 1)
    for i, p in enumerate(points):
        sp.points[i].co = (p.x, p.y, p.z, 1)
    ob = bpy.data.objects.new(name, cu)
    bpy.context.collection.objects.link(ob)
    ob.data.materials.append(material)
    if parent:
        ob.parent = parent
    return ob


def group(name, children):
    g = bpy.data.objects.new(name, None)
    bpy.context.collection.objects.link(g)
    for c in children:
        c.parent = g
    return g


# ------------------------------------------------------------------ bat

def bat_object():
    """Bat with its origin at the top of the handle, blade along local -Z,
    face along local -Y (the spine is on +Y). 96.5 cm long, blade 10.8 cm
    wide (Law 5 maximums: 96.52 cm, 10.8 cm; edges 4 cm, depth 6.7 cm)."""
    willow = willow_material()
    grip_m = grip_material()
    blade_len, handle_len = 0.655, 0.31
    rings = []
    stations = 34
    for k in range(stations + 1):
        s = k / stations  # 0 = toe, 1 = top of the shoulders
        z = -handle_len - blade_len * (1 - s)
        dist = s * blade_len  # metres from the toe
        w = 0.108
        if dist < 0.025:  # rounded toe corners
            w = 0.108 - 0.03 * (1 - dist / 0.025) ** 2
        if s > 0.90:  # shoulders slope into the handle
            u = (s - 0.90) / 0.10
            w = 0.108 - (0.108 - 0.036) * (u ** 0.7)
        edge = 0.038 if s < 0.9 else 0.038 - 0.012 * (s - 0.9) / 0.1
        # spine (the 'middle') highest about a quarter of the way up
        peak = 0.064 if s < 0.9 else 0.064 - 0.03 * (s - 0.9) / 0.1
        spine = edge + (peak - edge) * math.exp(-((dist - 0.17) / 0.26) ** 2)
        bow = 0.006 * math.sin(math.pi * s)  # slight bow of the face
        ring = []
        n = 12
        for i in range(n + 1):  # front face, left to right
            x = -w / 2 + w * i / n
            ring.append(Vector((x, -bow, z)))
        for i in range(n, -1, -1):  # back, right to left (spine profile)
            x = -w / 2 + w * i / n
            t = min(1.0, abs(2 * x / w))
            ring.append(Vector((x, -bow + edge * 0.15 + (spine - edge * 0.15) * (1 - t ** 2) ** 0.8, z)))
        rings.append(ring)
    blade = mesh_obj("bat_blade", loft(rings), willow)
    m = blade.modifiers.new("smooth", "SUBSURF")
    m.levels = 1
    m.render_levels = 2
    # handle: slightly oval cane with a rubber grip (spiral ribs in the material)
    hb = lathe([(0.0, -handle_len - 0.03), (0.0145, -handle_len - 0.03), (0.0155, -handle_len + 0.02),
                (0.0165, -0.03), (0.0175, -0.012), (0.012, 0.0), (0.0, 0.0)], segments=20)
    handle = mesh_obj("bat_handle", hb, grip_m)
    handle.scale = (1.0, 1.12, 1.0)
    bat = group("bat", [blade, handle])
    bat.rotation_mode = "QUATERNION"
    return bat


def willow_material():
    m = bpy.data.materials.get("willow")
    if m:
        return m
    m = bpy.data.materials.new("willow")
    m.use_nodes = True
    n, l = m.node_tree.nodes, m.node_tree.links
    b = n["Principled BSDF"]
    tc = n.new("ShaderNodeTexCoord")
    mp = n.new("ShaderNodeMapping")
    mp.inputs["Scale"].default_value = (70.0, 70.0, 1.5)
    wave = n.new("ShaderNodeTexWave")  # straight grain along the blade
    wave.bands_direction = "X"
    wave.inputs["Scale"].default_value = 1.0
    wave.inputs["Distortion"].default_value = 2.0
    wave.inputs["Detail"].default_value = 4.0
    l.new(tc.outputs["Object"], mp.inputs["Vector"])
    l.new(mp.outputs["Vector"], wave.inputs["Vector"])
    ramp = n.new("ShaderNodeValToRGB")
    ramp.color_ramp.elements[0].color = (0.72, 0.56, 0.34, 1)
    ramp.color_ramp.elements[1].color = (0.88, 0.76, 0.55, 1)
    l.new(wave.outputs["Fac"], ramp.inputs["Fac"])
    l.new(ramp.outputs["Color"], b.inputs["Base Color"])
    b.inputs["Roughness"].default_value = 0.42
    b.inputs["Coat Weight"].default_value = 0.2
    return m


def grip_material():
    m = bpy.data.materials.get("bat_grip")
    if m:
        return m
    m = bpy.data.materials.new("bat_grip")
    m.use_nodes = True
    n, l = m.node_tree.nodes, m.node_tree.links
    b = n["Principled BSDF"]
    b.inputs["Base Color"].default_value = (0.05, 0.08, 0.2, 1)
    b.inputs["Roughness"].default_value = 0.75
    tc = n.new("ShaderNodeTexCoord")
    wave = n.new("ShaderNodeTexWave")
    wave.wave_type = "RINGS"
    wave.rings_direction = "SPHERICAL"
    wave.bands_direction = "DIAGONAL"
    wave.wave_type = "BANDS"
    wave.inputs["Scale"].default_value = 60.0
    l.new(tc.outputs["Object"], wave.inputs["Vector"])
    bump = n.new("ShaderNodeBump")
    bump.inputs["Strength"].default_value = 0.6
    l.new(wave.outputs["Fac"], bump.inputs["Height"])
    l.new(bump.outputs["Normal"], b.inputs["Normal"])
    return m


# ------------------------------------------------------------------ head gear

def skull_centre(p):
    return p.head_centre + Vector((0, 0.072, 0.035))


def helmet(p, shell_color=(0.03, 0.10, 0.40)):
    """Batting helmet: shell (with face opening and ear guards), peak, steel
    grille (horizontal and vertical bars), chin strap."""
    sc = skull_centre(p)
    shell_m = mat("helmet_shell", shell_color, 0.2, coat=0.8)
    steel = mat("grille_steel", (0.55, 0.56, 0.6), 0.25, metal=1.0)
    bm = bmesh.new()
    bmesh.ops.create_uvsphere(bm, u_segments=40, v_segments=24, radius=0.128)
    for v in bm.verts:
        v.co = Vector((v.co.x * 0.92, v.co.y * 1.08, v.co.z * 1.0))
    eye_rel = p.head_centre.z - sc.z
    kill = []
    for f in bm.faces:
        c = f.calc_center_median()
        front = c.y < -0.02
        if c.z < eye_rel + 0.035 and front and abs(c.x) < 0.085:
            kill.append(f)  # face opening
        elif c.z < -0.105 or (c.z < -0.06 and c.y < 0.0 and abs(c.x) < 0.10):
            kill.append(f)
    bmesh.ops.delete(bm, geom=kill, context="FACES")
    bmesh.ops.translate(bm, verts=bm.verts, vec=sc)
    shell = mesh_obj("helmet_shell", bm, shell_m)
    so = shell.modifiers.new("thick", "SOLIDIFY")
    so.thickness = 0.008
    so.offset = 1
    # peak
    bm = bmesh.new()
    rows = []
    for j in range(4):
        r = 0.115 + 0.018 * j
        row = []
        for k in range(15):
            a = math.radians(-60 + 120 * k / 14)
            row.append(Vector((r * math.sin(a) * 0.92, -r * math.cos(a) * 1.08, eye_rel + 0.045 - 0.012 * j)) + sc)
        rows.append([bm.verts.new(x) for x in row])
    for a_, b_ in zip(rows, rows[1:]):
        for k in range(14):
            bm.faces.new((a_[k], a_[k + 1], b_[k + 1], b_[k]))
    peak = mesh_obj("helmet_peak", bm, shell_m)
    pk = peak.modifiers.new("thick", "SOLIDIFY")
    pk.thickness = 0.004
    # grille: horizontal bars curving round the face, two vertical bars
    parts = [shell, peak]
    for j, dz in enumerate((-0.005, -0.033, -0.062, -0.09)):
        r = 0.142 - 0.004 * j
        pts = []
        for k in range(17):
            a = math.radians(-58 + 116 * k / 16)
            pts.append(sc + Vector((r * math.sin(a) * 0.9, -r * math.cos(a) * 1.02, eye_rel + dz)))
        parts.append(tube_curve("grille_bar", pts, 0.0032, steel))
    for x in (-0.03, 0.03):
        pts = [sc + Vector((x, -0.140 - 0.004 * t, eye_rel + 0.0 - 0.09 * t)) for t in (0.0, 0.33, 0.66, 1.0)]
        parts.append(tube_curve("grille_post", pts, 0.0032, steel))
    strap = mat("strap", (0.05, 0.05, 0.06), 0.8)
    pts = [sc + Vector((0.085 * math.cos(a), -0.055 + 0.055 * math.sin(a) * 0.0 - 0.04 * math.sin(a), -0.13))
           for a in [math.radians(x) for x in range(0, 181, 15)]]
    parts.append(tube_curve("chin_strap", pts, 0.004, strap))
    g = group("helmet", parts)
    bone_attach(g, p.arm, "head")
    return g


def cap(p, color, name="cap"):
    sc = skull_centre(p)
    m = mat(name + "_m", color, 0.75)
    bm = bmesh.new()
    bmesh.ops.create_uvsphere(bm, u_segments=32, v_segments=16, radius=0.118)
    for v in bm.verts:
        v.co = Vector((v.co.x * 0.93, v.co.y * 1.05, v.co.z * 0.85))
    bmesh.ops.delete(bm, geom=[f for f in bm.faces if f.calc_center_median().z < -0.005], context="FACES")
    bmesh.ops.translate(bm, verts=bm.verts, vec=sc + Vector((0, 0, 0.0)))
    dome = mesh_obj(name + "_dome", bm, m)
    so = dome.modifiers.new("thick", "SOLIDIFY")
    so.thickness = 0.004
    so.offset = 1
    # curved brim
    bm = bmesh.new()
    rows = []
    for j in range(4):
        row = []
        for k in range(13):
            u = -1 + 2 * k / 12
            x = u * 0.085
            y = -0.105 - 0.025 * j * (1 - 0.35 * u * u)
            z = -0.008 * j - 0.01 * u * u
            row.append(sc + Vector((x, y, z)))
        rows.append([bm.verts.new(v) for v in row])
    for a_, b_ in zip(rows, rows[1:]):
        for k in range(12):
            bm.faces.new((a_[k], a_[k + 1], b_[k + 1], b_[k]))
    brim = mesh_obj(name + "_brim", bm, m)
    bt = brim.modifiers.new("thick", "SOLIDIFY")
    bt.thickness = 0.004
    button = mesh_obj(name + "_button", lathe([(0, 0), (0.007, 0.0), (0.006, 0.004), (0, 0.005)], 12), m)
    button.location = sc + Vector((0, 0, 0.1))
    g = group(name, [dome, brim, button])
    bone_attach(g, p.arm, "head")
    return g


def umpire_hat(p):
    sc = skull_centre(p)
    m = mat("umpire_hat", (0.92, 0.9, 0.84), 0.8)
    crown = mesh_obj("hat_crown", lathe([(0.0, 0.0), (0.108, 0.0), (0.104, 0.06), (0.092, 0.095), (0.0, 0.105)], 32),
                     m)
    crown.location = sc + Vector((0, 0.0, -0.005))
    brim = mesh_obj("hat_brim", lathe([(0.10, 0.0), (0.20, -0.025), (0.20, -0.021), (0.10, 0.004)], 40), m)
    brim.location = sc + Vector((0, 0.0, 0.0))
    band = mesh_obj("hat_band", lathe([(0.1065, 0.0), (0.1062, 0.018), (0.0, 0.018)], 32),
                    mat("hat_band", (0.05, 0.05, 0.08), 0.6))
    band.location = sc + Vector((0, 0, 0.0))
    g = group("umpire_hat", [crown, brim, band])
    bone_attach(g, p.arm, "head")
    return g


def sunglasses(p):
    lens = mat("lens", (0.02, 0.03, 0.05), 0.05, metal=0.3, coat=1.0)
    frame_m = mat("glasses_frame", (0.02, 0.02, 0.02), 0.3)
    parts = []
    for e in p.eyes:
        bm = bmesh.new()
        bmesh.ops.create_uvsphere(bm, u_segments=16, v_segments=8, radius=0.025)
        for v in bm.verts:
            v.co = Vector((v.co.x * 1.15, v.co.y * 0.25, v.co.z * 0.75))
        bmesh.ops.translate(bm, verts=bm.verts, vec=e + Vector((0, -0.022, 0.0)))
        parts.append(mesh_obj("lens", bm, lens))
    a, b = p.eyes
    parts.append(tube_curve("glasses_bridge", [a + Vector((0, -0.026, 0.012)), b + Vector((0, -0.026, 0.012))],
                            0.002, frame_m))
    for e, sx in ((a, 1), (b, -1)):
        parts.append(tube_curve("glasses_arm", [e + Vector((sx * 0.028, -0.02, 0.008)),
                                                e + Vector((sx * 0.07, 0.06, 0.004))], 0.0018, frame_m))
    g = group("sunglasses", parts)
    bone_attach(g, p.arm, "head")
    return g


def eyes(p, iris=(0.12, 0.07, 0.03)):
    """Eyeballs (white, coloured iris, black pupil, glossy) that can turn to
    look at something (damped-track constraint added by the scene)."""
    m = bpy.data.materials.new("eye_" + p.name)
    m.use_nodes = True
    n, l = m.node_tree.nodes, m.node_tree.links
    b = n["Principled BSDF"]
    tc = n.new("ShaderNodeTexCoord")
    norm = n.new("ShaderNodeVectorMath")
    norm.operation = "NORMALIZE"
    dot = n.new("ShaderNodeVectorMath")
    dot.operation = "DOT_PRODUCT"
    dot.inputs[1].default_value = (0, -1, 0)
    l.new(tc.outputs["Object"], norm.inputs[0])
    l.new(norm.outputs["Vector"], dot.inputs[0])
    ramp = n.new("ShaderNodeValToRGB")
    cr = ramp.color_ramp
    cr.elements[0].position = 0.80
    cr.elements[0].color = (0.85, 0.82, 0.78, 1)
    cr.elements[1].position = 0.86
    cr.elements[1].color = (*iris, 1)
    e3 = cr.elements.new(0.955)
    e3.color = tuple(c * 0.6 for c in iris) + (1,)
    e4 = cr.elements.new(0.965)
    e4.color = (0.01, 0.01, 0.01, 1)
    l.new(dot.outputs["Value"], ramp.inputs["Fac"])
    l.new(ramp.outputs["Color"], b.inputs["Base Color"])
    b.inputs["Roughness"].default_value = 0.05
    b.inputs["Coat Weight"].default_value = 1.0
    out = []
    for e in p.eyes:
        bm = bmesh.new()
        bmesh.ops.create_uvsphere(bm, u_segments=24, v_segments=16, radius=0.0118)
        ob = mesh_obj("eyeball", bm, m)
        ob.location = e + Vector((0, 0.002, 0))
        bone_attach(ob, p.arm, "head")
        out.append(ob)
    return out


# ------------------------------------------------------------------ pads etc.

def _leg_frame(p, side):
    knee = p.arm.data.bones["lowerleg01." + side].head_local.copy()
    ankle = p.arm.data.bones["foot." + side].head_local.copy()
    up = (knee - ankle).normalized()
    fwd = Vector((0, -1, 0))
    fwd = (fwd - fwd.dot(up) * up).normalized()
    right = fwd.cross(up)
    return knee, ankle, up, fwd, right


def leg_pad(p, side, keeper=False, color=(0.96, 0.96, 0.94)):
    """Batting pad: curved front shell from the top of the foot to above the
    knee, vertical cane bolsters, knee roll, side wings and three straps."""
    knee, ankle, up, fwd, right = _leg_frame(p, side)
    pm = mat("pad_white", color, 0.55)
    strap_m = mat("pad_strap", (0.05, 0.08, 0.25), 0.7)
    outer = 1 if side == "L" else -1  # +x is the player's left
    bottom = ankle - up * 0.02
    top = knee + up * (0.12 if keeper else 0.19)
    length = (top - bottom).length
    radius = 0.072
    parts = []
    # shell: half-cylinder in front of the shin, flaring at the knee
    rings = []
    for j in range(16):
        t = j / 15
        centre = bottom + (top - bottom) * t
        r = radius * (1.0 + 0.18 * math.exp(-((t - 0.72) / 0.12) ** 2) + 0.12 * (t < 0.12) * (0.12 - t) / 0.12)
        ring = []
        for k in range(15):
            a = math.radians(-115 + 230 * k / 14)
            ring.append(centre + fwd * (r * math.cos(a)) + right * (r * math.sin(a) * 1.15))
        rings.append(ring)
    bm = loft(rings, cap_ends=False)
    shell = mesh_obj("pad_shell", bm, pm)
    so = shell.modifiers.new("thick", "SOLIDIFY")
    so.thickness = 0.012
    sub = shell.modifiers.new("smooth", "SUBSURF")
    sub.levels = 1
    parts.append(shell)
    # vertical bolsters (the raised 'canes')
    for k in range(7):
        a = math.radians(-60 + 20 * k)
        pts = []
        for j in range(9):
            t = 0.05 + 0.62 * j / 8
            centre = bottom + (top - bottom) * t
            r = radius + 0.012
            pts.append(centre + fwd * (r * math.cos(a)) + right * (r * math.sin(a) * 1.15))
        parts.append(tube_curve("pad_bolster", pts, 0.011, pm))
    # knee roll: three horizontal bolsters
    for t in (0.70, 0.78, 0.86):
        centre = bottom + (top - bottom) * t
        r = radius * 1.22 + 0.01
        pts = [centre + fwd * (r * math.cos(math.radians(a))) + right * (r * math.sin(math.radians(a)) * 1.15)
               for a in range(-95, 96, 10)]
        parts.append(tube_curve("pad_kneeroll", pts, 0.013, pm))
    # straps round the back of the leg
    for t in (0.18, 0.45, 0.93):
        centre = bottom + (top - bottom) * t
        r = radius * 0.95
        pts = [centre + fwd * (r * math.cos(math.radians(a))) + right * (r * math.sin(math.radians(a)))
               for a in range(100, 261, 12)]
        parts.append(tube_curve("pad_strap", pts, 0.006, strap_m))
    g = group("pad_" + side, parts)
    bone_attach(g, p.arm, "lowerleg01." + side)
    return g


def thigh_guard(p, side="L"):
    """Thigh guard (worn UNDER the trousers on the front leg's inner thigh)."""
    hip = p.arm.data.bones["upperleg01." + side].head_local.copy()
    knee = p.arm.data.bones["lowerleg01." + side].head_local.copy()
    up = (hip - knee).normalized()
    m = mat("thigh_guard", (0.92, 0.92, 0.9), 0.6)
    inner = -1 if side == "L" else 1
    rings = []
    for j in range(10):
        t = 0.25 + 0.55 * j / 9
        c = knee + (hip - knee) * t
        ring = [c + Vector((inner * 0.035 * math.sin(math.radians(a)) + 0.045 * math.sin(math.radians(a)),
                            -0.075 * math.cos(math.radians(a)), 0)) for a in range(-70, 71, 14)]
        rings.append(ring)
    ob = mesh_obj("thigh_guard_" + side, loft(rings, cap_ends=False), m)
    so = ob.modifiers.new("thick", "SOLIDIFY")
    so.thickness = 0.008
    bone_attach(ob, p.arm, "upperleg01." + side)
    return ob


def shoe_soles(p, spikes=True):
    """Rubber sole under each foot, with metal spikes (6 front, 3 heel)."""
    sole_m = mat("sole", (0.15, 0.15, 0.16), 0.7)
    spike_m = mat("spike", (0.6, 0.6, 0.62), 0.3, metal=1.0)
    out = []
    for side in ("L", "R"):
        b = p.arm.data.bones["foot." + side]
        ankle = b.head_local.copy()
        d = b.tail_local - b.head_local
        fwd = Vector((d.x, d.y, 0)).normalized()
        right = fwd.cross(Vector((0, 0, 1)))
        heel = ankle - fwd * 0.07
        toe = ankle + fwd * 0.20
        rings = []
        for j in range(12):
            t = j / 11
            c = heel + (toe - heel) * t
            w = 0.045 * (0.85 + 0.25 * math.sin(math.pi * min(1, t * 1.2)))
            ring = []
            for k in range(12):
                a = 2 * math.pi * k / 12
                ring.append(Vector((c.x, c.y, 0.0)) + right * (w * math.cos(a)) + Vector((0, 0, 0.012 + 0.011 * math.sin(a))))
            rings.append(ring)
        sole = mesh_obj("sole_" + side, loft(rings), sole_m)
        parts = [sole]
        if spikes:
            for t, s in ((0.62, -0.6), (0.62, 0.6), (0.78, -0.7), (0.78, 0.7), (0.92, -0.3), (0.92, 0.3),
                         (0.05, -0.5), (0.05, 0.5), (0.15, 0.0)):
                c = heel + (toe - heel) * t
                pos = Vector((c.x, c.y, 0.0)) + right * (0.035 * s)
                sp = mesh_obj("spike", lathe([(0.0, -0.008), (0.0045, 0.0), (0.0045, 0.002), (0.0, 0.002)], 8),
                              spike_m)
                sp.location = pos + Vector((0, 0, 0.002))
                parts.append(sp)
        g = group("shoe_" + side, parts)
        bone_attach(g, p.arm, "foot." + side)
        out.append(g)
    return out


def number_on_back(p, text, color=(0.95, 0.95, 0.95)):
    cu = bpy.data.curves.new("num", "FONT")
    cu.body = text
    cu.size = 0.17
    cu.align_x = "CENTER"
    cu.extrude = 0.0008
    ob = bpy.data.objects.new("number", cu)
    bpy.context.collection.objects.link(ob)
    ob.data.materials.append(mat("number_" + str(color), color, 0.7))
    sp = p.arm.data.bones["spine03"].head_local
    # the back of the shirt at this height (furthest point of the shirt mesh)
    sh = p.shells["shirt"]
    zc = sp.z + 0.14
    back_y = max((v.co.y for v in sh.data.vertices if abs(v.co.z - zc) < 0.03 and abs(v.co.x) < 0.05),
                 default=sp.y + 0.15)
    ob.location = Vector((0, back_y + 0.004, zc))
    ob.rotation_euler = (math.radians(90), 0, math.radians(180))
    bone_attach(ob, p.arm, "spine02")
    return ob


# ------------------------------------------------------------------ stumps

def stumps_set(y0, wood=None):
    """Three stumps (71.1 cm above ground, spikes below) and two bails."""
    wood = wood or mat("stump_wood", (0.86, 0.80, 0.64), 0.35, coat=0.3)
    parts = []
    prof = [(0.0, -0.17), (0.004, -0.15), (0.0175, -0.02), (0.0175, 0.70), (0.017, 0.703), (0.012, 0.7035),
            (0.012, 0.711), (0.0, 0.711)]
    for x in (-0.0968, 0.0, 0.0968):
        s = mesh_obj("stump", lathe(prof, 20), wood)
        s.location = (x, y0, 0)
        parts.append(s)
    bprof = [(0.0, -0.0545), (0.005, -0.0545), (0.006, -0.04), (0.011, -0.033), (0.0118, -0.01), (0.0105, 0.0),
             (0.0118, 0.01), (0.011, 0.033), (0.006, 0.04), (0.005, 0.0545), (0.0, 0.0545)]
    for x in (-0.0484, 0.0484):
        b = mesh_obj("bail", lathe(bprof, 14, axis="X"), wood)
        b.location = (x, y0, 0.716)
        parts.append(b)
    return parts
