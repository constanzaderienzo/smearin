import maya.cmds as cmds

def add_smear_menu():
    # Add the "SMEAR" menu to the top bar
    if cmds.menu("smearMainMenu", exists=True):
        cmds.deleteUI("smearMainMenu")

    cmds.menu("smearMainMenu", label="SMEAR", parent="MayaWindow", tearOff=True)
    cmds.menuItem(label="Open SMEAR GUI", parent="smearMainMenu", command=lambda *_: open_bake_smear_gui())

def open_bake_smear_gui():
    # If the window already exists, delete it first
    if cmds.window("bakeSmearWindow", exists=True):
        cmds.deleteUI("bakeSmearWindow")

    # Create a new window
    cmds.window("bakeSmearWindow", title="Bake Smear")
    cmds.columnLayout(adjustableColumn=True)

    # Bake Smear button
    cmds.button(
        label="Bake Smear",
        command=lambda *_: _safe_bake()
    )

    # Settings Frame (Initially disabled)
    cmds.frameLayout(label="Settings", collapsable=True, collapse=False)
    cmds.columnLayout(adjustableColumn=True)

    # Elongated in-between frame
    cmds.checkBox("applyElongationCheckbox", label="Apply Elongation", enable=False)
    cmds.floatSliderGrp("pastStrengthSlider", label="Past Strength:", field=True, min=0, max=150, enable=False)
    cmds.floatSliderGrp("futureStrengthSlider", label="Future Strength:", field=True, min=0, max=150, enable=False)
    cmds.intSliderGrp("elongationSmoothWindowSlider", label="Smooth Window:", field=True, min=0, max=5, enable=False)

    # Motion Lines frame
    cmds.checkBox("generateMotionLinesCheckbox", label="Generate Motion Lines", enable=False)
    cmds.intSliderGrp("motionLinesCountSlider", label="Motion Lines Count:", field=True, min=0, max=100, enable=False)
    cmds.floatSliderGrp("motionLinesPastStrengthSlider", label="Motion Lines Past Strength:", field=True, min=0, max=150, enable=False)
    cmds.floatSliderGrp("motionLinesFutureStrengthSlider", label="Motion Lines Future Strength:", field=True, min=0, max=150, enable=False)
    cmds.intSliderGrp("motionLinesSmoothWindowSlider", label="Motion Lines Smooth Window:", field=True, min=0, max=5, enable=False)

    cmds.setParent("..")  # End columnLayout
    cmds.setParent("..")  # End frameLayout

    cmds.showWindow("bakeSmearWindow")


def _safe_bake():
    try:
        # Set the wait cursor (hourglass)
        cmds.waitCursor(state=True)

        if not cmds.objExists("smearControl1"):
            create_smear_related_nodes()

        # Store the original selection before any modifications
        original_selection = cmds.ls(selection=True)
        
        if not original_selection:
            cmds.error("Please select a mesh to bake.")
            return
        
        # Perform the baking operation on the selected mesh
        import vertex_cache_tool
        vertex_cache_tool.full_bake_and_trigger()

        enable_settings()

    except Exception as e:
        import traceback
        traceback.print_exc()
        cmds.warning("Bake failed. Check script editor for details.")
    
    finally:
        # Reset the wait cursor back to the default cursor
        cmds.waitCursor(state=False)

def create_smear_related_nodes():
    # Store the current selection
    sel = cmds.ls(selection=True)
    if not sel:
        cmds.error("No object selected. Please select an object before running createSmearRelatedNodes().")

    target = sel[0]  # The original selected mesh

    # Create deformer node
    deformer_nodes = cmds.deformer(target, type="SmearDeformerNode")
    deformer_node = deformer_nodes[0]

    cmds.connectAttr("time1.outTime", f"{deformer_node}.time")

    # Create control node
    control_node = cmds.createNode("SmearControlNode", name="smearControl1")
    cmds.connectAttr(f"{control_node}.cacheLoaded", f"{deformer_node}.cl")
    cmds.connectAttr(f"{control_node}.controlMessage", f"{deformer_node}.inputControlMessage", force=True)
    cmds.connectAttr(f"{control_node}.elongationStrengthPast", f"{deformer_node}.ps")
    cmds.connectAttr(f"{control_node}.elongationStrengthFuture", f"{deformer_node}.fs")
    cmds.connectAttr(f"{control_node}.elongationSmoothWindow", f"{deformer_node}.smwin")
    cmds.connectAttr(f"{control_node}.applyElongation", f"{deformer_node}.apl")

    # Motion Lines setup
    motion_lines_node = cmds.createNode("MotionLinesNode", name="MotionLinesNode1")
    motion_lines_transform = cmds.createNode("transform", name="MotionLines1")
    motion_lines_shape = cmds.createNode("mesh", name="MotionLinesShape1", parent=motion_lines_transform)
    cmds.sets(motion_lines_shape, add="initialShadingGroup")

    cmds.connectAttr("time1.outTime", f"{motion_lines_node}.tm")
    cmds.connectAttr(f"{motion_lines_node}.outputMesh", f"{motion_lines_shape}.inMesh")
    cmds.connectAttr(f"{target}.outMesh", f"{motion_lines_node}.in")
    cmds.connectAttr(f"{control_node}.controlMessage", f"{motion_lines_node}.icm", force=True)
    cmds.connectAttr(f"{control_node}.motionLinesStrengthPast", f"{motion_lines_node}.ps")
    cmds.connectAttr(f"{control_node}.motionLinesStrengthFuture", f"{motion_lines_node}.fs")
    cmds.connectAttr(f"{control_node}.motionLinesSmoothWindow", f"{motion_lines_node}.smwin")
    cmds.connectAttr(f"{control_node}.motionLinesCount", f"{motion_lines_node}.mlcnt")
    cmds.connectAttr(f"{control_node}.generateMotionLines", f"{motion_lines_node}.gen")
    cmds.connectAttr(f"{control_node}.cacheLoaded", f"{motion_lines_node}.cl")
    
    print("[SMEARin] Smear setup created successfully.")

    # Restore the original selection after creating nodes
    cmds.select(target)  # Re-select the original mesh

def enable_settings():
    # now the control node exists—bind each UI control and turn it on
    cmds.connectControl("applyElongationCheckbox",       "smearControl1.apl")
    cmds.connectControl("pastStrengthSlider",            "smearControl1.sp")
    cmds.connectControl("futureStrengthSlider",          "smearControl1.sf")
    cmds.connectControl("elongationSmoothWindowSlider",  "smearControl1.sw")

    cmds.connectControl("generateMotionLinesCheckbox",   "smearControl1.gml")
    cmds.connectControl("motionLinesCountSlider",        "smearControl1.mlcnt")
    cmds.connectControl("motionLinesPastStrengthSlider", "smearControl1.mlsp")
    cmds.connectControl("motionLinesFutureStrengthSlider","smearControl1.mlsf")
    cmds.connectControl("motionLinesSmoothWindowSlider", "smearControl1.mlsw")

    # now enable them
    for ctrl in [
      "applyElongationCheckbox","pastStrengthSlider","futureStrengthSlider","elongationSmoothWindowSlider",
      "generateMotionLinesCheckbox","motionLinesCountSlider","motionLinesPastStrengthSlider",
      "motionLinesFutureStrengthSlider","motionLinesSmoothWindowSlider"
    ]:
        cmds.control(ctrl, edit=True, enable=True)