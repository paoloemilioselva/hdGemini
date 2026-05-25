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
    GfVec3f color;
};

static void SubdivideQuad(const QuadNode& node, std::vector<QuadNode>& outNodes, const GfVec3f& camPos, const GfMatrix4f& viewProj, int screenWidth, int screenHeight, int maxDepth, float dicingScale, float waterHeight) {
    if (node.depth >= maxDepth) {
        outNodes.push_back(node);
        return;
    }

    // 4 corners on the water plane
    GfVec3f p0(node.minBound[0], waterHeight, node.minBound[1]);
    GfVec3f p1(node.maxBound[0], waterHeight, node.minBound[1]);
    GfVec3f p2(node.maxBound[0], waterHeight, node.maxBound[1]);
    GfVec3f p3(node.minBound[0], waterHeight, node.maxBound[1]);
    
    // Project points into NDC space
    auto Project = [&](const GfVec3f& p, GfVec2f& ndc, float& w) {
        GfVec4f p4(p[0], p[1], p[2], 1.0f);
        GfVec4f pProj = p4 * viewProj;
        w = pProj[3];
        float absW = std::max(1e-5f, std::abs(w));
        ndc = GfVec2f(pProj[0] / absW, pProj[1] / absW);
    };
    
    GfVec2f ndc0, ndc1, ndc2, ndc3;
    float w0, w1, w2, w3;
    Project(p0, ndc0, w0);
    Project(p1, ndc1, w1);
    Project(p2, ndc2, w2);
    Project(p3, ndc3, w3);

    bool insideFrustum = true;
    if (w0 < 0 && w1 < 0 && w2 < 0 && w3 < 0) insideFrustum = false;
    else if (ndc0[0] < -1.5f && ndc1[0] < -1.5f && ndc2[0] < -1.5f && ndc3[0] < -1.5f) insideFrustum = false;
    else if (ndc0[0] > 1.5f && ndc1[0] > 1.5f && ndc2[0] > 1.5f && ndc3[0] > 1.5f) insideFrustum = false;
    else if (ndc0[1] < -1.5f && ndc1[1] < -1.5f && ndc2[1] < -1.5f && ndc3[1] < -1.5f) insideFrustum = false;
    else if (ndc0[1] > 1.5f && ndc1[1] > 1.5f && ndc2[1] > 1.5f && ndc3[1] > 1.5f) insideFrustum = false;
    
    bool subdivide = false;
    float size = node.maxBound[0] - node.minBound[0];
    GfVec3f center3D((node.minBound[0]+node.maxBound[0])*0.5f, waterHeight, (node.minBound[1]+node.maxBound[1])*0.5f);
    float dist = (center3D - camPos).GetLength();

    if (insideFrustum) {
        if (w0 < 0 || w1 < 0 || w2 < 0 || w3 < 0) {
            // Intersects near plane/camera
            if (size > std::max(0.1f, dist * 0.05f)) subdivide = true;
        } else {
            // Calculate max edge length in NDC
            float e0 = (ndc1 - ndc0).GetLength();
            float e1 = (ndc2 - ndc1).GetLength();
            float e2 = (ndc3 - ndc2).GetLength();
            float e3 = (ndc0 - ndc3).GetLength();
            float maxEdgeNdc = std::max({e0, std::max(e1, std::max(e2, e3))});
            
            float maxPixelSize = maxEdgeNdc * 0.5f * std::max(screenWidth, screenHeight);
            if (maxPixelSize > dicingScale) subdivide = true;
        }
    } else {
        // Coarse fallback for off-screen/behind camera
        if (size > std::max(1.0f, dist * 0.2f)) subdivide = true;
    }
    
    if (subdivide) {
        GfVec2f center = (node.minBound + node.maxBound) * 0.5f;
        
        auto randColor = []() {
            return GfVec3f(
                (float)(rand() % 256) / 255.0f,
                (float)(rand() % 256) / 255.0f,
                (float)(rand() % 256) / 255.0f
            );
        };
        
        QuadNode tl = { node.minBound, center, node.depth + 1, randColor() };
        QuadNode tr = { GfVec2f(center[0], node.minBound[1]), GfVec2f(node.maxBound[0], center[1]), node.depth + 1, randColor() };
        QuadNode bl = { GfVec2f(node.minBound[0], center[1]), GfVec2f(center[0], node.maxBound[1]), node.depth + 1, randColor() };
        QuadNode br = { center, node.maxBound, node.depth + 1, randColor() };
        
        SubdivideQuad(tl, outNodes, camPos, viewProj, screenWidth, screenHeight, maxDepth, dicingScale, waterHeight);
        SubdivideQuad(tr, outNodes, camPos, viewProj, screenWidth, screenHeight, maxDepth, dicingScale, waterHeight);
        SubdivideQuad(bl, outNodes, camPos, viewProj, screenWidth, screenHeight, maxDepth, dicingScale, waterHeight);
        SubdivideQuad(br, outNodes, camPos, viewProj, screenWidth, screenHeight, maxDepth, dicingScale, waterHeight);
    } else {
        outNodes.push_back(node);
    }
}

