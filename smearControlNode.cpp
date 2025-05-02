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
MObject SmearControlNode::aElongationSmoothWindow;
MObject SmearControlNode::aElongationStrengthPast;
MObject SmearControlNode::aElongationStrengthFuture;
MObject SmearControlNode::aApplyElongation; 

MObject SmearControlNode::aMotionLinesCount;
MObject SmearControlNode::aMotionLinesStrengthPast; 
MObject SmearControlNode::aMotionLinesStrengthFuture; 
MObject SmearControlNode::aMotionLinesSmoothWindow; 
MObject SmearControlNode::aGenerateMotionLines;

MObject SmearControlNode::aControlMsg;
MObject SmearControlNode::trigger;

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

    trigger = nAttr.create("trigger", "trg", MFnNumericData::kBoolean);
    addAttribute(trigger);

    // Create elongationStrengthPast attribute.
    aElongationStrengthPast = nAttr.create("elongationStrengthPast", "sp", MFnNumericData::kDouble, 1.5, &status);
    CHECK_MSTATUS_AND_RETURN_IT(status);
    nAttr.setMin(0.0);
    nAttr.setMax(5.0);
    nAttr.setStorable(true);
    nAttr.setKeyable(true);
    addAttribute(aElongationStrengthPast);

    // Create elongationStrengthFuture attribute.
    aElongationStrengthFuture = nAttr.create("elongationStrengthFuture", "sf", MFnNumericData::kDouble, 1.5, &status);
    CHECK_MSTATUS_AND_RETURN_IT(status);
    nAttr.setMin(0.0);
    nAttr.setMax(5.0);
    nAttr.setStorable(true);
    nAttr.setKeyable(true);
    addAttribute(aElongationStrengthFuture);

    // Create elongationSmoothWindow attribute.
    aElongationSmoothWindow = nAttr.create("elongationSmoothWindow", "sw", MFnNumericData::kInt, 2, &status);
    CHECK_MSTATUS_AND_RETURN_IT(status);
    nAttr.setMin(0);
    nAttr.setMax(5);
    nAttr.setStorable(true);
    nAttr.setKeyable(true);
    addAttribute(aElongationSmoothWindow);

    // Create the boolean attribute for applying elongation.
    aApplyElongation = nAttr.create("applyElongation", "apl", MFnNumericData::kBoolean, true, &status);
    nAttr.setStorable(true);
    nAttr.setKeyable(true);
    addAttribute(aApplyElongation); 

    // Create elongationSmoothWindow attribute.
    aMotionLinesCount = nAttr.create("motionLinesCount", "mlcnt", MFnNumericData::kInt, 15, &status);
    CHECK_MSTATUS_AND_RETURN_IT(status);
    nAttr.setMin(0);
    nAttr.setMax(100);
    nAttr.setStorable(true);
    nAttr.setKeyable(true);
    addAttribute(aMotionLinesCount);

    // Create motionLinesStrengthPast attribute.
    // Controls the length of the backward (trailing) elongation effect.
    aMotionLinesStrengthPast = nAttr.create("motionLinesStrengthPast", "mlsp", MFnNumericData::kDouble, 1.5, &status);
    CHECK_MSTATUS_AND_RETURN_IT(status);
    nAttr.setMin(0.0);
    nAttr.setMax(5.0);
    nAttr.setStorable(true);
    nAttr.setKeyable(true);
    addAttribute(aMotionLinesStrengthPast);

    // Create motionLinesStrengthFuture attribute.
    // Controls the length of the forward (leading) elongation effect.
    aMotionLinesStrengthFuture = nAttr.create("motionLinesStrengthFuture", "mlsf", MFnNumericData::kDouble, 1.5, &status);
    CHECK_MSTATUS_AND_RETURN_IT(status);
    nAttr.setMin(0.0);
    nAttr.setMax(5.0);
    nAttr.setStorable(true);
    nAttr.setKeyable(true);
    addAttribute(aMotionLinesStrengthFuture);

    // Create motionLinesSmoothWindow attribute.
    // Controls the size of the smoothing window.
    aMotionLinesSmoothWindow = nAttr.create("motionLinesSmoothWindow", "mlsw", MFnNumericData::kInt, 2, &status);
    CHECK_MSTATUS_AND_RETURN_IT(status);
    nAttr.setMin(0);
    nAttr.setMax(5);
    nAttr.setStorable(true);
    nAttr.setKeyable(true);
    addAttribute(aMotionLinesSmoothWindow);

    // Create generateMotionLines attribute.
    // Boolean attribute to drive generation of motion lines.
    aGenerateMotionLines = nAttr.create("generateMotionLines", "gml", MFnNumericData::kBoolean, true, &status);
    CHECK_MSTATUS_AND_RETURN_IT(status);
    nAttr.setStorable(true);
    nAttr.setKeyable(true);
    addAttribute(aGenerateMotionLines);

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
