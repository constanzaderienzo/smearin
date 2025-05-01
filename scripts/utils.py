import maya.cmds as cmds
import maya.api.OpenMaya as om
import numpy as np

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
    return cmds.skinCluster(skin_cluster, query=True, influence=True)

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
