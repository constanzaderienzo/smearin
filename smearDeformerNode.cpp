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
MObject SmearDeformerNode::aCacheLoaded;

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

    aCacheLoaded = numAttr.create("cacheLoaded", "cl", MFnNumericData::kBoolean, false, &status);
    CHECK_MSTATUS_AND_RETURN_IT(status);
    addAttribute(aCacheLoaded);

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
    MStatus status;

    MDataHandle timeDataHandle = block.inputValue(time, &status);
    McheckErr(status, "Failed to obtain data handle for time input");
    MTime currentTime = timeDataHandle.asTime();
    double currentFrame = currentTime.as(MTime::kFilm);

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


    MPoint point;
    for (; !iter.isDone(); iter.next()) {
        const int vertIdx = iter.index();

        if (frameIndex < 0 || frameIndex >= motionOffsets.motionOffsets.size()) {
            continue;
        }

        // Get motion offset and apply strength
        double offset = smoothedOffsets[vertIdx];

        // Calculate the strength factor based on motion offset value 
        double t1 = (offset + 1.) / 2.; // remaps motion offset from [-1, 1] to [0, 1] 
        double interpolatedStrength = (1.0 - t1) * elongationStrengthPast + t1 * elongationStrengthFuture;

        const double beta = offset * interpolatedStrength;

        const int frameOffset = static_cast<int>(floor(beta));
        const double t2 = beta - frameOffset;

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

        MPoint interpolated = Smear::catmullRomInterpolate(p0, p1, p2, p3, t2);

        iter.setPosition(interpolated);
    }
    return MStatus::kSuccess;
}

MStatus SmearDeformerNode::deformArticulated(MDataBlock& block, MItGeometry& iter,
    MDagPath& meshPath)
{
    MStatus status;

    MDataHandle timeDataHandle = block.inputValue(time, &status);
    McheckErr(status, "Failed to obtain data handle for time input");
    MTime currentTime = timeDataHandle.asTime();

    // Maya seems to always evaluate deformer node at 24 fps, 
    // So if the viewport is set to 30 fps,
    // and the viewport's current frame is 30 
    // frameD will be 24. (frame 30 / 30 fps = 1 sec; 1 sec * 24 fps = frame 24)
    const double deformerEvaluationFPS = 24.0; 
    double frameD = currentTime.as(MTime::kFilm); 
    double sampleFrameD = frameD * Smear::cacheFPS / deformerEvaluationFPS; 
    int sampleFrame = static_cast<int>(sampleFrameD);

    // assume Smear::vertexCache[f] corresponds to Maya frame f <- NOT TRUE ANYMORE SINCE WE ARE USING VECTOR INSTEAD OF MAP 
    if (sampleFrame < 0 || sampleFrame >= (int)Smear::vertexCache.size())
        return MS::kFailure;

    const FrameCache& fc = Smear::vertexCache[sampleFrame];

    // references to the cached data
    const auto& basePos = fc.positions;      // vector<MPoint>
    const auto& deltas = fc.motionOffsets;  // MDoubleArray

    // 2) artist parameters (read earlier in deform() and stored in members)
    double sPast = elongationStrengthPast;
    double sFut = elongationStrengthFuture;

    // 3) for Catmull‑Rom we need positions at f−1,f,f+1,f+2
    auto getPos = [&](int fIdx, int vid)->MPoint {
        // clamp to valid range
        fIdx = std::clamp(fIdx, 0, (int)Smear::vertexCache.size() - 1);
        return Smear::vertexCache[fIdx].positions[vid];
        };

    //// 4) now for each vertex
    for (; !iter.isDone(); iter.next()) {
        int vid = iter.index();
        double delta = deltas[vid];

        // compute the “baked” displacement amount
        double beta = delta *(delta < 0 ? sPast : sFut);

        // determine which segment of the trajectory to sample
         //β∈[−1,1] → if β≥0 we move toward next frame, else toward prev
        int   baseFrame = sampleFrame + (int)std::floor(beta);
        double u = beta - std::floor(beta);

        // control points for Catmull‑Rom: p0,p1,p2,p3
        MPoint p0 = getPos(baseFrame - 1, vid);
        MPoint p1 = getPos(baseFrame, vid);
        MPoint p2 = getPos(baseFrame + 1, vid);
        MPoint p3 = getPos(baseFrame + 2, vid);

        // evaluate spline
        MPoint newP = Smear::catmullRomInterpolate(p0, p1, p2, p3, (float)u);

        // set the vertex
        iter.setPosition(newP);        
    }

    return MS::kSuccess;
}


MStatus SmearDeformerNode::deform(MDataBlock& block, MItGeometry& iter, const MMatrix& localToWorldMatrix, unsigned int multiIndex)
{
    MStatus status; 
       
    // Check if we should run deformation
    bool applyElongation = block.inputValue(aApplyElongation, &status).asBool();
    //bool triggerEnabled = block.inputValue(trigger, &status).asBool();
    if (!status || !applyElongation) {
        return MS::kSuccess;
    }

    // 1. Get current mesh information
    MDagPath meshPath, transformPath;
    getDagPaths(block, iter, multiIndex, meshPath, transformPath);
    MString meshName = meshPath.fullPathName();

    // 3. Get deformation parameters
    elongationStrengthPast = block.inputValue(aelongationStrengthPast).asDouble();
    elongationStrengthFuture = block.inputValue(aelongationStrengthFuture).asDouble();
    smoothingEnabled = block.inputValue(smoothEnabled).asBool();
    N = smoothingEnabled ? block.inputValue(elongationSmoothWindowSize).asInt() : 0;


    // 4. Perform deformation
    if (Smear::isMeshArticulated(meshPath)) {
        deformArticulated(block, iter, meshPath);
    }
    else {
        deformSimple(block, iter, meshPath, transformPath);
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

        iter.setPosition(Smear::catmullRomInterpolate(p0, p1, p2, p3, localT));
    }
}