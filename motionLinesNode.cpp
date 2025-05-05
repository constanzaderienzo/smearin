#include "motionLinesNode.h"
#include "smear.h"
#include "cylinder.h"    // Needed for CylinderMesh
#include <numeric>
#include <algorithm>
#include <random>
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
MObject MotionLinesNode::smoothWindowSize;
MObject MotionLinesNode::smoothEnabled;
MObject MotionLinesNode::aStrengthPast;
MObject MotionLinesNode::aStrengthFuture;
MObject MotionLinesNode::aMotionLineSegments; 
MObject MotionLinesNode::aGenerateMotionLines;
MObject MotionLinesNode::aMotionLinesCount; 
MObject MotionLinesNode::aRadius;
MObject MotionLinesNode::inputControlMsg;  // Message attribute for connecting to the control node
MObject MotionLinesNode::aCacheLoaded;

MStatus MotionLinesNode::selectSeeds(int count)
{
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

    const int totalVerts = static_cast<int>(allVerts.length());
    if (totalVerts == 0) {
        MGlobal::displayError("Mesh has no vertices to select from.");
            return MS::kFailure;
    }

    // Cap the count to the number of available vertices
    int numToSelect = std::min(count, totalVerts);

    // Generate a list of all indices and shuffle it
    std::vector<int> indices(totalVerts);
    std::iota(indices.begin(), indices.end(), 0);
    std::shuffle(indices.begin(), indices.end(), std::default_random_engine(12345));

    // Take the first 'numToSelect' unique indices
    for (int i = 0; i < numToSelect; ++i) {
        seedIndices.append(indices[i]);
    }

    return MS::kSuccess;
}



//-----------------------------------------------------------------
// Constructors and Creator Function
//-----------------------------------------------------------------
MotionLinesNode::MotionLinesNode():
    motionOffsetsSimple(), motionOffsetsBaked(false), cachedMotionLinesCount(0) 
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

    aCacheLoaded = nAttr.create("cacheLoaded", "cl", MFnNumericData::kBoolean, false, &status);
    CHECK_MSTATUS_AND_RETURN_IT(status);
    addAttribute(aCacheLoaded);

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
    addAttribute(aOutputMesh);


    // Smooth enabled attribute
    smoothEnabled = nAttr.create("smoothEnabled", "smenb", MFnNumericData::kBoolean, true, &status);
    CHECK_MSTATUS_AND_RETURN_IT(status);
    addAttribute(smoothEnabled);

    // Elongation Smooth Window Size (integer slider)
    smoothWindowSize = nAttr.create("smoothWindow", "smwin", MFnNumericData::kInt, 2, &status);
    CHECK_MSTATUS_AND_RETURN_IT(status);
    nAttr.setMin(0);
    nAttr.setMax(5);
    addAttribute(smoothWindowSize);

    // The length of the backward (trailing) elongation effect 
    aStrengthPast = nAttr.create("strengthPast", "ps", MFnNumericData::kDouble, 1.5, &status);
    CHECK_MSTATUS_AND_RETURN_IT(status);
    nAttr.setMin(0);
    nAttr.setMax(5);
    addAttribute(aStrengthPast);

    // The length of the forward (leading) elongation effect
    aStrengthFuture = nAttr.create("strengthFuture", "fs", MFnNumericData::kDouble, 1.5, &status);
    CHECK_MSTATUS_AND_RETURN_IT(status);
    nAttr.setMin(0);
    nAttr.setMax(5);
    addAttribute(aStrengthFuture);

    // Number of segments per line (int)
    aMotionLineSegments = nAttr.create("motionLineSegments", "mlseg", MFnNumericData::kInt, 3, &status);
    CHECK_MSTATUS_AND_RETURN_IT(status);
    nAttr.setMin(1);
    addAttribute(aMotionLineSegments);

    // Boolean attribute for applying motion lines generation.
    aGenerateMotionLines = nAttr.create("generateMotionLines", "gen", MFnNumericData::kBoolean, true, &status);
    CHECK_MSTATUS_AND_RETURN_IT(status);
    nAttr.setStorable(false);
    nAttr.setKeyable(false);
    addAttribute(aGenerateMotionLines);

    // Elongation Smooth Window Size (integer slider)
    aMotionLinesCount = nAttr.create("motionLinesCount", "mlcnt", MFnNumericData::kInt, 15, &status);
    CHECK_MSTATUS_AND_RETURN_IT(status);
    nAttr.setMin(0);
    nAttr.setMax(100);
    addAttribute(aMotionLinesCount);

    // Radius of the cylinders that make up a motion line
    aRadius = nAttr.create("motionLinesRadius", "mlr", MFnNumericData::kDouble, 0.05, &status);
    CHECK_MSTATUS_AND_RETURN_IT(status);
    nAttr.setMin(0);
    nAttr.setMax(1.0);
    addAttribute(aRadius);

    // Message attribute for connecting this node to the control node.
    inputControlMsg = mAttr.create("inputControlMessage", "icm", &status);
    CHECK_MSTATUS_AND_RETURN_IT(status);
    mAttr.setStorable(false);
    mAttr.setKeyable(false);
    addAttribute(inputControlMsg);

    attributeAffects(aInputMesh, aOutputMesh);
    attributeAffects(time, aOutputMesh);
    attributeAffects(smoothEnabled, aOutputMesh);
    attributeAffects(smoothWindowSize, aOutputMesh);
    attributeAffects(aStrengthPast, aOutputMesh);
    attributeAffects(aStrengthFuture, aOutputMesh);
    attributeAffects(aMotionLineSegments, aOutputMesh); 
    attributeAffects(aGenerateMotionLines, aOutputMesh);
    attributeAffects(aMotionLinesCount, aOutputMesh);
    attributeAffects(aRadius, aOutputMesh);
    attributeAffects(inputControlMsg, aOutputMesh);
    attributeAffects(aCacheLoaded, aOutputMesh);

    return MS::kSuccess;
}

