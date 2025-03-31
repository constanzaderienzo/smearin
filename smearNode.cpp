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
#include <maya/MItGeometry.h>

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
MObject SmearNode::inputGeom; 
MObject SmearNode::outputGeom;
MObject SmearNode::betaMax;
MObject SmearNode::enableDeformation;

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
    inputGeom = typedAttr.create("inputGeom", "ig", MFnData::kMesh);
    typedAttr.setStorable(true);
    typedAttr.setWritable(true);
    addAttribute(inputGeom);

    // Output mesh
    outputGeom = typedAttr.create("outputGeom", "out", MFnData::kMesh);
    typedAttr.setWritable(false);
    typedAttr.setStorable(false);
    addAttribute(outputGeom);

    enableDeformation = numericAttr.create("enableDeformation", "ed", MFnNumericData::kBoolean, 1);
    addAttribute(enableDeformation);

    betaMax = numericAttr.create("betaMax", "bm", MFnNumericData::kDouble, 1.0);
    numericAttr.setMin(0.0);
    numericAttr.setMax(5.0);
    addAttribute(betaMax);

    // Affects relationships
    attributeAffects(enableDeformation, outputGeom);
    attributeAffects(betaMax, outputGeom);
    attributeAffects(time, outputGeom);
    attributeAffects(inputGeom, outputGeom);

    return MS::kSuccess;
}

MStatus SmearNode::deform(MDataBlock& block,
    MItGeometry& iter,
    const MMatrix& localToWorldMatrix,
    unsigned int multiIndex)
{
    MStatus status;

    // Get parameters
    const bool enableDef = block.inputValue(enableDeformation).asBool();
    const double betaMaxVal = block.inputValue(betaMax).asDouble();

    if (!enableDef || !motionOffsetsBaked)
        return MS::kSuccess;

    // Validate frame data
    MTime currentTime = block.inputValue(time).asTime();
    const double frame = currentTime.as(MTime::kFilm);

    MDataHandle inputHandle = data.inputValue(inputGeom, &status);
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
    MPlug inputPlug = thisNodeFn.findPlug(inputGeom, true);

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

    const int frameIndex = static_cast<int>(frame - motionOffsetsSimple.startFrame);

    if (frameIndex < 0 || frameIndex >= motionOffsetsSimple.motionOffsets.size())
        return MS::kSuccess;

    // Get trajectory data
    const MDoubleArray& offsets = motionOffsetsSimple.motionOffsets[frameIndex];
    const std::vector<MPointArray>& trajectories = motionOffsetsSimple.vertexTrajectories;

    // Parallel deformation
    float weight = weightValue(block, multiIndex, iter.index());
    MPoint point;

    for (; !iter.isDone(); iter.next()) {
        const int vertIdx = iter.index();

        if (vertIdx >= offsets.length())
            continue;

        // Get trajectory points
        double beta = offsets[vertIdx] * betaMaxVal * weight;
        int baseFrame = frameIndex + static_cast<int>(floor(beta));
        double t = beta - floor(beta);

        // Clamp indices to valid range
        const int numFrames = trajectories.size();
        int f0 = std::max(0, baseFrame - 1);
        int f1 = std::max(0, std::min(numFrames - 1, baseFrame));
        int f2 = std::min(numFrames - 1, baseFrame + 1);
        int f3 = std::min(numFrames - 1, baseFrame + 2);

        // Get positions from trajectories
        const MPoint& p0 = trajectories[f0][vertIdx];
        const MPoint& p1 = trajectories[f1][vertIdx];
        const MPoint& p2 = trajectories[f2][vertIdx];
        const MPoint& p3 = trajectories[f3][vertIdx];

        // Apply interpolation
        point = iter.position();
        point = catmullRomInterpolate(p0, p1, p2, p3, t);
        iter.setPosition(point);
    }

    return MS::kSuccess;
}


MPoint SmearNode::catmullRomInterpolate(const MPoint& p0, const MPoint& p1,
    const MPoint& p2, const MPoint& p3,
    double t) {
    // Basis matrix coefficients
    const double t2 = t * t;
    const double t3 = t2 * t;

    return 0.5 * (
        (-1*p0 + 3 * p1 - 3 * p2 + p3) * t3 +
        (2 * p0 - 5 * p1 + 4 * p2 - p3) * t2 +
        (-1*p0 + p2) * t +
        2 * p1
        );
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
