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
    static MObject elongationSmoothWindowSize;
    static MObject smoothEnabled;
    static MObject aelongationStrengthPast;
    static MObject aelongationStrengthFuture; 
    static MObject aApplyElongation; 
    static MObject aCacheLoaded;


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
    void applyDeformation(MItGeometry& iter, int frameIndex);
    MStatus deformSimple(MDataBlock& block, MItGeometry& iter, MDagPath& meshPath, MDagPath& transformPath);
    MStatus deformArticulated(MDataBlock& block, MItGeometry& iter, MDagPath& meshPath);
    MStatus getDagPaths(MDataBlock& block, MItGeometry iter, unsigned int multiIndex, MDagPath& meshPath, MDagPath& transformPath);

private:
    MotionOffsetsSimple motionOffsets;
    bool motionOffsetsBaked;

    bool skinDataBaked;
    MObject m_skinCluster;
    MDagPathArray m_influenceBones;
    std::vector<BoneData> m_boneData;
    std::vector<std::vector<InfluenceData>> vertexWeights;

    // Artistic control variables
    double elongationStrengthPast;
    double elongationStrengthFuture;
    bool smoothingEnabled;
    int N;
};