#include "renderDelegate.h"
#include "renderParam.h"
#include "renderPass.h"
#include "renderBuffer.h"
#include "mesh.h"
#include "curves.h"
#include "instancer.h"
#include "light.h"
#include "material.h"
#include "volume.h"
#include "field.h"

#include "pxr/imaging/hd/renderIndex.h"
#include "pxr/imaging/hd/changeTracker.h"
#include "pxr/imaging/hd/camera.h"
#include "pxr/imaging/hd/extComputation.h"
#include "pxr/imaging/hd/tokens.h"
#include "pxr/base/gf/vec4f.h"
#include "pxr/base/vt/value.h"

#include <iostream>

PXR_NAMESPACE_USING_DIRECTIVE

TF_DEFINE_PUBLIC_TOKENS(HdGeminiAovTokens, HD_GEMINI_AOV_TOKENS);
TF_DEFINE_PUBLIC_TOKENS(HdGeminiRenderSettingsTokens, HD_GEMINI_RENDER_SETTINGS_TOKENS);

const TfTokenVector HdGeminiRenderDelegate::SUPPORTED_RPRIM_TYPES =
{
    HdPrimTypeTokens->mesh,
    HdPrimTypeTokens->volume,
    HdPrimTypeTokens->basisCurves,
};

const TfTokenVector HdGeminiRenderDelegate::SUPPORTED_SPRIM_TYPES =
{
    HdPrimTypeTokens->camera,
    HdPrimTypeTokens->extComputation,
    HdPrimTypeTokens->material,
    HdPrimTypeTokens->distantLight,
    HdPrimTypeTokens->sphereLight,
    HdPrimTypeTokens->domeLight,
    HdPrimTypeTokens->rectLight,
};

const TfTokenVector HdGeminiRenderDelegate::SUPPORTED_BPRIM_TYPES =
{
    HdPrimTypeTokens->renderBuffer,
    TfToken("openvdbAsset"),
};

std::mutex HdGeminiRenderDelegate::_mutexResourceRegistry;
std::atomic_int HdGeminiRenderDelegate::_counterResourceRegistry;
HdResourceRegistrySharedPtr HdGeminiRenderDelegate::_resourceRegistry;

#include <thread>
#include <chrono>

