#pragma once
#include "smearNode.h"
#include <maya/MPxNode.h>
#include <maya/MStatus.h>
#include <maya/MObject.h>
#include <maya/MPointArray.h>
#include <maya/MIntArray.h>
#include <maya/MFloatPointArray.h>

// Forward declaration for LSystem::Branch if not already defined
namespace LSystem {
    struct Branch;
}

class MotionLinesNode : public MPxNode
{
private: 
    // Caches motion offsets for simlpe objects. 
    // TODO: Add a way to cache motion offsets for non-simple objects
    MotionOffsetsSimple motionOffsets;
    // Tracks whether motion offsets are baked to avoid recomputation of offsets every frame
    bool motionOffsetsBaked;
    
    // Stores motion line seed vertex indices
    MIntArray seedIndices;
    int cachedMotionLinesCount; 

    // Selects seeds randomly 
    MStatus selectSeeds(int count); 

public:
    MotionLinesNode();
    ~MotionLinesNode() override;
    static void* creator();
    static MStatus initialize();
    MStatus compute(const MPlug& plug, MDataBlock& data) override;

    static MTypeId id;  // Unique node ID

    // Attributes 
    static MObject time;
    static MObject aInputMesh;
    static MObject aOutputMesh;
    static MObject smoothWindowSize;
    static MObject smoothEnabled;
    static MObject aStrengthPast;
    static MObject aStrengthFuture;
    static MObject aGenerateMotionLines;
    static MObject aMotionLinesCount; 
    static MObject aCacheLoaded;

    // Message attribute for connecting the control node.
    static MObject inputControlMsg;

    // Helper function declarations for creating geometry
    MObject MotionLinesNode::createMesh(const MTime& time, float angle, int stepSize, const MString& grammar, MObject& outData, MStatus& stat);
    MStatus appendCylinder(MPoint start, MPoint end, MPointArray& points, MIntArray& faceCounts, MIntArray& faceConnects);
    MObject createQuads(const MFloatPointArray& points, MObject& outData, MStatus& stat);
    MObject createReverseQuads(const MFloatPointArray& points, MObject& outData, MStatus& stat);
    MObject createTris(const MFloatPointArray& points, MObject& outData, MStatus& stat);
    MObject createReverseTris(const MFloatPointArray& points, MObject& outData, MStatus& stat);
};
