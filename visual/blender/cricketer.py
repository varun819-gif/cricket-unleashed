# Builds a rigged human cricketer in Blender from the free MakeHuman (CC0)
# base mesh, body-shape targets, skeleton and skin weights in
# visual/assets/makehuman. No MakeHuman program code is used: we read its
# data files (OBJ, .target text, .mhskel / .mhw JSON) ourselves.
#
# Coordinates (same as the C++ physics, docs/BALL_PHYSICS.md): metres, origin
# at the striker's middle stump, +y towards the bowler, +x off side (for a
# right-hander), +z up.
import json
import math
import os

import bpy
from mathutils import Matrix, Vector

HERE = os.path.dirname(os.path.abspath(__file__))
ASSETS = os.path.normpath(os.path.join(HERE, "..", "assets", "makehuman"))
SCALE = 0.1  # MakeHuman units are decimetres


def _mh_to_blender(x, y, z):
    # MakeHuman: y up, the figure faces +z. Blender: z up, front is -y.
    return Vector((x * SCALE, -z * SCALE, y * SCALE))


def load_base():
    verts, faces, group = [], [], None
    with open(os.path.join(ASSETS, "base.obj")) as f:
        for line in f:
            if line.startswith("v "):
                _, x, y, z = line.split()[:4]
                verts.append([float(x), float(y), float(z)])
            elif line.startswith("g "):
                group = line.split()[1]
            elif line.startswith("f ") and group == "body":
                faces.append([int(p.split("/")[0]) - 1 for p in line.split()[1:]])
    return verts, faces


def apply_target(verts, name, weight):
    with open(os.path.join(ASSETS, name + ".target")) as f:
        for line in f:
            if not line.strip() or line.startswith("#"):
                continue
            i, dx, dy, dz = line.split()
            v = verts[int(i)]
            v[0] += float(dx) * weight
            v[1] += float(dy) * weight
            v[2] += float(dz) * weight


def _mat(name, color, rough=0.6, sss=0.0, metal=0.0):
    m = bpy.data.materials.get(name) or bpy.data.materials.new(name)
    m.use_nodes = True
    b = m.node_tree.nodes["Principled BSDF"]
    b.inputs["Base Color"].default_value = (*color, 1)
    b.inputs["Roughness"].default_value = rough
    b.inputs["Metallic"].default_value = metal
    if sss > 0:
        b.inputs["Subsurface Weight"].default_value = sss
        b.inputs["Subsurface Radius"].default_value = (0.9, 0.35, 0.2)
        b.inputs["Subsurface Scale"].default_value = 0.01
    return m


