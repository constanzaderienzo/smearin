import json
import os
import maya.cmds as cmds
import maya.api.OpenMaya as om

def cache_vertex_trajectories(mesh_name, output_path, compute_offsets=True):
    """
    Cache vertex trajectories over the playback range and optionally compute motion offsets.
    """
    start = int(cmds.playbackOptions(q=True, min=True))
    end = int(cmds.playbackOptions(q=True, max=True))
    frames = list(range(start, end + 1))

    sel = om.MSelectionList()
    sel.add(mesh_name)
    dag_path = sel.getDagPath(0)
    mesh_fn = om.MFnMesh(dag_path)

    vertex_count = mesh_fn.numVertices
    vertex_trajectories = {}

    for frame in frames:
        cmds.currentTime(frame, edit=True)
        points = mesh_fn.getPoints(space=om.MSpace.kWorld)
        vertex_trajectories[str(frame)] = [[p.x, p.y, p.z] for p in points]

    data = {
        "vertex_count": vertex_count,
        "start_frame": start,
        "frames": vertex_trajectories
    }

    # Optionally compute motion_offsets based on per-vertex velocity magnitude
    if compute_offsets and len(frames) >= 2:
        motion_offsets = {}

        for i, frame in enumerate(frames):
            if i == 0 or i == len(frames) - 1:
                # No motion on first or last frame
                motion_offsets[str(frame)] = [0.0] * vertex_count
                continue

            prev_frame = vertex_trajectories[str(frames[i - 1])]
            next_frame = vertex_trajectories[str(frames[i + 1])]

            offsets = []
            for p0, p1 in zip(prev_frame, next_frame):
                v = [b - a for a, b in zip(p0, p1)]
                length = sum([x * x for x in v]) ** 0.5
                # Normalize to [-1, 1] if desired
                offsets.append(length)

            motion_offsets[str(frame)] = offsets

        data["motion_offsets"] = motion_offsets

    # Save the result
    with open(output_path, 'w') as f:
        json.dump(data, f, separators=(',', ':'))

    print(f"[SMEARin] Cached vertex trajectories to {output_path}")
