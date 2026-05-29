#ifndef HD_GEMINI_RENDERER_H
#define HD_GEMINI_RENDERER_H

#include "pxr/pxr.h"
#include "pxr/imaging/hd/renderThread.h"
#include "pxr/imaging/hd/aov.h"
#include "pxr/base/gf/matrix4d.h"
#include "pxr/base/gf/matrix4f.h"
#include "pxr/base/gf/rect2i.h"
#include "pxr/base/gf/vec3f.h"
#include "pxr/base/gf/range3f.h"
#include "pxr/base/gf/vec2f.h"
#include "pxr/usd/sdf/assetPath.h"
#include "mesh.h"
#include "curves.h"
#include "ocean.h"
#include "spectrum.h"
#include <vector>
#include <atomic>
#include <map>
#include <memory>
#include <mutex>

#ifdef HDGEMINI_HAS_SYCL
#include <sycl/sycl.hpp>
#endif

PXR_NAMESPACE_USING_DIRECTIVE

class HdGeminiRenderDelegate;
class HdGeminiLight;
class HdGeminiMaterial;

class HdGeminiRenderer final
{
public:
    HdGeminiRenderer();
    ~HdGeminiRenderer();

    void SetCamera(const GfMatrix4d& viewMatrix, const GfMatrix4d& projMatrix);
    void SetHydraCameraParams(float fStop, float focalLength, float focusDistance, float iso, float shutterSpeed);
    void SetDataWindow(const GfRect2i& dataWindow);
    void SetAovBindings(const HdRenderPassAovBindingVector& aovBindings);
    const HdRenderPassAovBindingVector& GetAovBindings() const { return _aovBindings; }

    void Render(HdRenderThread *renderThread, HdGeminiRenderDelegate* delegate);
    void Clear();
    void MarkAovBuffersUnconverged();
    void ReapplyPostProcess();
    bool IsConverged() const { return _isConverged; }

    void SetEnableSubsurface(bool enable) { _enableSubsurface = enable; }
    bool GetEnableSubsurface() const { return _enableSubsurface; }

    void SetEnableDenoiser(bool enable) { _enableDenoiser = enable; }
    void SetEnableFireflyFilter(bool enable) { _enableFireflyFilter = enable; }
    void SetEnableChromaticityBlur(bool enable) { _enableChromaticityBlur = enable; }
    void SetTargetSampleCount(int count) { _targetSampleCount = count; }
    void SetMaxReflectionBounces(int bounces) { _maxReflectionBounces = bounces; }
    void SetMaxRefractionBounces(int bounces) { _maxRefractionBounces = bounces; }
    void SetResolutionLevel(int level) { 
        _initialResolutionLevel = level; 
        _resolutionLevel = level;
        _isConverged = false;
    }
    void SetAntiAliasingFilter(int filter) { _antiAliasingFilter = filter; }
    void SetEnableSycl(bool enable) { _enableSycl = enable; }
    void SetEnableOnScreenStats(bool enable) { _enableOnScreenStats = enable; }

    void SetEnableDoF(bool enable) { _enableDoF = enable; }    void SetFocalLength(float fl) { _focalLength = fl; }
    void SetFStop(float fStop) { _fStop = fStop; }
    void SetFocusDistance(float fd) { _focusDistance = fd; }
    void SetBokehBlades(int blades) { _bokehBlades = blades; }
    void SetEnablePhysicalCamera(bool enable) { _enablePhysicalCamera = enable; }
    void SetISO(float iso) { _iso = iso; }
    void SetShutterSpeed(float shutterSpeed) { _shutterSpeed = shutterSpeed; }
    void SetEnableLensFlare(bool enable) { _enableLensFlare = enable; }
    void SetRenderIblBackground(bool render) { _renderIblBackground = render; }
    void SetLensDistortion(float distortion) { _lensDistortion = distortion; }
    void SetChromaticAberration(float ca) { _chromaticAberration = ca; }
    void SetPhysicalSkyEnable(bool enable) { _enablePhysicalSky = enable; }
    void SetPhysicalSkyAzimuth(float a) { _physicalSkyAzimuth = a; }
    void SetPhysicalSkyAltitude(float a) { _physicalSkyAltitude = a; }
    void SetPhysicalSkySunExposure(float exp) { _physicalSkySunExposure = exp; }
    void SetPhysicalSkySkyExposure(float exp) { _physicalSkySkyExposure = exp; }
    void SetVolumeStepSize(float step) { _volumeStepSize = step; }
    void SetVolumeDensityScale(float scale) { _volumeDensityScale = scale; }
    
