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
