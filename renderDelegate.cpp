#include "renderDelegate.h"
#include "renderParam.h"
#include "renderPass.h"
#include "mesh.h"
#include "camera.h"
#include "instancer.h"
#include "light.h"
#include "renderBuffer.h"

#include "pxr/imaging/hd/camera.h"
#include "pxr/imaging/hd/tokens.h"
#include "pxr/base/gf/vec4f.h"
#include "pxr/base/vt/value.h"

#include <iostream>

PXR_NAMESPACE_USING_DIRECTIVE

const TfTokenVector HdGeminiRenderDelegate::SUPPORTED_RPRIM_TYPES =
{
    HdPrimTypeTokens->mesh,
};

const TfTokenVector HdGeminiRenderDelegate::SUPPORTED_SPRIM_TYPES =
{
    HdPrimTypeTokens->camera,
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
        // Yield to allow UI thread to breathe
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
    if (typeId == HdPrimTypeTokens->mesh) {
        return new HdGeminiMesh(rprimId);
    }
    return nullptr;
}

void
HdGeminiRenderDelegate::DestroyRprim(HdRprim *rPrim)
{
    delete rPrim;
}

HdSprim *
HdGeminiRenderDelegate::CreateSprim(TfToken const& typeId,
                                    SdfPath const& sprimId)
{
    if (typeId == HdPrimTypeTokens->camera) {
        return new HdCamera(sprimId);
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
    if (typeId == HdPrimTypeTokens->camera) {
        return new HdCamera(SdfPath::EmptyPath());
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
        RemoveLight(sPrim->GetId());
        delete sPrim;
    }
}

HdBprim *
HdGeminiRenderDelegate::CreateBprim(TfToken const& typeId,
                                    SdfPath const& bprimId)
{
    if (typeId == HdPrimTypeTokens->renderBuffer) {
        return new HdGeminiRenderBuffer(bprimId);
    }
    return nullptr;
}

HdBprim *
HdGeminiRenderDelegate::CreateFallbackBprim(TfToken const& typeId)
{
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
        return HdAovDescriptor(HdFormatUNorm8Vec4, true,
                               VtValue(GfVec4f(0.0f)));
    } else if (name == HdAovTokens->depth) {
        return HdAovDescriptor(HdFormatFloat32, false, VtValue(1.0f));
    }
    return HdAovDescriptor();
}

void
HdGeminiRenderDelegate::AddMesh(const SdfPath& id, HdGeminiMesh* mesh)
{
    std::lock_guard<std::mutex> lock(_sceneLock);
    _meshes[id] = mesh;
}

void
HdGeminiRenderDelegate::RemoveMesh(const SdfPath& id)
{
    std::lock_guard<std::mutex> lock(_sceneLock);
    _meshes.erase(id);
}

void
HdGeminiRenderDelegate::AddLight(const SdfPath& id, HdGeminiLight* light)
{
    std::lock_guard<std::mutex> lock(_sceneLock);
    _lights[id] = light;
}

void
HdGeminiRenderDelegate::RemoveLight(const SdfPath& id)
{
    std::lock_guard<std::mutex> lock(_sceneLock);
    _lights.erase(id);
}

void
HdGeminiRenderDelegate::AddInstancer(const SdfPath& id, HdGeminiInstancer* instancer)
{
    std::lock_guard<std::mutex> lock(_sceneLock);
    _instancers[id] = instancer;
}

void
HdGeminiRenderDelegate::RemoveInstancer(const SdfPath& id)
{
    std::lock_guard<std::mutex> lock(_sceneLock);
    _instancers.erase(id);
}

HdGeminiInstancer*
HdGeminiRenderDelegate::GetInstancer(const SdfPath& id) const
{
    // Usually called from _PrepareScene which already holds _sceneLock.
    auto it = _instancers.find(id);
    if (it != _instancers.end()) {
        return it->second;
    }
    return nullptr;
}
