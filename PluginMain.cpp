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
    MStatus doIt(const MArgList&) override {
        MGlobal::displayInfo("Command executed!");

        MSelectionList selection;
        MGlobal::getActiveSelectionList(selection);
        
        if (selection.length() == 0) {
            MGlobal::displayError("No mesh selected!");
            return MS::kFailure;
        }

        MGlobal::displayInfo(MString("Selection Length: ") + selection.length());

        MDagPath dagPath;
        selection.getDagPath(0, dagPath);

        if (!dagPath.hasFn(MFn::kMesh)) {
            MGlobal::displayError("Selected object is not a mesh!");
            return MS::kFailure;
        }

        MGlobal::displayInfo("Mesh detected, proceeding with processing...");
            
        // Test extractAnimationFrameRange
        double startFrame, endFrame;
        MStatus status = Smear::extractAnimationFrameRange(dagPath.node(), startFrame, endFrame);
        if (status == MS::kSuccess) {
            MGlobal::displayInfo(MString("Start Frame: ") + startFrame);
            MGlobal::displayInfo(MString("End Frame: ") + endFrame);
        }
        else {
            MGlobal::displayError("Failed to extract animation frame range.");
            return status;
        }

        // Step 1: Get Motion Offsets
        MotionOffsetSimple motionOffsets;
        status = Smear::computeMotionOffsetsSimple(dagPath.node(), motionOffsets);
        if (!status) {
            MGlobal::displayError("Failed to compute motion offsets.");
            return status;
        }

        // Step 2: Compute Colors Based on Motion Offsets
        MFnMesh meshFn(dagPath);
        int numVertices = meshFn.numVertices();
        MIntArray vertexIndices;
        MColorArray colors;
        colors.setLength(numVertices);

        int lastFrameIndex = static_cast<int>(motionOffsets.motionOffsets.size()) - 1;
        if (lastFrameIndex < 0) {
            MGlobal::displayError("No motion offset data available.");
            return MS::kFailure;
        }

        const MVectorArray& lastFrameOffsets = motionOffsets.motionOffsets[lastFrameIndex];

        for (int i = 0; i < numVertices; i++) {
            colors[i] = motionOffsetToColor(lastFrameOffsets[i]);  // Use helper function
            MGlobal::displayInfo(MString("Offset: ") + lastFrameOffsets[i].x + lastFrameOffsets[i].y + lastFrameOffsets[i].z);

            vertexIndices.append(i);
        }

        // Step 3: Apply Vertex Colors to Mesh
        status = meshFn.setVertexColors(colors, vertexIndices);
        if (!status) {
            MGlobal::displayError("Failed to apply vertex colors.");
            return status;
        }

        MGlobal::displayInfo("Vertex colors applied successfully!");
        return MS::kSuccess;
    }

    static void* creator() {
        return new PluginMain();
    }
};

// Plugin registration
MStatus initializePlugin(MObject obj) {
    MStatus   status = MStatus::kSuccess;
    MFnPlugin plugin(obj, "SMEARin", "1.0", "Any");
    
    // Register Command
    status = plugin.registerCommand( "colorVertices", PluginMain::creator );
    if (!status) {
        status.perror("registerCommand");
        return status;
    }

    status = plugin.registerNode("SmearNode", SmearNode::id,
        SmearNode::creator, SmearNode::initialize);

    if (!status) {
        status.perror("registerNode");  
        return status;
    }
}

MStatus uninitializePlugin(MObject obj) {
    MStatus   status = MStatus::kSuccess;
    MFnPlugin plugin(obj);

    status = plugin.deregisterCommand("colorVertices");
    if (!status) {
        status.perror("deregisterCommand");
        return status;
    }

    status = plugin.deregisterNode(SmearNode::id);
    if (!status) {
        status.perror("deregisterNode");
        return status;
    }
}