    void SetEnableAdaptiveSampling(bool enable) { _enableAdaptiveSampling = enable; }
    void SetAdaptiveVarianceThreshold(float threshold) { _adaptiveVarianceThreshold = threshold; }
    void SetAdaptiveMinSamples(int samples) { _adaptiveMinSamples = samples; }

    void SetOceanEnable(bool enable) { _oceanEnable = enable; }
    void SetOceanDicingScale(float scale) { _oceanParams.dicingScale = scale; }
    void SetOceanContinuousDicing(bool enable) { _oceanParams.continuousDicing = enable; }
    void SetOceanWaterHeight(float height) { _oceanWaterHeight = height; }
    void SetOceanGridSize(int size) { _oceanParams.gridSize = size; }
    void SetOceanSize(float size) { _oceanParams.size = size; }
    void SetOceanAmplitude(int cascade, float amp) { if(cascade >= 0 && cascade < 3) _oceanParams.amplitude[cascade] = amp; }
    void SetOceanChoppiness(int cascade, float chop) { if(cascade >= 0 && cascade < 3) _oceanParams.choppiness[cascade] = chop; }
    void SetOceanStrength(int cascade, float str) { if(cascade >= 0 && cascade < 3) _oceanParams.strength[cascade] = str; }
    void SetOceanWindSpeed(int cascade, float speed) { if(cascade >= 0 && cascade < 3) _oceanParams.windSpeed[cascade] = speed; }
    void SetOceanWindDirection(int cascade, float x, float y) { if(cascade >= 0 && cascade < 3) _oceanParams.windDirection[cascade] = GfVec2f(x, y); }
    void SetOceanMinK(int cascade, float minK) { if(cascade >= 0 && cascade < 3) _oceanParams.minK[cascade] = minK; }
    void SetOceanMaxK(int cascade, float maxK) { if(cascade >= 0 && cascade < 3) _oceanParams.maxK[cascade] = maxK; }
    void SetOceanFoamVisibility(float vis) { _oceanParams.foamVisibility = vis; }
    void SetOceanDisableShader(bool disable) { _oceanParams.disableShader = disable; }
    void SetOceanTime(float time) { _time = time; }
    void SetOceanRepeat(bool repeat) { _oceanParams.repeat = repeat; }
    void SetOceanScatteringColor(const GfVec3f& color) { _oceanParams.scatteringColor = color; }
    void SetOceanScatteringDepth(float depth) { _oceanParams.scatteringDepth = depth; }
    
    void SetMeniscusSize(float size) { _meniscusSize = size; }
    void SetMeniscusBend(float bend) { _meniscusBend = bend; }
    void SetMeniscusTint(const GfVec3f& v) { _meniscusTint = v; }
    void SetMetersPerUnit(float v) { _metersPerUnit = v; }

    const HdGeminiOceanParams& GetOceanParams() const { return _oceanParams; }

private:
    bool _isConverged = false;
    bool _enableSubsurface = true;
    bool _enableDenoiser = true;
    bool _enableFireflyFilter = true;
    bool _enableChromaticityBlur = true;
    int _targetSampleCount = 32;
    int _maxReflectionBounces = 8;
    int _maxRefractionBounces = 8;
    int _initialResolutionLevel = 2;
    int _antiAliasingFilter = 1;
    bool _enableSycl = true;
    bool _enableOnScreenStats = false;
    
    bool _enableDoF = false;
    float _focalLength = 50.0f;
    float _fStop = 5.6f;
    float _focusDistance = 10.0f;
    int _bokehBlades = 0;
    bool _enablePhysicalCamera = false;
    float _iso = 100.0f;
    float _shutterSpeed = 0.02f;
    
