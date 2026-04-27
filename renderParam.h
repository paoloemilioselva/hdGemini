#ifndef HD_GEMINI_RENDER_PARAM_H
#define HD_GEMINI_RENDER_PARAM_H

#include "pxr/pxr.h"
#include "pxr/imaging/hd/renderDelegate.h"
#include "pxr/imaging/hd/renderThread.h"

#include <atomic>

PXR_NAMESPACE_USING_DIRECTIVE

class HdGeminiRenderer;
class HdGeminiRenderDelegate;

class HdGeminiRenderParam final : public HdRenderParam
{
public:
    HdGeminiRenderParam(HdGeminiRenderDelegate *delegate,
                        HdRenderThread *renderThread,
                        HdGeminiRenderer *renderer,
                        std::atomic<int> *sceneVersion)
        : _delegate(delegate), _renderThread(renderThread)
        , _renderer(renderer), _sceneVersion(sceneVersion)
    {}

    void AcquireSceneForEdit() {
        _renderThread->StopRender();
        (*_sceneVersion)++;
    }

    HdGeminiRenderDelegate* GetRenderDelegate() { return _delegate; }
    HdGeminiRenderer* GetRenderer() { return _renderer; }

private:
    HdGeminiRenderDelegate *_delegate;
    HdRenderThread *_renderThread;
    HdGeminiRenderer* _renderer;
    std::atomic<int> *_sceneVersion;
};

#endif // HD_GEMINI_RENDER_PARAM_H