//-----------------------------------------------------------------
// Cylinder Mesh Creation Related Helper Functions
//-----------------------------------------------------------------

MStatus MotionLinesNode::setMotionLinesNone(const MPlug& plug, MDataBlock& data)
{
    // Create an empty mesh and sets it to the output mesh 
    // so that existing motion lines dissapear 
    MStatus status; 
    MFnMeshData meshData;
    MObject newOutput = meshData.create(&status);
    MDataHandle outputHandle = data.outputValue(aOutputMesh, &status);
    CHECK_MSTATUS_AND_RETURN_IT(status);
    outputHandle.set(newOutput);
    data.setClean(plug);
    return status; 
}

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


MStatus MotionLinesNode::appendCylinder(MPoint start, MPoint end, double radius, 
    MPointArray& points, MIntArray& faceCounts, MIntArray& faceConnects)
{
    // Create a cylinder between the two points
    CylinderMesh cylinder(start, end, radius);
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

    MDataHandle genHandle = data.inputValue(aGenerateMotionLines, &status);
    McheckErr(status, "Failed to obtain data handle for applyElongation");
    bool genMotionLines = genHandle.asBool();

    // Only generate motion lines if it is enabled 
    if (!genMotionLines) {
        setMotionLinesNone(plug, data);
        return MS::kSuccess;
    }

    // Get shape and mesh path and determine if the mesh is articulated or not 
    MDataHandle inputHandle = data.inputValue(aInputMesh, &status);
    McheckErr(status, "Failed to get input mesh");
    MObject inputObj = inputHandle.asMesh();

    if (inputObj.isNull() || !inputObj.hasFn(MFn::kMesh)) {
        MGlobal::displayError("Input is not a valid mesh");
        return MS::kFailure;
    }

    // Get DAG paths for mesh and transform
    MFnDependencyNode thisNodeFn(thisMObject());
    MPlug inputPlug = thisNodeFn.findPlug(aInputMesh, true);

    MDagPath shapePath, transformPath;
    status = Smear::getDagPathsFromInputMesh(inputObj, inputPlug, transformPath, shapePath);
    McheckErr(status, "Failed to transform path and shape path from input object");

    // +++ Get time value +++
    MTime currentTime = data.inputValue(time, &status).asTime();
    McheckErr(status, "Failed to get time value");
    double frame = currentTime.as(MTime::kFilm);  // Get time in frames

    if (Smear::isMeshArticulated(shapePath)) {
        MDataHandle cacheLoadedHandle = data.inputValue(aCacheLoaded, &status);
        bool cacheLoaded = cacheLoadedHandle.asBool();

        // Motion offsets must be cached before generating motion lines
        if (!cacheLoaded) {
            return MS::kSuccess; 
        }

        // Maya seems to always evaluate deformer node at 24 fps, 
        // So if the viewport is set to 30 fps,
        // and the viewport's current frame is 30 
        // frameD will be 24. (frame 30 / 30 fps = 1 sec; 1 sec * 24 fps = frame 24)
        const double deformerEvaluationFPS = 24.0;
        double sampleFrameD = frame * Smear::cacheFPS / deformerEvaluationFPS;
        int sampleFrame = static_cast<int>(sampleFrameD);

        // assume Smear::vertexCache[f] corresponds to Maya frame f <- NOT TRUE ANYMORE SINCE WE ARE USING VECTOR INSTEAD OF MAP 
        if (Smear::vertexCache.find(sampleFrame) == Smear::vertexCache.end())
            return MS::kFailure;

        const FrameCache& fc = Smear::vertexCache[sampleFrame];
        const MDoubleArray& offsets = fc.motionOffsets; 
        const int numFrames = Smear::vertexCache.size(); 

        // Compute smoothed offsets, etc.
        const bool smoothingEnabled = data.inputValue(smoothEnabled).asBool();
        const int N = smoothingEnabled ? data.inputValue(smoothWindowSize).asInt() : 0;
        std::vector<double> smoothedOffsets(offsets.length(), 0.0);
        for (int vertIdx = 0; vertIdx < offsets.length(); ++vertIdx) {
            double totalWeight = 0.0;
            double smoothed = 0.0;
            for (int n = -N; n <= N; ++n) {
                const int frame = sampleFrame + n;
                if (Smear::vertexCache.find(frame) == Smear::vertexCache.end()) {
                    continue;
                }
                const double normalized = std::abs(n) / static_cast<double>(N + 1);
                const double weight = std::pow(1.0 - std::pow(normalized, 2.0), 2.0);

                if (Smear::vertexCache.find(sampleFrame) == Smear::vertexCache.end())
                    continue; 

                smoothed += Smear::vertexCache[frame].motionOffsets[vertIdx] * weight;
                totalWeight += weight;
            }
            smoothedOffsets[vertIdx] = totalWeight > 0.0 ? smoothed / totalWeight : offsets[vertIdx];
        }

        MPointArray mlPoints;
        MIntArray mlFaceCounts;
        MIntArray mlFaceConnects;

        // Artistic control param
        const double strengthPast = data.inputValue(aStrengthPast).asDouble();
        const double strengthFuture = data.inputValue(aStrengthFuture).asDouble();
        const double cylinderRadius = data.inputValue(aRadius).asDouble();
        const int segmentCount = data.inputValue(aMotionLineSegments).asInt();

        int motionLinesCount = data.inputValue(aMotionLinesCount).asInt();
        if (cachedMotionLinesCount != motionLinesCount) {
            selectSeeds(motionLinesCount);
            cachedMotionLinesCount = motionLinesCount;
        }

        for (unsigned int s = 0; s < seedIndices.length(); s++) {
            int vertexIndex = seedIndices[s]; 

            // Get the smoothed offset for this vertex.
            double offset = smoothedOffsets[vertexIndex];

            // Determine sampling direction:
            // +1 for positive (leading) offsets, -1 for negative (trailing) offsets.
            int direction = (offset >= 0.0) ? 1 : -1;

            // Determine the appropriate motion line strength factor.
            // These are assumed to be parameters from your node.
            double strengthFactor = (offset >= 0.0) ? strengthFuture : strengthPast;

            // Build a polyline along the vertex's trajectory.
            // Instead of sampling consecutive frames, multiply the segment index by the strength factor.
            MPointArray polyLine;
            for (int seg = 0; seg <= segmentCount; seg++) {
                double totalLength = strengthFactor; // treat strength as total motion line length in frames
                double frameInterval = totalLength / static_cast<double>(segmentCount);
                double sampleOffset = seg * frameInterval * direction;
                double sampleFrameD = sampleFrame + sampleOffset;

                // Integer and fractional components
                int f1 = static_cast<int>(floor(sampleFrameD));
                float t = static_cast<float>(sampleFrameD - f1);

                // Need f0, f1, f2, f3 for Catmull-Rom
                int f0 = f1 - 1;
                int f2 = f1 + 1;
                int f3 = f1 + 2;

                // Validate bounds
                if (Smear::vertexCache.find(f0) == Smear::vertexCache.end() ||
                    Smear::vertexCache.find(f1) == Smear::vertexCache.end() ||
                    Smear::vertexCache.find(f2) == Smear::vertexCache.end() ||
                    Smear::vertexCache.find(f3) == Smear::vertexCache.end())
                {
                    continue; // Or break;
                }

                const MPoint& p0 = Smear::vertexCache[f0].positions[vertexIndex];
                const MPoint& p1 = Smear::vertexCache[f1].positions[vertexIndex];
                const MPoint& p2 = Smear::vertexCache[f2].positions[vertexIndex];
                const MPoint& p3 = Smear::vertexCache[f3].positions[vertexIndex];

                MPoint interpolated = Smear::catmullRomInterpolate(p0, p1, p2, p3, t);
                polyLine.append(interpolated);
            }

            // Create cylinder segments between consecutive polyline points.
            for (unsigned int j = 0; j + 1 < polyLine.length(); j++) {
                status = appendCylinder(polyLine[j], polyLine[j + 1], cylinderRadius, 
                    mlPoints, mlFaceCounts, mlFaceConnects);
                if (status != MS::kSuccess) {
                    MGlobal::displayError("Failed to append cylinder for motion line segment.");
                    return status;
                }
            }
        }

        // Create new mesh data container
        MFnMeshData meshData;
        MObject newOutput = meshData.create(&status);

        // Copy mesh using API method
        MFnMesh inputFn(inputObj);
        MObject copiedMesh = inputFn.copy(inputObj, newOutput, &status);
        McheckErr(status, "Mesh copy failed");

        // Cast copied Mesh into MFnMesh
        MFnMesh outputFn(copiedMesh, &status);
        McheckErr(status, "Output mesh init failed");


        //MGlobal::displayInfo(MString() + "mlPoints.length(): " + mlPoints.length() + "mlFaceCounts.length(): " + mlFaceCounts.length());
        MFnMesh meshFn;
        MObject motionLinesMesh = meshFn.create(mlPoints.length(), mlFaceCounts.length(),
            mlPoints, mlFaceCounts, mlFaceConnects, newOutput, &status);
        if (status != MS::kSuccess) {
            MGlobal::displayError("Motion lines mesh creation failed.");
            return status;
        }

        MDataHandle outputHandle = data.outputValue(aOutputMesh, &status);
        CHECK_MSTATUS_AND_RETURN_IT(status);
        outputHandle.set(newOutput);
        data.setClean(plug);

        return MS::kSuccess;
    }
    else {
        return computeSimple(status, inputObj, data, shapePath, transformPath, frame, plug);
    }
}

