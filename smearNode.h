#pragma once
#include <maya/MPxDeformerNode.h>
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

class SmearNode : public MPxDeformerNode
{
private: 
	// Caches motion offsets for simlpe objects. 
	// TODO: Add a way to cache motion offsets for non-simple objects
	MotionOffsetsSimple motionOffsetsSimple; 
	// Tracks whether motion offsets are baked to avoid recomputation of offsets every frame
	bool motionOffsetsBaked; 
	MStatus getDagPathsFromInputMesh(MObject inputMeshDataObj, const MPlug& inputMeshPlug, MDagPath& transformPath, MDagPath& shapePath) const;
	

public:
	SmearNode();
	~SmearNode() override;
	static  void* creator();
	static  MStatus initialize();

	MStatus SmearNode::deform(MDataBlock& block, MItGeometry& iter, const MMatrix& localToWorldMatrix,unsigned int multiIndex) override;
	MColor computeColor(double offset);	

	static MPoint catmullRomInterpolate(const MPoint& p0, const MPoint& p1, const MPoint& p2, const MPoint& p3,double t);

	static MTypeId id;  // Unique node ID
	static MObject time; 
	static MObject inputGeom;  
	static MObject outputGeom; 
	static MColorArray currentColors; // Used for caching to avoid array reallocation every frame 
	static MObject betaMax;
	static MObject enableDeformation;
};

