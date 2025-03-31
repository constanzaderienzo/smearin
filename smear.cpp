#include "smear.h"
#include <maya/MFnDependencyNode.h>
#include <maya/MFnAnimCurve.h>
#include <maya/MPlug.h>
#include <maya/MPlugArray.h>
#include <maya/MPointArray.h> 
#include <maya/MAnimControl.h>
#include <maya/MGlobal.h>
#include <maya/MFnTransform.h>
#include <maya/MTransformationMatrix.h>
#include <maya/MItMeshVertex.h>
#include <maya/MSelectionList.h>
#include <maya/MMatrix.h>
#include <maya/MDagPath.h>

#define McheckErr(stat, msg)        \
    if (MS::kSuccess != stat) {     \
        MGlobal::displayError(msg); \
        return MS::kFailure;        \
    }

MStatus Smear::extractAnimationFrameRange(const MDagPath & transformPath, double& startFrame, double& endFrame) {
    MStatus status;

    // Validate input
    if (!transformPath.isValid() || !transformPath.hasFn(MFn::kTransform)) {
        MGlobal::displayError("Invalid transform node path");
        return MS::kFailure;
    }

    MObject transformNode = transformPath.node();
    startFrame = DBL_MAX;
    endFrame = -DBL_MAX;

    // Attributes we want to check for animation
    const char* animAttrs[] = { "translateX", "translateY", "translateZ",
                                "rotateX", "rotateY", "rotateZ",
                                "scaleX", "scaleY", "scaleZ" };

    MFnDependencyNode depNode(transformNode);
    bool foundKeys = false;

    // Check all relevant attributes
    for (const char* attrName : animAttrs) {
        MPlug plug = depNode.findPlug(attrName, true, &status);
        if (status != MS::kSuccess) continue;

        // Find input animation curves
        MPlugArray connections;
        plug.connectedTo(connections, true, false, &status);
        if (status != MS::kSuccess) continue;

        for (unsigned int i = 0; i < connections.length(); ++i) {
            MObject node = connections[i].node();
            if (!node.hasFn(MFn::kAnimCurve)) continue;

            MFnAnimCurve animCurve(node, &status);
            if (status != MS::kSuccess || animCurve.numKeys() == 0) continue;

            // Update frame range
            const double curveStart = animCurve.time(0).as(MTime::uiUnit());
            const double curveEnd = animCurve.time(animCurve.numKeys() - 1).as(MTime::uiUnit());

            startFrame = std::min(startFrame, curveStart);
            endFrame = std::max(endFrame, curveEnd);
            foundKeys = true;
        }
    }

    if (!foundKeys) {
        MGlobal::displayError("No animation curves found on transform node");
        return MS::kFailure;
    }

    MGlobal::displayInfo("extractAnimationFrameRange::startFrame: " + MString() + startFrame + " endFrame: " + endFrame);

    return MS::kSuccess;
}

bool compareTransformComponents(MTransformationMatrix::RotationOrder rotOrder, const MMatrix& matrix,
    const MVector& expectedTranslation,
    const double expectedRotation[3],  // in radians
    const double expectedScale[3],
    double tolerance = 1e-4,
    bool verbose = true)
{
    MTransformationMatrix xform(matrix);

    // Decompose
    MVector actualTranslation = xform.getTranslation(MSpace::kTransform);

    double actualRotation[3];
    xform.getRotation(actualRotation, rotOrder);

    double actualScale[3];
    xform.getScale(actualScale, MSpace::kTransform);

    // Floating-point comparison helper
    auto closeEnough = [=](double a, double b) {
        return std::abs(a - b) <= tolerance;
        };

    // Compare each component
    bool translationMatch =
        closeEnough(expectedTranslation.x, actualTranslation.x) &&
        closeEnough(expectedTranslation.y, actualTranslation.y) &&
        closeEnough(expectedTranslation.z, actualTranslation.z);

    bool rotationMatch =
        closeEnough(expectedRotation[0], actualRotation[0]) &&
        closeEnough(expectedRotation[1], actualRotation[1]) &&
        closeEnough(expectedRotation[2], actualRotation[2]);

    bool scaleMatch =
        closeEnough(expectedScale[0], actualScale[0]) &&
        closeEnough(expectedScale[1], actualScale[1]) &&
        closeEnough(expectedScale[2], actualScale[2]);

    if (verbose) {
        MString msg = "Transform Comparison Result:\n";
        msg += "  Translation match: " + MString(translationMatch ? "PASS" : "FAIL") + "\n";
        msg += "  Rotation match   : " + MString(rotationMatch ? "PASS" : "FAIL") + "\n";
        msg += "  Scale match      : " + MString(scaleMatch ? "PASS" : "FAIL");
        MGlobal::displayInfo(msg);
    }

    return translationMatch && rotationMatch && scaleMatch;
}


