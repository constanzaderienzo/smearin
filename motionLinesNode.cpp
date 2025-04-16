#include "motionLinesNode.h"
#include "smear.h"
#include "cylinder.h"    // Needed for CylinderMesh
#include <maya/MFnUnitAttribute.h>
#include <maya/MFnTypedAttribute.h>
#include <maya/MFnNumericAttribute.h>
#include <maya/MFnMessageAttribute.h>
#include <maya/MFnMesh.h>
#include <maya/MFnMeshData.h>
#include <maya/MPointArray.h>
#include <maya/MIntArray.h>
#include <maya/MFloatPointArray.h>
#include <maya/MFloatPoint.h>
#include <maya/MStatus.h>
#include <maya/MTime.h>
#include <maya/MGlobal.h>
#include <maya/MDagPath.h> 

#define McheckErr(stat, msg)        \
    if (MS::kSuccess != stat) {     \
        MGlobal::displayError(msg); \
        return MS::kFailure;        \
    }

//-----------------------------------------------------------------
// Static Attribute Declarations
//-----------------------------------------------------------------
MTypeId MotionLinesNode::id(0x98534); // Random id 
MObject MotionLinesNode::time;
MObject MotionLinesNode::aInputMesh;
MObject MotionLinesNode::aOutputMesh;
MObject MotionLinesNode::elongationSmoothWindowSize;
MObject MotionLinesNode::smoothEnabled;
MObject MotionLinesNode::aelongationStrengthPast;
MObject MotionLinesNode::aelongationStrengthFuture;
MObject MotionLinesNode::aGenerateMotionLines;
MObject MotionLinesNode::inputControlMsg;  // Message attribute for connecting to the control node

MStatus MotionLinesNode::selectSeeds(double density)
{
    // Validate density is between 0 and 1
    if (density < 0.0 || density > 1.0) {
        MGlobal::displayWarning("Density out of range; defaulting to 0.1 (10%).");
        density = 0.1;
    }

    seedIndices.clear();

    // Retrieve the input mesh from the node’s attribute.
    MPlug meshPlug(thisMObject(), aInputMesh);
    MObject meshObj = meshPlug.asMObject();
    if (meshObj.isNull()) {
        MGlobal::displayError("Input mesh is null. Cannot select seed vertices.");
        return MS::kFailure;
    }

    // Use MFnMesh to get vertex positions in world space.
    MFnMesh meshFn(meshObj);
    MPointArray allVerts;
    MStatus status = meshFn.getPoints(allVerts, MSpace::kWorld);
    if (!status) {
        MGlobal::displayError("Failed to retrieve points from the input mesh.");
        return status;
    }

    // Seed the random number generator.
    srand(12345);

    // Iterate over all vertices and randomly select based on the density.
    for (unsigned int i = 0; i < allVerts.length(); i++) {
        double randomVal = (double)rand() / (double)RAND_MAX;
        if (randomVal < density) {
            seedIndices.append((int)i);  // Record the vertex index as well.
        }
    }

    return MS::kSuccess;
}


//-----------------------------------------------------------------
// Constructors and Creator Function
//-----------------------------------------------------------------
MotionLinesNode::MotionLinesNode():
    motionOffsetsSimple(), motionOffsetsBaked(false)
{}
MotionLinesNode::~MotionLinesNode() {}

void* MotionLinesNode::creator() {
    return new MotionLinesNode;
}

