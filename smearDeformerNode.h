#pragma once

#include <maya/MPxDeformerNode.h>
#include <maya/MArrayDataBuilder.h>
#include <maya/MTypeId.h>
#include <maya/MVector.h>
#include <vector>
#include "smear.h"

/*
deformer - type "SmearDeformerNode" pCube1;
connectAttr "time1.outTime" "SmearDeformerNode1.time";
*/



class SmearDeformerNode : public MPxDeformerNode
{
public:
    static MTypeId id;

    // Attributes
    static MObject aEnable;
    static MObject aBetaMax;
    static MObject time;

    SmearDeformerNode() {};
    ~SmearDeformerNode() {};
    
    // Node lifecycle
    static void* creator();
    static MStatus initialize();

    // Deformation entry point
    virtual MStatus deform(MDataBlock& block,
        MItGeometry& iter,
        const MMatrix& localToWorldMatrix,
        unsigned int multiIndex) override;

    // Precomputed animation data
    MotionOffsetsSimple motionOffsets;
    // Tracks whether motion offsets are baked to avoid recomputation of offsets every frame
    bool motionOffsetsBaked;

private:

    // Interpolation helper
    MPoint catmullRomInterpolate(const MPoint& p0, const MPoint& p1,
        const MPoint& p2, const MPoint& p3,
        double t) const;
};