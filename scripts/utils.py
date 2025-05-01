import maya.cmds as cmds
import maya.api.OpenMaya as om
from maya.api import OpenMayaAnim  as oma
import numpy as np
from maya.api.OpenMaya import MVector

def get_selected_mesh():
    sel = cmds.ls(selection=True, dag=True, type='mesh')
    if not sel:
        raise RuntimeError("No mesh selected.")
    return sel[0]

def get_skin_cluster(mesh):
    history = cmds.listHistory(mesh)
    skin_clusters = cmds.ls(history, type='skinCluster')
    return skin_clusters[0] if skin_clusters else None

def get_influencing_joints(skin_cluster):
    short_names = cmds.skinCluster(skin_cluster, q=True, influence=True)
    return [cmds.ls(j, long=True)[0] for j in short_names]

def get_world_matrix(obj, frame):
    cmds.currentTime(frame)
    return om.MMatrix(cmds.getAttr(f"{obj}.worldMatrix[0]", time=frame))

def get_joint_head_tail(joint, frame):
    cmds.currentTime(frame)

    head_pos = om.MVector(*cmds.xform(joint, query=True, worldSpace=True, translation=True))
    
    children = cmds.listRelatives(joint, children=True, type="joint") or []
    if not children:
        return np.array(head_pos), np.array(head_pos)

    child = children[0]
    tail_pos = om.MVector(*cmds.xform(child, query=True, worldSpace=True, translation=True))

    return np.array(head_pos), np.array(tail_pos)

def get_vertex_positions(mesh, frame):
    cmds.currentTime(frame)
    sel = om.MSelectionList()
    sel.add(mesh)
    dag_path = sel.getDagPath(0)
    mesh_fn = om.MFnMesh(dag_path)

    verts = mesh_fn.getPoints(space=om.MSpace.kWorld)
    return np.array([[v.x, v.y, v.z] for v in verts])

def get_anim_vertices_and_joints_maya(start_frame, end_frame):
    mesh = get_selected_mesh()
    skin_cluster = get_skin_cluster(mesh)
    if not skin_cluster:
        raise RuntimeError("Mesh is not skinned or has no skinCluster.")

    joints = get_influencing_joints(skin_cluster)

    anim_vertices = {}
    anim_joints = {}

    for frame in range(start_frame, end_frame + 1):
        print(f"Processing frame {frame}")
        # Per-vertex world-space positions
        anim_vertices[frame] = get_vertex_positions(mesh, frame)

        # Joint world head/tail
        anim_joints[frame] = {}
        for joint in joints:
            head, tail = get_joint_head_tail(joint, frame)
            anim_joints[frame][joint] = [head, tail]

    return anim_vertices, anim_joints

def get_skin_weights(mesh, skin_cluster, joints):
    import maya.api.OpenMaya as om
    import maya.api.OpenMayaAnim as oma
    import maya.cmds as cmds
    import numpy as np

    # --- column index for each joint --------------------------
    col_of_joint = {j: idx for idx, j in enumerate(joints)}

    # --- MFnSkinCluster handle --------------------------------
    sel = om.MSelectionList(); sel.add(skin_cluster)
    skin_fn = oma.MFnSkinCluster(sel.getDependNode(0))

    # --- influence name list (by logical index) ---------------
    inf_paths = skin_fn.influenceObjects()
    inf_full  = [inf.fullPathName() for inf in inf_paths]

    # --- geometry dag path ------------------------------------
    sel_geo = om.MSelectionList(); sel_geo.add(mesh)
    geo_dag = sel_geo.getDagPath(0)
    num_verts = cmds.polyEvaluate(mesh, vertex=True)

    # --- build a “whole-mesh vertices” component --------------
    comp_fn  = om.MFnSingleIndexedComponent()
    vtx_comp = comp_fn.create(om.MFn.kMeshVertComponent)
    comp_fn.addElements(range(num_verts))

    # --- sparse query -----------------------------------------
    weights, inf_ids = skin_fn.getWeights(geo_dag, vtx_comp)

    # Convert to dense matrix  (numVerts × numJoints)
    W = np.zeros((num_verts, len(joints)), dtype=np.float32)

    v_stride = len(inf_paths)
    for v in range(num_verts):
        base = v * v_stride
        for local_inf, w in enumerate(weights[base:base+v_stride]):
            if w == 0.0:
                continue
            joint_name = inf_full[local_inf]
            col = col_of_joint.get(joint_name)

            if col is not None:
                W[v, col] = w
            else:
                print(f"[WARN] Skipping joint '{joint_name}' not found in col_of_joint")

    print(f"[DEBUG] First 5 rows of weights:\n{W[:5]}\n")

    return W

def compute_joint_velocities(anim_joints, frame, joint_name, dt=1.0):
    if frame + 1 in anim_joints and frame - 1 in anim_joints:
        prev = anim_joints[frame-1][joint_name]
        nxt  = anim_joints[frame+1][joint_name]
        v_root = (nxt[0] - prev[0]) / (2.0 * dt)
        v_tip  = (nxt[1] - prev[1]) / (2.0 * dt)
    else:                                   # first / last key
        prev = anim_joints[max(frame-1, min(anim_joints.keys()))][joint_name]
        curr = anim_joints[frame][joint_name]
        v_root = curr[0] - prev[0]
        v_tip  = curr[1] - prev[1]
    return normalize(v_root), normalize(v_tip)

def per_bone_deltas(frame, bone_idx, bone_name, verts_np, joints_np,
                    v_hat_root, v_hat_tip, weights_colinearity):
    head, tail = joints_np
    axis      = normalize(tail - head)
    length    = norm(tail - head)

    # 1 Projection parameter u ∈ [0,1]
    proj_len  = np.dot(verts_np - head, axis) / length   # (nVert,)
    u         = smoothstep(proj_len)

    # 2 Interpolated motion direction
    v_hat     = np.vstack([slerp(v_hat_root, v_hat_tip, ui) for ui in u])

    # 3 Ribbon normal
    b_dot_v   = np.sum(v_hat * axis, axis=1, keepdims=True)
    n_hat     = normalize(v_hat - b_dot_v * axis)

    # 4 Colinearity weight (already optional)
    w_coll    = 1.0 - (b_dot_v.squeeze() ** 2)

    # 5 Signed distance
    delta_raw = np.sum((verts_np - head) * n_hat, axis=1)

    # 6 Normalisation factor – we’ll fill this later outside the function
    return delta_raw, w_coll


def to_np(vec):               # MVector ➔ np.array
    return np.array([vec.x, vec.y, vec.z], dtype=np.float64)

def norm(v):                  # length
    return np.linalg.norm(v)

def normalize(v):
    n = norm(v)
    return v / n if n > 1e-8 else v

def dot(a, b):
    return float(np.dot(a, b))

def smoothstep(x):
    x = np.clip(x, 0.0, 1.0)
    return 3*x**2 - 2*x**3          # scalar or ndarray

def slerp(v0, v1, t):
    v0 = normalize(v0); v1 = normalize(v1)
    dot01 = np.clip(dot(v0, v1), -1.0, 1.0)
    omega = np.arccos(dot01)
    if omega < 1e-4:
        return normalize((1.0 - t) * v0 + t * v1)   # fallback to lerp
    so = np.sin(omega)
    return (np.sin((1.0 - t) * omega) / so) * v0 + (np.sin(t * omega) / so) * v1
