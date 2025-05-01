#include "smearDeformerNode.h"
#include <maya/MDagPath.h>
#include <maya/MItGeometry.h>
#include <maya/MFnUnitAttribute.h>
#include <maya/MFnTypedAttribute.h>
#include <maya/MFnNumericAttribute.h>
#include <maya/MFnMessageAttribute.h>
#include <maya/MStatus.h>
#include <maya/MGlobal.h>
#include <maya/MDagPathArray.h>
#include <maya/MFnDependencyNode.h>
#include <maya/MItDependencyGraph.h>
#include <math.h>
#include <maya/MPoint.h>
#include <maya/MTimeArray.h>
#include <maya/MFnSkinCluster.h>
#include <maya/MFnSingleIndexedComponent.h>
#include <maya/MAnimControl.h>
#include <filesystem>
namespace fs = std::filesystem;

#define McheckErr(stat, msg)        \
    if (MS::kSuccess != stat) {     \
        MGlobal::displayError(msg); \
        return MS::kFailure;        \
    }


MTypeId SmearDeformerNode::id(0x98530); // Random id 
MObject SmearDeformerNode::time;
MObject SmearDeformerNode::elongationSmoothWindowSize;
MObject SmearDeformerNode::smoothEnabled;
MObject SmearDeformerNode::aelongationStrengthPast;
MObject SmearDeformerNode::aelongationStrengthFuture; 
MObject SmearDeformerNode::aApplyElongation;
MObject SmearDeformerNode::trigger;

// Message attribute for connecting to the control node.
MObject SmearDeformerNode::inputControlMsg;

SmearDeformerNode::SmearDeformerNode():
    motionOffsets(), motionOffsetsBaked(false), skinDataBaked(false)
{}

SmearDeformerNode::~SmearDeformerNode()
{}

void* SmearDeformerNode::creator()
{
    return new SmearDeformerNode();
}

MStatus SmearDeformerNode::initialize()
{
    MFnNumericAttribute numAttr;
    MFnUnitAttribute unitAttr;
    MFnTypedAttribute typedAttr;
    MFnMessageAttribute mAttr;  // For message attributes
    MStatus status;

    trigger = numAttr.create("trigger", "trg", MFnNumericData::kBoolean);
    addAttribute(trigger);

    // Time attribute
    time = unitAttr.create("time", "tm", MFnUnitAttribute::kTime, 0.0);
    addAttribute(time);

    smoothEnabled = numAttr.create("smoothEnabled", "smenb", MFnNumericData::kBoolean, true);
    addAttribute(smoothEnabled);

    elongationSmoothWindowSize = numAttr.create("elongationSmoothWindow", "smwin", MFnNumericData::kInt, 2);
    numAttr.setMin(0);
    numAttr.setMax(5);
    addAttribute(elongationSmoothWindowSize);
    
    // The length of the backward (trailing) elongation effect 
    aelongationStrengthPast = numAttr.create("Past Strength", "ps", MFnNumericData::kDouble, 1.5);
    numAttr.setMin(0);
    numAttr.setMax(5);
    addAttribute(aelongationStrengthPast);
    
    // The length of the forward (leading) elongation effect
    aelongationStrengthFuture = numAttr.create("Future Strength", "fs", MFnNumericData::kDouble, 1.5);
    numAttr.setMin(0);
    numAttr.setMax(5);
    addAttribute(aelongationStrengthFuture);

    // Create the boolean attribute for applying elongation.
    aApplyElongation = numAttr.create("applyElongation", "apl", MFnNumericData::kBoolean, true, &status);
    numAttr.setStorable(false);
    numAttr.setKeyable(false);
    addAttribute(aApplyElongation);

    // Create the message attribute that will connect this deformer to the control node.
    inputControlMsg = mAttr.create("inputControlMessage", "icm", &status);
    mAttr.setStorable(false);
    mAttr.setKeyable(false);
    addAttribute(inputControlMsg);

    
    return MS::kSuccess;
}