def build_human(name, targets, skin, shirt, trousers, shoes):
    """targets: list of (target name, weight). Returns (body, armature)."""
    verts, faces = load_base()
    for t, w in targets:
        apply_target(verts, t, w)
    skel = json.load(open(os.path.join(ASSETS, "default.mhskel")))
    weights = json.load(open(os.path.join(ASSETS, "default_weights.mhw")))["weights"]

    bverts = [_mh_to_blender(*v) for v in verts]
    floor = min(bverts[i].z for f in faces for i in f)
    bverts = [v - Vector((0, 0, floor)) for v in bverts]

    mesh = bpy.data.meshes.new(name + "_mesh")
    mesh.from_pydata([tuple(v) for v in bverts], [], faces)
    mesh.update()
    body = bpy.data.objects.new(name, mesh)
    bpy.context.collection.objects.link(body)
    for p in mesh.polygons:
        p.use_smooth = True

    # Skeleton: each joint is the average of a few helper vertices.
    def joint(jname):
        ids = skel["joints"][jname]
        s = Vector()
        for i in ids:
            s += bverts[i]
        return s / len(ids)

    arm_data = bpy.data.armatures.new(name + "_rig")
    arm = bpy.data.objects.new(name + "_rig", arm_data)
    bpy.context.collection.objects.link(arm)
    bpy.context.view_layer.objects.active = arm
    bpy.ops.object.mode_set(mode="EDIT")
    for bname, b in skel["bones"].items():
        eb = arm_data.edit_bones.new(bname)
        eb.head = joint(b["head"])
        eb.tail = joint(b["tail"])
        if (eb.tail - eb.head).length < 1e-4:
            eb.tail = eb.head + Vector((0, 0, 0.01))
    for bname, b in skel["bones"].items():
        if b["parent"]:
            arm_data.edit_bones[bname].parent = arm_data.edit_bones[b["parent"]]
    bpy.ops.object.mode_set(mode="OBJECT")

    for bname, pairs in weights.items():
        g = body.vertex_groups.new(name=bname)
        for vi, w in pairs:
            g.add([vi], w, "REPLACE")
    mod = body.modifiers.new("rig", "ARMATURE")
    mod.object = arm
    body.parent = arm
    sub = body.modifiers.new("smooth", "SUBSURF")
    sub.levels = 1
    sub.render_levels = 1

    # Kit painted on by body region (dominant bone): skin for hands / neck /
    # head, shoes for feet, shirt for the torso and arms, trousers for legs.
    mats = [_mat(name + "_skin", skin, 0.45, sss=0.25), _mat(name + "_shirt", shirt, 0.75),
            _mat(name + "_trousers", trousers, 0.8), _mat(name + "_shoes", shoes, 0.5)]
    for m in mats:
        mesh.materials.append(m)
    dominant = {}
    best = {}
    for bname, pairs in weights.items():
        for vi, w in pairs:
            if w > best.get(vi, 0):
                best[vi] = w
                dominant[vi] = bname

    def region(bname):
        if bname is None:
            return 1
        if any(k in bname for k in ("finger", "metacarpal", "wrist", "head", "neck", "jaw", "eye", "tongue",
                                     "levator", "oris", "temporalis", "risorius", "orbicularis", "special")):
            if bname.startswith("neck01"):
                return 1  # collar
            return 0
        if any(k in bname for k in ("foot", "toe")):
            return 3
        if any(k in bname for k in ("upperleg", "lowerleg", "pelvis", "root")):
            return 2
        if "lowerarm02" in bname:
            return 0  # short sleeves end mid-forearm... T20 shirts: sleeves to elbow
        return 1

    for p in mesh.polygons:
        votes = [region(dominant.get(i)) for i in p.vertices]
        p.material_index = max(set(votes), key=votes.count)
    return body, arm


# ---------------------------------------------------------------- posing


def empty(name, loc, size=0.05):
    e = bpy.data.objects.new(name, None)
    e.empty_display_size = size
    e.location = loc
    bpy.context.collection.objects.link(e)
    return e


def ik(arm, bone, target, pole, chain, pole_angle=-90):
    pb = arm.pose.bones[bone]
    c = pb.constraints.new("IK")
    c.target = target
    c.pole_target = pole
    c.pole_angle = math.radians(pole_angle)
    c.chain_count = chain
    return c


def rot(arm, bone, x=0, y=0, z=0):
    pb = arm.pose.bones[bone]
    pb.rotation_mode = "XYZ"
    pb.rotation_euler = (math.radians(x), math.radians(y), math.radians(z))


def curl_fingers(arm, side, amount=70):
    for f in range(1, 6):
        for s in (1, 2, 3):
            n = "finger%d-%d.%s" % (f, s, side)
            if n in arm.pose.bones:
                rot(arm, n, x=amount if f > 1 else amount * 0.5)


def place(arm, loc, facing_deg):
    """Stand the rig at loc (feet), facing direction in degrees (0 = -y)."""
    arm.location = loc
    arm.rotation_euler = (0, 0, math.radians(facing_deg))


# ======================================================================
# Phase 2: dressed players (separate clothes, hair, eyes, varied bodies)
# ======================================================================
#
# The clothes are separate meshes made from the body's own surface: the
# faces under a shirt are copied, pushed outwards (looser where real kit is
# loose), given thickness, creases and a fabric texture, and skinned to the
# same skeleton, so they move with the body. The body skin under the clothes
# is removed so it can never poke through. This is not a cloth simulation:
# the creases are fixed, they do not swing or flutter.

