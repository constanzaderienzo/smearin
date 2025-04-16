#pragma once

#include <maya/MPxDeformerNode.h>
#include <maya/MArrayDataBuilder.h>
#include <maya/MTypeId.h>
#include <maya/MVector.h>
#include <vector>
#include "smear.h"

/*
deformer - type "SmearDeformerNode" pSphere1;
connectAttr "time1.outTime" "SmearDeformerNode1.time";
*/



class SmearDeformerNode : public MPxDeformerNode
{
public:
    static MTypeId id;
    

    // Attributes
    static MObject time;
    static MObject elongationSmoothWindowSize;
    static MObject smoothEnabled;
    static MObject aelongationStrengthPast;
    static MObject aelongationStrengthFuture; 
    static MObject aApplyElongation; 

    // Message attribute for connecting the control node.
    static MObject inputControlMsg;

    SmearDeformerNode();
    ~SmearDeformerNode();
    
    // Node lifecycle
    static void* creator();
    static MStatus initialize();

    // Deformation entry point
    virtual MStatus deform(MDataBlock& block,
        MItGeometry& iter,
        const MMatrix& localToWorldMatrix,
        unsigned int multiIndex) override;

private:
    MotionOffsetsSimple motionOffsets;
    bool motionOffsetsBaked;

    // Interpolation helper
    MPoint catmullRomInterpolate(const MPoint& p0, const MPoint& p1, const MPoint& p2, const MPoint& p3, float t);
};