#include "ocean.h"
#include <vector>

PXR_NAMESPACE_USING_DIRECTIVE

void HdGeminiOcean::GenerateGridTopology(
    std::vector<GfVec3f>& outBasePoints,
    std::vector<GfVec3i>& outIndices,
    std::vector<GfVec2f>& outUvs,
    std::vector<GfVec3f>& outColors,
    std::vector<int>& outTypes) const
{
    int N = _params.gridSize;
    float size = _params.size > 1e-5f ? _params.size : 100.0f;
    float halfSize = size * 0.5f;
    
    auto mapToGrid = [&](int i) -> float {
        float u = (float)i / (float)N * 2.0f - 1.0f;
        float dist = std::pow(std::abs(u), 3.0f); // Power of 3 gives good central detail
        return std::copysign(dist, u) * halfSize;
    };
    
    outBasePoints.reserve(N * N * 4 + N * 4 * 4 + 4);
    outUvs.reserve(N * N * 4 + N * 4 * 4 + 4);
    outColors.reserve(N * N * 4 + N * 4 * 4 + 4);
    outTypes.reserve(N * N * 4 + N * 4 * 4 + 4);
    outIndices.reserve(N * N * 2 + N * 4 * 2 + 2);

    auto randColor = []() {
        return GfVec3f(
            (float)(rand() % 256) / 255.0f,
            (float)(rand() % 256) / 255.0f,
            (float)(rand() % 256) / 255.0f
        );
    };

    for (int z = 0; z < N; ++z) {
        for (int x = 0; x < N; ++x) {
            float minX = mapToGrid(x);
            float minZ = mapToGrid(z);
            float maxX = mapToGrid(x + 1);
            float maxZ = mapToGrid(z + 1);

            GfVec3f p0(minX, _params.waterHeight, minZ);
            GfVec3f p1(maxX, _params.waterHeight, minZ);
            GfVec3f p2(maxX, _params.waterHeight, maxZ);
            GfVec3f p3(minX, _params.waterHeight, maxZ);

            int baseIdx = outBasePoints.size();
            outBasePoints.push_back(p0);
            outBasePoints.push_back(p1);
            outBasePoints.push_back(p2);
            outBasePoints.push_back(p3);

            float fx0 = (float)x / (float)N;
            float fz0 = (float)z / (float)N;
            float fx1 = (float)(x+1) / (float)N;
            float fz1 = (float)(z+1) / (float)N;

            outUvs.push_back(GfVec2f(fx0, fz0));
            outUvs.push_back(GfVec2f(fx1, fz0));
            outUvs.push_back(GfVec2f(fx1, fz1));
            outUvs.push_back(GfVec2f(fx0, fz1));

            if (_params.disableShader) {
                GfVec3f rc = randColor();
                outColors.push_back(rc);
                outColors.push_back(rc);
                outColors.push_back(rc);
                outColors.push_back(rc);
            } else {
                outColors.push_back(GfVec3f(0.0f));
                outColors.push_back(GfVec3f(0.0f));
                outColors.push_back(GfVec3f(0.0f));
                outColors.push_back(GfVec3f(0.0f));
            }

            outTypes.push_back(0);
            outTypes.push_back(0);
            outTypes.push_back(0);
            outTypes.push_back(0);

            outIndices.push_back(GfVec3i(baseIdx, baseIdx + 2, baseIdx + 1));
            outIndices.push_back(GfVec3i(baseIdx, baseIdx + 3, baseIdx + 2));
        }
    }

    // Add skirts and bottom
    float bottomY = _params.waterHeight - _params.extrusion;
    GfVec3f rc = _params.disableShader ? randColor() : GfVec3f(0.0f);

    auto addQuad = [&](const GfVec3f& p0, const GfVec3f& p1, const GfVec3f& p2, const GfVec3f& p3, int typeTop, int typeBottom) {
        int baseIdx = outBasePoints.size();
        outBasePoints.push_back(p0); outBasePoints.push_back(p1);
        outBasePoints.push_back(p2); outBasePoints.push_back(p3);
        
        outUvs.push_back(GfVec2f(0.0f, 0.0f)); outUvs.push_back(GfVec2f(1.0f, 0.0f));
        outUvs.push_back(GfVec2f(1.0f, 1.0f)); outUvs.push_back(GfVec2f(0.0f, 1.0f));
        
        outColors.push_back(rc); outColors.push_back(rc);
        outColors.push_back(rc); outColors.push_back(rc);
        
        outTypes.push_back(typeTop); outTypes.push_back(typeTop);
        outTypes.push_back(typeBottom); outTypes.push_back(typeBottom);

        outIndices.push_back(GfVec3i(baseIdx, baseIdx + 2, baseIdx + 1));
        outIndices.push_back(GfVec3i(baseIdx, baseIdx + 3, baseIdx + 2));
    };

    // +Z Wall (z = halfSize)
    for (int x = 0; x < N; ++x) {
        float minX = mapToGrid(x);
        float maxX = mapToGrid(x + 1);
        float zPos = halfSize;
        addQuad(GfVec3f(minX, _params.waterHeight, zPos), GfVec3f(maxX, _params.waterHeight, zPos),
                GfVec3f(maxX, bottomY, zPos), GfVec3f(minX, bottomY, zPos), 5, 6);
    }
    // -Z Wall (z = -halfSize)
    for (int x = 0; x < N; ++x) {
        float minX = mapToGrid(x);
        float maxX = mapToGrid(x + 1);
        float zPos = -halfSize;
        addQuad(GfVec3f(maxX, _params.waterHeight, zPos), GfVec3f(minX, _params.waterHeight, zPos),
                GfVec3f(minX, bottomY, zPos), GfVec3f(maxX, bottomY, zPos), 7, 8);
    }
    // +X Wall (x = halfSize)
    for (int z = 0; z < N; ++z) {
        float minZ = mapToGrid(z);
        float maxZ = mapToGrid(z + 1);
        float xPos = halfSize;
        addQuad(GfVec3f(xPos, _params.waterHeight, maxZ), GfVec3f(xPos, _params.waterHeight, minZ),
                GfVec3f(xPos, bottomY, minZ), GfVec3f(xPos, bottomY, maxZ), 1, 2);
    }
    // -X Wall (x = -halfSize)
    for (int z = 0; z < N; ++z) {
        float minZ = mapToGrid(z);
        float maxZ = mapToGrid(z + 1);
        float xPos = -halfSize;
        addQuad(GfVec3f(xPos, _params.waterHeight, minZ), GfVec3f(xPos, _params.waterHeight, maxZ),
                GfVec3f(xPos, bottomY, maxZ), GfVec3f(xPos, bottomY, minZ), 3, 4);
    }

    // Bottom Face
    addQuad(GfVec3f(-halfSize, bottomY, halfSize), GfVec3f(halfSize, bottomY, halfSize),
            GfVec3f(halfSize, bottomY, -halfSize), GfVec3f(-halfSize, bottomY, -halfSize), 9, 9);
}

