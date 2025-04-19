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

MStatus SmearDeformerNode::deform(MDataBlock& block, MItGeometry& iter, const MMatrix& localToWorldMatrix, unsigned int multiIndex)
{
    MStatus status; 
       
    MArrayDataHandle hInputArray = block.inputArrayValue(input, &status);
    if (!status) {
        MGlobal::displayError("Failed to get input geometry array: " + MString(status.errorString()));
        return status;
    
    MDataHandle applyHandle = block.inputValue(aApplyElongation, &status);
    McheckErr(status, "Failed to obtain data handle for applyElongation");
    bool applyElongation = applyHandle.asBool();
    if (!applyElongation) {
        // Do nothing if elongation is disabled.
        return MS::kSuccess;
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
    MDagPath meshPath;
    status = MDagPath::getAPathTo(meshNode, meshPath);
    CHECK_MSTATUS_AND_RETURN_IT(status);
    // DEBUG SAFETY
    MGlobal::displayInfo(MString("Processing: ") + meshPath.fullPathName());

    if (!skinDataBaked) {
        MGlobal::displayInfo("Calculating skin data.");
        // 1. Get skinCluster and influence bones
        MObject skinClusterObj;
        MDagPathArray influenceBones;
        status = Smear::getSkinClusterAndBones(meshPath, skinClusterObj, influenceBones);
        if (!status) return status;

        MFnSkinCluster skinFn(skinClusterObj, &status);
        CHECK_MSTATUS_AND_RETURN_IT(status);

        // 2. Create full vertex component
        MFnSingleIndexedComponent compFn;
        MObject vertexComp = compFn.create(MFn::kMeshVertComponent, &status);
        CHECK_MSTATUS_AND_RETURN_IT(status);

        uint numVertices = iter.count(&status);
        CHECK_MSTATUS_AND_RETURN_IT(status);

        for (uint i = 0; i < numVertices; ++i) {
            compFn.addElement(i);
        }

        // 3. Get all weights for all influences on all vertices
        MDagPath inputMeshPath = meshPath;
        uint numInfluences;
        MDoubleArray weights;

        status = skinFn.getWeights(inputMeshPath, vertexComp, weights, numInfluences);
        MGlobal::displayInfo("After get weights.");
        CHECK_MSTATUS_AND_RETURN_IT(status);

        // 4. Reshape into per-vertex storage
        vertexWeights.clear();
        vertexWeights.resize(numVertices);

        for (uint v = 0; v < numVertices; ++v) {
            std::vector<InfluenceData> influences;

            for (uint j = 0; j < numInfluences; ++j) {
                float w = static_cast<float>(weights[v * numInfluences + j]);
                if (w > 0.001f) {  // ignore negligible weights
                    influences.push_back({ j, w });
                }
            }
            vertexWeights[v] = influences;
        }

        skinDataBaked = true;
        MGlobal::displayInfo("Skin weights initialized and cached.");
    }

    return MStatus::kSuccess;

#if 0
    MTimeArray times;
    times = Smear::getAnimationRange();
    //Smear::getSkeletonInformation();

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
    const int N = smoothingEnabled ? block.inputValue(elongationSmoothWindowSize).asInt() : 0;
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
    const double elongationStrengthPast = block.inputValue(aelongationStrengthPast).asDouble();
    const double elongationStrengthFuture = block.inputValue(aelongationStrengthFuture).asDouble(); 

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

        MPoint interpolated = SmearDeformerNode::catmullRomInterpolate(p0, p1, p2, p3, t2);

        iter.setPosition(interpolated);
    }
#endif
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