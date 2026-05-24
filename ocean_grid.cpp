#include "ocean.h"
#include "pxr/base/gf/frustum.h"
#include "pxr/base/gf/matrix4f.h"
#include "pxr/base/gf/vec3f.h"
#include <vector>

PXR_NAMESPACE_USING_DIRECTIVE

struct QuadNode {
    GfVec2f minBound;
    GfVec2f maxBound;
    int depth;
};

static void SubdivideQuad(const QuadNode& node, std::vector<QuadNode>& outNodes, const GfVec3f& camPos, const GfMatrix4f& viewProj, int maxDepth, float lodScale) {
    GfVec2f center = (node.minBound + node.maxBound) * 0.5f;
    GfVec3f center3D(center[0], 0.0f, center[1]);
    
    float dist = (center3D - camPos).GetLength();
    float size = node.maxBound[0] - node.minBound[0];
    
    // We just rely on distance for LOD, which generates a concentric grid around the camera.
    bool needsSubdiv = (node.depth < maxDepth);
    if (needsSubdiv) {
        float desiredSize = std::max(0.1f, dist * lodScale);
        if (size <= desiredSize) needsSubdiv = false;
    }
    
    if (needsSubdiv) {
        QuadNode tl = { node.minBound, center, node.depth + 1 };
        QuadNode tr = { GfVec2f(center[0], node.minBound[1]), GfVec2f(node.maxBound[0], center[1]), node.depth + 1 };
        QuadNode bl = { GfVec2f(node.minBound[0], center[1]), GfVec2f(center[0], node.maxBound[1]), node.depth + 1 };
        QuadNode br = { center, node.maxBound, node.depth + 1 };
        
        SubdivideQuad(tl, outNodes, camPos, viewProj, maxDepth, lodScale);
        SubdivideQuad(tr, outNodes, camPos, viewProj, maxDepth, lodScale);
        SubdivideQuad(bl, outNodes, camPos, viewProj, maxDepth, lodScale);
        SubdivideQuad(br, outNodes, camPos, viewProj, maxDepth, lodScale);
    } else {
        outNodes.push_back(node);
    }
}

void HdGeminiOcean::GenerateGridTopology(
    const GfMatrix4f& viewProj, 
    const GfVec3f& cameraPos,
    const GfRange3f& bounds,
    std::vector<GfVec3f>& outBasePoints,
    std::vector<GfVec3i>& outIndices,
    std::vector<GfVec2f>& outUvs) const
{
    std::vector<QuadNode> leaves;
    QuadNode root;
    root.minBound = GfVec2f(bounds.GetMin()[0], bounds.GetMin()[2]);
    root.maxBound = GfVec2f(bounds.GetMax()[0], bounds.GetMax()[2]);
    root.depth = 0;
    
    int maxDepth = 24;
    float lodScale = 0.05f;
    
    SubdivideQuad(root, leaves, cameraPos, viewProj, maxDepth, lodScale);
    
    outBasePoints.reserve(leaves.size() * 4);
    outUvs.reserve(leaves.size() * 4);
    outIndices.reserve(leaves.size() * 2);
    
    for (const auto& leaf : leaves) {
        GfVec3f p0(leaf.minBound[0], _params.waterHeight, leaf.minBound[1]);
        GfVec3f p1(leaf.maxBound[0], _params.waterHeight, leaf.minBound[1]);
        GfVec3f p2(leaf.maxBound[0], _params.waterHeight, leaf.maxBound[1]);
        GfVec3f p3(leaf.minBound[0], _params.waterHeight, leaf.maxBound[1]);
        
        int baseIdx = outBasePoints.size();
        
        outBasePoints.push_back(p0);
        outBasePoints.push_back(p1);
        outBasePoints.push_back(p2);
        outBasePoints.push_back(p3);
        
        outUvs.push_back(GfVec2f(p0[0] / _params.size, p0[2] / _params.size));
        outUvs.push_back(GfVec2f(p1[0] / _params.size, p1[2] / _params.size));
        outUvs.push_back(GfVec2f(p2[0] / _params.size, p2[2] / _params.size));
        outUvs.push_back(GfVec2f(p3[0] / _params.size, p3[2] / _params.size));
        
        outIndices.push_back(GfVec3i(baseIdx, baseIdx + 2, baseIdx + 1));
        outIndices.push_back(GfVec3i(baseIdx, baseIdx + 3, baseIdx + 2));
    }
}

void HdGeminiOcean::DisplaceGrid(
    const std::vector<GfVec3f>& basePoints,
    std::vector<GfVec3f>& outPoints,
    std::vector<GfVec3f>& outNormals) const
{
    outPoints.resize(basePoints.size());
    outNormals.resize(basePoints.size());
    for (size_t i = 0; i < basePoints.size(); ++i) {
        outPoints[i] = GetDisplacedPosition(basePoints[i]);
        outNormals[i] = GetNormal(basePoints[i]);
    }
}
