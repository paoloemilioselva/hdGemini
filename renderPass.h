#ifndef HD_GEMINI_RENDER_PASS_H
#define HD_GEMINI_RENDER_PASS_H

#include "pxr/pxr.h"
#include "pxr/imaging/hd/renderPass.h"
#include "pxr/imaging/hd/renderThread.h"
#include "renderer.h"
#include "renderBuffer.h"

#include <atomic>

PXR_NAMESPACE_USING_DIRECTIVE

class HdGeminiRenderPass final : public HdRenderPass {
public:
    HdGeminiRenderPass(HdRenderIndex *index,
                       HdRprimCollection const &collection,
                       HdRenderThread *renderThread,
                       HdGeminiRenderer *renderer,
                       std::atomic<int> *sceneVersion);
    virtual ~HdGeminiRenderPass();

    virtual bool IsConverged() const override;

protected:
    virtual void _Execute(HdRenderPassStateSharedPtr const& renderPassState,
                          TfTokenVector const &renderTags) override;

    virtual void _MarkCollectionDirty() override {}

private:
    HdRenderThread *_renderThread;
    HdGeminiRenderer *_renderer;
    std::atomic<int> *_sceneVersion;
    int _lastSceneVersion;
    GfMatrix4d _viewMatrix, _projMatrix;
    GfRect2i _dataWindow;
    HdRenderPassAovBindingVector _aovBindings;
    HdGeminiRenderBuffer _colorBuffer;
    HdGeminiRenderBuffer _depthBuffer;
};

#endif // HD_GEMINI_RENDER_PASS_H
