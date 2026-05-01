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

static bool
IntersectAABB(const GfVec3f& rayOrigin, const GfVec3f& rayDir, const GfRange3f& range, float& tMinHit)
{
    if (range.IsEmpty()) return false;
    const GfVec3f& min = range.GetMin();
    const GfVec3f& max = range.GetMax();

    float tmin = -1e30f;
    float tmax = 1e30f;

    for (int i = 0; i < 3; ++i) {
        if (std::abs(rayDir[i]) < 1e-8) {
            if (rayOrigin[i] < min[i] || rayOrigin[i] > max[i]) return false;
        } else {
            float invDir = 1.0f / rayDir[i];
            float t1 = (min[i] - rayOrigin[i]) * invDir;
            float t2 = (max[i] - rayOrigin[i]) * invDir;
            if (t1 > t2) std::swap(t1, t2);
            tmin = std::max(tmin, t1);
            tmax = std::min(tmax, t2);
            if (tmin > tmax) return false;
        }
    }
    tMinHit = tmin;
    return tmax > 0 && tmax > 1e-4;
}

static GfRange3f TransformBounds(const GfRange3f& bounds, const GfMatrix4f& matrix) {
    if (bounds.IsEmpty()) return bounds;
    GfVec3f min = bounds.GetMin();
    GfVec3f max = bounds.GetMax();
    GfVec3f corners[8] = {
        GfVec3f(min[0], min[1], min[2]), GfVec3f(max[0], min[1], min[2]),
        GfVec3f(min[0], max[1], min[2]), GfVec3f(max[0], max[1], min[2]),
        GfVec3f(min[0], min[1], max[2]), GfVec3f(max[0], min[1], max[2]),
        GfVec3f(min[0], max[1], max[2]), GfVec3f(max[0], max[1], max[2])
    };
    GfRange3f result;
    for (int i = 0; i < 8; ++i) {
        result.ExtendBy(matrix.Transform(corners[i]));
    }
    return result;
}

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
        GfRange3f meshBounds = mesh->GetRange();
        if (meshBounds.IsEmpty()) continue;
        
        if (!mesh->GetInstancerId().IsEmpty()) {
            HdGeminiInstancer* instancer = delegate->GetInstancer(mesh->GetInstancerId());
            if (instancer) {
                VtMatrix4dArray transforms = instancer->ComputeInstanceTransforms(mesh->GetId());
                for (const auto& t : transforms) {
                    MeshInstance inst;
                    inst.mesh = mesh;
                    inst.transform = GfMatrix4f(t) * mesh->GetTransform();
                    inst.invTransform = inst.transform.GetInverse();
                    inst.bounds = TransformBounds(meshBounds, inst.transform);
                    inst.centroid = (inst.bounds.GetMin() + inst.bounds.GetMax()) * 0.5f;
                    _instances.push_back(inst);
                }
            }
            continue;
        }
        
        // Non-instanced mesh
        MeshInstance inst;
        inst.mesh = mesh;
        inst.transform = mesh->GetTransform();
        inst.invTransform = inst.transform.GetInverse();
        inst.bounds = TransformBounds(meshBounds, inst.transform);
        inst.centroid = (inst.bounds.GetMin() + inst.bounds.GetMax()) * 0.5f;
        _instances.push_back(inst);
    }
    
    _BuildTLAS();
}

void HdGeminiRenderer::_BuildTLAS()
{
    _tlasNodes.clear();
    _tlasInstanceIndices.clear();
    if (_instances.empty()) return;

    _tlasInstanceIndices.resize(_instances.size());
    for (size_t i = 0; i < _instances.size(); ++i) {
        _tlasInstanceIndices[i] = (int)i;
    }

    _tlasNodes.reserve(_instances.size() * 2);
    _tlasNodes.push_back(TLASNode()); // Root
    _SubdivideTLAS(0, 0, (int)_instances.size());
}

