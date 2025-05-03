#pragma once
#include <maya/MPxCommand.h>
#include <maya/MArgList.h>
#include <maya/MGlobal.h>

class LoadCacheCmd : public MPxCommand {
public:
    static void* creator() { return new LoadCacheCmd(); }
    MStatus doIt(const MArgList& args) override;
};