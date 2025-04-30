# vertex_cache_tool.py

import maya.cmds as cmds
import maya.api.OpenMaya as om
import json

vertex_cache = {}

def cache_vertex_trajectories(mesh_name, start_frame=None, end_frame=None):
    global vertex_cache
    vertex_cache.clear()
    if not start_frame:
        start_frame = int(cmds.playbackOptions(q=True, min=True))
    if not end_frame:
        end_frame = int(cmds.playbackOptions(q=True, max=True))
    sel = om.MSelectionList()
    sel.add(mesh_name)
    dag_path = sel.getDagPath(0)
    mesh_fn = om.MFnMesh(dag_path)
    for frame in range(start_frame, end_frame + 1):
        cmds.currentTime(frame, edit=True)
        points = mesh_fn.getPoints(om.MSpace.kWorld)
        positions = [(p.x, p.y, p.z) for p in points]
        vertex_cache[frame] = positions
    print(f"Cached {len(vertex_cache)} frames for mesh: {mesh_name}")

def save_vertex_cache_to_json(filepath):
    global vertex_cache
    with open(filepath, "w") as f:
        json.dump(vertex_cache, f)
    print(f"Saved vertex cache to: {filepath}")
