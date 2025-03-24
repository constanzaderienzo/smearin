#include "smear.h"
#include <maya/MFnDependencyNode.h>
#include <maya/MFnAnimCurve.h>
#include <maya/MPlug.h>
#include <maya/MPlugArray.h>
#include <maya/MAnimControl.h>
#include <maya/MGlobal.h>
#include <maya/MFnTransform.h>
#include <maya/MItMeshVertex.h>
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

    return MS::kSuccess;
}

MStatus Smear::computeWorldTransformPerFrame(const MDagPath& transformPath, std::vector<MMatrix>& transformationMatrices) {
    /*
    MStatus status;
    // Check if the provided path point to correct node types.
    if (!transformPath.hasFn(MFn::kTransform)) {
        MGlobal::displayError("tranformPath does not point to a transform node.");
        return MS::kFailure; // Not a transform node.
    }

    double startFrame, endFrame;
    status = extractAnimationFrameRange(transformPath, startFrame, endFrame);
    McheckErr(status, "Failed to extract animation frame range.");

    int numFrames = static_cast<int>(endFrame - startFrame + 1);
    transformationMatrices.resize(numFrames);

    for (int frame = 0; frame < numFrames; ++frame) {
        MTime currentTime(startFrame + frame, MTime::uiUnit());
        MAnimControl::setCurrentTime(currentTime);

        MFnTransform transformFn(object, &status);
        McheckErr(status, "Failed to create MFnTransform.");

        transformationMatrices[frame] = transformFn.transformation().asMatrix();
    }
    */
    return MS::kSuccess;
}

MStatus Smear::calculateCentroidOffsetFromPivot(const MDagPath& meshPath, const MDagPath& transformPath, MVector& centroidOffset) {
    /*
    MStatus status;

    // Check if the provided path point to correct node types.
    if (!meshPath.hasFn(MFn::kMesh)) {
        MGlobal::displayError("meshPath does not point to a mesh node.");
        return MS::kFailure; // Not a mesh node.
    }
    else if (!transformPath.hasFn(MFn::kTransform)) {
        MGlobal::displayError("tranformPath does not point to a transform node.");
        return MS::kFailure; // Not a transform node.
    }

    // Variables to store the transform and shape nodes
    MObject transformObj = object;
    MObject shapeObj;

    // Check if the object is a transform node
    if (object.hasFn(MFn::kTransform)) {
        MDagPath dagPath;
        status = MDagPath::getAPathTo(object, dagPath);
        if (!status) {
            MGlobal::displayError("Failed to get MDagPath for the transform node.");
            return MS::kFailure;
        }

        // Extend the path to the shape node
        status = dagPath.extendToShape();
        if (!status) {
            MGlobal::displayError("Failed to extend to shape node.");
            return MS::kFailure;
        }

        // Get the shape node
        shapeObj = dagPath.node();
        if (!shapeObj.hasFn(MFn::kMesh)) {
            MGlobal::displayError("No mesh found under the transform.");
            return MS::kFailure;
        }
    }
    else if (object.hasFn(MFn::kMesh)) {
        // If the input is already a mesh, use it directly
        shapeObj = object;

        // Get the transform node from the shape node
        MDagPath dagPath;
        status = MDagPath::getAPathTo(shapeObj, dagPath);
        if (!status) {
            MGlobal::displayError("Failed to get MDagPath for the shape node.");
            return MS::kFailure;
        }

        status = dagPath.pop(); // Move up to the transform node
        if (!status) {
            MGlobal::displayError("Failed to get transform node from shape node.");
            return MS::kFailure;
        }

        transformObj = dagPath.node();
    }
    else {
        MGlobal::displayError("Input object is neither a transform nor a mesh.");
        return MS::kFailure;
    }

    // Calculate the centroid using the shape node
    MFnMesh meshFn(shapeObj, &status);
    McheckErr(status, "Failed to create MFnMesh.");

    MVector sum(0.0, 0.0, 0.0);
    int numVertices = meshFn.numVertices();
    MItMeshVertex vertexIt(shapeObj, &status);
    McheckErr(status, "Failed to create MItMeshVertex.");

    // Debug: Print vertex positions
    MGlobal::displayInfo("Vertex positions:");
    for (; !vertexIt.isDone(); vertexIt.next()) {
        MPoint vertexPos = vertexIt.position(MSpace::kWorld);
        MGlobal::displayInfo(MString("Vertex ") + vertexIt.index() + ": " + vertexPos.x + ", " + vertexPos.y + ", " + vertexPos.z);
        sum += vertexPos;
    }

    MVector centroid = (numVertices > 0) ? sum / numVertices : MVector(0.0, 0.0, 0.0);

    // Debug: Print the centroid
    MGlobal::displayInfo(MString("Centroid: ") + centroid.x + ", " + centroid.y + ", " + centroid.z);

    // Get the pivot point of the transform node in world space
    MDagPath transformPath;
    status = MDagPath::getAPathTo(transformObj, transformPath);
    if (!status) {
        MGlobal::displayError("Failed to get MDagPath for the transform node.");
        return MS::kFailure;
    }

    MPoint pivotPoint = transformPath.inclusiveMatrix()[3]; // Get the translation component of the matrix
    MVector pivot(pivotPoint.x, pivotPoint.y, pivotPoint.z);

    // Debug: Print the pivot point
    MGlobal::displayInfo(MString("Pivot point: ") + pivot.x + ", " + pivot.y + ", " + pivot.z);

    // Calculate the centroid offset
    centroidOffset = centroid - pivot;
    */
    return MS::kSuccess;
}

