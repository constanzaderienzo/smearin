#include "smearNode.h"
#include <maya/MFnMesh.h>
#include <maya/MFnUnitAttribute.h>
#include <maya/MFnTypedAttribute.h>
#include <maya/MFnDependencyNode.h>
#include <maya/MStatus.h>
#include <maya/MGlobal.h>
#include <maya/MFnMeshData.h>
#include <maya/MFnTransform.h>
#include <maya/MPointArray.h>
#include <maya/MVectorArray.h>
#include <maya/MItMeshVertex.h>
#include <maya/MAnimControl.h>
#include <maya/MTime.h>
#include <maya/MFnNumericAttribute.h>
#include <maya/MDagPath.h>

#include "smear.h" // Include the Smear header
// Replace the vertex loop with parallel processing:
//#include <tbb/parallel_for.h>

#define McheckErr(stat, msg)        \
    if (MS::kSuccess != stat) {     \
        MGlobal::displayError(msg); \
        return MS::kFailure;        \
    }

MTypeId SmearNode::id(0x98520); // Random id 
MObject SmearNode::time;
MObject SmearNode::inputMesh;
MObject SmearNode::outputMesh;

MStatus SmearNode::getDagPathsFromInputMesh(MObject inputMeshDataObj, const MPlug & inputMeshPlug, MDagPath & transformPath, MDagPath & shapePath) const
{
    MStatus status;

    // Get the plug's source connection
    MPlugArray connectedPlugs;
    if (!inputMeshPlug.connectedTo(connectedPlugs, true, false) || connectedPlugs.length() == 0) {
        MGlobal::displayError("inputMesh is not connected to any mesh.");
        return MS::kFailure;
    }

    MPlug sourcePlug = connectedPlugs[0];
    MObject sourceNode = sourcePlug.node();

    // Get DAG path to that source node (should be a mesh shape)
    MDagPath dagPath;
    status = MDagPath::getAPathTo(sourceNode, dagPath);
    if (!status) {
        MGlobal::displayError("Failed to get MDagPath from connected source node.");
        return status;
    }

    if (dagPath.node().hasFn(MFn::kMesh)) {
        shapePath = dagPath;
        status = dagPath.pop();
        if (!status || !dagPath.node().hasFn(MFn::kTransform)) {
            MGlobal::displayError("Failed to get transform from mesh shape.");
            return MS::kFailure;
        }
        transformPath = dagPath;
    }
    else {
        MGlobal::displayError("Source node is not a mesh shape.");
        return MS::kFailure;
    }
    return MS::kSuccess;
}


SmearNode::SmearNode():
    motionOffsetsSimple(), motionOffsetsBaked(false) 
{}

SmearNode::~SmearNode()
{}

void* SmearNode::creator()
{
    return new SmearNode;
}

MStatus SmearNode::initialize() {
    MFnUnitAttribute unitAttr;
    MFnTypedAttribute typedAttr;
    MFnNumericAttribute numericAttr;
    MStatus status;

    // Time attribute
    time = unitAttr.create("time", "tm", MFnUnitAttribute::kTime, 0.0);
    addAttribute(time);

    // Input mesh
    inputMesh = typedAttr.create("inputMesh", "in", MFnData::kMesh);
    typedAttr.setStorable(true);
    addAttribute(inputMesh);

    // Output mesh
    outputMesh = typedAttr.create("outputMesh", "out", MFnData::kMesh);
    typedAttr.setWritable(false);
    typedAttr.setStorable(false);
    addAttribute(outputMesh);


    // Affects relationships
    attributeAffects(time, outputMesh);
    attributeAffects(inputMesh, outputMesh);

    return MS::kSuccess;
}

