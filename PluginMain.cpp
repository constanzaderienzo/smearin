#include <maya/MPxCommand.h>
#include <maya/MFnPlugin.h>
#include <maya/MGlobal.h>
#include <maya/MSelectionList.h>
#include <maya/MDagPath.h>
#include <maya/MFnMesh.h>
#include <maya/MColorArray.h>
#include <maya/MIntArray.h>
#include <maya/MStatus.h>
#include <maya/MMatrix.h>
#include <cstdlib> // for rand()
#include "smear.h"
#include "smearNode.h"
#include "smearDeformerNode.h"
#include "smearControlNode.h"
#include "motionLinesNode.h"
#include "loadCacheCmd.h"

/*
================================================================================
This plugin uses Python scripts that require NumPy. To ensure Maya can import
NumPy inside its embedded Python (mayapy), follow these steps:

1. Open an **elevated command prompt** (Run as Administrator):
   - Press Start
   - Search for "CMD"
   - Right-click and select "Run as administrator"

2. Navigate to Maya's Python directory:
   For example, for Maya 2023:
       cd "C:\Program Files\Autodesk\Maya2023\bin"

3. Install NumPy using mayapy:
       mayapy -m pip install numpy

This will install NumPy into Maya's embedded Python environment.
================================================================================
*/

class PluginMain : public MPxCommand {
private: 
    MColor PluginMain::motionOffsetToColor(const MVector& offset) {
        // Normalize the offset to get the direction
        MVector direction = offset.normal();

        // Map direction to RGB colors
        float r = fabs(direction.x); // X direction -> Red
        float g = fabs(direction.y); // Y direction -> Green
        float b = fabs(direction.z); // Z direction -> Blue

        // Ensure values are within [0, 1]
        r = std::min(std::max(r, 0.0f), 1.0f);
        g = std::min(std::max(g, 0.0f), 1.0f);
        b = std::min(std::max(b, 0.0f), 1.0f);

        return MColor(r, g, b, 1.0f); // Return the color
    }

public:
    MStatus doIt(const MArgList&) override {}

    static void* creator() {
        return new PluginMain();
    }
};

