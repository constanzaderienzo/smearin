#include <maya/MPxCommand.h>
#include <maya/MFnPlugin.h>
#include <maya/MGlobal.h>
#include <maya/MSelectionList.h>
#include <maya/MDagPath.h>
#include <maya/MFnMesh.h>
#include <maya/MColorArray.h>
#include <maya/MIntArray.h>
#include <maya/MStatus.h>
#include <cstdlib> // for rand()

class VertexColorCommand : public MPxCommand {
public:
    MStatus doIt(const MArgList&) override {
        MSelectionList selection;
        MGlobal::getActiveSelectionList(selection);
        
        if (selection.length() == 0) {
            MGlobal::displayError("No mesh selected!");
            return MS::kFailure;
        }

        MDagPath dagPath;
        selection.getDagPath(0, dagPath);

        if (!dagPath.hasFn(MFn::kMesh)) {
            MGlobal::displayError("Selected object is not a mesh!");
            return MS::kFailure;
        }

        MFnMesh meshFn(dagPath);
        MIntArray vertexIndices;
        MColorArray colors;

        int numVertices = meshFn.numVertices();
        for (int i = 0; i < numVertices; i++) {
            float r = static_cast<float>(rand()) / RAND_MAX;
            float g = static_cast<float>(rand()) / RAND_MAX;
            float b = static_cast<float>(rand()) / RAND_MAX;
            colors.append(MColor(r, g, b, 1.0f)); // RGBA
            vertexIndices.append(i);
        }

        MStatus status = meshFn.setVertexColors(colors, vertexIndices);
        if (!status) {
            MGlobal::displayError("Failed to apply vertex colors.");
            return status;
        }

        MGlobal::displayInfo("Vertex colors applied successfully!");
        return MS::kSuccess;
    }

    static void* creator() {
        return new VertexColorCommand();
    }
};

// Plugin registration
MStatus initializePlugin(MObject obj) {
    MFnPlugin plugin(obj, "SMEARin", "1.0", "Any");
    return plugin.registerCommand("colorVertices", VertexColorCommand::creator);
}

MStatus uninitializePlugin(MObject obj) {
    MFnPlugin plugin(obj);
    return plugin.deregisterCommand("colorVertices");
}
