#include "renderer.h"
#include "renderDelegate.h"
#include "renderBuffer.h"
#include "mesh.h"
#include <pxr/base/work/loops.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/gf/vec3i.h>
#include <pxr/base/gf/matrix4f.h>
#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/vt/array.h>
#include <pxr/imaging/hd/tokens.h>
#include <iostream>
#include <cmath>
#include <algorithm>

PXR_NAMESPACE_USING_DIRECTIVE

HdGeminiRenderer::HdGeminiRenderer()
    : _viewMatrix(1.0)
    , _projMatrix(1.0)
    , _inverseViewMatrix(1.0)
    , _inverseProjMatrix(1.0)
{
}

HdGeminiRenderer::~HdGeminiRenderer() = default;

void
HdGeminiRenderer::SetCamera(const GfMatrix4d& viewMatrix, const GfMatrix4d& projMatrix)
{
    _viewMatrix = viewMatrix;
    _projMatrix = projMatrix;
    _inverseViewMatrix = viewMatrix.GetInverse();
    _inverseProjMatrix = projMatrix.GetInverse();
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

static bool 
IntersectTriangle(const GfVec3f& rayOrigin, const GfVec3f& rayDir,
                  const GfVec3f& v0, const GfVec3f& v1, const GfVec3f& v2,
                  float& t)
{
    GfVec3f edge1 = v1 - v0;
    GfVec3f edge2 = v2 - v0;
    GfVec3f pvec = GfCross(rayDir, edge2);
    float det = GfDot(edge1, pvec);
    if (std::abs(det) < 1e-8) return false;
    float invDet = 1.0f / det;
    GfVec3f tvec = rayOrigin - v0;
    float u = GfDot(tvec, pvec) * invDet;
    if (u < 0.0f || u > 1.0f) return false;
    GfVec3f qvec = GfCross(tvec, edge1);
    float v = GfDot(rayDir, qvec) * invDet;
    if (v < 0.0f || u + v > 1.0f) return false;
    t = GfDot(edge2, qvec) * invDet;
    return (t > 1e-4);
}

void
HdGeminiRenderer::Render(HdRenderThread *renderThread, HdGeminiRenderDelegate* delegate)
{
    _RenderTiles(renderThread, delegate);
}

void
HdGeminiRenderer::_RenderTiles(HdRenderThread *renderThread, HdGeminiRenderDelegate* delegate)
{
    unsigned int width = _dataWindow.GetWidth();
    unsigned int height = _dataWindow.GetHeight();

    if (width == 0 || height == 0 || _aovBindings.empty()) return;

    HdGeminiRenderBuffer* colorBuffer = static_cast<HdGeminiRenderBuffer*>(_aovBindings[0].renderBuffer);

    const auto& meshes = delegate->GetMeshes();

    GfVec3f cameraPosWorld(_inverseViewMatrix.Transform(GfVec3f(0, 0, 0)));

    WorkParallelForN(height, [&](size_t y_start, size_t y_end) {
        for (size_t y = y_start; y < y_end; ++y) {
            for (size_t x = 0; x < width; ++x) {
                if (renderThread->IsStopRequested()) return;

                // Generate ray
                // Standard mapping: x=0 -> ndcX=-1 (left), y=0 -> ndcY=1 (top)
                float ndcX = (2.0f * (x + 0.5f) / width) - 1.0f;
                float ndcY = 1.0f - (2.0f * (y + 0.5f) / height);
                
                GfVec3f nearPlanePointCam(_inverseProjMatrix.Transform(GfVec3f(ndcX, ndcY, -1.0f)));
                GfVec3f nearPlanePointWorld(_inverseViewMatrix.Transform(nearPlanePointCam));
                GfVec3f rayDirWorld = (nearPlanePointWorld - cameraPosWorld).GetNormalized();

                float minT = 1e30f;
                GfVec3f hitColor(0.0f, 0.0f, 0.0f);
                bool hit = false;

                for (auto const& item : meshes) {
                    HdGeminiMesh* mesh = item.second;
                    const GfMatrix4f& transform = mesh->GetTransform();
                    const VtVec3fArray& points = mesh->GetPoints();
                    const VtVec3iArray& indices = mesh->GetIndices();

                    if (points.empty() || indices.empty()) continue;

                    // Transform ray to object space
                    GfMatrix4f invTransform = transform.GetInverse();
                    GfVec3f objRayOrigin = invTransform.Transform(cameraPosWorld);
                    GfVec3f objRayDir = invTransform.TransformDir(rayDirWorld);

                    for (const auto& tri : indices) {
                        float t;
                        if (IntersectTriangle(objRayOrigin, objRayDir, points[tri[0]], points[tri[1]], points[tri[2]], t)) {
                            if (t < minT) {
                                minT = t;
                                hit = true;
                                GfVec3f v0 = points[tri[0]];
                                GfVec3f v1 = points[tri[1]];
                                GfVec3f v2 = points[tri[2]];
                                GfVec3f n = GfCross(v1 - v0, v2 - v0).GetNormalized();
                                // Transform normal to world space
                                GfVec3f worldN = transform.TransformDir(n).GetNormalized();
                                float shade = std::abs(GfDot(worldN, -rayDirWorld));
                                hitColor = GfVec3f(shade * 0.8f + 0.2f);
                            }
                        }
                    }
                }

                // Write to buffer directly using (x, y)
                if (hit) {
                    GfVec4f finalColor(hitColor[0], hitColor[1], hitColor[2], 1.0f);
                    colorBuffer->Write(GfVec3i(x, y, 0), 4, finalColor.data());
                } else {
                    // Sky gradient
                    float sky = 0.3f + 0.2f * ndcY;
                    GfVec4f bgColor(sky * 0.8f, sky * 0.9f, sky, 1.0f);
                    colorBuffer->Write(GfVec3i(x, y, 0), 4, bgColor.data());
                }
            }
        }
    });
}

void
HdGeminiRenderer::Clear()
{
    for (auto const& binding : _aovBindings) {
        if (binding.renderBuffer && !binding.clearValue.IsEmpty()) {
            HdGeminiRenderBuffer* rb = static_cast<HdGeminiRenderBuffer*>(binding.renderBuffer);
            if (binding.aovName == HdAovTokens->color) {
                GfVec4f clearColor = _GetClearColor(binding.clearValue);
                rb->Clear(4, clearColor.data());
            } else if (rb->GetFormat() == HdFormatFloat32) {
                float clearValue = binding.clearValue.Get<float>();
                rb->Clear(1, &clearValue);
            }
        }
    }
}

GfVec4f
HdGeminiRenderer::_GetClearColor(VtValue const& clearValue)
{
    if (clearValue.IsHolding<GfVec4f>()) {
        return clearValue.UncheckedGet<GfVec4f>();
    } else if (clearValue.IsHolding<GfVec3f>()) {
        GfVec3f v = clearValue.UncheckedGet<GfVec3f>();
        return GfVec4f(v[0], v[1], v[2], 1.0f);
    }
    return GfVec4f(0.0f, 0.0f, 0.0f, 1.0f);
}

void
HdGeminiRenderer::MarkAovBuffersUnconverged()
{
}