MStatus executeMELScript() {
    MStatus status;

    const char* melScript = R"(
        global proc applySmearGUI() {
            // Delete any existing window
            if (`window -exists applySmearWindow`)
                deleteUI applySmearWindow;

            // Create the main window for the control panel
            window -title "Apply Smear" applySmearWindow;
            columnLayout -adjustableColumn true;

            // Add a button that calls createSmearRelatedNodes()
            button -label "Create Smear Nodes" -command ("createSmearRelatedNodes") createSmearButton;
            showWindow applySmearWindow;
        }

        // New procedure for creating the Nodes required for SMEAR and Motion Lines
        global proc createSmearRelatedNodes() {
            // Get the currently selected object(s)
            string $sel[] = `ls -sl`;
            if (size($sel) == 0) {
                error "No object selected. Please select an object before running createSmearRelatedNodes().";
            }
            string $target = $sel[0];

            // Create the deformer node on the selected object using the custom deformer type
            string $deformerNodes[] = `deformer -type "SmearDeformerNode" $target`;
            string $deformerNode = $deformerNodes[0];

            // Connect the scene time to the deformer node's time attribute
            connectAttr "time1.outTime" ($deformerNode + ".time");

            // Create the control node (SmearControlNode)
            string $controlNode = `createNode SmearControlNode -name "smearControl1"`;
            addAttr -ln "triggerSmear" -at "bool" -dv 0 $controlNode;
            connectAttr ($controlNode + ".triggerSmear") ($deformerNode + ".trigger");

            // Connect the message attribute from the control node to the deformer node.
            // (Assuming the control node defines a message attribute named "controlMessage"
            // and the deformer node defines its corresponding message attribute as "inputControlMessage")
            connectAttr -force ($controlNode + ".controlMessage") ($deformerNode + ".inputControlMessage");

            // Connect the 'elongationStrengthPast' attribute
            connectAttr ($controlNode + ".elongationStrengthPast") ($deformerNode + ".ps");

            // Connect the 'elongationStrengthFuture' attribute
            connectAttr ($controlNode + ".elongationStrengthFuture") ($deformerNode + ".fs");

            // Connect the 'elongationSmoothWindow' attribute
            connectAttr ($controlNode + ".elongationSmoothWindow") ($deformerNode + ".smwin");
     
            // Connect the 'applyElongation' attribute
            connectAttr ($controlNode + ".applyElongation") ($deformerNode + ".apl");
     
            // -----------------------------
            // Create and connect the Motion Lines node
            // -----------------------------

            // Create the MotionLines node (using its node type) and give it a name, e.g., "MotionLinesNode1"
            createNode MotionLinesNode -name "MotionLinesNode1";
            
            // Create transform and mesh that we will use to output motion lines mesh
            createNode transform -n MotionLines1;
            createNode mesh -n MotionLinesShape1 -p MotionLines1;
            sets -add initialShadingGroup MotionLinesShape1;
            
            string $motionLinesNode = "MotionLinesNode1";

            // Connect the scene time to the MotionLines node's "time" attribute
            connectAttr "time1.outTime" ($motionLinesNode + ".tm");
            
            // Connect the node mesh output to the mesh we just created so that motion lines are visible
            connectAttr MotionLinesNode1.outputMesh MotionLinesShape1.inMesh;

            // Connect the selected object's mesh to the MotionLines node's input mesh attribute.
            // This replaces the hardcoded cube; now the node will use the mesh from $target.
            connectAttr ($target + ".outMesh") ($motionLinesNode + ".in");

            // Connect the message attribute from the control node to the MotionLines node.
            // (Assuming MotionLinesNode defines its control message attribute as "inputControlMsg")
            connectAttr -force ($controlNode + ".controlMessage") ($motionLinesNode + ".icm");

            // Optionally, connect additional control node attributes to the MotionLines node's attributes
            connectAttr ($controlNode + ".motionLinesStrengthPast")   ($motionLinesNode + ".ps");
            connectAttr ($controlNode + ".motionLinesStrengthFuture") ($motionLinesNode + ".fs");
            connectAttr ($controlNode + ".motionLinesSmoothWindow")     ($motionLinesNode + ".smwin");
            connectAttr ($controlNode + ".motionLinesCount") ($motionLinesNode + ".mlcnt");
            connectAttr ($controlNode + ".generateMotionLines") ($motionLinesNode + ".gen");

            // Open the control panel window for editing node attributes
            smearControlGUI(); 
        }

    global proc smearControlGUI() {
        // Delete any existing window
        if (`window -exists smearControlWindow`)
            deleteUI smearControlWindow;

        // Create the main window for the control panel
        window -title "Smear Control Panel" smearControlWindow;
        columnLayout -adjustableColumn true;

        button 
            -label "Bake Smear"
            -command ("python(\"import vertex_cache_tool; vertex_cache_tool.full_bake_and_trigger()\");")
            bakeSmearButton;

        // Create a frame layout (collapsible section) for the "Elongated in-between" category
        frameLayout -label "Elongated in-between" -collapsable true -collapse false;
            columnLayout -adjustableColumn true;
                // Create a checkbox for toggling Motion Lines generation
                checkBox -label "Apply Elongation" applyElongationCheckbox;
                connectControl "applyElongationCheckbox" "smearControl1.applyElongation";

                // Create a slider for Past Strength (attribute smearControl1.sp)
                floatSliderGrp -label "Past Strength:" -field true -min 0 -max 5 pastStrengthSlider;
                connectControl "pastStrengthSlider" "smearControl1.sp";

                // Create a slider for Future Strength (attribute smearControl1.sf)
                floatSliderGrp -label "Future Strength:" -field true -min 0 -max 5 futureStrengthSlider;
                connectControl "futureStrengthSlider" "smearControl1.sf";

                // Create a slider for Smooth Window (attribute smearControl1.sw)
                intSliderGrp -label "Smooth Window:" -field true -min 0 -max 5 elongationSmoothWindowSlider;
                connectControl "elongationSmoothWindowSlider" "smearControl1.sw";
            setParent ..; // End inner columnLayout
        setParent ..; // End frameLayout

        // Create a frame layout (collapsible section) for the "Motion Lines" category
        frameLayout -label "Motion Lines" -collapsable true -collapse false;
            columnLayout -adjustableColumn true;
                // Create a checkbox for toggling Motion Lines generation
                // (assumed attribute name: smearControl1.generateMotionLines)
                checkBox -label "Generate Motion Lines" generateMotionLinesCheckbox;
                connectControl "generateMotionLinesCheckbox" "smearControl1.generateMotionLines";
                
                // Create a slider for Motion Lines Smooth Window
                // (assumed attribute name: smearControl1.motionLinesCount)
                intSliderGrp -label "Motion Lines Count:" -field true -min 0 -max 100 motionLinesCountlider;
                connectControl "motionLinesCountlider" "smearControl1.motionLinesCount";
                
                // Create a slider for Motion Lines Past Strength 
                // (assumed attribute name: smearControl1.motionLinesStrengthPast)
                floatSliderGrp -label "Motion Lines Past Strength:" -field true -min 0 -max 5 motionLinesPastStrengthSlider;
                connectControl "motionLinesPastStrengthSlider" "smearControl1.motionLinesStrengthPast";

                // Create a slider for Motion Lines Future Strength
                // (assumed attribute name: smearControl1.motionLinesStrengthFuture)
                floatSliderGrp -label "Motion Lines Future Strength:" -field true -min 0 -max 5 motionLinesFutureStrengthSlider;
                connectControl "motionLinesFutureStrengthSlider" "smearControl1.motionLinesStrengthFuture";

                // Create a slider for Motion Lines Smooth Window
                // (assumed attribute name: smearControl1.motionLinesSmoothWindow)
                intSliderGrp -label "Motion Lines Smooth Window:" -field true -min 0 -max 5 motionLinesSmoothWindowSlider;
                connectControl "motionLinesSmoothWindowSlider" "smearControl1.motionLinesSmoothWindow";
            setParent ..;  // End inner columnLayout
        setParent ..;      // End frameLayout

        // Show the window so it's visible
        showWindow smearControlWindow;
    }

    )";


    // Execute the MEL script
    status = MGlobal::executeCommand(melScript);
    if (status != MS::kSuccess) {
        cerr << "Error executing MEL script!" << endl;
    }

    MGlobal::executeCommand(R"(
        global string $gMainWindow;
        if (`menu -exists smearinMenu`) {
            deleteUI smearinMenu;
        }
        setParent $gMainWindow;
        menu -label "SMEARin" -tearOff true smearinMenu;

        // Create a new shelf button that calls createSmearRelatedNodes()
        menuItem 
            -label "Apply Smear"
            -annotation "Apply Smear to selected object" 
            -command "createSmearRelatedNodes"
            applySmearMenu;
    )");

    return status;
}

