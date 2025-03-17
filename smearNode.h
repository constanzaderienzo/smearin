#pragma once
#include <maya/MPxNode.h>
class SmearNode : public MPxNode
{
public:
	SmearNode() {};
	~SmearNode() override {};
	static  void* creator();
	static  MStatus initialize();
	MStatus compute(const MPlug& plug, MDataBlock& data) override;
	MColor computeColor(double frame); 

	static MTypeId id;  // Unique node ID
	static MObject time; 
	static MObject inputMesh;  
	static MObject outputMesh; 
	static MColorArray currentColors;
};

