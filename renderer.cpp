#include "renderer.h"
#include "renderDelegate.h"
#include "renderBuffer.h"
#include "mesh.h"
#include "instancer.h"
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

void
HdGeminiRenderer::Render(HdRenderThread *renderThread, HdGeminiRenderDelegate* delegate)
{
    // Mark buffers as unconverged while we render
    for (auto const& binding : _aovBindings) {
        if (binding.renderBuffer) {
            static_cast<HdGeminiRenderBuffer*>(binding.renderBuffer)->SetConverged(false);
        }
    }

    _PrepareScene(delegate);
    _RenderTiles(renderThread, delegate);

    // Mark buffers as converged once finished and resolve
    for (auto const& binding : _aovBindings) {
        if (binding.renderBuffer) {
            auto* rb = static_cast<HdGeminiRenderBuffer*>(binding.renderBuffer);
            rb->Resolve();
            rb->SetConverged(true);
        }
    }
}

void
HdGeminiRenderer::_PrepareScene(HdGeminiRenderDelegate* delegate)
{
    _instances.clear();
    const auto& meshes = delegate->GetMeshes();

    for (auto const& item : meshes) {
        HdGeminiMesh* mesh = item.second;
        
        if (!mesh->GetInstancerId().IsEmpty()) {
            HdGeminiInstancer* instancer = delegate->GetInstancer(mesh->GetInstancerId());
            if (instancer) {
                VtMatrix4dArray transforms = instancer->ComputeInstanceTransforms(mesh->GetId());
                for (const auto& t : transforms) {
                    MeshInstance inst;
                    inst.mesh = mesh;
                    inst.transform = GfMatrix4f(t) * mesh->GetTransform();
                    inst.invTransform = inst.transform.GetInverse();
                    _instances.push_back(inst);
                }
                continue;
            }
        }
        
        // Non-instanced mesh
        MeshInstance inst;
        inst.mesh = mesh;
        inst.transform = mesh->GetTransform();
        inst.invTransform = inst.transform.GetInverse();
        _instances.push_back(inst);
    }
}

void
HdGeminiRenderer::_RenderTiles(HdRenderThread *renderThread, HdGeminiRenderDelegate* delegate)
{
    unsigned int width = _dataWindow.GetWidth();
    unsigned int height = _dataWindow.GetHeight();

    if (width == 0 || height == 0 || _aovBindings.empty()) return;

    HdGeminiRenderBuffer* colorBuffer = static_cast<HdGeminiRenderBuffer*>(_aovBindings[0].renderBuffer);

    GfVec3f cameraPosWorld(_inverseViewMatrix.Transform(GfVec3f(0, 0, 0)));

    WorkParallelForN(height, [&](size_t y_start, size_t y_end) {
        for (size_t y = y_start; y < y_end; ++y) {
            for (size_t x = 0; x < width; ++x) {
                if (renderThread->IsStopRequested()) return;

                // Generate ray
                float ndcX = (2.0f * (x + 0.5f) / width) - 1.0f;
                float ndcY = (2.0f * (y + 0.5f) / height) - 1.0f;
                
                GfVec3f nearPlanePointCam(_inverseProjMatrix.Transform(GfVec3f(ndcX, ndcY, -1.0f)));
                GfVec3f nearPlanePointWorld(_inverseViewMatrix.Transform(nearPlanePointCam));
                GfVec3f rayDirWorld = (nearPlanePointWorld - cameraPosWorld).GetNormalized();

                float minT = 1e30f;
                GfVec3f hitColor(0.0f, 0.0f, 0.0f);
                bool hit = false;

                for (const auto& inst : _instances) {
                    // Transform ray to object space
                    GfVec3f objRayOrigin = inst.invTransform.Transform(cameraPosWorld);
                    GfVec3f objRayDir = inst.invTransform.TransformDir(rayDirWorld);

                    float t = minT;
                    GfVec3f normal;
                    if (inst.mesh->GetBVH().Intersect(objRayOrigin, objRayDir, t, normal)) {
                        if (t < minT) {
                            minT = t;
                            hit = true;
                            // Transform normal to world space
                            GfVec3f worldN = inst.transform.TransformDir(normal).GetNormalized();
                            float shade = std::abs(GfDot(worldN, -rayDirWorld));
                            hitColor = GfVec3f(shade * 0.8f + 0.2f);
                        }
                    }
                }

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
            // Progressive resolve every 32 rows
            if (y % 32 == 0) {
                colorBuffer->Resolve();
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
            rb->SetConverged(false);
            if (binding.aovName == HdAovTokens->color) {
                GfVec4f clearColor = _GetClearColor(binding.clearValue);
                rb->Clear(4, clearColor.data());
            } else if (rb->GetFormat() == HdFormatFloat32) {
                float clearValue = binding.clearValue.Get<float>();
                rb->Clear(1, &clearValue);
            }
            rb->Resolve();
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
    for (auto const& binding : _aovBindings) {
        if (binding.renderBuffer) {
            static_cast<HdGeminiRenderBuffer*>(binding.renderBuffer)->SetConverged(false);
        }
    }
}
