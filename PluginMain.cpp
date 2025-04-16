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

        // New procedure for creating the Nodes required for SMEAR
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

            // Connect the message attribute from the control node to the deformer node.
            // (Assuming the control node defines a message attribute named "controlMessage"
            // and the deformer node defines its corresponding message attribute as "inputControlMessage")
            connectAttr -force ($controlNode + ".controlMessage") ($deformerNode + ".inputControlMessage");

            // Optionally, you may want to drive the numeric attributes directly:
            // Here we assume the control node's numeric attributes are:
            //    "strengthPast" with short name "sp"
            //    "strengthFuture" with short name "sf"
            //    "smoothWindow" with short name "sw"
            // And the deformer node's corresponding attributes have short names "ps", "fs", and "smwin"

            // Connect the 'strengthPast' attribute
            connectAttr ($controlNode + ".strengthPast") ($deformerNode + ".ps");

            // Connect the 'strengthFuture' attribute
            connectAttr ($controlNode + ".strengthFuture") ($deformerNode + ".fs");

            // Connect the 'smoothWindow' attribute
            connectAttr ($controlNode + ".smoothWindow") ($deformerNode + ".smwin");
            
            // Connect the 'applyElongation' attribute
            connectAttr ($controlNode + ".applyElongation") ($deformerNode + ".apl");

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
    
        // Create a frame layout (collapsible section) for the "Elongated in-between" category
        frameLayout -label "Elongated in-between" -collapsable true -collapse false;
            columnLayout -adjustableColumn true;
                // Create a slider for Past Strength (attribute smearControl1.sp)
                floatSliderGrp -label "Past Strength:" -field true -min 0 -max 5 pastStrengthSlider;
                connectControl "pastStrengthSlider" "smearControl1.sp";

                // Create a slider for Future Strength (attribute smearControl1.sf)
                floatSliderGrp -label "Future Strength:" -field true -min 0 -max 5 futureStrengthSlider;
                connectControl "futureStrengthSlider" "smearControl1.sf";

                // Create a slider for Smooth Window (attribute smearControl1.sw)
                intSliderGrp -label "Smooth Window:" -field true -min 0 -max 5 smoothWindowSlider;
                connectControl "smoothWindowSlider" "smearControl1.sw";
            setParent ..; // End inner columnLayout
        setParent ..; // End frameLayout

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

    // Adds plugin related GUI to the Maya toolbar
    executeMELScript();

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

    return MStatus::kSuccess;
}
