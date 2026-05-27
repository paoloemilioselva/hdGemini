#include "ocean.h"
#include <vector>
#include "pxr/base/gf/matrix4d.h"
#include "pxr/base/gf/vec4d.h"

PXR_NAMESPACE_USING_DIRECTIVE

void HdGeminiOcean::GenerateGridTopology(
    const GfMatrix4d& viewMatrix,
    const GfMatrix4d& projMatrix,
    int viewportWidth,
    int viewportHeight,
    std::vector<GfVec3f>& outBasePoints,
    std::vector<GfVec3i>& outIndices,
    std::vector<GfVec2f>& outUvs,
    std::vector<GfVec3f>& outColors) const
{
    outBasePoints.clear();
    outIndices.clear();
    outUvs.clear();
    outColors.clear();

    if (_params.size <= 1e-5f) return;

    struct QuadNode {
        float x, z, size;
        int depth;
        int children[4];
        bool isLeaf;
        bool isCulled;
    };

    std::vector<QuadNode> tree;
    tree.push_back({0.0f, 0.0f, _params.size, 0, {-1,-1,-1,-1}, true, false});

    int MAX_DEPTH = 10;
    float maxAmp = std::abs(_params.amplitude[0]) + std::abs(_params.amplitude[1]) + std::abs(_params.amplitude[2]);
    if (maxAmp < 1.0f) maxAmp = 1.0f; 

    auto ShouldSubdivide = [&](const QuadNode& node) -> bool {
        float hs = node.size * 0.5f;
        GfVec3f corners[8] = {
            GfVec3f(node.x - hs, _params.waterHeight - maxAmp, node.z - hs),
            GfVec3f(node.x + hs, _params.waterHeight - maxAmp, node.z - hs),
            GfVec3f(node.x - hs, _params.waterHeight - maxAmp, node.z + hs),
            GfVec3f(node.x + hs, _params.waterHeight - maxAmp, node.z + hs),
            GfVec3f(node.x - hs, _params.waterHeight + maxAmp, node.z - hs),
            GfVec3f(node.x + hs, _params.waterHeight + maxAmp, node.z - hs),
            GfVec3f(node.x - hs, _params.waterHeight + maxAmp, node.z + hs),
            GfVec3f(node.x + hs, _params.waterHeight + maxAmp, node.z + hs)
        };
        
        float minX = 1e30f, maxX = -1e30f;
        float minY = 1e30f, maxY = -1e30f;
        bool allBehind = true;
        
        for (int i=0; i<8; ++i) {
            GfVec4d p = GfVec4d(corners[i][0], corners[i][1], corners[i][2], 1.0) * viewMatrix;
            if (p[2] <= 0.0) allBehind = false;
            
            GfVec4d clip = p * projMatrix;
            if (clip[3] != 0.0) {
                float w = std::abs((float)clip[3]);
                float ndcX = (float)clip[0] / w;
                float ndcY = (float)clip[1] / w;
                float sx = (ndcX * 0.5f + 0.5f) * viewportWidth;
                float sy = (ndcY * 0.5f + 0.5f) * viewportHeight;
                minX = std::min(minX, sx);
                maxX = std::max(maxX, sx);
                minY = std::min(minY, sy);
                maxY = std::max(maxY, sy);
            }
        }
        
        if (allBehind) return false;
        
        float edgeLen = std::max(maxX - minX, maxY - minY);
        return edgeLen > _params.dicingScale;
    };

    int head = 0;
    while (head < tree.size()) {
        QuadNode& node = tree[head];
        if (node.depth < MAX_DEPTH && ShouldSubdivide(node)) {
            node.isLeaf = false;
            float qs = node.size * 0.25f;
            float ns = node.size * 0.5f;
            int c0 = tree.size();
            tree.push_back({node.x - qs, node.z - qs, ns, node.depth + 1, {-1,-1,-1,-1}, true, false});
            tree.push_back({node.x + qs, node.z - qs, ns, node.depth + 1, {-1,-1,-1,-1}, true, false});
            tree.push_back({node.x - qs, node.z + qs, ns, node.depth + 1, {-1,-1,-1,-1}, true, false});
            tree.push_back({node.x + qs, node.z + qs, ns, node.depth + 1, {-1,-1,-1,-1}, true, false});
            node.children[0] = c0;
            node.children[1] = c0+1;
            node.children[2] = c0+2;
            node.children[3] = c0+3;
        }
        head++;
    }

    auto getDepthAt = [&](float x, float z) -> int {
        int curr = 0;
        while (!tree[curr].isLeaf) {
            float cx = tree[curr].x;
            float cz = tree[curr].z;
            if (x < cx && z < cz) curr = tree[curr].children[0];
            else if (x >= cx && z < cz) curr = tree[curr].children[1];
            else if (x < cx && z >= cz) curr = tree[curr].children[2];
            else curr = tree[curr].children[3];
        }
        return tree[curr].depth;
    };

    bool changed = true;
    while (changed) {
        changed = false;
        int nNodes = tree.size();
        for (int i=0; i<nNodes; ++i) {
            QuadNode& node = tree[i];
            if (!node.isLeaf) continue;
            
            float hs = node.size * 0.5f;
            float eps = _params.size * 1e-4f;
            
            int top = getDepthAt(node.x, node.z - hs - eps);
            int bot = getDepthAt(node.x, node.z + hs + eps);
            int left = getDepthAt(node.x - hs - eps, node.z);
            int right = getDepthAt(node.x + hs + eps, node.z);
            
            int maxNeighborDepth = std::max({top, bot, left, right});
            if (maxNeighborDepth > node.depth + 1 && node.depth < MAX_DEPTH) {
                node.isLeaf = false;
                float qs = node.size * 0.25f;
                float ns = node.size * 0.5f;
                int c0 = tree.size();
                tree.push_back({node.x - qs, node.z - qs, ns, node.depth + 1, {-1,-1,-1,-1}, true, false});
                tree.push_back({node.x + qs, node.z - qs, ns, node.depth + 1, {-1,-1,-1,-1}, true, false});
                tree.push_back({node.x - qs, node.z + qs, ns, node.depth + 1, {-1,-1,-1,-1}, true, false});
                tree.push_back({node.x + qs, node.z + qs, ns, node.depth + 1, {-1,-1,-1,-1}, true, false});
                node.children[0] = c0;
                node.children[1] = c0+1;
                node.children[2] = c0+2;
                node.children[3] = c0+3;
                changed = true;
            }
        }
    }

    auto randColor = []() { return GfVec3f((float)(rand() % 256) / 255.0f, (float)(rand() % 256) / 255.0f, (float)(rand() % 256) / 255.0f); };

    for (const auto& node : tree) {
        if (!node.isLeaf || node.isCulled) continue;
        
        float hs = node.size * 0.5f;
        float eps = _params.size * 1e-4f;
        
        int top = getDepthAt(node.x, node.z - hs - eps);
        int bot = getDepthAt(node.x, node.z + hs + eps);
        int left = getDepthAt(node.x - hs - eps, node.z);
        int right = getDepthAt(node.x + hs + eps, node.z);
        
        bool splitTop = (top > node.depth);
        bool splitBot = (bot > node.depth);
        bool splitLeft = (left > node.depth);
        bool splitRight = (right > node.depth);
        
        float x0 = node.x - hs;
        float x1 = node.x + hs;
        float z0 = node.z - hs;
        float z1 = node.z + hs;
        float y = _params.waterHeight;
        
        int vCenter = outBasePoints.size();
        outBasePoints.push_back(GfVec3f(node.x, y, node.z));
        
        int vTL = outBasePoints.size(); outBasePoints.push_back(GfVec3f(x0, y, z0));
        int vTR = outBasePoints.size(); outBasePoints.push_back(GfVec3f(x1, y, z0));
        int vBL = outBasePoints.size(); outBasePoints.push_back(GfVec3f(x0, y, z1));
        int vBR = outBasePoints.size(); outBasePoints.push_back(GfVec3f(x1, y, z1));
        
        int vT = -1, vB = -1, vL = -1, vR = -1;
        if (splitTop) { vT = outBasePoints.size(); outBasePoints.push_back(GfVec3f(node.x, y, z0)); }
        if (splitBot) { vB = outBasePoints.size(); outBasePoints.push_back(GfVec3f(node.x, y, z1)); }
        if (splitLeft) { vL = outBasePoints.size(); outBasePoints.push_back(GfVec3f(x0, y, node.z)); }
        if (splitRight) { vR = outBasePoints.size(); outBasePoints.push_back(GfVec3f(x1, y, node.z)); }
        
        GfVec3f rc = _params.disableShader ? randColor() : GfVec3f(0.0f);
        while (outColors.size() < outBasePoints.size()) {
            outColors.push_back(rc);
            float curX = outBasePoints[outColors.size() - 1][0];
            float curZ = outBasePoints[outColors.size() - 1][2];
            outUvs.push_back(GfVec2f((curX + _params.size*0.5f) / _params.size, (curZ + _params.size*0.5f) / _params.size));
        }
        
        if (splitTop) {
            outIndices.push_back(GfVec3i(vCenter, vTL, vT));
            outIndices.push_back(GfVec3i(vCenter, vT, vTR));
        } else {
            outIndices.push_back(GfVec3i(vCenter, vTL, vTR));
        }
        
        if (splitRight) {
            outIndices.push_back(GfVec3i(vCenter, vTR, vR));
            outIndices.push_back(GfVec3i(vCenter, vR, vBR));
        } else {
            outIndices.push_back(GfVec3i(vCenter, vTR, vBR));
        }
        
        if (splitBot) {
            outIndices.push_back(GfVec3i(vCenter, vBR, vB));
            outIndices.push_back(GfVec3i(vCenter, vB, vBL));
        } else {
            outIndices.push_back(GfVec3i(vCenter, vBR, vBL));
        }
        
        if (splitLeft) {
            outIndices.push_back(GfVec3i(vCenter, vBL, vL));
            outIndices.push_back(GfVec3i(vCenter, vL, vTL));
        } else {
            outIndices.push_back(GfVec3i(vCenter, vBL, vTL));
        }
    }
}

void HdGeminiOcean::DisplaceGrid(
    const std::vector<GfVec3f>& basePoints,
    const GfVec3f& cameraPos,
    std::vector<GfVec3f>& outDisplaced,
    std::vector<GfVec3f>& outNormals,
    std::vector<GfVec3f>& outColors) const
{
    outDisplaced.resize(basePoints.size());
    outNormals.resize(basePoints.size());
    outColors.resize(basePoints.size());
    for (size_t i = 0; i < basePoints.size(); ++i) {
        GfVec3f offsetPos = basePoints[i];
        outDisplaced[i] = GetDisplacedPosition(offsetPos);
        outNormals[i] = GetNormal(offsetPos);
        outColors[i] = GfVec3f(GetFoam(offsetPos));
    }
}
