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
};
