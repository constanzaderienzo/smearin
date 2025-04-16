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
    static MObject aStrengthPast; // Controls the length of the backward (trailing) elongation effect.
    static MObject aStrengthFuture; // Controls the length of the forward (leading) elongation effect.
    static MObject aSmoothWindow; // Controls the size of the smoothing window.

    // Message attribute to connect to the deformer node.
    static MObject aControlMsg;
};