MStatus SmearDeformerNode::deformSimple(MDataBlock& block, MItGeometry& iter, MDagPath& meshPath, MDagPath& transformPath) {
}
MStatus SmearDeformerNode::deformArticulated(MItGeometry& iter, MDagPath& meshPath) {
}

MStatus SmearDeformerNode::deform(MDataBlock& block, MItGeometry& iter, const MMatrix& localToWorldMatrix, unsigned int multiIndex)
{
    MStatus status; 
       
    // Check if we should run deformation
    bool applyElongation = block.inputValue(aApplyElongation, &status).asBool();
    bool triggerEnabled = block.inputValue(trigger, &status).asBool();
    if (!status || !applyElongation) {
        return MS::kSuccess;
    }

    // 1. Get current mesh information
    MDagPath meshPath, transformPath;
    getDagPaths(block, iter, multiIndex, meshPath, transformPath);
    MString meshName = meshPath.fullPathName();

    // Generate cache path (could be configurable via attribute)
    MString cachePath = createCachePath("pCylinder1");
    MGlobal::displayInfo(MString("Loading cache from: ") + cachePath);
    // Load cache if needed
    if (!loadVertexCache(cachePath)) {
        MString pythonCmd;
        pythonCmd.format(
            "vertex_cache_tool.cache_vertex_trajectories(r'^1s', r'^2s')",
            meshName.asChar(), cachePath.asChar()
        );

        if (MGlobal::executePythonCommand(pythonCmd) != MS::kSuccess) {
            MGlobal::displayError("Failed to generate vertex cache");
            return MS::kFailure;
        }
    }

    // 3. Get deformation parameters
    elongationStrengthPast = block.inputValue(aelongationStrengthPast).asDouble();
    elongationStrengthFuture = block.inputValue(aelongationStrengthFuture).asDouble();
    smoothingEnabled = block.inputValue(smoothEnabled).asBool();
    N = smoothingEnabled ? block.inputValue(elongationSmoothWindowSize).asInt() : 0;

    // 4. Perform deformation
    if (0) {
        deformSimple(block, iter, meshPath, transformPath);
    }
    else {
        deformArticulated(iter, meshPath);
    }

    return MS::kSuccess();
}

MStatus SmearDeformerNode::getDagPaths(MDataBlock& block, MItGeometry iter, unsigned int multiIndex, MDagPath& meshPath, MDagPath& transformPath) 
{
    MStatus status;
    MArrayDataHandle hInputArray = block.inputArrayValue(input, &status);
    if (!status) {
        MGlobal::displayError("Failed to get input geometry array: " + MString(status.errorString()));
        return status;
    }

    // Jump to the element corresponding to the current multiIndex.
    status = hInputArray.jumpToElement(multiIndex);
    if (!status) {
        MGlobal::displayError("Failed to jump to input element.");
        return status;
    }

    // Get the geometry data.
    MDataHandle hInput = hInputArray.inputValue(&status);
    if (!status) {
        MGlobal::displayError("Failed to get the input value.");
        return status;
    }
    MDataHandle hGeom = hInput.child(inputGeom);
    MObject geomObj = hGeom.data();
    // 2. Get connected mesh node from input plug
    MPlug inputPlug(thisMObject(), input);  // input[] plug
    MPlug inputElementPlug = inputPlug.elementByLogicalIndex(multiIndex, &status);
    CHECK_MSTATUS_AND_RETURN_IT(status);

    MPlug geomPlug = inputElementPlug.child(inputGeom, &status);
    CHECK_MSTATUS_AND_RETURN_IT(status);

    MPlugArray connections;
    geomPlug.connectedTo(connections, true, false, &status);  // look upstream.
    CHECK_MSTATUS_AND_RETURN_IT(status);

    MPlug srcPlug = connections[0];
    MObject meshNode = srcPlug.node();

    if (meshNode.hasFn(MFn::kMesh)) {
        // Already a mesh node
    }
    else if (meshNode.hasFn(MFn::kDependencyNode)) {
        // Possibly a shape's outMesh plug � we walk upstream
        MItDependencyGraph dgIt(srcPlug,
            MFn::kMesh,
            MItDependencyGraph::kUpstream,
            MItDependencyGraph::kDepthFirst,
            MItDependencyGraph::kPlugLevel);

        if (!dgIt.isDone()) {
            meshNode = dgIt.currentItem();  // This should be the actual mesh node
        }
        else {
            MGlobal::displayWarning("Upstream mesh node not found.");
            return MStatus::kFailure;
        }
    }

    // 3. Get DAG path to mesh
    status = MDagPath::getAPathTo(meshNode, meshPath);
    CHECK_MSTATUS_AND_RETURN_IT(status);

    // Ensure the path is to the shape, then get the transform path
    transformPath = meshPath;
    transformPath.pop(); // Removes the shape node, leaving the transform 

    return status;
}

