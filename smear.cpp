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
#include <maya/MFnMatrixData.h>
#include <maya/MFnIkJoint.h>
#include <maya/MItDependencyNodes.h>
#include <maya/MFnSkinCluster.h>
#include <maya/MItDependencyGraph.h>
#include <maya/MDagPathArray.h>
#include <maya/MItDag.h>

#define McheckErr(stat, msg)        \
    if (MS::kSuccess != stat) {     \
        MGlobal::displayError(msg); \
        return MS::kFailure;        \
    }


MTimeArray Smear::getAnimationRange() {
    MTimeArray timeArray;
    MStatus status;

    MGlobal::displayInfo("===== Extracting Animation Range =====");

    // Get start/end from Maya's timeline
    MTime startTime = MAnimControl::minTime();
    MTime endTime = MAnimControl::maxTime();

    MGlobal::displayInfo(MString("Timeline Range: ") +
        startTime.value() + " to " + endTime.value() +
        " (in " + startTime.unit() + ")");

    // Convert to frame numbers
    double startFrame = startTime.as(MTime::kFilm);
    double endFrame = endTime.as(MTime::kFilm);

    // Validate range
    if (endFrame <= startFrame) {
        MGlobal::displayError("Invalid animation range (end <= start)");
        return timeArray;
    }

    // Populate time array
    for (double frame = startFrame; frame <= endFrame; frame += 1.0) {
        timeArray.append(MTime(frame, MTime::kFilm));
    }

    MGlobal::displayInfo(MString("Generated ") + (int)timeArray.length() + " frames");
    return timeArray;
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

    //MGlobal::displayInfo("extractAnimationFrameRange::startFrame: " + MString() + startFrame + " endFrame: " + endFrame);

    return MS::kSuccess;
}

MStatus Smear::getDagPathsFromInputMesh(MObject inputMeshDataObj, const MPlug& inputMeshPlug, MDagPath& transformPath, MDagPath& shapePath)
{
    MStatus status;

    // Get the plug's source connection
    MPlugArray connectedPlugs;
    if (!inputMeshPlug.connectedTo(connectedPlugs, true, false) || connectedPlugs.length() == 0) {
        MGlobal::displayError("inputMesh is not connected to any mesh.");
        return MS::kFailure;
    }

    MPlug sourcePlug = connectedPlugs[0];
    MObject sourceNode = sourcePlug.node();

    // Get DAG path to that source node (should be a mesh shape)
    MDagPath dagPath;
    status = MDagPath::getAPathTo(sourceNode, dagPath);
    if (!status) {
        MGlobal::displayError("Failed to get MDagPath from connected source node.");
        return status;
    }

    if (dagPath.node().hasFn(MFn::kMesh)) {
        shapePath = dagPath;
        status = dagPath.pop();
        if (!status || !dagPath.node().hasFn(MFn::kTransform)) {
            MGlobal::displayError("Failed to get transform from mesh shape.");
            return MS::kFailure;
        }
        transformPath = dagPath;
    }
    else {
        MGlobal::displayError("Source node is not a mesh shape.");
        return MS::kFailure;
    }
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
            scalePlug.child(2).asDouble(context)};

        // Build transform matrix manually
        MTransformationMatrix xform;
        xform.setTranslation(translation, MSpace::kTransform);
        xform.setRotation(rotation, rotOrder);
        xform.setScale(scale, MSpace::kTransform);
        transformationMatrices[frame] = xform;        
    }

    return MS::kSuccess;
}


MStatus Smear::computeCentroidLocal(const MDagPath& shapePath, const MDagPath& transformPath, MVector& centroidLocal) {
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

    centroidLocal = (numVertices > 0) ? sum / numVertices : MVector(0.0, 0.0, 0.0);

    return MS::kSuccess;
}

