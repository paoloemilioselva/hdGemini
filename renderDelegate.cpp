#include "renderDelegate.h"
#include "renderParam.h"
#include "renderPass.h"
#include "renderBuffer.h"
#include "mesh.h"
#include "instancer.h"
#include "light.h"
#include "material.h"

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
    return {TfToken("mtlx")};
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
    return nullptr;
}

void
HdGeminiRenderDelegate::DestroyRprim(HdRprim *rPrim)
{
    if (rPrim) {
        RemoveMesh(rPrim->GetId());
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
        "Enable Depth of Field",
        HdGeminiRenderSettingsTokens->enableDoF,
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
        return VtValue(0.0f);
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
    }

    if (changed || postProcessChanged) {
        _renderThread.StopRender();
        if (changed) {
            _renderer.Clear();
        } else {
            _renderer.ReapplyPostProcess();
        }
        _sceneVersion++;
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
