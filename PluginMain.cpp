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
}
