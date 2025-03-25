#pragma once
#include <vector>
#include <maya/MVector.h>
#include <maya/MObject.h>
#include <maya/MFnTransform.h>
#include <maya/MFnMesh.h>
#include <maya/MVectorArray.h>

using std::cout;
using std::endl;

struct MotionOffsetsSimple {
    double startFrame;
    double endFrame;
    std::vector<MVectorArray> motionOffsets;  // 2D: motionOffsets[frame][vertex]
};

class Smear
{
private:
    // If this function does not compile, make sure to add "OpenMayaAnim.lib" in 
    // Project Properties -> Configuration Properties -> Linker -> Input -> Additional Dependencies
    static MStatus computeWorldTransformPerFrame(const MDagPath& transformPath, const double startFrame, const double endFrame, std::vector<MMatrix>& transformationMatrices);
    static MStatus calculateCentroidOffsetFromPivot(const MDagPath& shapePath, const MDagPath& transformPath, MVector& centroidOffset);
    static MStatus computeCentroidTrajectory(const MDagPath& shapePath, const MDagPath& transformPath, std::vector<MVector>& centroidPositions);
    static MStatus computeCentroidVelocity(const MDagPath& shapePath, const MDagPath& transformPath, std::vector<MVector>& centroidVelocities, double& startFrame, double& endFrame);
    static MStatus getTransformFromMesh(const MDagPath& shapePath, MDagPath& transformPath); 
public:
    static MStatus computeMotionOffsetsSimple(const MDagPath& shapePath, const MDagPath& transformPath, MotionOffsetsSimple& motionOffsets);
    static MStatus extractAnimationFrameRange(const MDagPath& transformPath, double& startFrame, double& endFrame);

};