    // Hydra camera state (when override is disabled)
    float _hydraFocalLength = 50.0f;
    float _hydraFStop = 5.6f;
    float _hydraFocusDistance = 10.0f;
    float _hydraIso = 100.0f;
    float _hydraShutterSpeed = 0.02f;
    bool _enableLensFlare = false;
    bool _renderIblBackground = true;
    float _lensDistortion = 0.0f;
    float _chromaticAberration = 0.0f;
    bool _enablePhysicalSky = false;
    float _physicalSkyAzimuth = 0.0f;
    float _physicalSkyAltitude = 90.0f;
    float _physicalSkySunExposure = 0.0f;
    float _physicalSkySkyExposure = 0.0f;
    float _volumeStepSize = 0.1f;
    float _volumeDensityScale = 1.0f;
    
    bool _enableAdaptiveSampling = true;
    float _adaptiveVarianceThreshold = 0.01f;
    int _adaptiveMinSamples = 16;
    
    bool _oceanEnable = false;
    float _oceanWaterHeight = 0.0f;
    float _meniscusSize = 0.015f;
    float _meniscusBend = 0.2f;
    GfVec3f _meniscusTint = GfVec3f(0.02f, 0.05f, 0.04f);
    float _metersPerUnit = 0.01f;
    HdGeminiOceanParams _oceanParams;
    std::unique_ptr<HdGeminiOcean> _globalOcean;
    std::unique_ptr<BVH> _globalOceanBvh;
    std::vector<GfVec3f> _globalOceanBasePoints;
    std::vector<GfVec3i> _globalOceanIndices;
    std::vector<GfVec2f> _globalOceanUvs;
    std::vector<GfVec3f> _globalOceanColors;
    float _time = 0.0f;

    struct SceneInstance {
        enum class Type { Mesh, Volume, BasisCurves, Ocean };
        Type type;
        class HdGeminiMesh* mesh = nullptr;
        const HdGeminiMesh::Subset* subset = nullptr;
        class HdGeminiVolume* volume = nullptr;
        class HdGeminiBasisCurves* curves = nullptr;
        const HdGeminiBasisCurves::Subset* curveSubset = nullptr;
        
        HdGeminiMaterial* material = nullptr;
        const void* densityGrid = nullptr; // nanovdb::FloatGrid* cast
        GfMatrix4f transform;
        GfMatrix4f invTransform;
        GfRange3f bounds;
        GfVec3f centroid;
        
        // For Ocean
        class BVH* dynamicBvh = nullptr;
    };

    struct TLASNode {
        GfRange3f bounds;
        int leftChild;
        int instanceCount;
    };

    struct HitRecord {
        float t = 1e30f;
        GfVec3f normal;
        GfVec3f smoothNormal;
        GfVec3f dpdu = GfVec3f(1, 0, 0);
        GfVec3f dpdv = GfVec3f(0, 1, 0);
        GfVec2f uv = GfVec2f(0.0f);
        GfVec3f baseColor = GfVec3f(1.0f);
        float metallic = 0.0f;
        float roughness = 0.5f;
        GfVec3f specularColor = GfVec3f(1.0f);
        float specular = 1.0f;
        float opacity = 1.0f;
        float ior = 1.5f;
        float transmission = 0.0f;
        GfVec3f transmissionColor = GfVec3f(1.0f);
        GfVec3f emission = GfVec3f(0.0f);
        SdfAssetPath diffuseTexture;
        SdfAssetPath normalTexture;
        SdfAssetPath metallicTexture;
        SdfAssetPath roughnessTexture;
        SdfAssetPath opacityTexture;
        SdfAssetPath transmissionTexture;
        int metallicTextureChannel = 0;
        int roughnessTextureChannel = 0;
        int opacityTextureChannel = 0;
        int transmissionTextureChannel = 0;
        bool hit = false;
        
