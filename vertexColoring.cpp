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

class VertexColorCommand : public MPxCommand {
private: 
    MColor motionOffsetToColor(const MVector& offset) {
        float magnitude = static_cast<float>(offset.length());

        if (magnitude == 0.0f) {
            return MColor(0.0f, 0.0f, 0.0f, 1.0f);  // No motion -> black
        }

        // Normalize offset to range [0,1] for color mapping
        float r = fabs(offset.x) / magnitude;
        float g = fabs(offset.y) / magnitude;
        float b = fabs(offset.z) / magnitude;

        return MColor(r, g, b, 1.0f);  // RGB mapped to motion direction
    }

public:
    MStatus doIt(const MArgList&) override {
        MSelectionList selection;
        MGlobal::getActiveSelectionList(selection);
        
        if (selection.length() == 0) {
            MGlobal::displayError("No mesh selected!");
            return MS::kFailure;
        }

        MDagPath dagPath;
        selection.getDagPath(0, dagPath);

        if (!dagPath.hasFn(MFn::kMesh)) {
            MGlobal::displayError("Selected object is not a mesh!");
            return MS::kFailure;
        }

        MFnMesh meshFn(dagPath);
        int numVertices = meshFn.numVertices();

        // Step 1: Get Motion Offsets
        MotionOffsetSimple motionOffsets;
        MStatus status = Smear::computeMotionOffsetsSimple(dagPath.node(), motionOffsets);
        if (!status) {
            MGlobal::displayError("Failed to compute motion offsets.");
            return status;
        }

        // Step 2: Compute Colors Based on Motion Offsets
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
        return new VertexColorCommand();
    }
};

// Plugin registration
MStatus initializePlugin(MObject obj) {
    MStatus   status = MStatus::kSuccess;
    MFnPlugin plugin(obj, "SMEARin", "1.0", "Any");
    
    // Register Command
    status = plugin.registerCommand( "colorVertices", VertexColorCommand::creator );
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