const MStatus& MotionLinesNode::computeSimple(MStatus& status, MObject& inputObj, MDataBlock& data, MDagPath& shapePath, MDagPath& transformPath, double frame, const MPlug& plug)
{
    // Create new mesh data container
    MFnMeshData meshData;
    MObject newOutput = meshData.create(&status);
    McheckErr(status, "Failed to create output mesh container");

    // Copy mesh using API method
    MFnMesh inputFn(inputObj);
    MObject copiedMesh = inputFn.copy(inputObj, newOutput, &status);
    McheckErr(status, "Mesh copy failed");

    // Cast copied Mesh into MFnMesh
    MFnMesh outputFn(copiedMesh, &status);
    McheckErr(status, "Output mesh init failed");

    const int numVertices = outputFn.numVertices();
    if (numVertices == 0) {
        MGlobal::displayError("Mesh has no vertices");
        return MS::kFailure;
    }

    int motionLinesCount = data.inputValue(aMotionLinesCount).asInt();
    if (cachedMotionLinesCount != motionLinesCount) {
        selectSeeds(motionLinesCount);
        cachedMotionLinesCount = motionLinesCount;
    }

    // +++ Compute motion offsets using Smear functions +++
    if (!motionOffsetsBaked) {
        status = Smear::computeMotionOffsetsSimple(shapePath, transformPath, motionOffsetsSimple);

        McheckErr(status, "Failed to compute motion offsets");
        motionOffsetsBaked = true;
    }

    int frameIndex = static_cast<int>(frame - motionOffsetsSimple.startFrame);

    if (frameIndex < 0 || frameIndex >= motionOffsetsSimple.motionOffsets.size()) {
        return MS::kSuccess;
    }

    const MDoubleArray& offsets = motionOffsetsSimple.motionOffsets[frameIndex];
    const std::vector<MPointArray>& trajectories = motionOffsetsSimple.vertexTrajectories;
    const int numFrames = trajectories.size();

    // Compute smoothed offsets, etc.
    const bool smoothingEnabled = data.inputValue(smoothEnabled).asBool();
    const int N = smoothingEnabled ? data.inputValue(smoothWindowSize).asInt() : 0;
    std::vector<double> smoothedOffsets(offsets.length(), 0.0);
    for (int vertIdx = 0; vertIdx < offsets.length(); ++vertIdx) {
        double totalWeight = 0.0;
        double smoothed = 0.0;
        for (int n = -N; n <= N; ++n) {
            const int frame = frameIndex + n;
            if (frame < 0 || frame >= motionOffsetsSimple.motionOffsets.size()) continue;
            const double normalized = std::abs(n) / static_cast<double>(N + 1);
            const double weight = std::pow(1.0 - std::pow(normalized, 2.0), 2.0);
            smoothed += motionOffsetsSimple.motionOffsets[frame][vertIdx] * weight;
            totalWeight += weight;
        }
        smoothedOffsets[vertIdx] = totalWeight > 0.0 ? smoothed / totalWeight : offsets[vertIdx];
    }

    MPointArray mlPoints;
    MIntArray mlFaceCounts;
    MIntArray mlFaceConnects;

    // Artistic control param
    const double strengthPast = data.inputValue(aStrengthPast).asDouble();
    const double strengthFuture = data.inputValue(aStrengthFuture).asDouble();
    const int segmentCount = 3;
    const double cylinderRadius = data.inputValue(aRadius).asDouble();

    for (unsigned int s = 0; s < seedIndices.length(); s++) {
        int vertexIndex = seedIndices[s];

        // Get the smoothed offset for this vertex.
        double offset = smoothedOffsets[vertexIndex];

        // Determine sampling direction:
        // +1 for positive (leading) offsets, -1 for negative (trailing) offsets.
        int direction = (offset >= 0.0) ? 1 : -1;

        // Determine the appropriate motion line strength factor.
        // These are assumed to be parameters from your node.
        double strengthFactor = (offset >= 0.0) ? strengthFuture : strengthPast;

        // Build a polyline along the vertex's trajectory.
        // Instead of sampling consecutive frames, multiply the segment index by the strength factor.
        MPointArray polyLine;
        for (int seg = 0; seg <= segmentCount; seg++) {
            // Calculate a frame increment scaled by the strength factor.
            int frameIncrement = static_cast<int>(round(seg * strengthFactor));
            int sampleFrame = frameIndex + frameIncrement * direction;
            if (sampleFrame < 0 || sampleFrame >= numFrames)
                break;
            polyLine.append(trajectories[sampleFrame][vertexIndex]);
        }

        // Create cylinder segments between consecutive polyline points.
        for (unsigned int j = 0; j < polyLine.length() - 1; j++) {
            status = appendCylinder(polyLine[j], polyLine[j + 1], 2.0, 
                mlPoints, mlFaceCounts, mlFaceConnects);
            if (status != MS::kSuccess) {
                MGlobal::displayError("Failed to append cylinder for motion line segment.");
                return status;
            }
        }
    }

    MFnMesh meshFn;
    MObject motionLinesMesh = meshFn.create(mlPoints.length(), mlFaceCounts.length(),
        mlPoints, mlFaceCounts, mlFaceConnects, newOutput, &status);
    if (status != MS::kSuccess) {
        MGlobal::displayError("Motion lines mesh creation failed.");
        return status;
    }

    MDataHandle outputHandle = data.outputValue(aOutputMesh, &status);
    CHECK_MSTATUS_AND_RETURN_IT(status);
    outputHandle.set(newOutput);
    data.setClean(plug);

    return MS::kSuccess;
}