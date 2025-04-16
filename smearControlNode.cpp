#include "smearControlNode.h"
#include <maya/MStatus.h>
#include <maya/MFnUnitAttribute.h>
#include <maya/MFnTypedAttribute.h>
#include <maya/MFnNumericAttribute.h>
#include <maya/MFnMessageAttribute.h>

#define McheckErr(stat, msg)        \
    if (MS::kSuccess != stat) {     \
        MGlobal::displayError(msg); \
        return MS::kFailure;        \
    }

MTypeId SmearControlNode::id(0x98523); // Random id 
MObject SmearControlNode::aSmoothWindow;
MObject SmearControlNode::aStrengthPast;
MObject SmearControlNode::aStrengthFuture;
MObject SmearControlNode::aControlMsg;

SmearControlNode::SmearControlNode()
{}

SmearControlNode::~SmearControlNode()
{}

void* SmearControlNode::creator()
{
    return new SmearControlNode;
}

MStatus SmearControlNode::initialize() {
    MFnUnitAttribute uAttr;
    MFnTypedAttribute tAttr;
    MFnNumericAttribute nAttr;
    MFnMessageAttribute mAttr;
    MStatus status;

    // Create strengthPast attribute.
    aStrengthPast = nAttr.create("strengthPast", "sp", MFnNumericData::kDouble, 1.5, &status);
    CHECK_MSTATUS_AND_RETURN_IT(status);
    nAttr.setMin(0.0);
    nAttr.setMax(5.0);
    nAttr.setStorable(true);
    nAttr.setKeyable(true);
    addAttribute(aStrengthPast);

    // Create strengthFuture attribute.
    aStrengthFuture = nAttr.create("strengthFuture", "sf", MFnNumericData::kDouble, 1.5, &status);
    CHECK_MSTATUS_AND_RETURN_IT(status);
    nAttr.setMin(0.0);
    nAttr.setMax(5.0);
    nAttr.setStorable(true);
    nAttr.setKeyable(true);
    addAttribute(aStrengthFuture);

    // Create smoothWindow attribute.
    aSmoothWindow = nAttr.create("smoothWindow", "sw", MFnNumericData::kInt, 2, &status);
    CHECK_MSTATUS_AND_RETURN_IT(status);
    nAttr.setMin(0);
    nAttr.setMax(5);
    nAttr.setStorable(true);
    nAttr.setKeyable(true);
    addAttribute(aSmoothWindow);

    // Create and add a message attribute.
    aControlMsg = mAttr.create("controlMessage", "ctrlMsg", &status);
    CHECK_MSTATUS_AND_RETURN_IT(status);
    mAttr.setStorable(false); // Typically, message attributes aren’t storable.
    addAttribute(aControlMsg);


    return MS::kSuccess;
}

MStatus SmearControlNode::compute(const MPlug& plug, MDataBlock& data) {

    return MS::kSuccess;
}