MStatus Smear::computeCentroidTrajectory(const MDagPath& meshPath, const MDagPath& transformPath, std::vector<MVectorArray>& centroidPositions) {
    MStatus status;
    // Check if the provided path point to correct node types.
    if (!meshPath.hasFn(MFn::kMesh)) {
        MGlobal::displayError("meshPath does not point to a mesh node.");
        return MS::kFailure; // Not a mesh node.
    }
    else if (!transformPath.hasFn(MFn::kTransform)) {
        MGlobal::displayError("tranformPath does not point to a transform node.");
        return MS::kFailure; // Not a transform node.
    }

    // Find the start and end frame to determine the range for which we compute trajectory 
    double startFrame, endFrame;
    status = extractAnimationFrameRange(transformPath, startFrame, endFrame);
    McheckErr(status, "Failed to extract animation frame range.");

    /*
    int numFrames = static_cast<int>(endFrame - startFrame + 1);
    centroidPositions.resize(numFrames);

    // Compute the centroid offset so that we can use this to 
    // quickly find the centroid based on pivot location 
    // Centroid is found through average position of vertex positions
    MVector centroidOffset;
    status = calculateCentroidOffsetFromPivot(meshPath, transformPath, centroidOffset);
    McheckErr(status, "Failed to calculate centroid offset.");

    // Parse all the transformations from each frame to see how the pivot moves from animation 
    std::vector<MMatrix> transformationMatrices;
    status = computeWorldTransformPerFrame(transformPath, transformationMatrices);
    McheckErr(status, "Failed to compute world transforms.");

    for (int frame = 0; frame < numFrames; ++frame) {
        // Find how the position of the centroid changes based on the pivot's transformations (how it moves, rotates, scales) 
        // by multiplying the transform to centroid offset 
        MPoint transformedOffset = MPoint(centroidOffset) * transformationMatrices[frame];
        centroidPositions[frame].append(MVector(transformedOffset));
    }
    */
    return MS::kSuccess;
}

