#include <maya/MSimple.h>
#include <maya/MGlobal.h>
#include <maya/MSelectionList.h>
#include <maya/MDagPath.h>
#include <maya/MFnMesh.h>
#include <maya/MFnTransform.h>
#include <maya/MItMeshVertex.h>
#include <maya/MMatrix.h>
#include <maya/MVector.h>
#include <maya/MStatus.h>
#include <maya/MFnPlugin.h>
#include <maya/MFnDependencyNode.h>
#include <maya/MFnAnimCurve.h>
#include <maya/MPlug.h>
#include <maya/MPlugArray.h>
#include <maya/MAnimControl.h>
#include "smear.h" // Include your Smear class

class SmearCommand : public MPxCommand {
public:
    MStatus doIt(const MArgList& args) override {
        MStatus status;

        // Get the selected object
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

        // Test Smear functionality
        Smear smear;
        double startFrame, endFrame;
        status = smear.extractAnimationFrameRange(dagPath.node(), startFrame, endFrame);
        if (status == MS::kSuccess) {
            MGlobal::displayInfo(MString("Start Frame: ") + startFrame);
            MGlobal::displayInfo(MString("End Frame: ") + endFrame);
        } else {
            MGlobal::displayError("Failed to extract animation frame range.");
        }

        return MS::kSuccess;
    }

    static void* creator() {
        return new SmearCommand();
    }
};

// Plugin initialization
MStatus initializePlugin(MObject obj) {
    MStatus status;
    MFnPlugin plugin(obj, "YourName", "1.0", "Any");

    // Register the command
    status = plugin.registerCommand("smearCommand", SmearCommand::creator);
    if (!status) {
        status.perror("Failed to register smearCommand");
        return status;
    }

    MGlobal::displayInfo("Smear plugin loaded successfully!");
    return status;
}

// Plugin uninitialization
MStatus uninitializePlugin(MObject obj) {
    MStatus status;
    MFnPlugin plugin(obj);

    // Deregister the command
    status = plugin.deregisterCommand("smearCommand");
    if (!status) {
        status.perror("Failed to deregister smearCommand");
        return status;
    }

    MGlobal::displayInfo("Smear plugin unloaded successfully!");
    return status;
}
