#ifndef HD_GEMINI_OCEAN_H
#define HD_GEMINI_OCEAN_H

#include "pxr/pxr.h"
#include "pxr/base/gf/vec2f.h"
#include "pxr/base/gf/vec3f.h"
#include "pxr/base/gf/vec3i.h"
#include "pxr/base/gf/matrix4f.h"
#include "pxr/base/gf/range3f.h"
#include <vector>
#include <complex>

PXR_NAMESPACE_USING_DIRECTIVE

struct HdGeminiOceanParams {
    int gridSize = 128;
    float dicingScale = 1.0f;
    float size[3] = { 100.0f, 10.0f, 1.0f };
    float amplitude[3] = { 0.0f, 0.0f, 0.0f };
    float choppiness[3] = { 1.2f, 1.2f, 1.2f };
    float windSpeed = 0.0f;
    float waterHeight = 0.0f;
    GfVec2f windDirection = GfVec2f(1.0f, 1.0f);
    float foamVisibility = 1.0f;
    bool disableShader = false;
    bool repeat = true;
    GfVec3f scatteringColor = GfVec3f(0.02f, 0.15f, 0.25f);
    float scatteringDepth = 10.0f;
    
    bool operator==(const HdGeminiOceanParams& o) const {
        return gridSize == o.gridSize && dicingScale == o.dicingScale &&
               size[0] == o.size[0] && size[1] == o.size[1] && size[2] == o.size[2] &&
               amplitude[0] == o.amplitude[0] && amplitude[1] == o.amplitude[1] && amplitude[2] == o.amplitude[2] &&
               choppiness[0] == o.choppiness[0] && choppiness[1] == o.choppiness[1] && choppiness[2] == o.choppiness[2] &&
               windSpeed == o.windSpeed && waterHeight == o.waterHeight && windDirection == o.windDirection &&
               foamVisibility == o.foamVisibility && disableShader == o.disableShader && repeat == o.repeat &&
               scatteringColor == o.scatteringColor && scatteringDepth == o.scatteringDepth;
    }
    bool operator!=(const HdGeminiOceanParams& o) const { return !(*this == o); }
};

class HdGeminiOcean {
public:
    HdGeminiOcean();
    ~HdGeminiOcean();

    void Init(const HdGeminiOceanParams& params);
    void Update(float time);

    // Returns the displaced position for an original world coordinate (x, y, z)
    // Assumes water surface is essentially on the XZ plane.
    GfVec3f GetDisplacedPosition(const GfVec3f& basePos) const;
    GfVec3f GetNormal(const GfVec3f& basePos) const;
    float GetFoam(const GfVec3f& basePos) const;

    const HdGeminiOceanParams& GetParams() const { return _params; }
    bool IsInitialized() const { return _initialized; }

    void GenerateGridTopology(
        const GfMatrix4f& viewProj, 
        const GfVec3f& cameraPos,
        const GfRange3f& bounds,
        int screenWidth,
        int screenHeight,
        std::vector<GfVec3f>& outBasePoints,
        std::vector<GfVec3i>& outIndices,
        std::vector<GfVec2f>& outUvs,
        std::vector<GfVec3f>& outColors) const;

    void DisplaceGrid(
        const std::vector<GfVec3f>& basePoints,
        std::vector<GfVec3f>& outDisplaced,
        std::vector<GfVec3f>& outNormals) const;

private:
    float Phillips(float kx, float kz, float amplitude) const;
    void ComputeH0();
    void PerformFFT2D(std::vector<std::complex<float>>& data) const;

    HdGeminiOceanParams _params;
    bool _initialized = false;
    float _lastTime = -1.0f;

    std::vector<std::complex<float>> _h0[3];
    std::vector<std::complex<float>> _h0_minus[3];
    
    std::vector<std::complex<float>> _h_kt_dz;
    std::vector<std::complex<float>> _h_kt_dx;
    std::vector<std::complex<float>> _h_kt_dy;
    
    // Spatial domain results
    std::vector<GfVec3f> _displacementMap[3];

    std::vector<int> _bitReversedIndices;
};

#endif // HD_GEMINI_OCEAN_H