//-----------------------------------------------------------------
// Attribute Initialization
//-----------------------------------------------------------------
MStatus MotionLinesNode::initialize() {
    MStatus status;
    MFnUnitAttribute    uAttr;
    MFnTypedAttribute   tAttr;
    MFnNumericAttribute nAttr;
    MFnMessageAttribute mAttr;

    // Time attribute
    time = uAttr.create("time", "tm", MFnUnitAttribute::kTime, 0.0, &status);
    CHECK_MSTATUS_AND_RETURN_IT(status);
    addAttribute(time);

    // Input mesh attribute
    aInputMesh = tAttr.create("inputMesh", "in", MFnData::kMesh, MObject::kNullObj, &status);
    CHECK_MSTATUS_AND_RETURN_IT(status);
    tAttr.setStorable(false);
    addAttribute(aInputMesh);

    // Output mesh attribute
    aOutputMesh = tAttr.create("outputMesh", "out", MFnData::kMesh, MObject::kNullObj, &status);
    CHECK_MSTATUS_AND_RETURN_IT(status);
    tAttr.setStorable(false);
    addAttribute(aOutputMesh);


    // Smooth enabled attribute
    smoothEnabled = nAttr.create("smoothEnabled", "smenb", MFnNumericData::kBoolean, true, &status);
    CHECK_MSTATUS_AND_RETURN_IT(status);
    addAttribute(smoothEnabled);

    // Elongation Smooth Window Size (integer slider)
    elongationSmoothWindowSize = nAttr.create("elongationSmoothWindow", "smwin", MFnNumericData::kInt, 2, &status);
    CHECK_MSTATUS_AND_RETURN_IT(status);
    nAttr.setMin(0);
    nAttr.setMax(5);
    addAttribute(elongationSmoothWindowSize);

    // The length of the backward (trailing) elongation effect 
    aelongationStrengthPast = nAttr.create("Past Strength", "ps", MFnNumericData::kDouble, 1.5, &status);
    CHECK_MSTATUS_AND_RETURN_IT(status);
    nAttr.setMin(0);
    nAttr.setMax(5);
    addAttribute(aelongationStrengthPast);

    // The length of the forward (leading) elongation effect
    aelongationStrengthFuture = nAttr.create("Future Strength", "fs", MFnNumericData::kDouble, 1.5, &status);
    CHECK_MSTATUS_AND_RETURN_IT(status);
    nAttr.setMin(0);
    nAttr.setMax(5);
    addAttribute(aelongationStrengthFuture);

    // Boolean attribute for applying motion lines generation.
    aGenerateMotionLines = nAttr.create("generateMotionLines", "gen", MFnNumericData::kBoolean, true, &status);
    CHECK_MSTATUS_AND_RETURN_IT(status);
    nAttr.setStorable(false);
    nAttr.setKeyable(false);
    addAttribute(aGenerateMotionLines);

    // Message attribute for connecting this node to the control node.
    inputControlMsg = mAttr.create("inputControlMessage", "icm", &status);
    CHECK_MSTATUS_AND_RETURN_IT(status);
    mAttr.setStorable(false);
    mAttr.setKeyable(false);
    addAttribute(inputControlMsg);

    attributeAffects(aInputMesh, aOutputMesh);
    attributeAffects(time, aOutputMesh);
    attributeAffects(smoothEnabled, aOutputMesh);
    attributeAffects(elongationSmoothWindowSize, aOutputMesh);
    attributeAffects(aelongationStrengthPast, aOutputMesh);
    attributeAffects(aelongationStrengthFuture, aOutputMesh);
    attributeAffects(aGenerateMotionLines, aOutputMesh);
    attributeAffects(inputControlMsg, aOutputMesh);

    return MS::kSuccess;
}

//-----------------------------------------------------------------
// Cylinder Mesh Creation Related Helper Functions
//-----------------------------------------------------------------

MObject MotionLinesNode::createMesh(const MTime& time,
    float angle,
    int stepSize,
    const MString& grammar,
    MObject& outData,
    MStatus& stat)

{
    // Arrays to store the combined mesh data
    MPointArray points;          // Vertex positions
    MIntArray faceCounts;        // Number of vertices per face
    MIntArray faceConnects;      // Vertex indices for each face

    // Iterate through each branch and create a cylinder
    //for (int i = 0; i < branches.size(); ++i) {
    //    LSystem::Branch& branch = branches.at(i);

    //    // Convert vec3 to MPoint
    //    MPoint start(branch.first[0], branch.first[2], branch.first[1]);
    //    MPoint end(branch.second[0], branch.second[2], branch.second[1]);

    //    // Create a cylinder for this branch
    //    CylinderMesh cylinder(start, end, 0.1); // Radius = 0.1

    //    // Append the cylinder's vertices and faces to the main arrays
    //    cylinder.appendToMesh(points, faceCounts, faceConnects);
    //}

    // Create the final mesh using the combined vertices and faces
    MFnMesh meshFn;
    MObject newMesh = meshFn.create(
        points.length(),      // Number of vertices
        faceCounts.length(),  // Number of faces
        points,               // Vertex positions
        faceCounts,           // Number of vertices per face
        faceConnects,         // Vertex indices for each face
        outData,              // Output mesh object
        &stat                 // Status of the operation
    );

    if (stat != MS::kSuccess) {
        return MObject::kNullObj;
    }

    return newMesh;
}


MStatus MotionLinesNode::appendCylinder(MPoint start, MPoint end,
    MPointArray& points, MIntArray& faceCounts, MIntArray& faceConnects)
{
    // Create a cylinder between the two points (Radius = 0.1)
    CylinderMesh cylinder(start, end, 0.1);
    // Append cylinder geometry to the main mesh arrays.
    cylinder.appendToMesh(points, faceCounts, faceConnects);

    return MS::kSuccess;
}


// Create a mesh from a set of points as quads. This method creates a quad-based mesh (e.g., for a cube).
MObject MotionLinesNode::createQuads(const MFloatPointArray& points, MObject& outData, MStatus& stat)
{
    const int numFaces = 6;
    int fc[numFaces] = { 4, 4, 4, 4, 4, 4 };
    MIntArray faceCounts(fc, numFaces);

    const int numFaceConnects = 24;
    int fcIndices[numFaceConnects] = { 0, 1, 2, 3,
                                        4, 5, 6, 7,
                                        3, 2, 6, 5,
                                        0, 3, 5, 4,
                                        0, 4, 7, 1,
                                        1, 7, 6, 2 };
    MIntArray faceConnects(fcIndices, numFaceConnects);

    MFnMesh meshFn;
    return meshFn.create(points.length(), faceCounts.length(),
        points, faceCounts, faceConnects, outData, &stat);
}

