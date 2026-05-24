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
    int resolution = 256;
    float size = 10.0f;
    float amplitude = 0.0f;
    float choppiness = 1.2f;
    float windSpeed = 0.0f;
    float waterHeight = 0.0f;
    GfVec2f windDirection = GfVec2f(1.0f, 1.0f);
    bool disableShader = false;
    bool repeat = true;
    
    bool operator==(const HdGeminiOceanParams& o) const {
        return resolution == o.resolution && size == o.size && amplitude == o.amplitude &&
               choppiness == o.choppiness && windSpeed == o.windSpeed && 
               waterHeight == o.waterHeight && windDirection == o.windDirection &&
               disableShader == o.disableShader && repeat == o.repeat;
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

    const HdGeminiOceanParams& GetParams() const { return _params; }
    bool IsInitialized() const { return _initialized; }

    void GenerateGridTopology(
        const GfMatrix4f& viewProj, 
        const GfVec3f& cameraPos,
        const GfRange3f& bounds,
        std::vector<GfVec3f>& outBasePoints,
        std::vector<GfVec3i>& outIndices,
        std::vector<GfVec2f>& outUvs) const;

    void DisplaceGrid(
        const std::vector<GfVec3f>& basePoints,
        std::vector<GfVec3f>& outPoints,
        std::vector<GfVec3f>& outNormals) const;

private:
    float Phillips(float kx, float kz) const;
    void ComputeH0();
    void PerformFFT2D(std::vector<std::complex<float>>& data) const;

    HdGeminiOceanParams _params;
    bool _initialized = false;
    float _lastTime = -1.0f;

    std::vector<std::complex<float>> _h0;
    std::vector<std::complex<float>> _h0_minus;
    
    std::vector<std::complex<float>> _h_kt_dz;
    std::vector<std::complex<float>> _h_kt_dx;
    std::vector<std::complex<float>> _h_kt_dy;
    
    // Spatial domain results
    std::vector<GfVec3f> _displacementMap;
    std::vector<GfVec3f> _normalMap;

    std::vector<int> _bitReversedIndices;
};

#endif // HD_GEMINI_OCEAN_H
