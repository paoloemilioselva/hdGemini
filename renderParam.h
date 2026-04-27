#ifndef HD_GEMINI_RENDER_PARAM_H
#define HD_GEMINI_RENDER_PARAM_H

#include "pxr/pxr.h"
#include "pxr/imaging/hd/renderDelegate.h"
#include "pxr/imaging/hd/renderThread.h"

#include <atomic>

PXR_NAMESPACE_OPEN_SCOPE

class HdGeminiRenderer;

class HdGeminiRenderParam final : public HdRenderParam
{
public:
    HdGeminiRenderParam(HdRenderThread *renderThread,
                        HdGeminiRenderer *renderer,
                        std::atomic<int> *sceneVersion)
        : _renderThread(renderThread), _renderer(renderer), _sceneVersion(sceneVersion)
    {}

    void AcquireSceneForEdit() {
        _renderThread->StopRender();
        (*_sceneVersion)++;
    }

    HdGeminiRenderer* GetRenderer() { return _renderer; }

private:
    HdRenderThread *_renderThread;
    HdGeminiRenderer* _renderer;
    std::atomic<int> *_sceneVersion;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // HD_GEMINI_RENDER_PARAM_H