MStatus Smear::cacheInclusiveTransforms(
    const MDagPath& transformPath,
    const double startFrame,
    const double endFrame,
    std::vector<MTransformationMatrix>& transformationMatrices) {
    MStatus status;

    if (!transformPath.hasFn(MFn::kTransform)) {
        MGlobal::displayError("Smear::computeWorldTransformPerFrame - transformPath is not a transform node.");
        return MS::kFailure;
    }

    MFnDependencyNode depNode(transformPath.node(), &status);
    McheckErr(status, "Failed to create MFnDependencyNode");

    auto numFrames = static_cast<int>(endFrame - startFrame + 1);
    transformationMatrices.resize(numFrames);

    // Get the transform's rotation order
    MFnTransform fnTransform(transformPath.node(), &status);
    McheckErr(status, "Failed to get MFnTransform");

    MTransformationMatrix::RotationOrder rotOrder = fnTransform.rotationOrder();

    for (int frame = 0; frame < numFrames; ++frame) {
        MTime currentTime(startFrame + frame, MTime::uiUnit());
        MDGContext context(currentTime);

        // Get plugs for transform attributes
        MPlug translatePlug = depNode.findPlug("translate", true);
        MPlug rotatePlug = depNode.findPlug("rotate", true);
        MPlug scalePlug = depNode.findPlug("scale", true);

        // Evaluate plugs at this time
        MVector translation(
            translatePlug.child(0).asDouble(context),
            translatePlug.child(1).asDouble(context),
            translatePlug.child(2).asDouble(context));

        double rotation[3] = {
            rotatePlug.child(0).asDouble(context),
            rotatePlug.child(1).asDouble(context),
            rotatePlug.child(2).asDouble(context) };

        double scale[3] = {
            scalePlug.child(0).asDouble(context),
            scalePlug.child(1).asDouble(context),
            scalePlug.child(2).asDouble(context) };

        // Build transform matrix manually
        MTransformationMatrix xform;
        xform.setTranslation(translation, MSpace::kTransform);
        xform.setRotation(rotation, rotOrder);
        xform.setScale(scale, MSpace::kTransform);
        transformationMatrices[frame] = xform;

        // Debug print

        //MString msg = MString() + "Frame " + frame + ":\n"
        //+ "  Translation = (" +
        //    translation.x + ", " +
        //    translation.y + ", " +
        //    translation.z + ")\n"

        //+ "  Rotation (XYZ degrees) = (" +
        //    rotation[0] + ", " +
        //    rotation[1] + ", " +
        //    rotation[2] + ")\n"

        //+ "  Scale = (" +
        //    scale[0] + ", " +
        //    scale[1] + ", " +
        //    scale[2] + ")";
        //MGlobal::displayInfo(msg);

    }

    return MS::kSuccess;
}


