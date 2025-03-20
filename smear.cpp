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

MStatus Smear::extractAnimationFrameRange(const MObject& obj, double& startFrame, double& endFrame) {
    MStatus status;
    startFrame = DBL_MAX;
    endFrame = DBL_MIN;

    MFnDependencyNode depNode(obj, &status);
    McheckErr(status, "Failed to create MFnDependencyNode.");

    const char* animAttrs[] = { "translateX", "translateY", "translateZ",
                                "rotateX", "rotateY", "rotateZ",
                                "scaleX", "scaleY", "scaleZ" };

    for (const char* attr : animAttrs) {
        MPlug plug = depNode.findPlug(attr, true, &status);
        if (!status) continue;

        MPlugArray connections;
        plug.connectedTo(connections, true, false, &status);
        if (!status) continue;

        for (unsigned int i = 0; i < connections.length(); ++i) {
            MObject animCurveObj = connections[i].node();
            if (!animCurveObj.hasFn(MFn::kAnimCurve)) continue;

            MFnAnimCurve animCurve(animCurveObj, &status);
            if (!status || animCurve.numKeys() == 0) continue;

            startFrame = std::min(startFrame, animCurve.time(0).as(MTime::uiUnit()));
            endFrame = std::max(endFrame, animCurve.time(animCurve.numKeys() - 1).as(MTime::uiUnit()));
        }
    }

    if (startFrame == DBL_MAX || endFrame == DBL_MIN) {
        MGlobal::displayError("No keyframes found on the object.");
        return MS::kFailure;
    }

    return MS::kSuccess;
}

MStatus Smear::computeWorldTransformPerFrame(const MObject& object, std::vector<MMatrix>& transformationMatrices) {
    MStatus status;

    double startFrame, endFrame;
    status = extractAnimationFrameRange(object, startFrame, endFrame);
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

    return MS::kSuccess;
}

MStatus Smear::calculateCentroidOffsetFromPivot(MObject& object, MVector& centroidOffset) {
    MStatus status;

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
    return MS::kSuccess;
}

MStatus Smear::computeCentroidTrajectory(MObject& object, std::vector<MVectorArray>& centroidPositions) {
    MStatus status;

    // Find the start and end frame to determine the range for which we compute trajectory 
    double startFrame, endFrame;
    status = extractAnimationFrameRange(object, startFrame, endFrame);
    McheckErr(status, "Failed to extract animation frame range.");

    int numFrames = static_cast<int>(endFrame - startFrame + 1);
    centroidPositions.resize(numFrames);

    // Compute the centroid offset so that we can use this to 
    // quickly find the centroid based on pivot location 
    // Centroid is found through average position of vertex positions
    MVector centroidOffset;
    status = calculateCentroidOffsetFromPivot(object, centroidOffset);
    McheckErr(status, "Failed to calculate centroid offset.");

    // Parse all the transformations from each frame to see how the pivot moves from animation 
    std::vector<MMatrix> transformationMatrices;
    status = computeWorldTransformPerFrame(object, transformationMatrices);
    McheckErr(status, "Failed to compute world transforms.");

    for (int frame = 0; frame < numFrames; ++frame) {
        // Find how the position of the centroid changes based on the pivot's transformations (how it moves, rotates, scales) 
        // by multiplying the transform to centroid offset 
        MPoint transformedOffset = MPoint(centroidOffset) * transformationMatrices[frame];
        centroidPositions[frame].append(MVector(transformedOffset));
    }

    return MS::kSuccess;
}

MStatus Smear::computeCentroidVelocity(MObject& object, std::vector<MVector>& centroidVelocities, double& startFrame, double& endFrame) {
    MStatus status;

    std::vector<MVectorArray> centroidPositions;
    status = computeCentroidTrajectory(object, centroidPositions);
    McheckErr(status, "Failed to compute centroid trajectory.");

    int numFrames = static_cast<int>(centroidPositions.size());
    if (numFrames < 2) {
        MGlobal::displayError("Not enough frames to compute velocity.");
        return MS::kFailure;
    }

    centroidVelocities.resize(numFrames - 1);

    for (int frame = 0; frame < numFrames - 1; ++frame) {
        MVector centroidCurrent = centroidPositions[frame][0];
        MVector centroidNext = centroidPositions[frame + 1][0];
        centroidVelocities[frame] = centroidNext - centroidCurrent;
    }

    return MS::kSuccess;
}

MStatus Smear::computeMotionOffsetsSimple(MObject& object, MotionOffsetsSimple& motionOffsets) {
    MStatus status;

    // Just passing along centroid velocity for now 
    // No real motion offset calculation yet 
    std::vector<MVector> centroidVelocities;
    status = computeCentroidVelocity(object, centroidVelocities, motionOffsets.startFrame, motionOffsets.endFrame);
    McheckErr(status, "Failed to compute centroid velocity.");

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

    return MS::kSuccess;
}