MStatus Smear::computeCentroidTrajectory(double startFrame, double endFrame, const std::vector<MTransformationMatrix>& transformationMatrices, 
                                         const MVector& centroidLocal, std::vector<MVector>& centroidPositions) {
    MStatus status;
    int numFrames = static_cast<int>(endFrame - startFrame + 1);
    centroidPositions.resize(numFrames);
    // MGlobal::displayInfo("Smear::computeCentroidTrajectory - centroid positions");
    for (int frame = 0; frame < numFrames; ++frame) {
        // Find how the position of the centroid changes based on the pivot's transformations (how it moves, rotates, scales) 
        // by multiplying the transform to centroid offset 
        MPoint transformedOffset = MPoint(centroidLocal) * transformationMatrices[frame].asMatrix();

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
    //MGlobal::displayInfo("Smear::calculatePerFrameMotionOffsets - completed!");
    return MS::kSuccess; 
}

MStatus Smear::getVerticesAtFrame(const MDagPath& shapePath, const MDagPath& transformPath, double frame, MPointArray& vertices) {
    MStatus status;

    // 1. Set up frame evaluation context
    MTime evalTime(frame, MTime::kFilm);
    MDGContext ctx(evalTime);
    //MGlobal::displayInfo(MString("Evaluating frame: ") + frame +
        //" at time: " + evalTime.as(MTime::uiUnit()));

    // 2. Get worldMatrix array plug
    MFnDependencyNode transformFn(transformPath.node());
    MPlug worldMatrixPlug = transformFn.findPlug("worldMatrix", true, &status);
    McheckErr(status, "Failed to find worldMatrix plug");

    // 3. Access first element of worldMatrix array
    MPlug elementPlug = worldMatrixPlug.elementByLogicalIndex(0, &status);
    McheckErr(status, "Failed to get worldMatrix[0]");
        
    // 4. Get matrix data in context
    MObject matrixData;
    elementPlug.getValue(matrixData, ctx);
    if (matrixData.isNull()) {
        MGlobal::displayError("Null matrix data received");
        return MS::kFailure;
    }

    // 5. Validate matrix data type
    if (!matrixData.hasFn(MFn::kMatrixData)) {
        MGlobal::displayError("Matrix data has incorrect type");
        return MS::kFailure;
    }

    // 6. Extract matrix from data
    MFnMatrixData matrixFn(matrixData, &status);
    McheckErr(status, "Failed to create MFnMatrixData");

    MMatrix worldMatrix = matrixFn.matrix();

    // 7. Get object-space vertices in context
    MFnDependencyNode shapeNode(shapePath.node());
    MPlug outMeshPlug = shapeNode.findPlug("outMesh", true, &status);
    McheckErr(status, "Failed to find outMesh plug");

    MObject meshData;
    outMeshPlug.getValue(meshData, ctx);
    if (meshData.isNull()) {
        MGlobal::displayError("Null mesh data received");
        return MS::kFailure;
    }

    // 8. Get object-space vertices
    MFnMesh meshFn(meshData, &status);
    McheckErr(status, "Failed to create MFnMesh")

    MPointArray objSpaceVerts;
    status = meshFn.getPoints(objSpaceVerts, MSpace::kObject);
    McheckErr(status, "Failed to get object-space vertices");

    // 9. Transform to world space
    vertices.setLength(objSpaceVerts.length());
    for (unsigned int i = 0; i < objSpaceVerts.length(); ++i) {
        vertices[i] = objSpaceVerts[i] * worldMatrix;
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


    // Find the start and end frame to determine the range for which we compute trajectory 
    double startFrame = -1;
    double endFrame = -1;
    status = extractAnimationFrameRange(transformPath, startFrame, endFrame);
    McheckErr(status, "Failed to extract animation frame range.");
    motionOffsets.startFrame = startFrame;
    motionOffsets.endFrame = endFrame;

    // Parse all the transformations from each frame to see how the pivot moves from animation 
    std::vector<MTransformationMatrix> transformationMatrices;
    status = computeWorldTransformPerFrame(transformPath, startFrame, endFrame, transformationMatrices);
    McheckErr(status, "Failed to compute world transforms.");

    // Compute the centroid offset so that we can use this to 
    // quickly find the centroid based on pivot location 
    // Centroid is found through average position of vertex positions
    MVector centroidLocal;
    status = computeCentroidLocal(shapePath, transformPath, centroidLocal);
    McheckErr(status, "Failed to calculate centroid offset.");

    // Calculate the centroid's positions over time 
    std::vector<MVector> centroidPositions; 
    status = computeCentroidTrajectory(startFrame, endFrame, transformationMatrices, centroidLocal, centroidPositions);
    
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

    // These object space vertices will be transformed into world space verteices later
    MPointArray objectSpaceVertices;
    status = meshFn.getPoints(objectSpaceVertices, MSpace::kObject);
    McheckErr(status, "Smear::computeMotionOffsetsSimple - Failed to get object space vertex positions");
    
    //MGlobal::displayInfo("Smear::computeMotionOffsetsSimple - Num Frames frame:" + MString() + numFrames);

    // Store vertex trajectories
    motionOffsets.vertexTrajectories.resize(numFrames);
    MPointArray vertices;

    // Iterate through each frame to find motion offsets for each frame
    for (int frame = 0; frame < numFrames; ++frame) {
        //MGlobal::displayInfo("Smear::computeMotionOffsetsSimple - Current frame:" + MString() + frame);

        status = getVerticesAtFrame(shapePath, transformPath, startFrame + frame, vertices);
        McheckErr(status, "Failed to get world-space vertices");

        motionOffsets.vertexTrajectories[frame] = vertices;

        MDoubleArray& currentFrameMotionOffsets = motionOffsets.motionOffsets[frame];
        status = calculatePerFrameMotionOffsets(objectSpaceVertices, transformationMatrices[frame], centroidPositions[frame], centroidVelocities[frame], currentFrameMotionOffsets);
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

MStatus Smear::getSkeletonInformation()
{
    MStatus status;

    // Iterate through all joints in the scene
    MItDependencyNodes jointIter(MFn::kJoint, &status);
    for (; !jointIter.isDone(); jointIter.next())
    {
        MObject jointObj = jointIter.item();
        MFnIkJoint jointFn(jointObj, &status);
        McheckErr(status, "Failed to obtain joints");
        
        // Get joint name
        MString jointName = jointFn.name();

        MFnTransform transformFn(jointObj, &status);
        McheckErr(status, "Failed to obtain the transform");
            
        // Get translation
        MVector jointPos = jointFn.getTranslation(MSpace::kTransform);

        // Get rotation (in radians)
        MTransformationMatrix::RotationOrder rotOrder = transformFn.rotationOrder();
        double rotation[3];
        transformFn.getRotation(rotation, rotOrder);

        // Get scale
        double scale[3];
        jointFn.getScale(scale);

        // Get parent joint
        MObject parentObj = jointFn.parent(0, &status);
        MString parentName = "None";
        if (status == MS::kSuccess && parentObj.hasFn(MFn::kJoint))
        {
            MFnIkJoint parentFn(parentObj);
            parentName = parentFn.name();
        }

        const double conversion = (180.0 / 3.141592653589793238463);
        MGlobal::displayInfo(MString("Joint: ") + jointName +
            " | Parent: " + parentName +
            " | Rotation: (" + rotation[0] * conversion + ", " + rotation[1] * conversion + ", " + rotation[2] * conversion + ")" +
            " | Scale: (" + scale[0] + ", " + scale[1] + ", " + scale[2] + ")" +
            " | Position: (" + jointPos.x + ", " + jointPos.y + ", " + jointPos.z + ")");
        
    }
}

MStatus Smear::getSkinClusterAndBones(const MDagPath& inputPath,
    MObject& skinClusterObj,
    MDagPathArray& influenceBones)
{
    MStatus status;

    // Make sure we’re starting from the transform
    MDagPath meshPath = inputPath;
    if (meshPath.apiType() == MFn::kMesh) {
        meshPath.pop();
    }

    // Get shape from meshPath
    MFnDagNode meshFn(meshPath);
    MObject shapeObj;
    for (unsigned int i = 0; i < meshFn.childCount(); ++i) {
        MObject child = meshFn.child(i);
        if (child.hasFn(MFn::kMesh)) {
            shapeObj = child;
            break;
        }
    }

    if (shapeObj.isNull()) {
        MGlobal::displayError("No shape found under transform: " + meshPath.fullPathName());
        return MS::kFailure;
    }

    // Loop through all skinClusters in the scene
    MItDependencyNodes itSkin(MFn::kSkinClusterFilter);
    while (!itSkin.isDone()) {
        MObject skinObj = itSkin.item();
        MFnSkinCluster skinFn(skinObj, &status);
        if (!status) {
            itSkin.next();
            continue;
        }

        // Check if this skinCluster deforms our shape
        uint32_t numGeoms = skinFn.numOutputConnections();
        for (uint32_t i = 0; i < numGeoms; ++i) {
            uint32_t index = skinFn.indexForOutputConnection(i, &status);
            if (!status) continue;

            MDagPath skinnedPath;
            status = skinFn.getPathAtIndex(index, skinnedPath);
            if (!status) continue;

            if (skinnedPath.node() == shapeObj) {
                // Found the right skinCluster
                skinClusterObj = skinObj;

                // Get influences
                skinFn.influenceObjects(influenceBones, &status);
                if (!status) {
                    MGlobal::displayError("Failed to get influences");
                    return status;
                }

                MGlobal::displayInfo("Found skinCluster: " + MFnDependencyNode(skinClusterObj).name());
                return MS::kSuccess;
            }
        }

        itSkin.next();
    }

    MGlobal::displayError("No matching skinCluster found for mesh: " + meshPath.fullPathName());
    return MS::kFailure;
}
