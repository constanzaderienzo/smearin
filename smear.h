#pragma once
#include <vector>
#include <maya/MVector.h>
#include <maya/MObject.h>
#include <maya/MFnTransform.h>
#include <maya/MFnMesh.h>
#include <maya/MVectorArray.h>

using std::cout;
using std::endl;

struct MotionOffsetSimple {
    double startFrame;
    double endFrame;
    std::vector<MVectorArray> motionOffsets;  // 2D: motionOffsets[frame][vertex]
};

class Smear
{
private:
    // If this function does not compile, make sure to add "OpenMayaAnim.lib" in 
    // Project Properties -> Configuration Properties -> Linker -> Input -> Additional Dependencies
    static MStatus extractAnimationFrameRange(const MObject& obj, double& startFrame, double& endFrame);
    static MStatus computeWorldTransformPerFrame(const MObject& object, std::vector<MMatrix>& transformationMatrices);
    static MStatus calculateCentroidOffsetFromPivot(MObject& object, MVector& centroidOffset);
    static MStatus computeCentroidTrajectory(MObject& object, std::vector<MVectorArray>& centroidPositions);
    static MStatus computeCentroidVelocity(MObject& object, std::vector<MVector>& centroidVelocities, double& startFrame, double& endFrame);

public:
    static MStatus computeMotionOffsetsSimple(MObject& object, MotionOffsetSimple& motionOffsets); 
};
