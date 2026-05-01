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
    , _resolutionLevel(4)
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

    _PrepareScene(renderThread, delegate);
    if (renderThread->IsStopRequested()) return;
    _RenderTiles(renderThread, delegate);

    if (renderThread->IsStopRequested()) return;

    // Mark buffers as converged once finished and resolve
    for (auto const& binding : _aovBindings) {
        if (binding.renderBuffer) {
            auto* rb = static_cast<HdGeminiRenderBuffer*>(binding.renderBuffer);
            rb->Resolve();
            if (_resolutionLevel <= 1) {
                rb->SetConverged(true);
            }
        }
    }

    if (_resolutionLevel > 1) {
        _resolutionLevel /= 2;
    } else if (_resolutionLevel == 1) {
        _resolutionLevel = 0; // Mark as converged
    }
}

void
HdGeminiRenderer::_PrepareScene(HdRenderThread *renderThread, HdGeminiRenderDelegate* delegate)
{
    _instances.clear();
    
    std::lock_guard<std::mutex> lock(delegate->GetSceneLock());
    const auto& meshes = delegate->GetMeshes();

    for (auto const& item : meshes) {
        if (renderThread->IsStopRequested()) return;
        
        HdGeminiMesh* mesh = item.second;
        // if (!mesh->IsVisible()) continue;

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
    
    // Pre-build BVHs sequentially before parallel rendering
    for (auto& inst : _instances) {
        if (renderThread->IsStopRequested()) return;
        inst.mesh->GetBVH();
    }
    
    _BuildTLAS(renderThread);
}

void HdGeminiRenderer::_BuildTLAS(HdRenderThread *renderThread)
{
    _tlasNodes.clear();
    _tlasInstanceIndices.clear();
    if (_instances.empty() || renderThread->IsStopRequested()) return;

    _tlasInstanceIndices.resize(_instances.size());
    for (size_t i = 0; i < _instances.size(); ++i) {
        _tlasInstanceIndices[i] = (int)i;
    }

    _tlasNodes.reserve(_instances.size() * 2);
    _tlasNodes.push_back(TLASNode()); // Root
    _SubdivideTLAS(0, 0, (int)_instances.size(), renderThread);
}

void HdGeminiRenderer::_SubdivideTLAS(int nodeIdx, int start, int end, HdRenderThread *renderThread)
{
    if (renderThread->IsStopRequested()) return;

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

    _SubdivideTLAS(leftChildIdx, start, i, renderThread);
    _SubdivideTLAS(leftChildIdx + 1, i, end, renderThread);
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

    int res = _resolutionLevel;
    const int bucketSize = 16;
    size_t numBucketsX = (width + bucketSize - 1) / bucketSize;
    size_t numBucketsY = (height + bucketSize - 1) / bucketSize;
    size_t numBuckets = numBucketsX * numBucketsY;

    WorkParallelForN(numBuckets, [&](size_t b_start, size_t b_end) {
        for (size_t b = b_start; b < b_end; ++b) {
            if (renderThread->IsStopRequested()) return;

            size_t bx = b % numBucketsX;
            size_t by = b / numBucketsX;

            size_t startX = bx * bucketSize;
            size_t startY = by * bucketSize;
            size_t endX = std::min(startX + bucketSize, (size_t)width);
            size_t endY = std::min(startY + bucketSize, (size_t)height);

            // Align start to resolution level to avoid tearing
            startX = (startX / res) * res;
            startY = (startY / res) * res;

            for (size_t y = startY; y < endY; y += res) {
                if (renderThread->IsStopRequested()) return;

                for (size_t x = startX; x < endX; x += res) {
                    if (renderThread->IsStopRequested()) return;

                    // Generate ray from center of the block
                    float ndcX = (2.0f * (x + res * 0.5f) / width) - 1.0f;
                    float ndcY = (2.0f * (y + res * 0.5f) / height) - 1.0f;
                    
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

                    GfVec4f finalColor;
                    if (hit) {
                        finalColor = GfVec4f(hitColor[0], hitColor[1], hitColor[2], 1.0f);
                    } else {
                        float sky = 0.3f + 0.2f * ndcY;
                        finalColor = GfVec4f(sky * 0.8f, sky * 0.9f, sky, 1.0f);
                    }

                    // Write to block
                    for (int dy = 0; dy < res && y + dy < height; ++dy) {
                        for (int dx = 0; dx < res && x + dx < width; ++dx) {
                            colorBuffer->Write(GfVec3i(x + dx, y + dy, 0), 4, finalColor.data());
                        }
                    }
                }
            }
            if (!renderThread->IsStopRequested()) {
                colorBuffer->ResolveBucket(startX, startY, endX, endY);
            }
        }
    });
}

void
HdGeminiRenderer::Clear()
{
    _resolutionLevel = 4;
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
