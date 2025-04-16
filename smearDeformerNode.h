#pragma once

#include <maya/MPxDeformerNode.h>
#include <maya/MArrayDataBuilder.h>
#include <maya/MTypeId.h>
#include <maya/MDagPathArray.h>
#include <maya/MVector.h>
#include <vector>
#include "smear.h"

/*
deformer -type "SmearDeformerNode" pCylinder1;
connectAttr "time1.outTime" "SmearDeformerNode1.time";
*/

struct InfluenceData {
    uint influenceIndex;
    float weight;
};

class SmearDeformerNode : public MPxDeformerNode
{
public:
    static MTypeId id;
    

    // Attributes
    static MObject time;
    static MObject smoothWindowSize;
    static MObject smoothEnabled;
    static MObject aStrength;

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

    bool skinDataBaked;
    MObject m_skinCluster;
    MDagPathArray m_influenceBones;
    std::vector<BoneData> m_boneData;
    std::vector<std::vector<InfluenceData>> vertexWeights;


    // Interpolation helper
    MPoint catmullRomInterpolate(const MPoint& p0, const MPoint& p1, const MPoint& p2, const MPoint& p3, float t);
};