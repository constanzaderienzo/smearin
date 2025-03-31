#include "smearDeformerNode.h"
#include <maya/MItGeometry.h>
#include <maya/MFnUnitAttribute.h>
#include <math.h>

MTypeId SmearDeformerNode::id(0x98530); // Random id 
MObject SmearDeformerNode::time;

void* SmearDeformerNode::creator()
{
    return new SmearDeformerNode();
}

MStatus SmearDeformerNode::initialize()
{

    MFnUnitAttribute unitAttr;
    //MFnTypedAttribute typedAttr;
    MStatus status;

    // Time attribute
    time = unitAttr.create("time", "tm", MFnUnitAttribute::kTime, 0.0);
    addAttribute(time);

    // Input mesh
    //inputMesh = typedAttr.create("inputMesh", "in", MFnData::kMesh);
    //typedAttr.setStorable(true);
    //addAttribute(inputMesh);

    return MS::kSuccess;
}

MStatus SmearDeformerNode::deform(MDataBlock& block, MItGeometry& iter, const MMatrix& localToWorldMatrix, unsigned int multiIndex)
{
    MTime currentTime = block.inputValue(time).asTime();
    double currentFrame = currentTime.as(MTime::kFilm);
    //const double frame = currentTime.as(MTime::kFilm);
 
    MPoint point; 
    for (; !iter.isDone(); iter.next()) {
        const int vertIdx = iter.index();
        point = iter.position();
        point += MPoint(2.f, 2.f, 2.f, 0.f) * sin(currentFrame);

        // Apply to vertex
        iter.setPosition(point);
    }
    return MStatus();
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