MStatus Smear::computeCentroidLocal(const MDagPath& shapePath, const MDagPath& transformPath, MVector & centroidLocal) {
    MStatus status;

    // Check if the provided path point to correct node types.
    if (!shapePath.hasFn(MFn::kMesh)) {
        MGlobal::displayError("Smear::calculateCentroidOffsetFromPivot - meshPath does not point to a mesh node.");
        return MS::kFailure; // Not a mesh node.
    }
    else if (!transformPath.hasFn(MFn::kTransform)) {
        MGlobal::displayError("Smear::calculateCentroidOffsetFromPivot - tranformPath does not point to a transform node.");
        return MS::kFailure; // Not a transform node.
    }

    // Calculate the centroid using the shape node
    MFnMesh meshFn(shapePath.node(), &status);
    McheckErr(status, "Failed to create MFnMesh.");

    MVector sum(0.0, 0.0, 0.0);
    int numVertices = meshFn.numVertices();
    MItMeshVertex vertexIt(shapePath.node(), &status);
    McheckErr(status, "Failed to create MItMeshVertex.");


    // Debug: Print vertex positions
    //MGlobal::displayInfo("Vertex positions:");
    for (; !vertexIt.isDone(); vertexIt.next()) {
        MPoint vertexPos = vertexIt.position(MSpace::kObject);
        //MGlobal::displayInfo(MString("Vertex ") + vertexIt.index() + ": " + vertexPos.x + ", " + vertexPos.y + ", " + vertexPos.z);
        sum += vertexPos;
    }

    MVector centroidObjectSpace = (numVertices > 0) ? sum / numVertices : MVector(0.0, 0.0, 0.0);

    centroidLocal = centroidObjectSpace;

    return MS::kSuccess;
}

MStatus Smear::computeCentroidTrajectoryWorld(double startFrame, double endFrame, const std::vector<MMatrix>& inclusiveMatrices, 
                                         const MVector& centroidOffset, std::vector<MVector>& centroidPositions) {
    MStatus status;
    int numFrames = static_cast<int>(endFrame - startFrame + 1);
    centroidPositions.resize(numFrames);
    // MGlobal::displayInfo("Smear::computeCentroidTrajectory - centroid positions");
    for (int frame = 0; frame < numFrames; ++frame) {
        // Find how the position of the centroid changes based on the pivot's transformations (how it moves, rotates, scales) 
        // by multiplying the transform to centroid offset 
        MPoint transformedOffset = MPoint(centroidOffset) * inclusiveMatrices[frame];

        centroidPositions[frame] = MVector(transformedOffset);
    }

    return MS::kSuccess;
}

MStatus Smear::computeCentroidVelocity(const std::vector<MVector>& centroidPositions, std::vector<MVector>& centroidVelocities) {
    MStatus status;

    // Check if there are at least two frames
    int numFrames = static_cast<int>(centroidPositions.size());
    if (numFrames < 2) {
        MGlobal::displayError("Not enough frames to compute velocity.");
        return MS::kFailure;
    }

    // Use the centroid positions to compute centroid velocity at each frame
    centroidVelocities.resize(numFrames - 1);
    //MGlobal::displayInfo("Smear::computeCentroidVelocity - centroid positions");
    for (int frame = 0; frame < numFrames - 1; ++frame) {
        centroidVelocities[frame] = centroidPositions[frame + 1] - centroidPositions[frame];
    }

    return MS::kSuccess;
}

MStatus Smear::computeSignedDistanceToPlane(const MPoint& point, const MPoint& pointOnPlane, const MVector& planeNormal, double& signedDist)
{
    MStatus status;

    MVector diff = point - pointOnPlane;
    signedDist = diff * planeNormal;  // Dot product

    return MS::kSuccess;
}

