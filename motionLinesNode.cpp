#include "motionLinesNode.h"
#include <maya/MFnUnitAttribute.h>
#include <maya/MFnTypedAttribute.h>
#include <maya/MFnNumericAttribute.h>
#include <maya/MFnMessageAttribute.h>

MTypeId MotionLinesNode::id(0x98534); // Random id 
MObject MotionLinesNode::time;
MObject MotionLinesNode::aInputMesh;
MObject MotionLinesNode::elongationSmoothWindowSize;
MObject MotionLinesNode::smoothEnabled;
MObject MotionLinesNode::aelongationStrengthPast;
MObject MotionLinesNode::aelongationStrengthFuture;
MObject MotionLinesNode::aGenerateMotionLines;

// Message attribute for connecting to the control node.
MObject MotionLinesNode::inputControlMsg;

MotionLinesNode::MotionLinesNode()
{}

MotionLinesNode::~MotionLinesNode()
{}

void* MotionLinesNode::creator()
{
    return new MotionLinesNode;
}

MStatus MotionLinesNode::initialize() {
    MFnUnitAttribute uAttr;
    MFnTypedAttribute tAttr;
    MFnNumericAttribute nAttr;
    MFnMessageAttribute mAttr;
    MStatus status;

    // Time attribute
    time = uAttr.create("time", "tm", MFnUnitAttribute::kTime, 0.0);
    addAttribute(time);

    // Input mesh
    aInputMesh = tAttr.create("inputMesh", "in", MFnData::kMesh);
    tAttr.setStorable(false);
    addAttribute(aInputMesh);

    smoothEnabled = nAttr.create("smoothEnabled", "smenb", MFnNumericData::kBoolean, true);
    addAttribute(smoothEnabled);

    elongationSmoothWindowSize = nAttr.create("elongationSmoothWindow", "smwin", MFnNumericData::kInt, 2);
    nAttr.setMin(0);
    nAttr.setMax(5);
    addAttribute(elongationSmoothWindowSize);

    // The length of the backward (trailing) elongation effect 
    aelongationStrengthPast = nAttr.create("Past Strength", "ps", MFnNumericData::kDouble, 1.5);
    nAttr.setMin(0);
    nAttr.setMax(5);
    addAttribute(aelongationStrengthPast);

    // The length of the forward (leading) elongation effect
    aelongationStrengthFuture = nAttr.create("Future Strength", "fs", MFnNumericData::kDouble, 1.5);
    nAttr.setMin(0);
    nAttr.setMax(5);
    addAttribute(aelongationStrengthFuture);

    // Create the boolean attribute for applying elongation.
    aGenerateMotionLines = nAttr.create("generateMotionLines", "gen", MFnNumericData::kBoolean, true, &status);
    nAttr.setStorable(false);
    nAttr.setKeyable(false);
    addAttribute(aGenerateMotionLines);

    // Create the message attribute that will connect this deformer to the control node.
    inputControlMsg = mAttr.create("inputControlMessage", "icm", &status);
    mAttr.setStorable(false);
    mAttr.setKeyable(false);
    addAttribute(inputControlMsg);

    

    return MS::kSuccess;
}

MStatus MotionLinesNode::compute(const MPlug& plug, MDataBlock& data) {

    return MS::kSuccess;
}