        float coat = 0.0f;
        GfVec3f coatColor = GfVec3f(1.0f);
        float coatRoughness = 0.1f;
        float coatIor = 1.5f;
        float transmissionDepth = 0.0f;
        GfVec3f transmissionScatter = GfVec3f(0.0f);
        float sheen = 0.0f;
        GfVec3f sheenColor = GfVec3f(1.0f);
        float sheenRoughness = 0.3f;
        float subsurface = 0.0f;
        GfVec3f subsurfaceColor = GfVec3f(1.0f);
        GfVec3f subsurfaceRadius = GfVec3f(1.0f);
        float subsurfaceScale = 1.0f;
        float subsurfaceAnisotropy = 0.0f;
        bool thinWalled = false;
        float diffuseRoughness = 0.0f;
        
        // Volume properties
        bool isVolumeHit = false;
        const void* densityGrid = nullptr;
        bool isOcean = false;
    };

    struct TextureData {
        std::vector<float> pixels;
        int width = 0;
        int height = 0;
    };

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


    struct RayState {
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
    };

    struct LightSample {
        int lightIdx = -1;
        float u = 0.0f;
        float v = 0.0f;
    };

    struct Reservoir {
        LightSample sample;
        float w_sum = 0.0f;
        float W = 0.0f;
        int M = 0;
        
        void Update(const LightSample& s, float weight, float randVal) {
            w_sum += weight;
            M += 1;
            if (randVal < weight / std::max(w_sum, 1e-6f)) {
                sample = s;
            }
        }
    };

    struct Photon {
        GfVec3f pos;
        GfVec3f wi;
        GfVec3f powerRGB;
    };

    struct PhotonMap {
        std::vector<Photon> photons;
        std::unordered_map<uint64_t, std::vector<int>> spatialHash;
        float searchRadius = 0.1f;
        
        void Clear() {
            photons.clear();
            spatialHash.clear();
        }
        
        uint64_t Hash(const GfVec3f& p) const {
            int x = (int)std::floor(p[0] / searchRadius);
            int y = (int)std::floor(p[1] / searchRadius);
            int z = (int)std::floor(p[2] / searchRadius);
            return (uint64_t)((x * 73856093) ^ (y * 19349663) ^ (z * 83492791));
        }
        
        void AddPhoton(const Photon& p) {
            int idx = (int)photons.size();
            photons.push_back(p);
            spatialHash[Hash(p.pos)].push_back(idx);
        }
    };

    struct GuidingVoxel {
        float bins[16] = {0};
        float totalRadiance = 0;
        
        void Deposit(const GfVec3f& dir, float radiance) {
            if (radiance <= 0) return;
            float sum = std::abs(dir[0]) + std::abs(dir[1]) + std::abs(dir[2]);
            if (sum < 1e-6f) return;
            float u = dir[0] / sum;
            float v = dir[1] / sum;
            if (dir[2] < 0) {
                float tu = u;
                u = (1.0f - std::abs(v)) * (u >= 0.0f ? 1.0f : -1.0f);
                v = (1.0f - std::abs(tu)) * (v >= 0.0f ? 1.0f : -1.0f);
            }
            u = u * 0.5f + 0.5f;
            v = v * 0.5f + 0.5f;
            int bx = std::clamp((int)(u * 4.0f), 0, 3);
            int by = std::clamp((int)(v * 4.0f), 0, 3);
            int binIdx = by * 4 + bx;
            
            #pragma omp atomic
            bins[binIdx] += radiance;
            #pragma omp atomic
            totalRadiance += radiance;
        }
        