// Create a mesh from a set of points as reverse quads.
MObject MotionLinesNode::createReverseQuads(const MFloatPointArray& points, MObject& outData, MStatus& stat)
{
    const int numFaces = 6;
    int fc[numFaces] = { 4, 4, 4, 4, 4, 4 };
    MIntArray faceCounts(fc, numFaces);

    const int numFaceConnects = 24;
    int fcIndices[numFaceConnects] = { 0, 3, 2, 1,
                                        4, 7, 6, 5,
                                        3, 5, 6, 2,
                                        0, 4, 5, 3,
                                        0, 1, 7, 4,
                                        1, 2, 6, 7 };
    MIntArray faceConnects(fcIndices, numFaceConnects);

    MFnMesh meshFn;
    return meshFn.create(points.length(), faceCounts.length(),
        points, faceCounts, faceConnects, outData, &stat);
}

// Create a mesh using triangular faces.
MObject MotionLinesNode::createTris(const MFloatPointArray& points, MObject& outData, MStatus& stat)
{
    const int numFaces = 12;
    int fc[numFaces] = { 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3 };
    MIntArray faceCounts(fc, numFaces);

    const int numFaceConnects = 36;
    int fcIndices[numFaceConnects] = { 0, 1, 2,
                                       2, 3, 0,
                                       4, 5, 6,
                                       6, 7, 4,
                                       3, 2, 6,
                                       6, 5, 3,
                                       0, 3, 5,
                                       5, 4, 0,
                                       0, 4, 7,
                                       7, 1, 0,
                                       1, 7, 6,
                                       6, 2, 1 };
    MIntArray faceConnects(fcIndices, numFaceConnects);

    MFnMesh meshFn;
    return meshFn.create(points.length(), faceCounts.length(),
        points, faceCounts, faceConnects, outData, &stat);
}

// Create a mesh using reverse triangles.
MObject MotionLinesNode::createReverseTris(const MFloatPointArray& points, MObject& outData, MStatus& stat)
{
    const int numFaces = 12;
    int fc[numFaces] = { 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3, 3 };
    MIntArray faceCounts(fc, numFaces);

    const int numFaceConnects = 36;
    int fcIndices[numFaceConnects] = { 0, 2, 1,
                                       2, 0, 3,
                                       4, 6, 5,
                                       6, 4, 7,
                                       3, 6, 2,
                                       6, 3, 5,
                                       0, 5, 3,
                                       5, 0, 4,
                                       0, 7, 4,
                                       7, 0, 1,
                                       1, 6, 7,
                                       6, 1, 2 };
    MIntArray faceConnects(fcIndices, numFaceConnects);

    MFnMesh meshFn;
    return meshFn.create(points.length(), faceCounts.length(),
        points, faceCounts, faceConnects, outData, &stat);
}

MStatus MotionLinesNode::compute(const MPlug& plug, MDataBlock& data) {
    MStatus status;

    // +++ Get time value +++
    MTime currentTime = data.inputValue(time, &status).asTime();
    McheckErr(status, "Failed to get time value");
    double frame = currentTime.as(MTime::kFilm);  // Get time in frames

    MDataHandle inputHandle = data.inputValue(aInputMesh, &status);
    McheckErr(status, "Failed to get input mesh");
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

    // Get DAG paths for mesh and transform
    MFnDependencyNode thisNodeFn(thisMObject());
    MPlug inputPlug = thisNodeFn.findPlug(aInputMesh, true);

    MDagPath shapePath, transformPath;
    status = Smear::getDagPathsFromInputMesh(inputObj, inputPlug, transformPath, shapePath);
    McheckErr(status, "Failed to tranform path and shape path from input object");

    // Cast copied Mesh into MfnMesh
    MFnMesh outputFn(copiedMesh, &status);
    McheckErr(status, "Output mesh init failed");

    const int numVertices = outputFn.numVertices();
    if (numVertices == 0) {
        MGlobal::displayError("Mesh has no vertices");
        return MS::kFailure;
    }

    // +++ Compute motion offsets using Smear functions +++
    if (!motionOffsetsBaked) {
        status = Smear::computeMotionOffsetsSimple(shapePath, transformPath, motionOffsetsSimple);

        // For now, hard code density but should be an attrib later
        double lineDensity = 0.1; 
        selectSeeds(lineDensity);

        McheckErr(status, "Failed to compute motion offsets");
        motionOffsetsBaked = true;
    }

    int frameIndex = static_cast<int>(frame - motionOffsetsSimple.startFrame);

    //MGlobal::displayInfo("Current frame: " + MString() + frame +
    //    " Start frame: " + motionOffsetsSimple.startFrame +
    //    " Frame index: " + frameIndex);

    if (frameIndex < 0 || frameIndex >= motionOffsetsSimple.motionOffsets.size()) {
        return MS::kSuccess;
    }

    MDoubleArray& currentFrameOffsets = motionOffsetsSimple.motionOffsets[frameIndex];
    if (currentFrameOffsets.length() != numVertices) {
        MGlobal::displayError("Offset/vertex count mismatch");
        return MS::kFailure;
    }

    return MS::kSuccess;
}
