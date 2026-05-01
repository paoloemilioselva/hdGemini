#include "renderPass.h"
#include "renderer.h"
#include <pxr/imaging/hd/renderPassState.h>
#include <pxr/imaging/hd/tokens.h>
#include <pxr/base/gf/vec3i.h>
#include <pxr/base/gf/vec4f.h>
#include <pxr/base/vt/value.h>

PXR_NAMESPACE_USING_DIRECTIVE

HdGeminiRenderPass::HdGeminiRenderPass(HdRenderIndex *index,
                                       HdRprimCollection const &collection,
                                       HdRenderThread *renderThread,
                                       HdGeminiRenderer *renderer,
                                       std::atomic<int> *sceneVersion)
    : HdRenderPass(index, collection)
    , _renderThread(renderThread)
    , _renderer(renderer)
    , _sceneVersion(sceneVersion)
    , _lastSceneVersion(0)
    , _viewMatrix(1.0)
    , _projMatrix(1.0)
    , _dataWindow(GfVec2i(0), 0, 0)
    , _colorBuffer(SdfPath::EmptyPath())
    , _depthBuffer(SdfPath::EmptyPath())
{
}

HdGeminiRenderPass::~HdGeminiRenderPass()
{
    _renderThread->StopRender();
}

bool
HdGeminiRenderPass::IsConverged() const
{
    if (_aovBindings.empty()) {
        return true;
    }

    for (size_t i = 0; i < _aovBindings.size(); ++i) {
        if (_aovBindings[i].renderBuffer &&
            !_aovBindings[i].renderBuffer->IsConverged()) {
            return false;
        }
    }
    return true;
}

static GfRect2i
_GetDataWindow(HdRenderPassStateSharedPtr const& renderPassState)
{
    const CameraUtilFraming &framing = renderPassState->GetFraming();
    if (framing.IsValid()) {
        return framing.dataWindow;
    } else {
        const GfVec4f vp = renderPassState->GetViewport();
        return GfRect2i(GfVec2i(0), int(vp[2]), int(vp[3]));        
    }
}

void
HdGeminiRenderPass::_Execute(HdRenderPassStateSharedPtr const& renderPassState,
                             TfTokenVector const &renderTags)
{
    bool needStartRender = false;
    int currentSceneVersion = _sceneVersion->load();
    if (_lastSceneVersion != currentSceneVersion) {
        needStartRender = true;
        _lastSceneVersion = currentSceneVersion;
    }

    const GfMatrix4d view = renderPassState->GetWorldToViewMatrix();
    const GfMatrix4d proj = renderPassState->GetProjectionMatrix();
    if (_viewMatrix != view || _projMatrix != proj) {
        _viewMatrix = view;
        _projMatrix = proj;
        _renderThread->StopRender();
        _renderer->SetCamera(_viewMatrix, _projMatrix);
        needStartRender = true;
    }

    const GfRect2i dataWindow = _GetDataWindow(renderPassState);
    if (_dataWindow != dataWindow) {
        _dataWindow = dataWindow;
        _renderThread->StopRender();
        _renderer->SetDataWindow(dataWindow);

        if (!renderPassState->GetFraming().IsValid()) {
            const GfVec3i dimensions(_dataWindow.GetWidth(), _dataWindow.GetHeight(), 1);
            _colorBuffer.Allocate(dimensions, HdFormatUNorm8Vec4, false);
            _depthBuffer.Allocate(dimensions, HdFormatFloat32, false);
        }

        needStartRender = true;
    }

    HdRenderPassAovBindingVector aovBindings = renderPassState->GetAovBindings();
    if (_aovBindings != aovBindings || _renderer->GetAovBindings().empty()) {
        _aovBindings = aovBindings;
        _renderThread->StopRender();

        if (aovBindings.empty()) {
            HdRenderPassAovBinding colorAov;
            colorAov.aovName = HdAovTokens->color;
            colorAov.renderBuffer = &_colorBuffer;
            colorAov.clearValue = VtValue(GfVec4f(0.0f, 0.0f, 0.0f, 1.0f));
            aovBindings.push_back(colorAov);
            HdRenderPassAovBinding depthAov;
            depthAov.aovName = HdAovTokens->depth;
            depthAov.renderBuffer = &_depthBuffer;
            depthAov.clearValue = VtValue(1.0f);
            aovBindings.push_back(depthAov);
        }

        _renderer->SetAovBindings(aovBindings);
        _renderer->Clear();
        needStartRender = true;
    }

    if (needStartRender) {
        _renderer->MarkAovBuffersUnconverged();
        _renderThread->StartRender();
    }
}