SKIN, SHIRT, TROUSERS, SHOES, HANDS = range(5)
FACE_BONES = ("head", "neck02", "neck03", "jaw", "eye", "tongue", "levator", "oris", "temporalis", "risorius",
              "orbicularis", "special", "oculi")


def _region(bname, z, waist_z, short_sleeves, neck_z=1.45):
    if bname is None:
        return SKIN if z > neck_z else SHIRT
    if any(k in bname for k in ("finger", "metacarpal", "wrist")):
        return HANDS
    if any(k in bname for k in FACE_BONES) or (z > neck_z and not bname.startswith(("clavicle", "shoulder"))):
        return SKIN
    if any(k in bname for k in ("foot", "toe")):
        return SHOES
    if "lowerarm" in bname:
        return SKIN if short_sleeves else SHIRT
    if any(k in bname for k in ("upperleg", "lowerleg")):
        return TROUSERS
    if any(k in bname for k in ("pelvis", "root", "spine05", "spine04")):
        return SHIRT if z > waist_z else TROUSERS
    return SHIRT


def fabric_material(name, color, trim=None, stripe_z=None, weave=1400.0):
    """Cloth: base colour with subtle dye variation, a fine weave bump, soft
    sheen, horizontal crease shading. Optional trim colour band (Generated
    coordinates follow the undeformed garment, so bands stay put)."""
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    n, l = m.node_tree.nodes, m.node_tree.links
    b = n["Principled BSDF"]
    tc = n.new("ShaderNodeTexCoord")
    noise = n.new("ShaderNodeTexNoise")
    noise.inputs["Scale"].default_value = 6.0
    ramp = n.new("ShaderNodeValToRGB")
    ramp.color_ramp.elements[0].color = tuple(max(0.0, c * 0.82) for c in color) + (1,)
    ramp.color_ramp.elements[1].color = tuple(min(1.0, c * 1.08) for c in color) + (1,)
    l.new(noise.outputs["Fac"], ramp.inputs["Fac"])
    col = ramp.outputs["Color"]
    if trim is not None and stripe_z is not None:
        sep = n.new("ShaderNodeSeparateXYZ")
        l.new(tc.outputs["Generated"], sep.inputs["Vector"])
        lo = n.new("ShaderNodeMath")
        lo.operation = "GREATER_THAN"
        lo.inputs[1].default_value = stripe_z[0]
        hi = n.new("ShaderNodeMath")
        hi.operation = "LESS_THAN"
        hi.inputs[1].default_value = stripe_z[1]
        l.new(sep.outputs[stripe_z[2]], lo.inputs[0])
        l.new(sep.outputs[stripe_z[2]], hi.inputs[0])
        both = n.new("ShaderNodeMath")
        both.operation = "MULTIPLY"
        l.new(lo.outputs[0], both.inputs[0])
        l.new(hi.outputs[0], both.inputs[1])
        mix = n.new("ShaderNodeMix")
        mix.data_type = "RGBA"
        l.new(both.outputs[0], mix.inputs["Factor"])
        l.new(col, mix.inputs["A"])
        mix.inputs["B"].default_value = (*trim, 1)
        col = mix.outputs["Result"]
    l.new(col, b.inputs["Base Color"])
    # weave: two fine wave patterns crossing
    w1 = n.new("ShaderNodeTexWave")
    w1.inputs["Scale"].default_value = weave
    w1.bands_direction = "X"
    w2 = n.new("ShaderNodeTexWave")
    w2.inputs["Scale"].default_value = weave
    w2.bands_direction = "Z"
    for w in (w1, w2):
        l.new(tc.outputs["Object"], w.inputs["Vector"])
    add = n.new("ShaderNodeMath")
    l.new(w1.outputs["Fac"], add.inputs[0])
    l.new(w2.outputs["Fac"], add.inputs[1])
    # broad creases (soft bands)
    crease = n.new("ShaderNodeTexWave")
    crease.bands_direction = "Z"
    crease.inputs["Scale"].default_value = 5.0
    crease.inputs["Distortion"].default_value = 14.0
    crease.inputs["Detail"].default_value = 3.0
    l.new(tc.outputs["Generated"], crease.inputs["Vector"])
    mixb = n.new("ShaderNodeMath")
    mixb.operation = "MULTIPLY_ADD"
    mixb.inputs[1].default_value = 2.5
    l.new(crease.outputs["Fac"], mixb.inputs[0])
    l.new(add.outputs[0], mixb.inputs[2])
    bump = n.new("ShaderNodeBump")
    bump.inputs["Strength"].default_value = 0.12
    bump.inputs["Distance"].default_value = 0.002
    l.new(mixb.outputs[0], bump.inputs["Height"])
    l.new(bump.outputs["Normal"], b.inputs["Normal"])
    b.inputs["Roughness"].default_value = 0.82
    if "Sheen Weight" in b.inputs:
        b.inputs["Sheen Weight"].default_value = 0.35
    return m