void HdGeminiOcean::DisplaceGrid(
    const std::vector<GfVec3f>& basePoints,
    const std::vector<int>& types,
    const GfVec3f& cameraPos,
    std::vector<GfVec3f>& outDisplaced,
    std::vector<GfVec3f>& outNormals,
    std::vector<GfVec3f>& outColors) const
{
    outDisplaced.resize(basePoints.size());
    outNormals.resize(basePoints.size());
    outColors.resize(basePoints.size());
    GfVec3f camOffset(cameraPos[0], 0.0f, cameraPos[2]);
    for (size_t i = 0; i < basePoints.size(); ++i) {
        int type = types[i];
        GfVec3f offsetPos = basePoints[i] + camOffset;
        if (type == 0) {
            // Surface
            outDisplaced[i] = GetDisplacedPosition(offsetPos);
            outNormals[i] = GetNormal(offsetPos);
            outColors[i] = GfVec3f(GetFoam(offsetPos));
        } else if (type == 9) {
            // Bottom face
            outDisplaced[i] = offsetPos;
            outNormals[i] = GfVec3f(0, -1, 0);
            outColors[i] = GfVec3f(0.0f);
        } else if (type % 2 == 1) {
            // Wall Top (1, 3, 5, 7)
            outDisplaced[i] = GetDisplacedPosition(offsetPos);
            if (type == 1) outNormals[i] = GfVec3f(1, 0, 0);
            else if (type == 3) outNormals[i] = GfVec3f(-1, 0, 0);
            else if (type == 5) outNormals[i] = GfVec3f(0, 0, 1);
            else if (type == 7) outNormals[i] = GfVec3f(0, 0, -1);
            outColors[i] = GfVec3f(0.0f);
        } else {
            // Wall Bottom (2, 4, 6, 8)
            outDisplaced[i] = offsetPos;
            if (type == 2) outNormals[i] = GfVec3f(1, 0, 0);
            else if (type == 4) outNormals[i] = GfVec3f(-1, 0, 0);
            else if (type == 6) outNormals[i] = GfVec3f(0, 0, 1);
            else if (type == 8) outNormals[i] = GfVec3f(0, 0, -1);
            outColors[i] = GfVec3f(0.0f);
        }
    }
}
