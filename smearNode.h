#pragma once
#include <maya/MPxNode.h>

/*
*	createNode SmearNode;
	connectAttr "time1.outTime" "SmearNode1.time";
	connectAttr "pCube1.outMesh" "SmearNode1.inputMesh";

	// Run this line after running above three lines to avoid errors 
	if (`isConnected "polyCube1.output" "pCubeShape1.inMesh"`) {
	disconnectAttr "polyCube1.output" "pCubeShape1.inMesh";
}
	connectAttr "SmearNode1.outputMesh" "pCube1.inMesh";
*/

class SmearNode : public MPxNode
{
public:
	SmearNode() {};
	~SmearNode() override {};
	static  void* creator();
	static  MStatus initialize();
	MStatus compute(const MPlug& plug, MDataBlock& data) override;
	MColor computeColor(const MVector& offset);

	static MTypeId id;  // Unique node ID
	static MObject time; 
	static MObject inputMesh;  
	static MObject outputMesh; 
	static MColorArray currentColors; // Used for caching to avoid array reallocation every frame 
};

