import json
import os
import maya.cmds as cmds
import maya.api.OpenMaya as om
import maya.mel as mel    
from utils import cache_vertex_trajectories_with_deltas

import os
import maya.cmds as cmds
from utils import cache_vertex_trajectories_with_deltas

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

    # make a cache folder next to current working dir
    cache_dir = os.path.join(os.getcwd(), "cache")
    os.makedirs(cache_dir, exist_ok=True)
    output_path = os.path.join(cache_dir, "cache.json")

    # now pass the shape name, not an MDagPath
    cache_vertex_trajectories_with_deltas(meshShape, output_path, progress_fn=progress_fn)

    print(f"[SMEARin] Preprocessing complete. Cache written to {output_path}")
    return output_path


def full_bake_and_trigger():
    try:
        path = run_preprocess() 
        if path:
            clean_path = path.replace('\\', '/')

            mel.eval(f'loadCache "{clean_path}"')

            cmds.setAttr("smearControl1.cacheLoaded", 1)
            cmds.refresh() 
    except Exception as e:
        cmds.error(f"[SMEARin] Bake failed: {e}")
