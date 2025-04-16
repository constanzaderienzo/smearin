#include "smearControlNode.h"
#include <maya/MStatus.h>
#include <maya/MFnUnitAttribute.h>
#include <maya/MFnTypedAttribute.h>
#include <maya/MFnNumericAttribute.h>


#define McheckErr(stat, msg)        \
    if (MS::kSuccess != stat) {     \
        MGlobal::displayError(msg); \
        return MS::kFailure;        \
    }

MTypeId SmearControlNode::id(0x98523); // Random id 

SmearControlNode::SmearControlNode()
{}

SmearControlNode::~SmearControlNode()
{}

void* SmearControlNode::creator()
{
    return new SmearControlNode;
}

MStatus SmearControlNode::initialize() {
    MFnUnitAttribute unitAttr;
    MFnTypedAttribute typedAttr;
    MFnNumericAttribute numericAttr;
    MStatus status;

    return MS::kSuccess;
}

MStatus SmearControlNode::compute(const MPlug& plug, MDataBlock& data) {

    return MS::kSuccess;
}
