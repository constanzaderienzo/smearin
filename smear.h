#pragma once
#include <vector>
#include <maya/MVector.h>
#include <maya/MObject.h>
#include <maya/MFnTransform.h>
#include <maya/MFnMesh.h>
#include <maya/MVectorArray.h>
#include <maya/MPointArray.h>
#include <maya/MDoubleArray.h>

using std::cout;
using std::endl;

struct MotionOffsetsSimple {
    double startFrame;
    double endFrame;
    std::vector<MDoubleArray> motionOffsets;  // 2D: motionOffsets[frame][vertex]
    std::vector<MPointArray> vertexTrajectories; // Store per-vertex trajectory
};

class Smear
{
private:
    // If this function does not compile, make sure to add "OpenMayaAnim.lib" in 
    // Project Properties -> Configuration Properties -> Linker -> Input -> Additional Dependencies
    static MStatus computeWorldTransformPerFrame(const MDagPath& transformPath, const double startFrame, const double endFrame, std::vector<MTransformationMatrix>& transformationMatrices);
    static MStatus computeCentroidLocal(const MDagPath& shapePath, const MDagPath& transformPath, MVector& centroidLocal);
    static MStatus computeCentroidTrajectory(double startFrame, double endFrame, const std::vector<MTransformationMatrix>& transformationMatrices, const MVector& centroidLocal, std::vector<MVector>& centroidPositions);
    static MStatus computeCentroidVelocity(const std::vector<MVector>& centroidPositions, std::vector<MVector>& centroidVelocities);
    static MStatus getTransformFromMesh(const MDagPath& shapePath, MDagPath& transformPath); 
    static MStatus computeSignedDistanceToPlane(const MPoint& point, const MPoint& pointOnPlane, const MVector& planeNormal, double& signedDist);
    static MStatus calculatePerFrameMotionOffsets(const MPointArray& vertexPositions, const MTransformationMatrix& transformationMatrix, const MPoint& centroid, const MVector& centroidVelocity, MDoubleArray& motionOffsets);
    static MStatus getVerticesAtFrame(const MDagPath& shapePath, const MDagPath& transformPath, double frame, MPointArray& vertices);
public:
    static MStatus computeMotionOffsetsSimple(const MDagPath& shapePath, const MDagPath& transformPath, MotionOffsetsSimple& motionOffsets);
    static MStatus extractAnimationFrameRange(const MDagPath& transformPath, double& startFrame, double& endFrame);
    static MStatus getDagPathsFromInputMesh(MObject inputMeshDataObj, const MPlug& inputMeshPlug, MDagPath& transformPath, MDagPath& shapePath);
};
