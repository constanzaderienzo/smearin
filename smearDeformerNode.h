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
connectAttr "pSphere1.outMesh" "SmearDeformerNode1.inputMesh" 
*/



class SmearDeformerNode : public MPxDeformerNode
{
public:
    static MTypeId id;
    

    // Attributes
    static MObject aEnable;
    static MObject aBetaMax;
    static MObject time;
    static MObject inputMesh;

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
    MPoint catmullRomInterpolate(const MPoint& p0, const MPoint& p1,
        const MPoint& p2, const MPoint& p3,
        double t) const;
};