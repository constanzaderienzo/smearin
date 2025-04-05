#pragma once
#include <maya/MPxNode.h>
#include "smear.h"

/*
	createNode SmearNode;
	connectAttr "time1.outTime" "SmearNode1.time";
	connectAttr "pCube1.outMesh" "SmearNode1.inputMesh";

	if (`isConnected "polyCube1.output" "pCubeShape1.inMesh"`) {
	disconnectAttr "polyCube1.output" "pCubeShape1.inMesh";
}
	connectAttr "SmearNode1.outputMesh" "pCube1.inMesh";
*/

class SmearNode : public MPxNode
{
private: 
	// Caches motion offsets for simlpe objects. 
	// TODO: Add a way to cache motion offsets for non-simple objects
	MotionOffsetsSimple motionOffsetsSimple; 
	// Tracks whether motion offsets are baked to avoid recomputation of offsets every frame
	bool motionOffsetsBaked; 
	

public:
	SmearNode();
	~SmearNode() override;
	static  void* creator();
	static  MStatus initialize();
	MStatus compute(const MPlug& plug, MDataBlock& data) override;
	MColor computeColor(double offset);	

	static MTypeId id;  // Unique node ID
	static MObject time; 
	static MObject inputMesh;  
	static MObject outputMesh; 
	static MColorArray currentColors; // Used for caching to avoid array reallocation every frame 
};

