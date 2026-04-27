#include "renderer.h"
#include <pxr/base/work/loops.h>
#include <iostream>

PXR_NAMESPACE_OPEN_SCOPE

HdGeminiRenderer::HdGeminiRenderer()
    : _viewMatrix(1.0)
    , _projMatrix(1.0)
{
}

HdGeminiRenderer::~HdGeminiRenderer() = default;

void
HdGeminiRenderer::SetCamera(const GfMatrix4d& viewMatrix, const GfMatrix4d& projMatrix)
{
    _viewMatrix = viewMatrix;
    _projMatrix = projMatrix;
}

void
HdGeminiRenderer::SetDataWindow(const GfRect2i& dataWindow)
{
    _dataWindow = dataWindow;
}

void
HdGeminiRenderer::SetAovBindings(const HdRenderPassAovBindingVector& aovBindings)
{
    _aovBindings = aovBindings;
}

void
HdGeminiRenderer::Render(HdRenderThread *renderThread)
{
    // Basic raytracing implementation placeholder
}

void
HdGeminiRenderer::Clear()
{
}

void
HdGeminiRenderer::MarkAovBuffersUnconverged()
{
}

PXR_NAMESPACE_CLOSE_SCOPE
