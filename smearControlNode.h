#pragma once
#include <maya/MPxNode.h>

/*

createNode SmearControlNode;

*/

class SmearControlNode :
    public MPxNode
{
private: 

public:
	SmearControlNode();
	~SmearControlNode() override;
	static  void* creator();
	static  MStatus initialize();
	MStatus compute(const MPlug& plug, MDataBlock& data) override;

    static MTypeId id;  // Unique node ID

    // Attribute declarations
    static MObject aElongationStrengthPast; // Controls the length of the backward (trailing) elongation effect.
    static MObject aElongationStrengthFuture; // Controls the length of the forward (leading) elongation effect.
    static MObject aElongationSmoothWindow; // Controls the size of the smoothing window.
    static MObject aApplyElongation; 

    static MObject aMotionLinesCount; 
    static MObject aMotionLinesStrengthPast; // Controls the length of the backward (trailing) motion lines effect.
    static MObject aMotionLinesStrengthFuture; // Controls the length of the forward (leading) motion lines effect.
    static MObject aMotionLinesSmoothWindow; // Controls the size of the smoothing window.
    static MObject aMotionLinesSegments; // Controls how many segments make up a motion line 
    static MObject aMotionLinesRadius; // Controls the thickness of motion lines 
    static MObject aGenerateMotionLines;

    // Message attribute to connect to the deformer node.
    static MObject aControlMsg;
    // For pre-process
    static MObject aCacheLoaded;
};