MStatus Smear::computeCentroidVelocity(const MDagPath& meshPath, const MDagPath& transformPath, std::vector<MVector>& centroidVelocities, double& startFrame, double& endFrame) {
    MStatus status;
    // Check if the provided path point to correct node types.
    if (!meshPath.hasFn(MFn::kMesh)) {
        MGlobal::displayError("meshPath does not point to a mesh node.");
        return MS::kFailure; // Not a mesh node.
    }
    else if (!transformPath.hasFn(MFn::kTransform)) {
        MGlobal::displayError("tranformPath does not point to a transform node.");
        return MS::kFailure; // Not a transform node.
    }

    

    // Compute centroid positions through each frame
    std::vector<MVectorArray> centroidPositions;
    status = computeCentroidTrajectory(meshPath, transformPath, centroidPositions);
    McheckErr(status, "Failed to compute centroid trajectory.");

    /*
    // Check if there are at least two frames
    int numFrames = static_cast<int>(centroidPositions.size());
    if (numFrames < 2) {
        MGlobal::displayError("Not enough frames to compute velocity.");
        return MS::kFailure;
    }

    // Use the centroid positions to compute centroid velocity at each frame
    centroidVelocities.resize(numFrames - 1);
    for (int frame = 0; frame < numFrames - 1; ++frame) {
        MVector centroidCurrent = centroidPositions[frame][0];
        MVector centroidNext = centroidPositions[frame + 1][0];
        centroidVelocities[frame] = centroidNext - centroidCurrent;
    }
    */

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

    // Just passing along centroid velocity for now 
    // No real motion offset calculation yet 
    std::vector<MVector> centroidVelocities;
    status = computeCentroidVelocity(shapePath, transformPath, centroidVelocities, motionOffsets.startFrame, motionOffsets.endFrame);
    McheckErr(status, "Failed to compute centroid velocity.");

    /*
    int numFrames = static_cast<int>(centroidVelocities.size());
    if (numFrames == 0) {
        MGlobal::displayError("No motion detected.");
        return MS::kFailure;
    }

    motionOffsets.motionOffsets.resize(numFrames);
    MObject shapeObj;
    if (object.hasFn(MFn::kTransform)) {
        MDagPath dagPath;
        status = MDagPath::getAPathTo(object, dagPath);
        if (!status) {
            MGlobal::displayError("Failed to get MDagPath for the transform node.");
            return MS::kFailure;
        }

        // Extend the path to the shape node
        status = dagPath.extendToShape();
        if (!status) {
            MGlobal::displayError("Failed to extend to shape node.");
            return MS::kFailure;
        }

        // Get the shape node
        shapeObj = dagPath.node();
        if (!shapeObj.hasFn(MFn::kMesh)) {
            MGlobal::displayError("No mesh found under the transform.");
            return MS::kFailure;
        }
    }

    MFnMesh meshFn(shapeObj, &status);
    McheckErr(status, "computeMotionOffsetsSimple: Failed to create MFnMesh.");
    int numVertices = meshFn.numVertices();

    MGlobal::displayInfo(MString("Num Frames: ") + numFrames);

    for (int frame = 0; frame < numFrames; ++frame) {
        MVectorArray& frameOffsets = motionOffsets.motionOffsets[frame];
        frameOffsets.setLength(numVertices);

        for (int v = 0; v < numVertices; ++v) {
            frameOffsets[v] = centroidVelocities[frame];
            MGlobal::displayInfo(MString("Centroid Velocity: ") + frameOffsets[v].x + frameOffsets[v].y + frameOffsets[v].z);
        }
    }
    */

    // ==========================================================
    // DEBUGGING: Tracking function calls and setting dummy data
    // ==========================================================
    
    /*
    // Tracking how many times this function has been called to debug 
    // motion offset caching realted issues in SmearNode
    static int functionInvocationCount = 0;
    functionInvocationCount++;
    MGlobal::displayInfo(MString("Function has been called ") + functionInvocationCount + " times.");

    // Craeate dummy data 
    motionOffsets.startFrame = 0; 
    motionOffsets.endFrame = 5; 
    motionOffsets.motionOffsets.resize(2); // Create two frames of data
    int vertexCount = 5; 
    // Fill each frame with dummy motion offsets
    for (int i = 0; i < motionOffsets.endFrame; ++i) {
        motionOffsets.motionOffsets[i].setLength(vertexCount); // Assume 3 vertices for testing
        for (int j = 0; j < vertexCount; ++j) {
            motionOffsets.motionOffsets[i][j] = MVector(i * 5, i * 5, i * 5);
        }
    }
    */

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