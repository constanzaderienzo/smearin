#include "smearDeformerNode.h"
#include <maya/MDagPath.h>
#include <maya/MItGeometry.h>
#include <maya/MFnUnitAttribute.h>
#include <maya/MFnTypedAttribute.h>
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

    MFnUnitAttribute unitAttr;
    MFnTypedAttribute typedAttr;
    MStatus status;

    // Time attribute
    time = unitAttr.create("time", "tm", MFnUnitAttribute::kTime, 0.0);
    addAttribute(time);
    
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
    
    const MDoubleArray& offsets = motionOffsets.motionOffsets[frameIndex];
    const std::vector<MPointArray>& trajectories = motionOffsets.vertexTrajectories;
    const int numFrames = trajectories.size();

    MPoint point; 
    for (; !iter.isDone(); iter.next()) {
        const int vertIdx = iter.index();
        MVector normal = iter.normal(); 
        point = iter.position(); 

        if (frameIndex < 0 || frameIndex >= motionOffsets.motionOffsets.size()) {
            point += MPoint(0.f, 0.f, 0.f, 0.f); 
        }
        else {
            // Calculate interpolation parameters
            double offset = offsets[vertIdx];
            const double beta = offsets[vertIdx] * 0.5 * 2.0;
            const int baseFrame = frameIndex + static_cast<int>(floor(beta));
            const double t = beta - floor(beta);

            // Clamp frame indices
            const int f0 = std::max(0, baseFrame - 1);
            const int f1 = std::max(0, std::min(numFrames - 1, baseFrame));
            const int f2 = std::min(numFrames - 1, baseFrame + 1);
            const int f3 = std::min(numFrames - 1, baseFrame + 2);

            // Get trajectory points
            const MPoint& p0 = trajectories[f0][vertIdx];
            const MPoint& p1 = trajectories[f1][vertIdx];
            const MPoint& p2 = trajectories[f2][vertIdx];
            const MPoint& p3 = trajectories[f3][vertIdx];

            // Calculate new position
            point = iter.position();
            MPoint interpolated = SmearDeformerNode::catmullRomInterpolate(p0, p1, p2, p3, t);
            point += interpolated - p1;
        }

        // Apply to vertex
        iter.setPosition(point);
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

//MStatus SmearDeformerNode::deform(MDataBlock& block,
//    MItGeometry& iter,
//    const MMatrix& localToWorldMatrix,
//    unsigned int multiIndex)
//{
//    MStatus status;
//
//    // 1. Get deformation parameters
//    const bool enableDef = true; /*block.inputValue(aEnable).asBool();*/
//    const double betaMaxVal = 0.5f;/*block.inputValue(aBetaMax).asDouble();*/
//    if (!enableDef || !motionOffsetsBaked) return MS::kSuccess;
//
//    // 2. Get current frame and validate
//    MTime currentTime = block.inputValue(aTime).asTime();
//    const double frame = currentTime.as(MTime::kFilm);
//    const int frameIndex = static_cast<int>(frame - motionOffsets.startFrame);
//
//    if (frameIndex < 0 || frameIndex >= motionOffsets.motionOffsets.size()) {
//        return MS::kSuccess;
//    }
//
//    // 3. Get trajectory data
//    //const MDoubleArray& offsets = motionOffsets.motionOffsets[frameIndex];
//    //const std::vector<MPointArray>& trajectories = motionOffsets.vertexTrajectories;
//    //const int numFrames = trajectories.size();
//
//    // 4. Get weight data
//    //MStatus returnStatus;
//    //MDataHandle chunkEnvelopeData = block.inputValue(smearWeight, &returnStatus);
//    //float chunkWeight = chunkEnvelopeData.asFloat();
//
//    //MArrayDataHandle hInput = block.outputArrayValue(input);
//    //hInput.jumpToElement(multiIndex);
//    //const float weight = weightValue(block, multiIndex, iter.index());
//    const float weight = 2.0;
//
//    // 5. Deformation loop
//    MPoint point;
//    for (; !iter.isDone(); iter.next()) {
//        const int vertIdx = iter.index();
//
//        // Skip if no offset data or weight is zero
//        if (vertIdx >= offsets.length() || weight <= 0.0001f) continue;
//
//        // Calculate interpolation parameters
//        const double beta = offsets[vertIdx] * betaMaxVal * weight;
//        const int baseFrame = frameIndex + static_cast<int>(floor(beta));
//        const double t = beta - floor(beta);
//
//        // Clamp frame indices
//        const int f0 = std::max(0, baseFrame - 1);
//        const int f1 = std::max(0, std::min(numFrames - 1, baseFrame));
//        const int f2 = std::min(numFrames - 1, baseFrame + 1);
//        const int f3 = std::min(numFrames - 1, baseFrame + 2);
//
//        // Get trajectory points
//        const MPoint& p0 = trajectories[f0][vertIdx];
//        const MPoint& p1 = trajectories[f1][vertIdx];
//        const MPoint& p2 = trajectories[f2][vertIdx];
//        const MPoint& p3 = trajectories[f3][vertIdx];
//
//        // Calculate new position
//        point = iter.position();
//        point += SmearDeformerNode::catmullRomInterpolate(p0, p1, p2, p3, t) - p1;
//
//        // Apply to vertex
//        iter.setPosition(point);
//    }
//
//    return MS::kSuccess;
//}