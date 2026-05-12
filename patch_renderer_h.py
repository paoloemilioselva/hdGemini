import sys
import re

with open("renderer.h", "r") as f:
    content = f.read()

structs_to_add = """
#ifdef HDGEMINI_HAS_SYCL
    struct SYCLLightData {
        int type; // 1=distant, 2=dome, 3=rect, 4=local
        float transform[16];
        float color[3];
        float intensity;
        float width;
        float height;
        float shapingConeAngle;
        float shapingConeSoftness;
    };

    struct HitState {
        bool hit;
        float t;
        float hitPos[3];
        float shadingNormal[3];
        float baseColor[3];
        float metallic;
        float roughness;
        float specularColor[3];
        float specular;
        float opacity;
        float ior;
        float transmission;
        float transmissionColor[3];
        float emission[3];
        
        float coat;
        float coatColor[3];
        float coatRoughness;
        float coatIor;
        float transmissionDepth;
        float transmissionScatter[3];
        float sheen;
        float sheenColor[3];
        float sheenRoughness;
        float subsurface;
        float subsurfaceColor[3];
        float subsurfaceRadius[3];
        float subsurfaceScale;
        float subsurfaceAnisotropy;
        bool thinWalled;
        float diffuseRoughness;
    };

    struct ShadowRay {
        float origin[3];
        float dir[3];
        float tMax;
        float payload[4];
        int pixelIdx;
        bool active;
    };
#endif
"""

# Find TextureData definition and insert new structs after it
insert_idx = content.find("    struct TextureData {")
if insert_idx != -1:
    end_td = content.find("    };", insert_idx) + 6
    content = content[:end_td] + "\n" + structs_to_add + content[end_td:]

# Update RayState
ray_state_find = """    struct RayState {
        float origin[3];
        float dir[3];
        float exposureMultiplier;
        SampledWavelengths lambda;
        uint32_t rng;
        int x;
        int y;
        bool active;
    };"""
ray_state_replace = """    struct RayState {
        float origin[3];
        float dir[3];
        float exposureMultiplier;
        SampledWavelengths lambda;
        uint32_t rng;
        int x;
        int y;
        bool active;
#ifdef HDGEMINI_HAS_SYCL
        float throughput[4];
        float totalRadiance[4];
        int bounce;
        int reflectionBounces;
        int refractionBounces;
        bool isInside;
        float albedo[3];
        float normal[3];
#endif
    };"""
content = content.replace(ray_state_find, ray_state_replace)

# Add USM pointers to private members
usm_find = """#ifdef HDGEMINI_HAS_SYCL
    sycl::queue* _syclQueue = nullptr;
    RayState* _rayBuffer = nullptr;
    size_t _rayBufferSize = 0;
#endif"""
usm_replace = """#ifdef HDGEMINI_HAS_SYCL
    sycl::queue* _syclQueue = nullptr;
    RayState* _rayBuffer = nullptr;
    size_t _rayBufferSize = 0;
    
    HitState* _hitBuffer = nullptr;
    size_t _hitBufferSize = 0;
    
    ShadowRay* _shadowRayBuffer = nullptr;
    size_t _shadowRayBufferSize = 0;
    
    SYCLLightData* _lightBuffer = nullptr;
    size_t _lightBufferSize = 0;
    int _numActiveLights = 0;
    bool _hasDomeLight = false;
    
    float* _usmEnvMapPixels = nullptr;
    float* _usmEnvMapRowCdf = nullptr;
    float* _usmEnvMapColCdf = nullptr;
    size_t _usmEnvMapPixelsSize = 0;
    size_t _usmEnvMapRowCdfSize = 0;
    size_t _usmEnvMapColCdfSize = 0;
#endif"""
content = content.replace(usm_find, usm_replace)

with open("renderer.h", "w") as f:
    f.write(content)
print("renderer.h updated")
