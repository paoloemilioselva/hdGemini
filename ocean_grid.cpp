#include "ocean.h"
#include <vector>

PXR_NAMESPACE_USING_DIRECTIVE

void HdGeminiOcean::GenerateGridTopology(
    std::vector<GfVec3f>& outBasePoints,
    std::vector<GfVec3i>& outIndices,
    std::vector<GfVec2f>& outUvs,
    std::vector<GfVec3f>& outColors) const
{
    int N = _params.gridSize;
    float size = _params.size[0] > 1e-5f ? _params.size[0] : 100.0f;
    float halfSize = size * 0.5f;
    float step = size / (float)N;
    
    outBasePoints.reserve(N * N * 4);
    outUvs.reserve(N * N * 4);
    outColors.reserve(N * N * 4);
    outIndices.reserve(N * N * 2);

    auto randColor = []() {
        return GfVec3f(
            (float)(rand() % 256) / 255.0f,
            (float)(rand() % 256) / 255.0f,
            (float)(rand() % 256) / 255.0f
        );
    };

    for (int z = 0; z < N; ++z) {
        for (int x = 0; x < N; ++x) {
            float minX = -halfSize + x * step;
            float minZ = -halfSize + z * step;
            float maxX = minX + step;
            float maxZ = minZ + step;

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
                outColors.push_back(GfVec3f(GetFoam(p0)));
                outColors.push_back(GfVec3f(GetFoam(p1)));
                outColors.push_back(GfVec3f(GetFoam(p2)));
                outColors.push_back(GfVec3f(GetFoam(p3)));
            }

            outIndices.push_back(GfVec3i(baseIdx, baseIdx + 2, baseIdx + 1));
            outIndices.push_back(GfVec3i(baseIdx, baseIdx + 3, baseIdx + 2));
        }
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