MStatus Smear::calculatePerFrameMotionOffsets(const MPointArray& objectSpaceVertices, const MTransformationMatrix& transformationMatrix, const MPoint& centroid, const MVector& centroidVelocity, MDoubleArray& motionOffsets)
{
    MStatus status;
    // Store the magnitude of the largest motion offset
    // to be used in normalizing motion offsets later
    double maxMotionOffsetMag = DBL_MIN;

    // Iterate and compute signed distance from the plane formed by the centroid and its velocity 
    // for each vertex
    int vertCount = objectSpaceVertices.length();
    status = motionOffsets.setLength(vertCount);
    McheckErr(status, "SmearNode::computeMotionOffsets - invalid vertex count!");

    for (int i = 0; i < vertCount; ++i) {
        double& motionOffset = motionOffsets[i];
        const MPoint& objectSpaceVertexPosition = objectSpaceVertices[i];

        // Convert the object space vertex postiion to world position 
        MPoint worldSpaceVertexPosition =  objectSpaceVertexPosition * transformationMatrix.asMatrix();
        // Motion offset for simple object is just the signed dist to plane
        status = computeSignedDistanceToPlane(worldSpaceVertexPosition, centroid, centroidVelocity.normal(), motionOffset);
        
        // Check the magnitude of motion offset and record if it's the largest so far 
        maxMotionOffsetMag = std::max(maxMotionOffsetMag, std::abs(motionOffset));
    }

    // Normalize motion offsets 
    for (int i = 0; i < vertCount; ++i) {
        motionOffsets[i] /= maxMotionOffsetMag; 
        motionOffsets[i] = std::max(-1.0, std::min(1.0, motionOffsets[i]));
        //MGlobal::displayInfo(MString("Motion Offset Post: ") + motionOffsets[i]);
    }
    MGlobal::displayInfo("Smear::calculatePerFrameMotionOffsets - completed!");
    return MS::kSuccess; 
}

MStatus Smear::getVerticesAtFrame(const MDagPath& shapePath, double frame, MPointArray& vertices)
{
    MStatus status; 
    MTime currentTime(frame, MTime::uiUnit());
    MDGContext context(currentTime);

    // Use MFnMesh in the evaluation context
    MFnMesh meshFn(shapePath.node(), &status);
    McheckErr(status, "Failed to create MFnMesh in frame context");

    // Create a context-aware plug for input geometry
    MObject meshObj = shapePath.node();
    MFnDependencyNode depNode(meshObj);
    MPlug outMeshPlug = depNode.findPlug("outMesh", true, &status);
    McheckErr(status, "Failed to find outMesh plug");

    MObject meshDataObj;
    outMeshPlug.getValue(meshDataObj, context); // Evaluate plug at specific time

    MFnMesh frameMesh(meshDataObj, &status); // meshDataObj is a mesh shape at this time
    McheckErr(status, "Failed to create MFnMesh from data object");

    status = frameMesh.getPoints(vertices, MSpace::kWorld);
    McheckErr(status, "Failed to get points for frame " + MString() + frame);

    return MS::kSuccess; 
}