void HdGeminiRenderer::_SubdivideTLAS(int nodeIdx, int start, int end)
{
    TLASNode& node = _tlasNodes[nodeIdx];
    node.bounds.SetEmpty();
    for (int i = start; i < end; ++i) {
        node.bounds.ExtendBy(_instances[_tlasInstanceIndices[i]].bounds.GetMin());
        node.bounds.ExtendBy(_instances[_tlasInstanceIndices[i]].bounds.GetMax());
    }

    int count = end - start;
    if (count <= 2) { // leaf
        node.leftChild = -start - 1;
        node.instanceCount = count;
        return;
    }

    GfVec3f size = node.bounds.GetSize();
    int axis = 0;
    if (size[1] > size[0]) axis = 1;
    if (size[2] > size[axis]) axis = 2;

    float splitPos = node.bounds.GetMin()[axis] + size[axis] * 0.5f;

    int i = start;
    int j = end - 1;
    while (i <= j) {
        if (_instances[_tlasInstanceIndices[i]].centroid[axis] < splitPos) {
            i++;
        } else {
            std::swap(_tlasInstanceIndices[i], _tlasInstanceIndices[j]);
            j--;
        }
    }

    int leftCount = i - start;
    if (leftCount == 0 || leftCount == count) {
        i = start + count / 2;
    }

    int leftChildIdx = (int)_tlasNodes.size();
    _tlasNodes.push_back(TLASNode());
    _tlasNodes.push_back(TLASNode());
    node.leftChild = leftChildIdx;
    node.instanceCount = 0;

    _SubdivideTLAS(leftChildIdx, start, i);
    _SubdivideTLAS(leftChildIdx + 1, i, end);
}

bool HdGeminiRenderer::_IntersectTLAS(int nodeIdx, const GfVec3f& rayOrigin, const GfVec3f& rayDir, float& t, GfVec3f& normal, GfVec3f& hitColor, HdRenderThread* renderThread) const
{
    if (_tlasNodes.empty() || renderThread->IsStopRequested()) return false;

    const TLASNode& node = _tlasNodes[nodeIdx];
    float tAabb;
    if (!IntersectAABB(rayOrigin, rayDir, node.bounds, tAabb)) return false;
    if (tAabb > t) return false;

    if (node.leftChild < 0) {
        bool hit = false;
        int start = -node.leftChild - 1;
        for (int i = 0; i < node.instanceCount; ++i) {
            if (renderThread->IsStopRequested()) return false;
            const auto& inst = _instances[_tlasInstanceIndices[start + i]];
            
            GfVec3f objRayOrigin = inst.invTransform.Transform(rayOrigin);
            GfVec3f objRayDir = inst.invTransform.TransformDir(rayDir);
            
            float instT = t;
            GfVec3f instNormal;
            if (inst.mesh->GetBVH().Intersect(objRayOrigin, objRayDir, instT, instNormal)) {
                if (instT < t) {
                    t = instT;
                    normal = inst.transform.TransformDir(instNormal).GetNormalized();
                    hitColor = GfVec3f(std::abs(GfDot(normal, -rayDir)) * 0.8f + 0.2f);
                    hit = true;
                }
            }
        }
        return hit;
    } else {
        bool hitLeft = _IntersectTLAS(node.leftChild, rayOrigin, rayDir, t, normal, hitColor, renderThread);
        bool hitRight = _IntersectTLAS(node.leftChild + 1, rayOrigin, rayDir, t, normal, hitColor, renderThread);
        return hitLeft || hitRight;
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

                float t = 1e30f;
                GfVec3f hitColor(0.0f, 0.0f, 0.0f);
                GfVec3f normal;
                
                bool hit = false;
                if (!_tlasNodes.empty()) {
                    hit = _IntersectTLAS(0, cameraPosWorld, rayDirWorld, t, normal, hitColor, renderThread);
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
            if (y % 32 == 0 && !renderThread->IsStopRequested()) {
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
