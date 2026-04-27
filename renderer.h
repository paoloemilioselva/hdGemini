#ifndef HD_GEMINI_RENDERER_H
#define HD_GEMINI_RENDERER_H

#include "pxr/pxr.h"
#include "pxr/imaging/hd/renderThread.h"
#include "pxr/imaging/hd/aov.h"
#include "pxr/base/gf/matrix4d.h"
#include "pxr/base/gf/rect2i.h"

#include <atomic>

PXR_NAMESPACE_OPEN_SCOPE

class HdGeminiRenderer final
{
public:
    HdGeminiRenderer();
    ~HdGeminiRenderer();

    void SetCamera(const GfMatrix4d& viewMatrix, const GfMatrix4d& projMatrix);
    void SetDataWindow(const GfRect2i& dataWindow);
    void SetAovBindings(const HdRenderPassAovBindingVector& aovBindings);
    const HdRenderPassAovBindingVector& GetAovBindings() const { return _aovBindings; }

    void Render(HdRenderThread *renderThread);
    void Clear();
    void MarkAovBuffersUnconverged();

private:
    HdRenderPassAovBindingVector _aovBindings;
    GfRect2i _dataWindow;
    GfMatrix4d _viewMatrix;
    GfMatrix4d _projMatrix;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // HD_GEMINI_RENDERER_H