void HdGeminiOcean::GenerateGridTopology(
    const GfMatrix4f& viewProj, 
    const GfVec3f& cameraPos,
    const GfRange3f& bounds,
    int screenWidth,
    int screenHeight,
    std::vector<GfVec3f>& outBasePoints,
    std::vector<GfVec3i>& outIndices,
    std::vector<GfVec2f>& outUvs,
    std::vector<GfVec3f>& outColors) const
{
    std::vector<QuadNode> leaves;
    QuadNode root;
    root.minBound = GfVec2f(bounds.GetMin()[0], bounds.GetMin()[2]);
    root.maxBound = GfVec2f(bounds.GetMax()[0], bounds.GetMax()[2]);
    root.depth = 0;
    root.color = GfVec3f(0.5f, 0.5f, 0.5f);
    
    int maxDepth = 24;
    SubdivideQuad(root, leaves, cameraPos, viewProj, screenWidth, screenHeight, maxDepth, std::max(0.1f, _params.dicingScale), _params.waterHeight);
    
    outBasePoints.reserve(leaves.size() * 4);
    outUvs.reserve(leaves.size() * 4);
    outColors.reserve(leaves.size() * 4);
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
        
        // UVs are normalized to macro size
        float macroSize = _params.size[0] > 1e-5f ? _params.size[0] : 10.0f;
        outUvs.push_back(GfVec2f(p0[0] / macroSize, p0[2] / macroSize));
        outUvs.push_back(GfVec2f(p1[0] / macroSize, p1[2] / macroSize));
        outUvs.push_back(GfVec2f(p2[0] / macroSize, p2[2] / macroSize));
        outUvs.push_back(GfVec2f(p3[0] / macroSize, p3[2] / macroSize));
        
        if (_params.disableShader) {
            outColors.push_back(leaf.color);
            outColors.push_back(leaf.color);
            outColors.push_back(leaf.color);
            outColors.push_back(leaf.color);
        } else {
            outColors.push_back(GfVec3f(GetFoam(p0)));
            outColors.push_back(GfVec3f(GetFoam(p1)));
            outColors.push_back(GfVec3f(GetFoam(p2)));
            outColors.push_back(GfVec3f(GetFoam(p3)));
        }
        
        outIndices.push_back(GfVec3i(baseIdx, baseIdx + 2, baseIdx + 1));
        outIndices.push_back(GfVec3i(baseIdx, baseIdx + 3, baseIdx + 2));
    }
}

void HdGeminiOcean::DisplaceGrid(
    const std::vector<GfVec3f>& basePoints,
    std::vector<GfVec3f>& outDisplaced,
    std::vector<GfVec3f>& outNormals) const
{
    outDisplaced.resize(basePoints.size());
    outNormals.resize(basePoints.size());
    for (size_t i = 0; i < basePoints.size(); ++i) {
        outDisplaced[i] = GetDisplacedPosition(basePoints[i]);
        outNormals[i] = GetNormal(basePoints[i]);
    }
}