def skin_material(name, tone):
    """Skin with subsurface scattering, colour variation, pores, redder lips
    and darker brows (from the 'tint' colour attribute)."""
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    n, l = m.node_tree.nodes, m.node_tree.links
    b = n["Principled BSDF"]
    attr = n.new("ShaderNodeAttribute")
    attr.attribute_name = "tint"
    noise = n.new("ShaderNodeTexNoise")
    noise.inputs["Scale"].default_value = 30.0
    ramp = n.new("ShaderNodeValToRGB")
    ramp.color_ramp.elements[0].color = tuple(c * 0.9 for c in tone) + (1,)
    ramp.color_ramp.elements[1].color = tuple(min(1, c * 1.06) for c in tone) + (1,)
    l.new(noise.outputs["Fac"], ramp.inputs["Fac"])
    mult = n.new("ShaderNodeMix")
    mult.data_type = "RGBA"
    mult.blend_type = "MULTIPLY"
    mult.inputs["Factor"].default_value = 1.0
    l.new(ramp.outputs["Color"], mult.inputs["A"])
    l.new(attr.outputs["Color"], mult.inputs["B"])
    l.new(mult.outputs["Result"], b.inputs["Base Color"])
    b.inputs["Subsurface Weight"].default_value = 0.3
    b.inputs["Subsurface Radius"].default_value = (1.0, 0.4, 0.25)
    b.inputs["Subsurface Scale"].default_value = 0.006
    pores = n.new("ShaderNodeTexNoise")
    pores.inputs["Scale"].default_value = 900.0
    bump = n.new("ShaderNodeBump")
    bump.inputs["Strength"].default_value = 0.08
    bump.inputs["Distance"].default_value = 0.0004
    l.new(pores.outputs["Fac"], bump.inputs["Height"])
    l.new(bump.outputs["Normal"], b.inputs["Normal"])
    rr = n.new("ShaderNodeMapRange")
    rr.inputs["To Min"].default_value = 0.38
    rr.inputs["To Max"].default_value = 0.55
    l.new(noise.outputs["Fac"], rr.inputs["Value"])
    l.new(rr.outputs["Result"], b.inputs["Roughness"])
    return m


def hair_material(name, melanin=0.9, redness=0.1):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    n, l = m.node_tree.nodes, m.node_tree.links
    out = n["Material Output"]
    n.remove(n["Principled BSDF"])
    h = n.new("ShaderNodeBsdfHairPrincipled")
    h.parametrization = "MELANIN"
    h.inputs["Melanin"].default_value = melanin
    h.inputs["Melanin Redness"].default_value = redness
    h.inputs["Roughness"].default_value = 0.35
    l.new(h.outputs[0], out.inputs["Surface"])
    return m


def _hair_cap_material(name, melanin):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    n, l = m.node_tree.nodes, m.node_tree.links
    b = n["Principled BSDF"]
    dark = 0.06 * (1.2 - melanin)
    b.inputs["Base Color"].default_value = (dark * 1.3, dark, dark * 0.8, 1)
    b.inputs["Roughness"].default_value = 0.45
    # strands: stretched noise -> bump
    tc = n.new("ShaderNodeTexCoord")
    mp = n.new("ShaderNodeMapping")
    mp.inputs["Scale"].default_value = (900.0, 900.0, 40.0)
    noise = n.new("ShaderNodeTexNoise")
    noise.inputs["Detail"].default_value = 4.0
    l.new(tc.outputs["Object"], mp.inputs["Vector"])
    l.new(mp.outputs["Vector"], noise.inputs["Vector"])
    bump = n.new("ShaderNodeBump")
    bump.inputs["Strength"].default_value = 0.5
    bump.inputs["Distance"].default_value = 0.001
    l.new(noise.outputs["Fac"], bump.inputs["Height"])
    l.new(bump.outputs["Normal"], b.inputs["Normal"])
    return m


