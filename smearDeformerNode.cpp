#include "smearDeformerNode.h"
#include <maya/MDagPath.h>
#include <maya/MItGeometry.h>
#include <maya/MFnUnitAttribute.h>
#include <maya/MFnTypedAttribute.h>
#include <maya/MFnNumericAttribute.h>
#include <maya/MStatus.h>
#include <maya/MGlobal.h>
#include <maya/MDagPathArray.h>
#include <maya/MFnDependencyNode.h>
#include <math.h>
#include <maya/MPoint.h>


#define McheckErr(stat, msg)        \
    if (MS::kSuccess != stat) {     \
        MGlobal::displayError(msg); \
        return MS::kFailure;        \
    }


MTypeId SmearDeformerNode::id(0x98530); // Random id 
MObject SmearDeformerNode::time;
MObject SmearDeformerNode::smoothWindowSize;
MObject SmearDeformerNode::smoothEnabled;
MObject SmearDeformerNode::aStrength;


SmearDeformerNode::SmearDeformerNode():
    motionOffsets(), motionOffsetsBaked(false)
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
    MStatus status;

    // Time attribute
    time = unitAttr.create("time", "tm", MFnUnitAttribute::kTime, 0.0);
    addAttribute(time);

    smoothEnabled = numAttr.create("smoothEnabled", "smenb", MFnNumericData::kBoolean, true);
    addAttribute(smoothEnabled);

    smoothWindowSize = numAttr.create("smoothWindow", "smwin", MFnNumericData::kInt, 2);
    numAttr.setMin(0);
    numAttr.setMax(5);
    addAttribute(smoothWindowSize);

    aStrength = numAttr.create("strength", "s", MFnNumericData::kDouble, 1.5);
    numAttr.setMin(0);
    numAttr.setMax(5);
    addAttribute(aStrength);
    
    return MS::kSuccess;
}

MStatus SmearDeformerNode::deform(MDataBlock& block, MItGeometry& iter, const MMatrix& localToWorldMatrix, unsigned int multiIndex)
{
    MStatus status; 

    MDataHandle timeDataHandle = block.inputValue(time, &status); 
    McheckErr(status, "Failed to obtain data handle for time input"); 

    MTime currentTime = timeDataHandle.asTime();
    double currentFrame = currentTime.as(MTime::kFilm);
    
    // Get mesh and transform DAG path
    MFnDependencyNode thisNodeFn(thisMObject());
    MObject thisNode = thisMObject();
    MPlug inputPlug(thisMObject(), input);
    inputPlug = inputPlug.elementByLogicalIndex(0).child(inputGeom);

    MObject meshObj;
    inputPlug.getValue(meshObj);
    McheckErr(status, "Failed to get mesh object");
    
    //MPlug inputPlug = thisNodeFn.findPlug(inputMesh, true);
    MDagPath meshPath, transformPath;
    status = Smear::getDagPathsFromInputMesh(meshObj, inputPlug, transformPath, meshPath);

    MGlobal::displayInfo(MString("Mesh path: ") + meshPath.fullPathName());
    MGlobal::displayInfo(MString("Transform path: ") + transformPath.fullPathName());

    // Check if the provided path point to correct node types.
    if (!meshPath.hasFn(MFn::kMesh)) {
        MGlobal::displayError("Smear::calculateCentroidOffsetFromPivot - meshPath does not point to a mesh node.");
        //return MS::kFailure; // Not a mesh node.
    }
    else if (!transformPath.hasFn(MFn::kTransform)) {
        MGlobal::displayError("Smear::calculateCentroidOffsetFromPivot - tranformPath does not point to a transform node.");
        //return MS::kFailure; // Not a transform node.
    }

    // +++ Compute motion offsets using Smear functions +++
    if (!motionOffsetsBaked) {
        status = Smear::computeMotionOffsetsSimple(meshPath, transformPath, motionOffsets);
        McheckErr(status, "Failed to compute motion offsets");
        motionOffsetsBaked = true;
    }

    int frameIndex = static_cast<int>(currentFrame - motionOffsets.startFrame);
    
    if (frameIndex < 0 || frameIndex >= motionOffsets.motionOffsets.size()) {
        return MS::kSuccess; // Skip invalid frames
    }
    const MDoubleArray& offsets = motionOffsets.motionOffsets[frameIndex];
    const std::vector<MPointArray>& trajectories = motionOffsets.vertexTrajectories;
    const int numFrames = trajectories.size();

    const bool smoothingEnabled = block.inputValue(smoothEnabled).asBool();
    const int N = smoothingEnabled ? block.inputValue(smoothWindowSize).asInt() : 0;
    std::vector<double> smoothedOffsets(offsets.length(), 0.0);

    // Precompute smoothed offsets for all vertices
    for (int vertIdx = 0; vertIdx < offsets.length(); ++vertIdx) {
        double totalWeight = 0.0;
        double smoothed = 0.0;

        for (int n = -N; n <= N; ++n) {
            const int frame = frameIndex + n;

            // Skip out-of-bounds frames
            if (frame < 0 || frame >= motionOffsets.motionOffsets.size()) continue;

            // Calculate weight
            const double normalized = std::abs(n) / static_cast<double>(N + 1);
            const double weight = std::pow(1.0 - std::pow(normalized, 2.0), 2.0);

            smoothed += motionOffsets.motionOffsets[frame][vertIdx] * weight;
            totalWeight += weight;
        }

        // Store final smoothed offset
        smoothedOffsets[vertIdx] = totalWeight > 0.0 ? smoothed / totalWeight : offsets[vertIdx];
    }

    // Artistic control param
    const double strength = block.inputValue(aStrength).asDouble();

    MPoint point; 
    for (; !iter.isDone(); iter.next()) {
        const int vertIdx = iter.index();

        if (frameIndex < 0 || frameIndex >= motionOffsets.motionOffsets.size()) {
            continue;
        }

        // Get motion offset and apply strength
        double offset = smoothedOffsets[vertIdx];
        const double beta = offset * strength;

        const int frameOffset = static_cast<int>(floor(beta));
        const double t = beta - frameOffset;

        const int baseFrame = frameIndex + frameOffset;
        // Clamp frame indices
        const int f0 = std::max(0, std::min(numFrames - 1, baseFrame - 1));
        const int f1 = std::max(0, std::min(numFrames - 1, baseFrame));
        const int f2 = std::max(0, std::min(numFrames - 1, baseFrame + 1));
        const int f3 = std::max(0, std::min(numFrames - 1, baseFrame + 2));

        // Get trajectory points
        const MPoint& p0 = trajectories[f0][vertIdx];
        const MPoint& p1 = trajectories[f1][vertIdx];
        const MPoint& p2 = trajectories[f2][vertIdx];
        const MPoint& p3 = trajectories[f3][vertIdx];

        MPoint interpolated = SmearDeformerNode::catmullRomInterpolate(p0, p1, p2, p3, t);

        iter.setPosition(interpolated);
    }
    return MS::kSuccess();
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