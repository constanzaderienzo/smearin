#include "smear.h"
#include <maya/MFnDependencyNode.h>
#include <maya/MFnAnimCurve.h>
#include <maya/MPlug.h>
#include <maya/MPlugArray.h>
#include <maya/MAnimControl.h>
#include <maya/MGlobal.h>
#include <maya/MFnTransform.h>
#include <maya/MTransformationMatrix.h>
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
    MGlobal::displayInfo("extractAnimationFrameRange: valid transformPath pointing to transform node");

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


MStatus Smear::computeWorldTransformPerFrame(const MDagPath& transformPath,
    const double startFrame,
    const double endFrame,
    std::vector<MMatrix>& transformationMatrices) {
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
            scalePlug.child(2).asDouble(context)};

        // Build transform matrix manually
        MTransformationMatrix xform;
        xform.setTranslation(translation, MSpace::kTransform);
        xform.setRotation(rotation, rotOrder);
        xform.setScale(scale, MSpace::kTransform);

        MMatrix matrix = xform.asMatrix();
        transformationMatrices[frame] = matrix;

        // Debug print
        /*
        MString msg = MString() + "Frame " + frame + ":\n"
        + "  Translation = (" +
            translation.x + ", " +
            translation.y + ", " +
            translation.z + ")\n"

        + "  Rotation (XYZ degrees) = (" +
            rotation[0] + ", " +
            rotation[1] + ", " +
            rotation[2] + ")\n"

        + "  Scale = (" +
            scale[0] + ", " +
            scale[1] + ", " +
            scale[2] + ")";
        MGlobal::displayInfo(msg);
        */
        if (!compareTransformComponents(rotOrder, transformationMatrices[frame], translation, rotation, scale, 1e-4, false)) {
            MGlobal::displayError("Transformation matrix does not match expected translation, rotation, or scale"); 
        }
        
    }

    return MS::kSuccess;
}


MStatus Smear::calculateCentroidOffsetFromPivot(const MDagPath& shapePath, const MDagPath& transformPath, MVector& centroidOffset) {
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
        MPoint vertexPos = vertexIt.position(MSpace::kWorld);
        //MGlobal::displayInfo(MString("Vertex ") + vertexIt.index() + ": " + vertexPos.x + ", " + vertexPos.y + ", " + vertexPos.z);
        sum += vertexPos;
    }

    MVector centroid = (numVertices > 0) ? sum / numVertices : MVector(0.0, 0.0, 0.0);

    // Debug: Print the centroid
    //MGlobal::displayInfo(MString("Centroid: ") + centroid.x + ", " + centroid.y + ", " + centroid.z);

    MPoint pivotPoint = transformPath.inclusiveMatrix()[3]; // Get the translation component of the matrix
    MVector pivot(pivotPoint.x, pivotPoint.y, pivotPoint.z);

    // Debug: Print the pivot point
    MGlobal::displayInfo(MString("Pivot point: ") + pivot.x + ", " + pivot.y + ", " + pivot.z);

    // Calculate the centroid offset
    centroidOffset = centroid - pivot;

    return MS::kSuccess;
}

MStatus Smear::computeCentroidTrajectory(const MDagPath& meshPath, const MDagPath& transformPath, std::vector<MVector>& centroidPositions) {
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
    double startFrame = -1;
    double endFrame = -1;
    status = extractAnimationFrameRange(transformPath, startFrame, endFrame);
    MGlobal::displayInfo("Smear::computeCentroidTrajectory - startframe: " + MString() + startFrame + " endFrame: " + endFrame);
    McheckErr(status, "Failed to extract animation frame range.");

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
    status = computeWorldTransformPerFrame(transformPath, startFrame, endFrame, transformationMatrices);
    McheckErr(status, "Failed to compute world transforms.");

    // MGlobal::displayInfo("Smear::computeCentroidTrajectory - centroid positions");
    for (int frame = 0; frame < numFrames; ++frame) {
        // Find how the position of the centroid changes based on the pivot's transformations (how it moves, rotates, scales) 
        // by multiplying the transform to centroid offset 
        MPoint transformedOffset = MPoint(centroidOffset) * transformationMatrices[frame];
        centroidPositions[frame] = MVector(transformedOffset);



        // Print for debugging
        /*
        MString debugMsg = "(" + MString() + 
            centroidPositions[frame].x + ", " +
            centroidPositions[frame].y + ", " +
            centroidPositions[frame].z + ")";
        MGlobal::displayInfo(debugMsg);
        */
    }

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
    std::vector<MVector> centroidPositions;
    status = computeCentroidTrajectory(meshPath, transformPath, centroidPositions);
    McheckErr(status, "Failed to compute centroid trajectory.");

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
        MVector centroidCurrent = centroidPositions[frame];
        MVector centroidNext = centroidPositions[frame + 1];
        centroidVelocities[frame] = centroidNext - centroidCurrent;

        // Print for debugging
        /*
        MString debugMsg = "(" + MString() +
            centroidVelocities[frame].x + ", " +
            centroidVelocities[frame].y + ", " +
            centroidVelocities[frame].z + ")";
        MGlobal::displayInfo(debugMsg);
        */
    }



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