def _shell(name, all_co, normals, faces, offset_fn, weights, arm, material, thick, folds, relax=0):
    used = sorted({i for f in faces for i in f})
    idx = {v: k for k, v in enumerate(used)}
    co = [all_co[v] + normals[v] * offset_fn(v) for v in used]
    me = bpy.data.meshes.new(name)
    me.from_pydata([tuple(c) for c in co], [], [[idx[i] for i in f] for f in faces])
    me.update()
    for p in me.polygons:
        p.use_smooth = True
    ob = bpy.data.objects.new(name, me)
    bpy.context.collection.objects.link(ob)
    me.materials.append(material)
    for bname, pairs in weights.items():
        sel = [(idx[vi], w) for vi, w in pairs if vi in idx]
        if sel:
            g = ob.vertex_groups.new(name=bname)
            for vi, w in sel:
                g.add([vi], w, "REPLACE")
    if relax > 0:
        # Cloth hangs over the body instead of following every muscle:
        # smooth the shape before the creases are added.
        sm = ob.modifiers.new("drape", "SMOOTH")
        sm.factor = 0.9
        sm.iterations = relax
    if folds > 0:
        tex = bpy.data.textures.new(name + "_folds", "STUCCI")
        tex.noise_scale = 0.06
        tex.turbulence = 6.0
        d = ob.modifiers.new("folds", "DISPLACE")
        d.texture = tex
        d.strength = folds
        d.mid_level = 0.5
        d.texture_coords = "LOCAL"
    sub = ob.modifiers.new("smooth", "SUBSURF")
    sub.levels = 1
    sub.render_levels = 1
    a = ob.modifiers.new("rig", "ARMATURE")
    a.object = arm
    if thick > 0:
        s = ob.modifiers.new("thickness", "SOLIDIFY")
        s.thickness = thick
        s.offset = -1
    ob.parent = arm
    return ob


class Player:
    pass