        float Pdf(const GfVec3f& dir) const {
            if (totalRadiance <= 0) return 0.0f;
            float sum = std::abs(dir[0]) + std::abs(dir[1]) + std::abs(dir[2]);
            if (sum < 1e-6f) return 0.0f;
            float u = dir[0] / sum;
            float v = dir[1] / sum;
            if (dir[2] < 0) {
                float tu = u;
                u = (1.0f - std::abs(v)) * (u >= 0.0f ? 1.0f : -1.0f);
                v = (1.0f - std::abs(tu)) * (v >= 0.0f ? 1.0f : -1.0f);
            }
            u = u * 0.5f + 0.5f;
            v = v * 0.5f + 0.5f;
            int bx = std::clamp((int)(u * 4.0f), 0, 3);
            int by = std::clamp((int)(v * 4.0f), 0, 3);
            int binIdx = by * 4 + bx;
            // The bin area on the octahedral map is 1/16
            // But we need solid angle PDF. The PDF on the sphere is p_sphere = p_oct * J.
            // Simplified: we'll return bin_radiance / totalRadiance. We'll handle the Jacobian in the MIS weight or assume uniform within bin.
            return (bins[binIdx] / totalRadiance) * (16.0f / (4.0f * (float)M_PI)); 
        }
        
        GfVec3f Sample(float u1, float u2, float u3) const {
            if (totalRadiance <= 0) return GfVec3f(0.0f, 0.0f, 1.0f);
            
            // Pick bin
            float target = u3 * totalRadiance;
            float accum = 0.0f;
            int chosenBin = 15;
            for (int i = 0; i < 16; ++i) {
                accum += bins[i];
                if (accum >= target) {
                    chosenBin = i;
                    break;
                }
            }
            
            int bx = chosenBin % 4;
            int by = chosenBin / 4;
            float u = (bx + u1) / 4.0f;
            float v = (by + u2) / 4.0f;
            
            u = u * 2.0f - 1.0f;
            v = v * 2.0f - 1.0f;
            GfVec3f dir(u, v, 1.0f - std::abs(u) - std::abs(v));
            if (dir[2] < 0) {
                float tu = dir[0];
                dir[0] = (1.0f - std::abs(dir[1])) * (tu >= 0.0f ? 1.0f : -1.0f);
                dir[1] = (1.0f - std::abs(tu)) * (dir[1] >= 0.0f ? 1.0f : -1.0f);
            }
            return dir.GetNormalized();
        }
    };

    struct GuidingGrid {
        std::vector<GuidingVoxel> voxels;
        float voxelSize = 10.0f; // Scale based on scene
        
        GuidingGrid() { voxels.resize(1000003); }
        
        uint32_t Hash(const GfVec3f& p) const {
            int x = (int)std::floor(p[0] / voxelSize);
            int y = (int)std::floor(p[1] / voxelSize);
            int z = (int)std::floor(p[2] / voxelSize);
            return (uint32_t)((x * 73856093) ^ (y * 19349663) ^ (z * 83492791)) % 1000003;
        }
        
        void Clear() {
            std::fill(voxels.begin(), voxels.end(), GuidingVoxel());
        }
    };

    struct RenderBucket {
        uint32_t startX, startY, endX, endY;
        uint32_t activePixels;
        float maxVariance;
    };

    static GfVec4f _GetClearColor(VtValue const& clearValue);
    void _RenderTiles(HdRenderThread *renderThread, HdGeminiRenderDelegate* delegate);
#ifdef HDGEMINI_HAS_SYCL
    void _RenderTilesSYCL(HdRenderThread *renderThread, HdGeminiRenderDelegate* delegate);
#endif
    void _PrepareScene(HdRenderThread *renderThread, HdGeminiRenderDelegate* delegate);
    void _BuildTLAS(class HdRenderThread *renderThread);
    void _SubdivideTLAS(int nodeIdx, int start, int end, class HdRenderThread *renderThread);
    bool _IntersectTLAS(const GfVec3f& rayOrigin, const GfVec3f& rayDir, HitRecord& hit, class HdRenderThread* renderThread, uint32_t sampleIdx, uint32_t& qmcDim, uint32_t& rng) const;
    SampledSpectrum _TraceShadowRay(const GfVec3f& rayOrigin, const GfVec3f& rayDir, float maxDist, bool currentlyInside, float currentTransmissionDepth, const GfVec3f& currentTransmissionColor, const GfVec3f& currentTransmissionScatter, class HdRenderThread* renderThread, uint32_t sampleIdx, uint32_t& qmcDim, uint32_t& rng, const SampledWavelengths& lambda) const;
    SampledSpectrum _TraceRay(const GfVec3f& rayOrigin, const GfVec3f& rayDir, int depth, bool isInteractive, class HdRenderThread* renderThread, uint32_t sampleIdx, uint32_t& qmcDim, uint32_t& rng, const SampledWavelengths& lambda, GfVec3f* outAlbedo = nullptr, GfVec3f* outNormal = nullptr, float* outDepth = nullptr, float exposureMultiplier = 1.0f, struct Reservoir* temporalReservoir = nullptr) const;
    void _TracePhoton(class HdRenderThread* renderThread, uint32_t sampleIdx, uint32_t& rng, const SampledWavelengths& lambda);
    GfVec3f _SampleEnvironment(const GfVec3f& rayDir) const;
    GfVec3f _SamplePhysicalSky(const GfVec3f& rayDir, const GfVec3f& sunDir, bool includeSun) const;
    GfVec3f _GetSunTransmittance(const GfVec3f& sunDir) const;
    
