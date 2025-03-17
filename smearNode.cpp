    #include "smearNode.h"
#include <maya/MFnMesh.h>
#include <maya/MFnUnitAttribute.h>
#include <maya/MFnTypedAttribute.h>
#include <maya/MStatus.h>
#include <maya/MGlobal.h>
#include <maya/MFnMeshData.h>
#include <maya/MPointArray.h>

#define McheckErr(stat,msg)			\
	if ( MS::kSuccess != stat ) {	\
		cerr << msg;				\
		return MS::kFailure;		\
	}

MTypeId SmearNode::id(0x98520); // Random id 
MObject SmearNode::time;
MObject SmearNode::inputMesh;
MObject SmearNode::outputMesh;
MColorArray SmearNode::currentColors; 

void* SmearNode::creator()
{
    return new SmearNode;
}

MStatus SmearNode::initialize() {
    MFnUnitAttribute unitAttr;
    MFnTypedAttribute typedAttr;
    MStatus status;

    // Time attribute
    time = unitAttr.create("time", "tm", MFnUnitAttribute::kTime, 0.0);
    addAttribute(time);

    // Input mesh
    inputMesh = typedAttr.create("inputMesh", "in", MFnData::kMesh);
    typedAttr.setStorable(true);
    addAttribute(inputMesh);

    // Output mesh
    outputMesh = typedAttr.create("outputMesh", "out", MFnData::kMesh);
    typedAttr.setWritable(false);
    typedAttr.setStorable(false);
    addAttribute(outputMesh);

    // Affects relationships
    attributeAffects(time, outputMesh);
    attributeAffects(inputMesh, outputMesh);

    return MS::kSuccess;
}

MStatus SmearNode::compute(const MPlug& plug, MDataBlock& data) {
    if (plug != outputMesh) return MS::kUnknownParameter;

    MStatus status;

    // +++ Get time value +++
    MTime currentTime = data.inputValue(time, &status).asTime();
    McheckErr(status, "Failed to get time value");
    double frame = currentTime.as(MTime::kFilm);  // Get time in frames

    MDataHandle inputHandle = data.inputValue(inputMesh, &status);
    if (!status) {
        MGlobal::displayError("Failed to get input mesh handle");
        return status;
    }

    // Validate mesh type
    MObject inputObj = inputHandle.asMesh();
    if (inputObj.isNull() || !inputObj.hasFn(MFn::kMesh)) {
        MGlobal::displayError("Input is not a valid mesh");
        return MS::kFailure;
    }

    // Create new mesh data
    MFnMeshData meshData;
    MObject newOutput = meshData.create(&status);
    McheckErr(status, "Failed to create output mesh container");

    // Copy mesh using API method
    MFnMesh inputFn(inputObj);
    MObject copiedMesh = inputFn.copy(inputObj, newOutput, &status);
    McheckErr(status, "Mesh copy failed");

    // Apply vertex colors
    MFnMesh outputFn(copiedMesh, &status);
    McheckErr(status, "Output mesh init failed");

    const int numVertices = outputFn.numVertices();
    if (numVertices == 0) {
        MGlobal::displayError("Mesh has no vertices");
        return MS::kFailure;
    }

    MColorArray colors(numVertices);
    MIntArray vtxIndices(numVertices);

    // +++ Time-based color calculation +++
    MColor color = computeColor(frame);
    for (int i = 0; i < numVertices; ++i) {
        colors.set(color, i);
        vtxIndices[i] = i;
    }

    MGlobal::displayInfo("SmearNode compute() triggered! Time: " + MString() + MString(std::to_string(frame).c_str()));

    // Create/update color set
    MString colorSet("smearSet");
    outputFn.createColorSetWithName(colorSet);
    outputFn.setCurrentColorSetName(colorSet);

    // +++ Apply colors to specific color set with time variation +++
    status = outputFn.setVertexColors(colors, vtxIndices);
    McheckErr(status, "Failed to set colors");

    // Force viewport update
    outputFn.updateSurface();

    // Assign output
    MDataHandle outputHandle = data.outputValue(outputMesh);
    outputHandle.set(newOutput);
    data.setClean(plug);

    return MS::kSuccess;
}


MColor SmearNode::computeColor(double frame)
{
    float r = 0.5f + 0.5f * std::sin(frame * 0.1f);
    float g = 0.5f + 0.5f * std::sin(frame * 0.2f);
    float b = 0.5f + 0.5f * std::sin(frame * 0.3f);
    return MColor(r, g, b, 1.0f);
}