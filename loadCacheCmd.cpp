#include "loadCacheCmd.h"
#include "smear.h"

MStatus LoadCacheCmd::doIt(const MArgList& args) {
    if (args.length() < 1) {
        MGlobal::displayError("Usage: loadCache <path_to_cache.json>");
        return MS::kFailure;
    }

    MString path = args.asString(0);
    bool success = Smear::loadCache(path);

    if (success) {
        MGlobal::displayInfo("SMEARin: Cache loaded successfully.");
        MGlobal::displayInfo(MString("[SMEARin] C++ loadCache succeeded; got ")
            + Smear::vertexCache.size() + " frames.");
        return MS::kSuccess;
    }
    else {
        MGlobal::displayError("SMEARin: Failed to load cache.");
        return MS::kFailure;
    }
}