    bool _IsPointInsideOcean(const GfVec3f& pos) const;

    GfVec4f _SampleTexture(const SdfAssetPath& path, const GfVec2f& uv, bool forceLinear = false) const;
    GfVec4f _SampleTextureData(const TextureData& data, const GfVec2f& uv) const;

    void _Denoise();
    void _ApplyPostProcess();
    void _DrawStats();
    void _DrawChar(int x, int y, char c, const GfVec4f& color, int scale = 2);

    HdRenderPassAovBindingVector _aovBindings;
    GfRect2i _dataWindow;
    GfMatrix4d _viewMatrix;
    GfMatrix4d _projMatrix;
    GfMatrix4d _inverseViewMatrix;
    GfMatrix4d _inverseProjMatrix;
    std::vector<SceneInstance> _instances;
    std::vector<HdGeminiLight*> _activeLights;
    std::vector<float> _lightPowerCdf;
    float _lightPowerTotal = 0.0f;
    int _resolutionLevel = 4;
    int _frameCount = 0;
    
    // Cached AOV buffers
    class HdGeminiRenderBuffer* _colorBuffer = nullptr;
    class HdGeminiRenderBuffer* _albedoBuffer = nullptr;
    class HdGeminiRenderBuffer* _normalBuffer = nullptr;
    class HdGeminiRenderBuffer* _depthBuffer = nullptr;

    std::vector<GfVec3f> _accumHeroRGB;
    std::vector<GfVec3f> _accumDiffRGB;

    mutable std::atomic<long long> _rayCount{0};
    long long _lastRayCount = 0;
    float _raysPerSecond = 0.0f;
    float _lastProgressionTimeMs = 0.0f;
    
    std::vector<RenderBucket> _buckets;
    unsigned int _colorBufferVersion = 0xFFFFFFFF;
    int _lastWidth = 0;
    int _lastHeight = 0;
    std::vector<Reservoir> _temporalReservoirs;
    PhotonMap _photonMap;
    bool _enableSPPM = true;
    uint32_t _sppmPasses = 0;
    
    mutable GuidingGrid _guidingGrid;
    bool _enablePathGuiding = true;
    
    std::chrono::time_point<std::chrono::high_resolution_clock> _lastStatsUpdateTime;
    std::chrono::time_point<std::chrono::high_resolution_clock> _renderStartTime;
    std::chrono::time_point<std::chrono::high_resolution_clock> _renderEndTime;

    std::vector<TLASNode> _tlasNodes;
    std::vector<int> _tlasInstanceIndices;

    std::vector<float> _envMapPixels;
    int _envMapWidth = 0;
    int _envMapHeight = 0;
    std::vector<float> _envMapRowCdf;
    std::vector<float> _envMapColCdf;
    float _envMapTotalLuminance = 0.0f;
    SdfAssetPath _lastEnvMapPath;

    mutable std::map<std::string, TextureData> _textureCache;
    mutable std::mutex _textureMutex;

#ifdef HDGEMINI_HAS_SYCL
    sycl::queue* _syclQueue = nullptr;
    std::string _syclDeviceName = "Unknown";
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
#endif
};

#endif // HD_GEMINI_RENDERER_H
