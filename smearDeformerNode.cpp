#include "smearDeformerNode.h"
#include <maya/MItGeometry.h>

MTypeId SmearDeformerNode::id(0x98530); // Random id 

void* SmearDeformerNode::creator()
{
    return new SmearDeformerNode();
}

MStatus SmearDeformerNode::initialize()
{

    return MS::kSuccess;
}

MStatus SmearDeformerNode::deform(MDataBlock& block, MItGeometry& iter, const MMatrix& localToWorldMatrix, unsigned int multiIndex)
{
    MPoint point;
    for (; !iter.isDone(); iter.next()) {
        const int vertIdx = iter.index();
        point = iter.position();
        point += MPoint(2.f, 2.f, 2.f, 0.f);

        // Apply to vertex
        iter.setPosition(point);
    }
    return MStatus();
}
