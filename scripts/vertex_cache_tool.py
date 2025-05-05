import json
import os
import maya.cmds as cmds
import maya.api.OpenMaya as om
import maya.mel as mel    
from utils import cache_vertex_trajectories_with_deltas

import os
import maya.cmds as cmds
from utils import cache_vertex_trajectories_with_deltas, get_skin_cluster

def run_preprocess(progress_fn=None):
    sel = cmds.ls(selection=True)
    if not sel:
        cmds.error("No object selected.")
        return None

    transform = sel[0]
    shapes = cmds.listRelatives(transform, shapes=True, fullPath=True) or []
    if not shapes:
        cmds.error(f"{transform} has no mesh shape!")
        return None

    meshShape = shapes[0]

    skin_cluster = get_skin_cluster(transform)
    if not skin_cluster:
        cmds.warning(f"Mesh '{transform}' is not skinned. Skipping smear bake.")
        return None

    # make a cache folder next to current working dir
    cache_dir = os.path.join(os.getcwd(), "cache")
    os.makedirs(cache_dir, exist_ok=True)
    safe_name = meshShape.replace("|", "_").replace(":", "_")
    output_path = os.path.join(cache_dir, f"{safe_name}_cache.json")

    # now pass the shape name, not an MDagPath
    cache_vertex_trajectories_with_deltas(meshShape, output_path, progress_fn=progress_fn)

    print(f"[SMEARin] Preprocessing complete. Cache written to {output_path}")
    return output_path