MStatus SmearNode::compute(const MPlug& plug, MDataBlock& data) {
    if (plug != outputMesh) return MS::kUnknownParameter;

    MStatus status;

    // +++ Get time value +++
    MTime currentTime = data.inputValue(time, &status).asTime();
    McheckErr(status, "Failed to get time value");
    double frame = currentTime.as(MTime::kFilm);  // Get time in frames

    MDataHandle inputHandle = data.inputValue(inputMesh, &status);
    McheckErr(status, "Failed to get input mesh");
    MObject inputObj = inputHandle.asMesh();

    if (inputObj.isNull() || !inputObj.hasFn(MFn::kMesh)) {
        MGlobal::displayError("Input is not a valid mesh");
        return MS::kFailure;
    }

    // Create new mesh data
    MFnMeshData meshData;
    MObject newOutput = meshData.create(&status);
    McheckErr(status, "Failed to create output mesh container");

    // Copy mesh using API method
    MFnMesh inputFn(inputObj);
    MObject copiedMesh = inputFn.copy(inputObj, newOutput, &status);
    McheckErr(status, "Mesh copy failed");

    // Get DAG paths for mesh and transform
    MFnDependencyNode thisNodeFn(thisMObject());
    MPlug inputPlug = thisNodeFn.findPlug(inputMesh, true);

    MDagPath shapePath, transformPath;
    status = getDagPathsFromInputMesh(inputObj, inputPlug, transformPath, shapePath);
    McheckErr(status, "Failed to tranform path and shape path from input object");
    
    // Cast copied Mesh into MfnMesh
    MFnMesh outputFn(copiedMesh, &status);
    McheckErr(status, "Output mesh init failed");

    const int numVertices = outputFn.numVertices();
    if (numVertices == 0) {
        MGlobal::displayError("Mesh has no vertices");
        return MS::kFailure;
    }

    // +++ Compute motion offsets using Smear functions +++
    if (!motionOffsetsBaked) {
        status = Smear::computeMotionOffsetsSimple(shapePath, transformPath, motionOffsetsSimple);
        McheckErr(status, "Failed to compute motion offsets");
        motionOffsetsBaked = true;
    }

    int frameIndex = static_cast<int>(frame - motionOffsetsSimple.startFrame);

    MGlobal::displayInfo("Current frame: " + MString() + frame +
        " Start frame: " + motionOffsetsSimple.startFrame +
        " Frame index: " + frameIndex);

    if (frameIndex < 0 || frameIndex >= motionOffsetsSimple.motionOffsets.size()) {
        return MS::kSuccess;
    }

    MDoubleArray& currentFrameOffsets = motionOffsetsSimple.motionOffsets[frameIndex];
    if (currentFrameOffsets.length() != numVertices) {
        MGlobal::displayError("Offset/vertex count mismatch");
        return MS::kFailure;
    }

    // +++ Map motion offsets to colors +++
    MColorArray colors(numVertices);
    MIntArray vtxIndices(numVertices);

    for (int i = 0; i < numVertices; ++i) {
        double offset = currentFrameOffsets[i];
        colors[i] = computeColor(offset);
        vtxIndices[i] = i;  
    }

    // Create/update color set
    const MString colorSet("smearSet");

    outputFn.createColorSetWithName(colorSet);
    outputFn.setCurrentColorSetName(colorSet);

    // +++ Apply colors to specific color set +++
    status = outputFn.setVertexColors(colors, vtxIndices);
    McheckErr(status, "Failed to set colors");

    // Force viewport update
    outputFn.updateSurface();

    // Set output
    data.outputValue(outputMesh).set(newOutput);
    data.setClean(plug);

    return MS::kSuccess;
}

MColor SmearNode::computeColor(double offset) {
    if (offset > 0.01) {
        return MColor(1.0f, 0.0f, 0.0f, 1.0f); // Pure red (forward)
    }
    else if (offset < -0.01) {
        return MColor(0.0f, 0.0f, 1.0f, 1.0f); // Pure blue (backward)
    }
    else {
        return MColor(1.0f, 1.0f, 1.0f, 1.0f); // Pure white (backward)
    }
}