// Plugin registration
MStatus initializePlugin(MObject obj) {
    MStatus   status = MStatus::kSuccess;
    MFnPlugin plugin(obj, "SMEARin", "1.0", "Any");

    status = plugin.registerNode(
        "SmearNode", 
        SmearNode::id,
        SmearNode::creator, 
        SmearNode::initialize);

    if (!status) {
        status.perror("registerNode SmearNode");  
        return status;
    }

    status = plugin.registerNode(
        "SmearDeformerNode",             // Node name used in Maya
        SmearDeformerNode::id,           // MTypeId
        SmearDeformerNode::creator,      // Creator function
        SmearDeformerNode::initialize,   // Initialize function
        MPxNode::kDeformerNode           // Very important for deformer!
    );

    if (!status) {
        status.perror("registerNode SmearDeformerNode");
        return status;
    }

    status = plugin.registerNode(
        "SmearControlNode",             // Node name used in Maya
        SmearControlNode::id,           // MTypeId
        SmearControlNode::creator,      // Creator function
        SmearControlNode::initialize   // Initialize function
    );

    if (!status) {
        status.perror("registerNode SmearControlNode");
        return status;
    }

    status = plugin.registerNode(
        "MotionLinesNode",             // Node name used in Maya
        MotionLinesNode::id,           // MTypeId
        MotionLinesNode::creator,      // Creator function
        MotionLinesNode::initialize   // Initialize function
    );

    if (!status) {
        status.perror("registerNode MotionLinesNode");
        return status;
    }

    plugin.registerCommand("loadCache", LoadCacheCmd::creator);


    MGlobal::executePythonCommand(R"(
import sys, os
scripts_path = os.path.abspath(os.path.join(os.getcwd(), '../scripts'))
if scripts_path not in sys.path:
    sys.path.insert(0, scripts_path)

try:
    import vertex_cache_tool  # preload into sys.modules
    print('[SMEARin] Preloaded vertex_cache_tool')
except Exception as e:
    import traceback
    traceback.print_exc()
)");

    // Adds plugin related GUI to the Maya toolbar
    executeMELScript();

    Smear::loadCache("C:\\Users\\Admin\\Documents\\School\\cis-6600-advanced-topics-in-computer-graphics\\SMEARin\\smearin\\cache\\cache.json"); 

    return MStatus::kSuccess;
}

MStatus uninitializePlugin(MObject obj) {
    MStatus   status = MStatus::kSuccess;
    MFnPlugin plugin(obj);

    status = plugin.deregisterNode(SmearNode::id);
    if (!status) {
        status.perror("deregisterNode SmearNode");
        return status;
    }

    status = plugin.deregisterNode(SmearDeformerNode::id);
    if (!status) {
        status.perror("deregisterNode SmearDeformerNode");
        return status;
    }

    status = plugin.deregisterNode(SmearControlNode::id);
    if (!status) {
        status.perror("deregisterNode SmearControlNode");
        return status;
    }

    status = plugin.deregisterNode(MotionLinesNode::id);
    if (!status) {
        status.perror("deregisterNode MotionLinesNode");
        return status;
    }

    plugin.deregisterCommand("loadCache");


    return MStatus::kSuccess;
}
