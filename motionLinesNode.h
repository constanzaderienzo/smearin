#pragma once
#include <maya/MPxNode.h>
class MotionLinesNode :
    public MPxNode
{
private: 

public: 
	MotionLinesNode();
	~MotionLinesNode() override;
	static  void* creator();
	static  MStatus initialize();
	MStatus compute(const MPlug& plug, MDataBlock& data) override;

	static MTypeId id;  // Unique node ID

    // Attributes 
	static MObject time;
	static MObject aInputMesh;
	static MObject elongationSmoothWindowSize;
	static MObject smoothEnabled;
	static MObject aelongationStrengthPast;
	static MObject aelongationStrengthFuture;
	static MObject aGenerateMotionLines;

	// Message attribute for connecting the control node.
	static MObject inputControlMsg;
};

