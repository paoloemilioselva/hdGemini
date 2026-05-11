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
#include "spectrum.h"
#include <vector>
#include <atomic>
#include <map>
#include <memory>
#include <mutex>
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
    void SetResolutionLevel(int level) { _initialResolutionLevel = level; }
    
    void SetEnableDoF(bool enable) { _enableDoF = enable; }
    void SetFocalLength(float fl) { _focalLength = fl; }
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
    void SetPhysicalSkyTime(float t) { _physicalSkyTime = t; }
    void SetPhysicalSkySunExposure(float exp) { _physicalSkySunExposure = exp; }
    void SetPhysicalSkySkyExposure(float exp) { _physicalSkySkyExposure = exp; }

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
    
    bool _enableDoF = false;
    float _focalLength = 50.0f;
    float _fStop = 5.6f;
    float _focusDistance = 10.0f;
    int _bokehBlades = 0;
    bool _enablePhysicalCamera = false;
    float _iso = 100.0f;
    float _shutterSpeed = 0.02f;
    bool _enableLensFlare = false;
    bool _renderIblBackground = true;
    float _lensDistortion = 0.0f;
    float _chromaticAberration = 0.0f;
    bool _enablePhysicalSky = false;
    float _physicalSkyTime = 12.0f;
    float _physicalSkySunExposure = 0.0f;
    float _physicalSkySkyExposure = 0.0f;

    struct MeshInstance {
        HdGeminiMesh* mesh;
        const HdGeminiMesh::Subset* subset;
        HdGeminiMaterial* material = nullptr;
        GfMatrix4f transform;
        GfMatrix4f invTransform;
        GfRange3f bounds;
        GfVec3f centroid;
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
    };

    struct TextureData {
        std::vector<float> pixels;
        int width = 0;
        int height = 0;
    };

    static GfVec4f _GetClearColor(VtValue const& clearValue);
    void _RenderTiles(HdRenderThread *renderThread, HdGeminiRenderDelegate* delegate);
    void _PrepareScene(HdRenderThread *renderThread, HdGeminiRenderDelegate* delegate);
    void _BuildTLAS(HdRenderThread *renderThread);
    void _SubdivideTLAS(int nodeIdx, int start, int end, HdRenderThread *renderThread);
    bool _IntersectTLAS(const GfVec3f& rayOrigin, const GfVec3f& rayDir, HitRecord& hit, HdRenderThread* renderThread) const;
    SampledSpectrum _TraceRay(const GfVec3f& rayOrigin, const GfVec3f& rayDir, int depth, bool isInteractive, HdRenderThread* renderThread, uint32_t& rng, const SampledWavelengths& lambda, GfVec3f* outAlbedo = nullptr, GfVec3f* outNormal = nullptr) const;
    GfVec3f _SampleEnvironment(const GfVec3f& rayDir) const;
    GfVec3f _SamplePhysicalSky(const GfVec3f& rayDir, const GfVec3f& sunDir) const;
    GfVec3f _GetSunTransmittance(const GfVec3f& sunDir) const;
    GfVec3f _SampleTexture(const SdfAssetPath& path, const GfVec2f& uv, bool forceLinear = false) const;
    GfVec3f _SampleTextureData(const TextureData& data, const GfVec2f& uv) const;

    void _Denoise();
    void _ApplyPostProcess();

    HdRenderPassAovBindingVector _aovBindings;
    GfRect2i _dataWindow;
    GfMatrix4d _viewMatrix;
    GfMatrix4d _projMatrix;
    GfMatrix4d _inverseViewMatrix;
    GfMatrix4d _inverseProjMatrix;
    std::vector<MeshInstance> _instances;
    std::vector<HdGeminiLight*> _activeLights;
    int _resolutionLevel = 4;
    int _frameCount = 0;
    
    // Cached AOV buffers
    class HdGeminiRenderBuffer* _colorBuffer = nullptr;
    class HdGeminiRenderBuffer* _albedoBuffer = nullptr;
    class HdGeminiRenderBuffer* _normalBuffer = nullptr;

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
};

#endif // HD_GEMINI_RENDERER_H