// General deformation application using offsets + trajectories
void SmearDeformerNode::applyDeformation(MItGeometry& iter, int frameIndex) {
    const int numFrames = static_cast<int>(motionOffsets.vertexTrajectories.size());
    const MDoubleArray& offsets = motionOffsets.motionOffsets[frameIndex];

    std::vector<double> finalOffsets(offsets.length());
    for (int i = 0; i < offsets.length(); ++i) {
        double sum = 0.0, total = 0.0;
        for (int j = -N; j <= N; ++j) {
            int idx = frameIndex + j;
            if (idx < 0 || idx >= motionOffsets.motionOffsets.size()) continue;
            double w = std::pow(1.0 - std::pow(std::abs(j) / double(N + 1), 2.0), 2.0);
            sum += motionOffsets.motionOffsets[idx][i] * w;
            total += w;
        }
        finalOffsets[i] = (total > 0.0) ? sum / total : offsets[i];
    }

    for (; !iter.isDone(); iter.next()) {
        int idx = iter.index();
        double offset = finalOffsets[idx];
        double t = (offset + 1.0) / 2.0;
        double strength = (1.0 - t) * elongationStrengthPast + t * elongationStrengthFuture;
        double beta = offset * strength;

        int baseFrame = frameIndex + int(floor(beta));
        double localT = beta - floor(beta);

        int f0 = std::clamp(baseFrame - 1, 0, numFrames - 1);
        int f1 = std::clamp(baseFrame, 0, numFrames - 1);
        int f2 = std::clamp(baseFrame + 1, 0, numFrames - 1);
        int f3 = std::clamp(baseFrame + 2, 0, numFrames - 1);

        const MPoint& p0 = motionOffsets.vertexTrajectories[f0][idx];
        const MPoint& p1 = motionOffsets.vertexTrajectories[f1][idx];
        const MPoint& p2 = motionOffsets.vertexTrajectories[f2][idx];
        const MPoint& p3 = motionOffsets.vertexTrajectories[f3][idx];

        iter.setPosition(catmullRomInterpolate(p0, p1, p2, p3, localT));
    }
}

MPoint SmearDeformerNode::catmullRomInterpolate(const MPoint& p0, const MPoint& p1, const MPoint& p2, const MPoint& p3, float t) {
    // SMEAR paper uses standard Catmull-Rom interpolation (Section 4.1)
    const float t2 = t * t;
    const float t3 = t2 * t;

    // Basis matrix coefficients (as per original Catmull-Rom formulation)
    const float a0 = -0.5f * t3 + t2 - 0.5f * t;
    const float a1 = 1.5f * t3 - 2.5f * t2 + 1.0f;
    const float a2 = -1.5f * t3 + 2.0f * t2 + 0.5f * t;
    const float a3 = 0.5f * t3 - 0.5f * t2;

    // Combine control points
    return p0 * a0 + p1 * a1 + p2 * a2 + p3 * a3;
}