static void _RenderCallback(HdGeminiRenderer *renderer,
                            HdRenderThread *renderThread,
                            HdGeminiRenderDelegate *delegate)
{
    renderer->Clear();
    while (!renderThread->IsStopRequested() && !renderer->IsConverged()) {
        renderer->Render(renderThread, delegate);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

HdGeminiRenderDelegate::HdGeminiRenderDelegate()
    : HdRenderDelegate()
{
    _Initialize();
}

HdGeminiRenderDelegate::HdGeminiRenderDelegate(
    HdRenderSettingsMap const& settingsMap)
    : HdRenderDelegate(settingsMap)
{
    _Initialize();
}

void
HdGeminiRenderDelegate::_Initialize()
{
    _sceneVersion.store(0);
    _renderParam = std::make_shared<HdGeminiRenderParam>(
        this, &_renderThread, &_renderer, &_sceneVersion);

    _renderThread.SetRenderCallback(
        std::bind(_RenderCallback, &_renderer, &_renderThread, this));
    _renderThread.StartThread();

    std::lock_guard<std::mutex> guard(_mutexResourceRegistry);
    if (_counterResourceRegistry.fetch_add(1) == 0) {
        _resourceRegistry = std::make_shared<HdResourceRegistry>();
    }
    
    // Create fallback GeminiWater material for ocean meshes
    HdGeminiMaterial* waterMat = new HdGeminiMaterial(SdfPath("/GeminiWater_Default"));
    waterMat->SetIor(1.33f);
    waterMat->SetTransmission(1.0f);
    waterMat->SetRoughness(0.0f);
    waterMat->SetMetallic(0.0f);
    waterMat->SetSpecular(1.0f);
    waterMat->SetDiffuseColor(GfVec3f(1.0f));
    waterMat->SetTransmissionDepth(10.0f);
    waterMat->SetTransmissionScatter(GfVec3f(0.02f, 0.15f, 0.25f));
    AddMaterial(SdfPath("/GeminiWater_Default"), waterMat);
}

HdGeminiRenderDelegate::~HdGeminiRenderDelegate()
{
    {
        std::lock_guard<std::mutex> guard(_mutexResourceRegistry);
        if (_counterResourceRegistry.fetch_sub(1) == 1) {
            _resourceRegistry.reset();
        }
    }
    _renderThread.StopThread();
    
    auto it = _materials.find(SdfPath("/GeminiWater_Default"));
    if (it != _materials.end()) {
        delete it->second;
        _materials.erase(it);
    }
}

HdRenderParam*
HdGeminiRenderDelegate::GetRenderParam() const
{
    return _renderParam.get();
}

const TfTokenVector&
HdGeminiRenderDelegate::GetSupportedRprimTypes() const
{
    return SUPPORTED_RPRIM_TYPES;
}

const TfTokenVector&
HdGeminiRenderDelegate::GetSupportedSprimTypes() const
{
    return SUPPORTED_SPRIM_TYPES;
}

const TfTokenVector&
HdGeminiRenderDelegate::GetSupportedBprimTypes() const
{
    return SUPPORTED_BPRIM_TYPES;
}

TfTokenVector
HdGeminiRenderDelegate::GetShaderSourceTypes() const
{
    return {TfToken("mtlx"), TfToken("UsdPreviewSurface"), TfToken("preview")};
}

TfTokenVector
HdGeminiRenderDelegate::GetMaterialRenderContexts() const
{
    return {TfToken("mtlx"), TfToken()};
}

TfToken
HdGeminiRenderDelegate::GetMaterialBindingPurpose() const
{
    return HdTokens->full;
}

HdResourceRegistrySharedPtr
HdGeminiRenderDelegate::GetResourceRegistry() const
{
    return _resourceRegistry;
}

HdRenderPassSharedPtr
HdGeminiRenderDelegate::CreateRenderPass(HdRenderIndex *index,
                            HdRprimCollection const& collection)
{
    return HdRenderPassSharedPtr(new HdGeminiRenderPass(
        index, collection, &_renderThread, &_renderer, &_sceneVersion));
}

HdInstancer *
HdGeminiRenderDelegate::CreateInstancer(HdSceneDelegate *delegate,
                                        SdfPath const& id)
{
    HDGEMINI_LOG << "[Gemini] CreateInstancer: " << id.GetText() << std::endl;
    HdGeminiInstancer* instancer = new HdGeminiInstancer(delegate, id);
    AddInstancer(id, instancer);
    return instancer;
}

void
HdGeminiRenderDelegate::DestroyInstancer(HdInstancer *instancer)
{
    RemoveInstancer(instancer->GetId());
    delete instancer;
}

HdRprim *
HdGeminiRenderDelegate::CreateRprim(TfToken const& typeId,
                                    SdfPath const& rprimId)
{
    HDGEMINI_LOG << "[Gemini] CreateRprim: " << typeId.GetText() << " " << rprimId.GetText() << std::endl;
    if (typeId == HdPrimTypeTokens->mesh) {
        return new HdGeminiMesh(rprimId);
    }
    if (typeId == HdPrimTypeTokens->volume) {
        return new HdGeminiVolume(rprimId);
    }
    if (typeId == HdPrimTypeTokens->basisCurves) {
        return new HdGeminiBasisCurves(rprimId);
    }
    return nullptr;
}

void
HdGeminiRenderDelegate::DestroyRprim(HdRprim *rPrim)
{
    if (rPrim) {
        RemoveMesh(rPrim->GetId());
        RemoveBasisCurves(rPrim->GetId());
        delete rPrim;
    }
}

HdSprim *
HdGeminiRenderDelegate::CreateSprim(TfToken const& typeId,
                                    SdfPath const& sprimId)
{
    HDGEMINI_LOG << "[Gemini] CreateSprim: " << typeId.GetText() << " " << sprimId.GetText() << std::endl;
    if (typeId == HdPrimTypeTokens->camera) {
        return new HdCamera(sprimId);
    } else if (typeId == HdPrimTypeTokens->extComputation) {
        return new HdExtComputation(sprimId);
    } else if (typeId == HdPrimTypeTokens->material) {
        HdGeminiMaterial* mat = new HdGeminiMaterial(sprimId);
        AddMaterial(sprimId, mat);
        return mat;
    } else if (typeId == HdPrimTypeTokens->distantLight ||
               typeId == HdPrimTypeTokens->sphereLight ||
               typeId == HdPrimTypeTokens->domeLight ||
               typeId == HdPrimTypeTokens->rectLight) {
        return new HdGeminiLight(sprimId, typeId);
    }
    return nullptr;
}

HdSprim *
HdGeminiRenderDelegate::CreateFallbackSprim(TfToken const& typeId)
{
    HDGEMINI_LOG << "[Gemini] CreateFallbackSprim: " << typeId.GetText() << std::endl;
    if (typeId == HdPrimTypeTokens->camera) {
        return new HdCamera(SdfPath::EmptyPath());
    } else if (typeId == HdPrimTypeTokens->extComputation) {
        return new HdExtComputation(SdfPath::EmptyPath());
    } else if (typeId == HdPrimTypeTokens->material) {
        HdGeminiMaterial* mat = new HdGeminiMaterial(SdfPath::EmptyPath());
        AddMaterial(SdfPath::EmptyPath(), mat);
        return mat;
    } else if (typeId == HdPrimTypeTokens->distantLight ||
               typeId == HdPrimTypeTokens->sphereLight ||
               typeId == HdPrimTypeTokens->domeLight ||
               typeId == HdPrimTypeTokens->rectLight) {
        return new HdGeminiLight(SdfPath::EmptyPath(), typeId);
    }
    return nullptr;
}

void
HdGeminiRenderDelegate::DestroySprim(HdSprim *sPrim)
{
    if (sPrim) {
        if (sPrim->GetId().IsEmpty() || GetMaterial(sPrim->GetId())) {
            RemoveMaterial(sPrim->GetId());
        } else {
            RemoveLight(sPrim->GetId());
        }
        delete sPrim;
    }
}

HdBprim *
HdGeminiRenderDelegate::CreateBprim(TfToken const& typeId,
                                    SdfPath const& bprimId)
{
    HDGEMINI_LOG << "[Gemini] CreateBprim: " << typeId.GetText() << " " << bprimId.GetText() << std::endl;
    if (typeId == HdPrimTypeTokens->renderBuffer) {
        return new HdGeminiRenderBuffer(bprimId);
    }
    if (typeId == TfToken("openvdbAsset")) {
        return new HdGeminiField(bprimId, typeId);
    }
    return nullptr;
}

HdBprim *
HdGeminiRenderDelegate::CreateFallbackBprim(TfToken const& typeId)
{
    HDGEMINI_LOG << "[Gemini] CreateFallbackBprim: " << typeId.GetText() << std::endl;
    if (typeId == HdPrimTypeTokens->renderBuffer) {
        return new HdGeminiRenderBuffer(SdfPath::EmptyPath());
    }
    return nullptr;
}

void
HdGeminiRenderDelegate::DestroyBprim(HdBprim *bPrim)
{
    delete bPrim;
}

void
HdGeminiRenderDelegate::CommitResources(HdChangeTracker *tracker)
{
}

HdAovDescriptor
HdGeminiRenderDelegate::GetDefaultAovDescriptor(TfToken const& name) const
{
    if (name == HdAovTokens->color) {
        return HdAovDescriptor(HdFormatFloat32Vec4, true,
                               VtValue(GfVec4f(0.0f)));
    } else if (name == HdAovTokens->depth) {
        return HdAovDescriptor(HdFormatFloat32, false, VtValue(1.0f));
    } else if (name == HdGeminiAovTokens->albedo) {
        return HdAovDescriptor(HdFormatFloat32Vec3, false, VtValue(GfVec3f(0.0f)));
    } else if (name == HdGeminiAovTokens->normal) {
        return HdAovDescriptor(HdFormatFloat32Vec3, false, VtValue(GfVec3f(0.0f)));
    }
    return HdAovDescriptor();
}

HdRenderSettingDescriptorList
HdGeminiRenderDelegate::GetRenderSettingDescriptors() const
{
    HdRenderSettingDescriptorList list;
    list.push_back({
        "Enable Subsurface Scattering",
        HdGeminiRenderSettingsTokens->enableSubsurface,
        VtValue(true)
    });
    list.push_back({
        "Enable Subdivision",
        HdGeminiRenderSettingsTokens->enableSubdivision,
        VtValue(true)
    });
    list.push_back({
        "Enable OIDN Denoiser",
        HdGeminiRenderSettingsTokens->enableDenoiser,
        VtValue(true)
    });
    list.push_back({
        "Enable Pre-Pass: Firefly Filter",
        HdGeminiRenderSettingsTokens->enableFireflyFilter,
        VtValue(true)
    });
    list.push_back({
        "Enable Pre-Pass: Chromaticity Blur",
        HdGeminiRenderSettingsTokens->enableChromaticityBlur,
        VtValue(true)
    });
    list.push_back({
        "Target Sample Count",
        HdGeminiRenderSettingsTokens->targetSampleCount,
        VtValue(32)
    });
    list.push_back({
        "Max Reflection Bounces",
        HdGeminiRenderSettingsTokens->maxReflectionBounces,
        VtValue(8)
    });
    list.push_back({
        "Max Refraction Bounces",
        HdGeminiRenderSettingsTokens->maxRefractionBounces,
        VtValue(8)
    });
    list.push_back({
        "Resolution Level",
        HdGeminiRenderSettingsTokens->resolutionLevel,
        VtValue(2)
    });
    list.push_back({
        "Anti-Aliasing Filter (0=None, 1=Box, 2=Tent, 3=Gaussian)",
        HdGeminiRenderSettingsTokens->antiAliasingFilter,
        VtValue(1)
    });
    list.push_back({
        "Enable DoF",
        HdGeminiRenderSettingsTokens->enableDoF,
        VtValue(false)
    });
    list.push_back({
        "Enable SYCL GPU Acceleration",
        HdGeminiRenderSettingsTokens->enableSycl,
        VtValue(true)
    });
    list.push_back({
        "Enable On-Screen Stats",
        HdGeminiRenderSettingsTokens->enableOnScreenStats,
        VtValue(false)
    });
    list.push_back({
        "Focal Length (mm)",
        HdGeminiRenderSettingsTokens->focalLength,
        VtValue(50.0f)
    });
    list.push_back({
        "F-Stop (Aperture)",
        HdGeminiRenderSettingsTokens->fStop,
        VtValue(5.6f)
    });
    list.push_back({
        "Focus Distance",
        HdGeminiRenderSettingsTokens->focusDistance,
        VtValue(10.0f)
    });
    list.push_back({
        "Bokeh Blades",
        HdGeminiRenderSettingsTokens->bokehBlades,
        VtValue(0)
    });
    list.push_back({
        "Enable Physical Camera Exposure",
        HdGeminiRenderSettingsTokens->enablePhysicalCamera,
        VtValue(false)
    });
    list.push_back({
        "ISO",
        HdGeminiRenderSettingsTokens->iso,
        VtValue(100.0f)
    });
    list.push_back({
        "Shutter Speed",
        HdGeminiRenderSettingsTokens->shutterSpeed,
        VtValue(0.02f)
    });
    list.push_back({
        "Enable Lens Flare",
        HdGeminiRenderSettingsTokens->enableLensFlare,
        VtValue(false)
    });
    list.push_back({
        "Render IBL Background",
        HdGeminiRenderSettingsTokens->renderIblBackground,
        VtValue(true)
    });
    list.push_back({
        "Lens Distortion",
        HdGeminiRenderSettingsTokens->lensDistortion,
        VtValue(0.0f)
    });
    list.push_back({
        "Chromatic Aberration",
        HdGeminiRenderSettingsTokens->chromaticAberration,
        VtValue(0.0f)
    });
    list.push_back({
        "Enable Physical Sky",
        HdGeminiRenderSettingsTokens->physicalSkyEnable,
        VtValue(false)
    });
    list.push_back({
        "Physical Sky Azimuth",
        HdGeminiRenderSettingsTokens->physicalSkyAzimuth,
        VtValue(0.0f)
    });
    list.push_back({
        "Physical Sky Altitude",
        HdGeminiRenderSettingsTokens->physicalSkyAltitude,
        VtValue(90.0f)
    });
    list.push_back({
        "Physical Sky Sun Exposure",
        HdGeminiRenderSettingsTokens->physicalSkySunExposure,
        VtValue(0.0f)
    });
    list.push_back({
        "Physical Sky Sky Exposure",
        HdGeminiRenderSettingsTokens->physicalSkySkyExposure,
        VtValue(0.0f)
    });
    list.push_back({
        "Volume Step Size",
        HdGeminiRenderSettingsTokens->volumeStepSize,
        VtValue(0.1f)
    });
    list.push_back({
        "Volume Density Scale",
        HdGeminiRenderSettingsTokens->volumeDensityScale,
        VtValue(1.0f)
    });
    list.push_back({
        "Enable Adaptive Sampling",
        HdGeminiRenderSettingsTokens->enableAdaptiveSampling,
        VtValue(true)
    });
    list.push_back({
        "Adaptive Variance Threshold",
        HdGeminiRenderSettingsTokens->adaptiveVarianceThreshold,
        VtValue(0.01f)
    });
    list.push_back({
        "Adaptive Min Samples",
        HdGeminiRenderSettingsTokens->adaptiveMinSamples,
        VtValue(16)
    });
    list.push_back({
        "Ocean Enable",
        HdGeminiRenderSettingsTokens->oceanEnable,
        VtValue(false)
    });
    list.push_back({
        "Ocean Water Height",
        HdGeminiRenderSettingsTokens->oceanWaterHeight,
        VtValue(0.0f)
    });
    list.push_back({
        "Ocean Grid Size",
        HdGeminiRenderSettingsTokens->oceanGridSize,
        VtValue(128)
    });
    list.push_back({
        "Ocean Time",
        HdGeminiRenderSettingsTokens->oceanTime,
        VtValue(0.0f)
    });
    list.push_back({
        "Ocean Dicing Scale",
        HdGeminiRenderSettingsTokens->oceanDicingScale,
        VtValue(1.0f)
    });
    list.push_back({
        "Ocean Size 1",
        HdGeminiRenderSettingsTokens->oceanSize1,
        VtValue(100.0f)
    });
    list.push_back({
        "Ocean Size 2",
        HdGeminiRenderSettingsTokens->oceanSize2,
        VtValue(10.0f)
    });
    list.push_back({
        "Ocean Size 3",
        HdGeminiRenderSettingsTokens->oceanSize3,
        VtValue(1.0f)
    });
    list.push_back({
        "Ocean Amplitude 1",
        HdGeminiRenderSettingsTokens->oceanAmplitude1,
        VtValue(0.0f)
    });
    list.push_back({
        "Ocean Amplitude 2",
        HdGeminiRenderSettingsTokens->oceanAmplitude2,
        VtValue(0.0f)
    });
    list.push_back({
        "Ocean Amplitude 3",
        HdGeminiRenderSettingsTokens->oceanAmplitude3,
        VtValue(0.0f)
    });
    list.push_back({
        "Ocean Choppiness 1",
        HdGeminiRenderSettingsTokens->oceanChoppiness1,
        VtValue(1.2f)
    });
    list.push_back({
        "Ocean Choppiness 2",
        HdGeminiRenderSettingsTokens->oceanChoppiness2,
        VtValue(1.2f)
    });
    list.push_back({
        "Ocean Choppiness 3",
        HdGeminiRenderSettingsTokens->oceanChoppiness3,
        VtValue(1.2f)
    });
    list.push_back({
        "Ocean Strength 1",
        HdGeminiRenderSettingsTokens->oceanStrength1,
        VtValue(1.0f)
    });
    list.push_back({
        "Ocean Strength 2",
        HdGeminiRenderSettingsTokens->oceanStrength2,
        VtValue(1.0f)
    });
    list.push_back({
        "Ocean Strength 3",
        HdGeminiRenderSettingsTokens->oceanStrength3,
        VtValue(1.0f)
    });
    list.push_back({
        "Ocean Foam Visibility",
        HdGeminiRenderSettingsTokens->oceanFoamVisibility,
        VtValue(1.0f)
    });
    list.push_back({
        "Ocean Wind Speed",
        HdGeminiRenderSettingsTokens->oceanWindSpeed,
        VtValue(0.0f)
    });
    list.push_back({
        "Ocean Wind Direction X",
        HdGeminiRenderSettingsTokens->oceanWindDirectionX,
        VtValue(1.0f)
    });
    list.push_back({
        "Ocean Wind Direction Y",
        HdGeminiRenderSettingsTokens->oceanWindDirectionY,
        VtValue(1.0f)
    });
    
    list.push_back({
        "Ocean Disable Shader",
        HdGeminiRenderSettingsTokens->oceanDisableShader,
        VtValue(false)
    });
    list.push_back({
        "Ocean Repeat",
        HdGeminiRenderSettingsTokens->oceanRepeat,
        VtValue(true)
    });
    list.push_back({
        "Ocean Scattering Color",
        HdGeminiRenderSettingsTokens->oceanScatteringColor,
        VtValue(GfVec3f(0.02f, 0.15f, 0.25f))
    });
    list.push_back({
        "Ocean Scattering Depth",
        HdGeminiRenderSettingsTokens->oceanScatteringDepth,
        VtValue(10.0f)
    });
    return list;
}

VtValue
HdGeminiRenderDelegate::GetRenderSetting(TfToken const& key) const
{
    VtValue v = HdRenderDelegate::GetRenderSetting(key);
    if (!v.IsEmpty()) {
        return v;
    }
    
    if (key == HdGeminiRenderSettingsTokens->enableSubsurface) {
        return VtValue(true);
    } else if (key == HdGeminiRenderSettingsTokens->enableSubdivision) {
        return VtValue(true);
    } else if (key == HdGeminiRenderSettingsTokens->enableDenoiser) {
        return VtValue(true);
    } else if (key == HdGeminiRenderSettingsTokens->enableFireflyFilter) {
        return VtValue(true);
    } else if (key == HdGeminiRenderSettingsTokens->enableChromaticityBlur) {
        return VtValue(true);
    } else if (key == HdGeminiRenderSettingsTokens->targetSampleCount) {
        return VtValue(32);
    } else if (key == HdGeminiRenderSettingsTokens->maxReflectionBounces) {
        return VtValue(8);
    } else if (key == HdGeminiRenderSettingsTokens->maxRefractionBounces) {
        return VtValue(8);
    } else if (key == HdGeminiRenderSettingsTokens->resolutionLevel) {
        return VtValue(2);
    } else if (key == HdGeminiRenderSettingsTokens->antiAliasingFilter) {
        return VtValue(1);
    } else if (key == HdGeminiRenderSettingsTokens->enableSycl) {
        return VtValue(true);
    } else if (key == HdGeminiRenderSettingsTokens->enableOnScreenStats) {
        return VtValue(false);
    } else if (key == HdGeminiRenderSettingsTokens->enableDoF) {
        return VtValue(false);
    } else if (key == HdGeminiRenderSettingsTokens->focalLength) {
        return VtValue(50.0f);
    } else if (key == HdGeminiRenderSettingsTokens->fStop) {
        return VtValue(5.6f);
    } else if (key == HdGeminiRenderSettingsTokens->focusDistance) {
        return VtValue(10.0f);
    } else if (key == HdGeminiRenderSettingsTokens->bokehBlades) {
        return VtValue(0);
    } else if (key == HdGeminiRenderSettingsTokens->enablePhysicalCamera) {
        return VtValue(false);
    } else if (key == HdGeminiRenderSettingsTokens->iso) {
        return VtValue(100.0f);
    } else if (key == HdGeminiRenderSettingsTokens->shutterSpeed) {
        return VtValue(0.02f);
    } else if (key == HdGeminiRenderSettingsTokens->enableLensFlare) {
        return VtValue(false);
    } else if (key == HdGeminiRenderSettingsTokens->renderIblBackground) {
        return VtValue(true);
    } else if (key == HdGeminiRenderSettingsTokens->lensDistortion) {
        return VtValue(0.0f);
    } else if (key == HdGeminiRenderSettingsTokens->chromaticAberration) {
        return VtValue(0.0f);
    } else if (key == HdGeminiRenderSettingsTokens->physicalSkyEnable) {
        return VtValue(false);
    } else if (key == HdGeminiRenderSettingsTokens->physicalSkyAzimuth) {
        return VtValue(0.0f);
    } else if (key == HdGeminiRenderSettingsTokens->physicalSkyAltitude) {
        return VtValue(90.0f);
    } else if (key == HdGeminiRenderSettingsTokens->physicalSkySunExposure) {
        return VtValue(0.0f);
    } else if (key == HdGeminiRenderSettingsTokens->physicalSkySkyExposure) {
        return VtValue(0.0f);    } else if (key == HdGeminiRenderSettingsTokens->volumeStepSize) {
        return VtValue(0.1f);
    } else if (key == HdGeminiRenderSettingsTokens->volumeDensityScale) {
        return VtValue(1.0f);
    } else if (key == HdGeminiRenderSettingsTokens->enableAdaptiveSampling) {
        return VtValue(true);
    } else if (key == HdGeminiRenderSettingsTokens->adaptiveVarianceThreshold) {
        return VtValue(0.01f);
    } else if (key == HdGeminiRenderSettingsTokens->adaptiveMinSamples) {
        return VtValue(16);
    } else if (key == HdGeminiRenderSettingsTokens->oceanEnable) {
        return VtValue(false);
    } else if (key == HdGeminiRenderSettingsTokens->oceanWaterHeight) {
        return VtValue(0.0f);
    } else if (key == HdGeminiRenderSettingsTokens->oceanGridSize) {
        return VtValue(128);
    } else if (key == HdGeminiRenderSettingsTokens->oceanDicingScale) {
        return VtValue(1.0f);
    } else if (key == HdGeminiRenderSettingsTokens->oceanSize1) {
        return VtValue(100.0f);
    } else if (key == HdGeminiRenderSettingsTokens->oceanSize2) {
        return VtValue(10.0f);
    } else if (key == HdGeminiRenderSettingsTokens->oceanSize3) {
        return VtValue(1.0f);
    } else if (key == HdGeminiRenderSettingsTokens->oceanAmplitude1) {
        return VtValue(0.0f);
    } else if (key == HdGeminiRenderSettingsTokens->oceanAmplitude2) {
        return VtValue(0.0f);
    } else if (key == HdGeminiRenderSettingsTokens->oceanAmplitude3) {
        return VtValue(0.0f);
    } else if (key == HdGeminiRenderSettingsTokens->oceanChoppiness1) {
        return VtValue(1.2f);
    } else if (key == HdGeminiRenderSettingsTokens->oceanChoppiness2) {
        return VtValue(1.2f);
    } else if (key == HdGeminiRenderSettingsTokens->oceanChoppiness3) {
        return VtValue(1.2f);
    } else if (key == HdGeminiRenderSettingsTokens->oceanStrength1) {
        return VtValue(1.0f);
    } else if (key == HdGeminiRenderSettingsTokens->oceanStrength2) {
        return VtValue(1.0f);
    } else if (key == HdGeminiRenderSettingsTokens->oceanStrength3) {
        return VtValue(1.0f);
    } else if (key == HdGeminiRenderSettingsTokens->oceanFoamVisibility) {
        return VtValue(1.0f);
    } else if (key == HdGeminiRenderSettingsTokens->oceanWindSpeed) {
        return VtValue(0.0f);
    } else if (key == HdGeminiRenderSettingsTokens->oceanWindDirectionX) {
        return VtValue(1.0f);
    } else if (key == HdGeminiRenderSettingsTokens->oceanWindDirectionY) {
        return VtValue(1.0f);
    } else if (key == HdGeminiRenderSettingsTokens->oceanDisableShader) {
        return VtValue(false);
    } else if (key == HdGeminiRenderSettingsTokens->oceanTime) {
        return VtValue(0.0f);
    } else if (key == HdGeminiRenderSettingsTokens->oceanRepeat) {
        return VtValue(true);
    } else if (key == HdGeminiRenderSettingsTokens->oceanScatteringColor) {
        return VtValue(GfVec3f(0.02f, 0.15f, 0.25f));
    } else if (key == HdGeminiRenderSettingsTokens->oceanScatteringDepth) {
        return VtValue(10.0f);
    }
    
    return VtValue();
}

void
HdGeminiRenderDelegate::SetRenderSetting(TfToken const& key, VtValue const& value)
{
    VtValue current = GetRenderSetting(key);
    if (current == value) return;

    auto getFloat = [](const VtValue& v, float def = 0.0f) {
        if (v.IsHolding<float>()) return v.Get<float>();
        if (v.IsHolding<double>()) return (float)v.Get<double>();
        if (v.IsHolding<int>()) return (float)v.Get<int>();
        return def;
    };
    auto getInt = [](const VtValue& v, int def = 0) {
        if (v.IsHolding<int>()) return v.Get<int>();
        if (v.IsHolding<float>()) return (int)v.Get<float>();
        if (v.IsHolding<double>()) return (int)v.Get<double>();
        return def;
    };

    bool changed = false;
    bool postProcessChanged = false;

    if (key == HdGeminiRenderSettingsTokens->enableSubsurface) {
        if (value.IsHolding<bool>()) {
            _renderer.SetEnableSubsurface(value.Get<bool>());
            changed = true;
        }
    } else if (key == HdGeminiRenderSettingsTokens->enableSubdivision) {
        if (value.IsHolding<bool>()) {
            changed = true;
            for (auto& pair : _meshes) {
                // Request a sync on all meshes
            }
        }
    } else if (key == HdGeminiRenderSettingsTokens->enableDenoiser) {
        if (value.IsHolding<bool>()) {
            _renderer.SetEnableDenoiser(value.Get<bool>());
            postProcessChanged = true;
        }
    } else if (key == HdGeminiRenderSettingsTokens->enableFireflyFilter) {
        if (value.IsHolding<bool>()) {
            _renderer.SetEnableFireflyFilter(value.Get<bool>());
            postProcessChanged = true;
        }
    } else if (key == HdGeminiRenderSettingsTokens->enableChromaticityBlur) {
        if (value.IsHolding<bool>()) {
            _renderer.SetEnableChromaticityBlur(value.Get<bool>());
            postProcessChanged = true;
        }
    } else if (key == HdGeminiRenderSettingsTokens->targetSampleCount) {
        _renderer.SetTargetSampleCount(getInt(value, 32));
        changed = true;
    } else if (key == HdGeminiRenderSettingsTokens->maxReflectionBounces) {
        _renderer.SetMaxReflectionBounces(getInt(value, 8));
        changed = true;
    } else if (key == HdGeminiRenderSettingsTokens->maxRefractionBounces) {
        _renderer.SetMaxRefractionBounces(getInt(value, 8));
        changed = true;
    } else if (key == HdGeminiRenderSettingsTokens->resolutionLevel) {
        _renderer.SetResolutionLevel(getInt(value, 2));
        changed = true;
    } else if (key == HdGeminiRenderSettingsTokens->antiAliasingFilter) {
        _renderer.SetAntiAliasingFilter(getInt(value, 1));
        changed = true;
    } else if (key == HdGeminiRenderSettingsTokens->enableSycl) {
        if (value.IsHolding<bool>()) { _renderer.SetEnableSycl(value.Get<bool>()); changed = true; }
    } else if (key == HdGeminiRenderSettingsTokens->enableOnScreenStats) {
        if (value.IsHolding<bool>()) { _renderer.SetEnableOnScreenStats(value.Get<bool>()); postProcessChanged = true; }
    } else if (key == HdGeminiRenderSettingsTokens->enableDoF) {
        if (value.IsHolding<bool>()) { _renderer.SetEnableDoF(value.Get<bool>()); changed = true; }
    } else if (key == HdGeminiRenderSettingsTokens->focalLength) {
        _renderer.SetFocalLength(getFloat(value, 50.0f)); changed = true;
    } else if (key == HdGeminiRenderSettingsTokens->fStop) {
        _renderer.SetFStop(getFloat(value, 5.6f)); changed = true;
    } else if (key == HdGeminiRenderSettingsTokens->focusDistance) {
        _renderer.SetFocusDistance(getFloat(value, 10.0f)); changed = true;
    } else if (key == HdGeminiRenderSettingsTokens->bokehBlades) {
        _renderer.SetBokehBlades(getInt(value, 0)); changed = true;
    } else if (key == HdGeminiRenderSettingsTokens->enablePhysicalCamera) {
        if (value.IsHolding<bool>()) { _renderer.SetEnablePhysicalCamera(value.Get<bool>()); changed = true; }
    } else if (key == HdGeminiRenderSettingsTokens->iso) {
        _renderer.SetISO(getFloat(value, 100.0f)); changed = true;
    } else if (key == HdGeminiRenderSettingsTokens->shutterSpeed) {
        _renderer.SetShutterSpeed(getFloat(value, 0.02f)); changed = true;
    } else if (key == HdGeminiRenderSettingsTokens->enableLensFlare) {
        if (value.IsHolding<bool>()) { _renderer.SetEnableLensFlare(value.Get<bool>()); postProcessChanged = true; }
    } else if (key == HdGeminiRenderSettingsTokens->renderIblBackground) {
        if (value.IsHolding<bool>()) { _renderer.SetRenderIblBackground(value.Get<bool>()); changed = true; }
    } else if (key == HdGeminiRenderSettingsTokens->lensDistortion) {
        _renderer.SetLensDistortion(getFloat(value, 0.0f)); changed = true; // Lens distortion is evaluated during tracing
    } else if (key == HdGeminiRenderSettingsTokens->chromaticAberration) {
        _renderer.SetChromaticAberration(getFloat(value, 0.0f)); postProcessChanged = true;
    } else if (key == HdGeminiRenderSettingsTokens->physicalSkyEnable) {
        if (value.IsHolding<bool>()) { _renderer.SetPhysicalSkyEnable(value.Get<bool>()); changed = true; }
    } else if (key == HdGeminiRenderSettingsTokens->physicalSkyAzimuth) {
        _renderer.SetPhysicalSkyAzimuth(getFloat(value, 0.0f)); changed = true;
    } else if (key == HdGeminiRenderSettingsTokens->physicalSkyAltitude) {
        _renderer.SetPhysicalSkyAltitude(getFloat(value, 90.0f)); changed = true;
    } else if (key == HdGeminiRenderSettingsTokens->physicalSkySunExposure) {
        _renderer.SetPhysicalSkySunExposure(getFloat(value, 0.0f)); changed = true;
    } else if (key == HdGeminiRenderSettingsTokens->physicalSkySkyExposure) {
        _renderer.SetPhysicalSkySkyExposure(getFloat(value, 0.0f)); changed = true;
    } else if (key == HdGeminiRenderSettingsTokens->volumeStepSize) {
        _renderer.SetVolumeStepSize(getFloat(value, 0.1f)); changed = true;
    } else if (key == HdGeminiRenderSettingsTokens->volumeDensityScale) {
        _renderer.SetVolumeDensityScale(getFloat(value, 1.0f)); changed = true;
    } else if (key == HdGeminiRenderSettingsTokens->enableAdaptiveSampling) {
        if (value.IsHolding<bool>()) { _renderer.SetEnableAdaptiveSampling(value.Get<bool>()); changed = true; }
    } else if (key == HdGeminiRenderSettingsTokens->adaptiveVarianceThreshold) {
        _renderer.SetAdaptiveVarianceThreshold(getFloat(value, 0.01f));
        changed = true;
    } else if (key == HdGeminiRenderSettingsTokens->adaptiveMinSamples) {
        _renderer.SetAdaptiveMinSamples(getInt(value, 16));
        changed = true;
    } else if (key == HdGeminiRenderSettingsTokens->oceanEnable) {
        if (value.IsHolding<bool>()) {
            _renderer.SetOceanEnable(value.Get<bool>());
            changed = true;
        }
    } else if (key == HdGeminiRenderSettingsTokens->oceanWaterHeight) {
        _renderer.SetOceanWaterHeight(getFloat(value, 0.0f));
        changed = true;
    } else if (key == HdGeminiRenderSettingsTokens->oceanGridSize) {
        _renderer.SetOceanGridSize(getInt(value, 128));
        changed = true;
    } else if (key == HdGeminiRenderSettingsTokens->oceanDicingScale) {
        _renderer.SetOceanDicingScale(getFloat(value, 1.0f));
        changed = true;
    } else if (key == HdGeminiRenderSettingsTokens->oceanSize1) {
        _renderer.SetOceanSize(0, getFloat(value, 100.0f));
        changed = true;
    } else if (key == HdGeminiRenderSettingsTokens->oceanSize2) {
        _renderer.SetOceanSize(1, getFloat(value, 10.0f));
        changed = true;
    } else if (key == HdGeminiRenderSettingsTokens->oceanSize3) {
        _renderer.SetOceanSize(2, getFloat(value, 1.0f));
        changed = true;
    } else if (key == HdGeminiRenderSettingsTokens->oceanAmplitude1) {
        _renderer.SetOceanAmplitude(0, getFloat(value, 0.0f));
        changed = true;
    } else if (key == HdGeminiRenderSettingsTokens->oceanAmplitude2) {
        _renderer.SetOceanAmplitude(1, getFloat(value, 0.0f));
        changed = true;
    } else if (key == HdGeminiRenderSettingsTokens->oceanAmplitude3) {
        _renderer.SetOceanAmplitude(2, getFloat(value, 0.0f));
        changed = true;
    } else if (key == HdGeminiRenderSettingsTokens->oceanChoppiness1) {
        _renderer.SetOceanChoppiness(0, getFloat(value, 1.2f));
        changed = true;
    } else if (key == HdGeminiRenderSettingsTokens->oceanChoppiness2) {
        _renderer.SetOceanChoppiness(1, getFloat(value, 1.2f));
        changed = true;
    } else if (key == HdGeminiRenderSettingsTokens->oceanChoppiness3) {
        _renderer.SetOceanChoppiness(2, getFloat(value, 1.2f));
        changed = true;
    } else if (key == HdGeminiRenderSettingsTokens->oceanStrength1) {
        _renderer.SetOceanStrength(0, getFloat(value, 1.0f));
        changed = true;
    } else if (key == HdGeminiRenderSettingsTokens->oceanStrength2) {
        _renderer.SetOceanStrength(1, getFloat(value, 1.0f));
        changed = true;
    } else if (key == HdGeminiRenderSettingsTokens->oceanStrength3) {
        _renderer.SetOceanStrength(2, getFloat(value, 1.0f));
        changed = true;
    } else if (key == HdGeminiRenderSettingsTokens->oceanFoamVisibility) {
        _renderer.SetOceanFoamVisibility(getFloat(value, 1.0f));
        changed = true;
    } else if (key == HdGeminiRenderSettingsTokens->oceanWindSpeed) {
        _renderer.SetOceanWindSpeed(getFloat(value, 0.0f));
        changed = true;
    } else if (key == HdGeminiRenderSettingsTokens->oceanWindDirectionX) {
        float y = getFloat(GetRenderSetting(HdGeminiRenderSettingsTokens->oceanWindDirectionY), 1.0f);
        _renderer.SetOceanWindDirection(GfVec2f(getFloat(value, 1.0f), y));
        changed = true;
    } else if (key == HdGeminiRenderSettingsTokens->oceanWindDirectionY) {
        float x = getFloat(GetRenderSetting(HdGeminiRenderSettingsTokens->oceanWindDirectionX), 1.0f);
        _renderer.SetOceanWindDirection(GfVec2f(x, getFloat(value, 1.0f)));
        changed = true;
    } else if (key == HdGeminiRenderSettingsTokens->oceanDisableShader) {
        if (value.IsHolding<bool>()) {
            _renderer.SetOceanDisableShader(value.Get<bool>());
            changed = true;
        }
    } else if (key == HdGeminiRenderSettingsTokens->oceanTime) {
        _renderer.SetOceanTime(getFloat(value, 0.0f));
        changed = true;
    } else if (key == HdGeminiRenderSettingsTokens->oceanRepeat) {
        if (value.IsHolding<bool>()) {
            _renderer.SetOceanRepeat(value.Get<bool>());
            changed = true;
        }
    } else if (key == HdGeminiRenderSettingsTokens->oceanScatteringColor) {
        if (value.IsHolding<GfVec3f>()) {
            GfVec3f c = value.Get<GfVec3f>();
            _renderer.SetOceanScatteringColor(c);
            auto it = _materials.find(SdfPath("/GeminiWater_Default"));
            if (it != _materials.end()) {
                it->second->SetTransmissionScatter(c);
            }
            changed = true;
        }
    } else if (key == HdGeminiRenderSettingsTokens->oceanScatteringDepth) {
        float d = 10.0f;
        if (value.IsHolding<float>()) d = value.Get<float>();
        else if (value.IsHolding<double>()) d = (float)value.Get<double>();
        _renderer.SetOceanScatteringDepth(d);
        auto it = _materials.find(SdfPath("/GeminiWater_Default"));
        if (it != _materials.end()) {
            it->second->SetTransmissionDepth(d);
        }
        changed = true;
    }

    if (changed || postProcessChanged) {
        _renderThread.StopRender();
        if (changed) {
            _renderer.Clear();
            _sceneVersion++;
        } else {
            _renderer.ReapplyPostProcess();
        }
    }
    
    HdRenderDelegate::SetRenderSetting(key, value);
}

void
HdGeminiRenderDelegate::AddMesh(const SdfPath& id, HdGeminiMesh* mesh)
{
    std::lock_guard<std::recursive_mutex> lock(_sceneLock);
    _meshes[id] = mesh;
}

void
HdGeminiRenderDelegate::RemoveMesh(const SdfPath& id)
{
    std::lock_guard<std::recursive_mutex> lock(_sceneLock);
    _meshes.erase(id);
}

void
HdGeminiRenderDelegate::AddLight(const SdfPath& id, HdGeminiLight* light)
{
    std::lock_guard<std::recursive_mutex> lock(_sceneLock);
    _lights[id] = light;
}

void
HdGeminiRenderDelegate::RemoveLight(const SdfPath& id)
{
    std::lock_guard<std::recursive_mutex> lock(_sceneLock);
    _lights.erase(id);
}

void
HdGeminiRenderDelegate::AddVolume(const SdfPath& id, HdGeminiVolume* volume)
{
    std::lock_guard<std::recursive_mutex> guard(_sceneLock);
    _volumes[id] = volume;
    _sceneVersion.fetch_add(1);
}

void
HdGeminiRenderDelegate::RemoveVolume(const SdfPath& id)
{
    std::lock_guard<std::recursive_mutex> guard(_sceneLock);
    _volumes.erase(id);
    _sceneVersion.fetch_add(1);
}

void
HdGeminiRenderDelegate::AddBasisCurves(const SdfPath& id, HdGeminiBasisCurves* curves)
{
    std::lock_guard<std::recursive_mutex> lock(_sceneLock);
    _basisCurves[id] = curves;
}

void
HdGeminiRenderDelegate::RemoveBasisCurves(const SdfPath& id)
{
    std::lock_guard<std::recursive_mutex> lock(_sceneLock);
    _basisCurves.erase(id);
}

void
HdGeminiRenderDelegate::AddInstancer(const SdfPath& id, HdGeminiInstancer* instancer)
{
    std::lock_guard<std::recursive_mutex> lock(_sceneLock);
    _instancers[id] = instancer;
}

void
HdGeminiRenderDelegate::RemoveInstancer(const SdfPath& id)
{
    std::lock_guard<std::recursive_mutex> lock(_sceneLock);
    _instancers.erase(id);
}

HdGeminiInstancer*
HdGeminiRenderDelegate::GetInstancer(const SdfPath& id) const
{
    auto it = _instancers.find(id);
    if (it != _instancers.end()) {
        return it->second;
    }
    return nullptr;
}

void
HdGeminiRenderDelegate::AddMaterial(const SdfPath& id, HdGeminiMaterial* material)
{
    std::lock_guard<std::recursive_mutex> lock(_sceneLock);
    _materials[id] = material;
}

void
HdGeminiRenderDelegate::RemoveMaterial(const SdfPath& id)
{
    std::lock_guard<std::recursive_mutex> lock(_sceneLock);
    _materials.erase(id);
}

HdGeminiMaterial*
HdGeminiRenderDelegate::GetMaterial(const SdfPath& id) const
{
    auto it = _materials.find(id);
    if (it != _materials.end()) {
        return it->second;
    }
    return nullptr;
}
