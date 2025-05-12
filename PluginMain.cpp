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
    MColor motionOffsetToColor(const MVector& offset) {
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
    import gui
    gui.add_smear_menu()
    print('[SMEARin] Preloaded vertex_cache_tool and GUI scripts')
except Exception as e:
    import traceback
    traceback.print_exc()
)");

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