def build_player(name, spec):
    """spec keys: targets [(name, w)], skin (rgb), shirt, trim, trousers,
    shoes, sleeves 'short'|'long', gloves None|'bat'|'keeper', hair
    {'length','color' (melanin), 'count'} or None, brows (melanin)."""
    verts, faces_all = load_base()
    for t, w in spec["targets"]:
        apply_target(verts, t, w)
    skel = json.load(open(os.path.join(ASSETS, "default.mhskel")))
    weights = json.load(open(os.path.join(ASSETS, "default_weights.mhw")))["weights"]
    co = [_mh_to_blender(*v) for v in verts]
    floor = min(co[i].z for f in faces_all for i in f)
    co = [v - Vector((0, 0, floor)) for v in co]

    # Normals from the full body surface.
    tmp = bpy.data.meshes.new("tmp")
    tmp.from_pydata([tuple(v) for v in co], [], faces_all)
    tmp.update()
    normals = [v.normal.copy() for v in tmp.vertices]
    bpy.data.meshes.remove(tmp)

    def joint(j):
        ids = skel["joints"][j]
        s = Vector()
        for i in ids:
            s += co[i]
        return s / len(ids)

    dominant, best = {}, {}
    for bname, pairs in weights.items():
        for vi, w in pairs:
            if w > best.get(vi, 0):
                best[vi] = w
                dominant[vi] = bname
    waist_z = joint(skel["bones"]["spine05"]["head"]).z + 0.05
    short = spec.get("sleeves", "short") == "short"
    neck_z = joint(skel["bones"]["neck02"]["head"]).z
    vreg = {i: _region(dominant.get(i), co[i].z, waist_z, short, neck_z) for f in faces_all for i in f}

    def face_region(f):
        votes = [vreg[i] for i in f]
        return max(set(votes), key=votes.count)

    groups = {r: [] for r in range(5)}
    for f in faces_all:
        if all(dominant.get(i) is None for i in f) and co[f[0]].z > neck_z:
            continue  # loose pieces around the eyes with no skin weights
        groups[face_region(f)].append(f)
    gloves = spec.get("gloves")
    skin_faces = groups[SKIN] + ([] if gloves else groups[HANDS])

    # ---- armature
    arm_data = bpy.data.armatures.new(name + "_rig")
    arm = bpy.data.objects.new(name + "_rig", arm_data)
    bpy.context.collection.objects.link(arm)
    bpy.context.view_layer.objects.active = arm
    bpy.ops.object.mode_set(mode="EDIT")
    for bname, b in skel["bones"].items():
        eb = arm_data.edit_bones.new(bname)
        eb.head = joint(b["head"])
        eb.tail = joint(b["tail"])
        if (eb.tail - eb.head).length < 1e-4:
            eb.tail = eb.head + Vector((0, 0, 0.01))
    for bname, b in skel["bones"].items():
        if b["parent"]:
            arm_data.edit_bones[bname].parent = arm_data.edit_bones[b["parent"]]
    bpy.ops.object.mode_set(mode="OBJECT")

    # ---- body (visible skin only)
    used = sorted({i for f in skin_faces for i in f})
    idx = {v: k for k, v in enumerate(used)}
    me = bpy.data.meshes.new(name + "_body")
    me.from_pydata([tuple(co[v]) for v in used], [], [[idx[i] for i in f] for f in skin_faces])
    me.update()
    for p in me.polygons:
        p.use_smooth = True
    body = bpy.data.objects.new(name + "_body", me)
    bpy.context.collection.objects.link(body)
    for bname, pairs in weights.items():
        sel = [(idx[vi], w) for vi, w in pairs if vi in idx]
        if sel:
            g = body.vertex_groups.new(name=bname)
            for vi, w in sel:
                g.add([vi], w, "REPLACE")
    # Tint: lips a little redder, brow ridge a little darker.
    eyes = [joint(skel["bones"][e]["head"]) for e in ("eye.L", "eye.R")]
    tint = me.color_attributes.new("tint", "FLOAT_COLOR", "POINT")
    head_c = (eyes[0] + eyes[1]) / 2
    skull = head_c + Vector((0, 0.085, 0.03))  # centre of the skull, behind the eyes
    eye_z = head_c.z
    scalp, brow = [], []
    for k, v in enumerate(used):
        p = co[v]
        c = (1.0, 1.0, 1.0)
        bn = dominant.get(v) or ""
        if "oris" in bn and p.z < eye_z - 0.055 and p.y < head_c.y + 0.03:
            c = (0.93, 0.8, 0.78)
        for e in eyes:
            if abs(p.x - e.x) < 0.024 and 0.015 < p.z - e.z < 0.027 and p.y < e.y + 0.008 and abs(p.x) > 0.012:
                brow.append(k)
                c = (0.85, 0.8, 0.78)
        # Scalp: above the hairline at the front; at the sides and back down
        # to the top of the ears / the nape.
        rel = p - skull
        if (p.z > eye_z + 0.055) or (rel.y > -0.02 and p.z > eye_z + 0.0) or (rel.y > 0.045 and p.z > eye_z - 0.075):
            if p.z > neck_z + 0.02:
                scalp.append(k)
        tint.data[k].color = (*c, 1.0)
    me.materials.append(skin_material(name + "_skin", spec["skin"]))
    a = body.modifiers.new("rig", "ARMATURE")
    a.object = arm
    sub = body.modifiers.new("smooth", "SUBSURF")
    sub.levels = 1
    sub.render_levels = 1
    body.parent = arm

    # ---- hair and eyebrows (particle hair, follows the head)
    hair = spec.get("hair")
    if hair and scalp:
        # Short hair = a thin hair 'cap' over the scalp (dark, strand-textured)
        # plus a light fuzz of real strands on top.
        sset = set(scalp)
        cap_faces = [[used[i] for i in pf.vertices] for pf in me.polygons if all(i in sset for i in pf.vertices)]
        hm = hair_material(name + "_hair", hair["color"])
        capm = _hair_cap_material(name + "_hcap", hair["color"])
        _shell(name + "_haircap", co, normals, cap_faces, lambda v: hair.get("volume", 0.006), weights, arm, capm,
               0.0, 0.0)
        g = body.vertex_groups.new(name="scalp")
        g.add(scalp, 1.0, "REPLACE")
        _hair(body, "scalp", hair["length"], hair.get("count", 6000), hm, clump=hair.get("clump", 0.5), rough=0.4)
    if brow:
        g = body.vertex_groups.new(name="brows")
        g.add(brow, 1.0, "REPLACE")
        _hair(body, "brows", 0.0035, 250, hair_material(name + "_brow", spec.get("brows", 0.95)), clump=0.2,
              rough=0.1)

    # ---- clothes
    shirt_m = fabric_material(name + "_shirt", spec["shirt"], spec.get("trim"), (0.94, 1.01, "Z"))
    trousers_m = fabric_material(name + "_trousers", spec["trousers"])
    shoe_m = _mat(name + "_shoes", spec.get("shoes", (0.92, 0.92, 0.92)), 0.4)
    knee_z = joint(skel["bones"]["lowerleg01.L"]["head"]).z

    def shirt_off(v):
        bn = dominant.get(v) or ""
        if "arm" in bn:
            return 0.016
        if co[v].z < neck_z - 0.06:
            return 0.024  # loose over the chest, back and stomach
        return 0.012

    def trouser_off(v):
        z = co[v].z
        return 0.016 + 0.016 * max(0.0, min(1.0, (knee_z - z) / knee_z)) + 0.004 * (z > waist_z - 0.1)

    shells = {
        "shirt": _shell(name + "_shirt", co, normals, groups[SHIRT], shirt_off, weights, arm, shirt_m, 0.003, 0.007,
                        relax=14),
        "trousers": _shell(name + "_trousers", co, normals, groups[TROUSERS], trouser_off, weights, arm,
                           trousers_m, 0.003, 0.008, relax=8),
        "shoes": _shell(name + "_shoes", co, normals, groups[SHOES], lambda v: 0.010, weights, arm, shoe_m, 0.004, 0),
    }
    if gloves:
        gm = _mat(name + "_gloves", spec.get("glove_color", (0.95, 0.95, 0.95)), 0.6)
        off = 0.011 if gloves == "bat" else 0.019
        shells["gloves"] = _shell(name + "_gloves", co, normals, groups[HANDS], lambda v: off, weights, arm, gm,
                                  0.004, 0.0015)
    p = Player()
    p.name, p.arm, p.body, p.shells = name, arm, body, shells
    p.eyes = eyes
    p.head_centre = head_c
    p.joint = joint
    p.skel = skel
    return p


def _hair(body, group, length, count, material, clump=0.3, rough=0.2, along=False):
    body.data.materials.append(material)
    mod = body.modifiers.new(group + "_hair", "PARTICLE_SYSTEM")
    ps = mod.particle_system
    ps.vertex_group_density = group
    s = ps.settings
    s.type = "HAIR"
    s.count = count
    s.hair_length = length
    s.use_advanced_hair = along
    s.emit_from = "FACE"
    s.material = len(body.data.materials)
    s.child_type = "INTERPOLATED"
    s.child_percent = 3
    s.rendered_child_count = 6
    s.clump_factor = clump
    s.roughness_1 = rough * 0.004
    s.roughness_2 = rough * 0.002
    s.kink = "NO" 
    s.root_radius = 0.6
    s.tip_radius = 0.1
    s.radius_scale = 0.0012
    # Scalp hair: plain hair grows along the skin normal with an exact length.
    # Eyebrows: 'advanced' hair, where the emit speeds (metres) set the length
    # and lay the hairs along the skin.
    if along:
        s.normal_factor = length * 0.15
        s.tangent_factor = length
        s.tangent_phase = 0.0
    s.effector_weights.gravity = 0.0
    s.display_step = 3
    s.render_step = 4
    return ps