MStatus Smear::computeMotionOffsetsSimple(const MDagPath& shapePath, const MDagPath& transformPath, MotionOffsetsSimple& motionOffsets) {
    MStatus status;
    
    // Check if the provided path point to correct node types.
    if (!shapePath.hasFn(MFn::kMesh)) {
        MGlobal::displayError("shapePath does not point to a mesh node.");
        return MS::kFailure; // Not a mesh node.
    }
    else if (!transformPath.hasFn(MFn::kTransform)) {
        MGlobal::displayError("tranformPath does not point to a transform node.");
        return MS::kFailure; // Not a transform node.
    }

    MGlobal::displayInfo("Both shapePath and transformPath point to their respective nodes!");

    // Find the start and end frame to determine the range for which we compute trajectory 
    double startFrame = -1;
    double endFrame = -1;
    status = extractAnimationFrameRange(transformPath, startFrame, endFrame);
    MGlobal::displayInfo("Smear::computeCentroidTrajectory - startframe: " + MString() + startFrame + " endFrame: " + endFrame);
    McheckErr(status, "Failed to extract animation frame range.");
    motionOffsets.startFrame = startFrame;
    motionOffsets.endFrame = endFrame;

    MGlobal::displayInfo(MString("Start Frame: ") + startFrame + MString("End Frame: ") + endFrame);

    // Compute the centroid offset so that we can use this to 
    // quickly find the centroid based on pivot location 
    // Centroid is found through average position of vertex positions
    MVector centroidLocal;
    status = computeCentroidLocal(shapePath, transformPath, centroidLocal);
    McheckErr(status, "Failed to calculate centroid offset.");

    // Parse all the transformations from each frame to see how the pivot moves from animation 
    std::vector<MMatrix> inclusiveMatrices;
    status = cacheInclusiveTransforms(transformPath, startFrame, endFrame, inclusiveMatrices);
    McheckErr(status, "Failed to compute world transforms.");


    // Calculate the centroid's positions over time 
    std::vector<MVector> centroidPositions; 
    status = computeCentroidTrajectoryWorld(startFrame, endFrame, inclusiveMatrices,
        centroidLocal, centroidPositions);
    
    // Just passing along centroid velocity for now 
    // No real motion offset calculation yet 
    std::vector<MVector> centroidVelocities;
    status = computeCentroidVelocity(centroidPositions, centroidVelocities);
    McheckErr(status, "Failed to compute centroid velocity.");
    
    const int numFrames = static_cast<int>(endFrame - startFrame + 1);
    if (numFrames == 0) {
        MGlobal::displayError("No motion detected.");
        return MS::kFailure;
    }
    
    motionOffsets.motionOffsets.resize(numFrames);
    MFnMesh meshFn(shapePath, &status);
    McheckErr(status, "computeMotionOffsetsSimple: Failed to create MFnMesh.");
    int numVertices = meshFn.numVertices();

    MGlobal::displayInfo(MString("Num Frames: ") + numFrames);

    for (int i = 0; i < centroidVelocities.size() ; ++i) {
        MGlobal::displayInfo("Frame: " + i);
        MGlobal::displayInfo("Centroid position: " + MString() + centroidPositions[i].x + " " + centroidPositions[i].y + " " + centroidPositions[i].z);
        MGlobal::displayInfo("Centroid velocity: " + MString() + centroidVelocities[i].x + " " + centroidVelocities[i].y + " " + centroidPositions[i].z);
    }

    // These object space vertices will be transformed into world space verteices later
    MPointArray objectSpaceVertices;
    status = meshFn.getPoints(objectSpaceVertices, MSpace::kObject);
    McheckErr(status, "Smear::computeMotionOffsetsSimple - Failed to get object space vertex positions");
    
    MGlobal::displayInfo("Smear::computeMotionOffsetsSimple - Num Frames frame:" + MString() + numFrames);

    // Store vertex trajectories
    motionOffsets.vertexTrajectories.resize(numFrames);
    MPointArray frameVertices;

    // Iterate through each frame to find motion offsets for each frame
    for (int frame = 0; frame < numFrames; ++frame) {
        MGlobal::displayInfo("Smear::computeMotionOffsetsSimple - Current frame:" + MString() + frame);

        getVerticesAtFrame(shapePath, startFrame + frame, frameVertices);
        motionOffsets.vertexTrajectories[frame] = frameVertices;

        MDoubleArray& currentFrameMotionOffsets = motionOffsets.motionOffsets[frame];
        status = calculatePerFrameMotionOffsets(objectSpaceVertices, inclusiveMatrices[frame], centroidPositions[frame], centroidVelocities[frame], currentFrameMotionOffsets);
        McheckErr(status, "Failed to calculate per frame motion offset for frame " + MString() + frame); 
    }
    
    return MS::kSuccess;
}

MStatus Smear::getTransformFromMesh(const MDagPath& meshPath, MDagPath& transformPath) {
    if (!meshPath.hasFn(MFn::kMesh)) {
        return MS::kFailure; // Not a mesh node.
    }

    transformPath = meshPath;
    transformPath.pop(); // Move to parent transform.
    return MS::kSuccess;
}