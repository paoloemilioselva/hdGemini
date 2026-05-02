#include "renderer.h"
#include "renderDelegate.h"
#include "renderBuffer.h"
#include "mesh.h"
#include "instancer.h"
#include "light.h"
#include <pxr/base/work/loops.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/gf/vec3i.h>
#include <pxr/base/gf/matrix4f.h>
#include <pxr/base/gf/matrix4d.h>
#include <pxr/base/vt/array.h>
#include <pxr/imaging/hd/tokens.h>
#include <pxr/imaging/hio/image.h>
#include <pxr/imaging/hio/types.h>
#include <iostream>
#include <cmath>
#include <algorithm>
#include <random>
#include <thread>
#include <chrono>

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

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

static float RandomFloat(uint32_t& state) {
    state = state * 1664525 + 1013904223;
    return (float)state / (float)0xFFFFFFFF;
}

static GfVec3f SampleCosineHemisphere(float u1, float u2) {
    float r = std::sqrt(u1);
    float theta = 2.0f * M_PI * u2;
    return GfVec3f(r * std::cos(theta), std::sqrt(1.0f - u1), r * std::sin(theta));
}

static GfVec3f AlignToNormal(const GfVec3f& sample, const GfVec3f& normal) {
    GfVec3f up = std::abs(normal[1]) < 0.999f ? GfVec3f(0, 1, 0) : GfVec3f(1, 0, 0);
    GfVec3f tangent = GfCross(up, normal).GetNormalized();
    GfVec3f bitangent = GfCross(normal, tangent);
    return sample[0] * tangent + sample[1] * normal + sample[2] * bitangent;
}

HdGeminiRenderer::HdGeminiRenderer()
    : _viewMatrix(1.0)
    , _projMatrix(1.0)
    , _inverseViewMatrix(1.0)
    , _inverseProjMatrix(1.0)
    , _resolutionLevel(4)
    , _frameCount(0)
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
    for (auto const& binding : _aovBindings) {
        if (binding.renderBuffer) {
            static_cast<HdGeminiRenderBuffer*>(binding.renderBuffer)->SetConverged(false);
        }
    }

    _PrepareScene(renderThread, delegate);
    if (renderThread->IsStopRequested()) return;
    _RenderTiles(renderThread, delegate);

    if (renderThread->IsStopRequested()) return;

    for (auto const& binding : _aovBindings) {
        if (binding.renderBuffer) {
            auto* rb = static_cast<HdGeminiRenderBuffer*>(binding.renderBuffer);
            rb->Resolve();
        }
    }

    if (_resolutionLevel > 1) {
        _resolutionLevel /= 2;
    } else {
        _resolutionLevel = 1;
        _frameCount++;
    }
}

void
HdGeminiRenderer::_PrepareScene(HdRenderThread *renderThread, HdGeminiRenderDelegate* delegate)
{
    _instances.clear();
    
    std::lock_guard<std::mutex> lock(delegate->GetSceneLock());
    const auto& meshes = delegate->GetMeshes();
    const auto& lights = delegate->GetLights();

    bool foundDome = false;
    for (const auto& lightPair : lights) {
        HdGeminiLight* light = lightPair.second;
        if (light->GetLightType() == HdPrimTypeTokens->domeLight) {
            SdfAssetPath texPath = light->GetTextureFile();
            if (!texPath.GetAssetPath().empty()) {
                if (texPath != _lastEnvMapPath) {
                    HioImageSharedPtr image = HioImage::OpenForReading(texPath.GetResolvedPath());
                    if (image) {
                        _envMapWidth = image->GetWidth();
                        _envMapHeight = image->GetHeight();
                        _envMapPixels.assign(_envMapWidth * _envMapHeight * 3, 0.0f);
                        HioImage::StorageSpec spec;
                        spec.width = _envMapWidth;
                        spec.height = _envMapHeight;
                        spec.format = HioFormatFloat32Vec3;
                        spec.data = _envMapPixels.data();
                        image->Read(spec);
                        _lastEnvMapPath = texPath;

                        _envMapRowCdf.assign(_envMapHeight + 1, 0.0f);
                        _envMapColCdf.assign(_envMapHeight * (_envMapWidth + 1), 0.0f);

                        _envMapTotalLuminance = 0.0f;
                        for (int y = 0; y < _envMapHeight; ++y) {
                            float rowLuminance = 0.0f;
                            float sinTheta = std::sin(M_PI * (float)(y + 0.5f) / (float)_envMapHeight);
                            for (int x = 0; x < _envMapWidth; ++x) {
                                size_t idx = (y * _envMapWidth + x) * 3;
                                float lum = 0.2126f * _envMapPixels[idx] + 0.7152f * _envMapPixels[idx+1] + 0.0722f * _envMapPixels[idx+2];
                                lum *= sinTheta;
                                rowLuminance += lum;
                                _envMapColCdf[y * (_envMapWidth + 1) + x + 1] = _envMapColCdf[y * (_envMapWidth + 1) + x] + lum;
                            }
                            if (rowLuminance > 0) {
                                for (int x = 0; x <= _envMapWidth; ++x) {
                                    _envMapColCdf[y * (_envMapWidth + 1) + x] /= rowLuminance;
                                }
                            }
                            _envMapTotalLuminance += rowLuminance;
                            _envMapRowCdf[y + 1] = _envMapRowCdf[y] + rowLuminance;
                        }
                        if (_envMapTotalLuminance > 0) {
                            for (int y = 0; y <= _envMapHeight; ++y) {
                                _envMapRowCdf[y] /= _envMapTotalLuminance;
                            }
                        }
                    }
                }
                foundDome = !_envMapPixels.empty();
            }
            break;
        }
    }
    if (!foundDome) {
        _envMapPixels.clear();
        _envMapWidth = _envMapHeight = 0;
        _envMapRowCdf.clear();
        _envMapColCdf.clear();
        _envMapTotalLuminance = 0.0f;
    }

    for (auto const& item : meshes) {
        if (renderThread->IsStopRequested()) return;
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

        MeshInstance inst;
        inst.mesh = mesh;
        inst.transform = mesh->GetTransform();
        inst.invTransform = inst.transform.GetInverse();
        inst.bounds = TransformBounds(meshBounds, inst.transform);
        inst.centroid = (inst.bounds.GetMin() + inst.bounds.GetMax()) * 0.5f;
        _instances.push_back(inst);
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
    _tlasNodes.push_back(TLASNode());
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
    if (count <= 2) {
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

    if (i == start || i == end) i = start + count / 2;

    int leftChildIdx = (int)_tlasNodes.size();
    _tlasNodes.push_back(TLASNode());
    _tlasNodes.push_back(TLASNode());
    node.leftChild = leftChildIdx;
    node.instanceCount = 0;

    _SubdivideTLAS(leftChildIdx, start, i, renderThread);
    _SubdivideTLAS(leftChildIdx + 1, i, end, renderThread);
}

bool HdGeminiRenderer::_IntersectTLAS(int nodeIdx, const GfVec3f& rayOrigin, const GfVec3f& rayDir, HitRecord& hit, HdRenderThread* renderThread) const
{
    if (_tlasNodes.empty() || renderThread->IsStopRequested()) return false;

    const TLASNode& node = _tlasNodes[nodeIdx];
    float tAabb;
    if (!IntersectAABB(rayOrigin, rayDir, node.bounds, tAabb)) return false;
    if (tAabb > hit.t) return false;

    if (node.leftChild < 0) {
        bool wasHit = false;
        int start = -node.leftChild - 1;
        for (int i = 0; i < node.instanceCount; ++i) {
            if (renderThread->IsStopRequested()) return false;
            const auto& inst = _instances[_tlasInstanceIndices[start + i]];
            GfVec3f objRayOrigin = inst.invTransform.Transform(rayOrigin);
            GfVec3f objRayDir = inst.invTransform.TransformDir(rayDir);
            float instT = hit.t;
            GfVec3f instNormal;
            if (inst.mesh->GetBVH().Intersect(objRayOrigin, objRayDir, instT, instNormal)) {
                if (instT < hit.t) {
                    hit.t = instT;
                    hit.normal = inst.transform.TransformDir(instNormal).GetNormalized();
                    hit.baseColor = GfVec3f(1.0f);
                    const VtVec3fArray& colors = inst.mesh->GetColors();
                    if (!colors.empty()) hit.baseColor = colors[0];
                    hit.hit = true;
                    wasHit = true;
                }
            }
        }
        return wasHit;
    } else {
        bool hitLeft = _IntersectTLAS(node.leftChild, rayOrigin, rayDir, hit, renderThread);
        bool hitRight = _IntersectTLAS(node.leftChild + 1, rayOrigin, rayDir, hit, renderThread);
        return hitLeft || hitRight;
    }
}

GfVec3f HdGeminiRenderer::_SampleEnvironment(const GfVec3f& rayDir, const std::map<SdfPath, HdGeminiLight*>& lights) const
{
    if (_envMapPixels.empty()) {
        return GfVec3f(0.0f);
    }
    float theta = std::acos(std::clamp(rayDir[1], -1.0f, 1.0f));
    float phi = std::atan2(rayDir[2], rayDir[0]);
    if (phi < 0) phi += 2.0f * M_PI;
    float u = phi / (2.0f * M_PI);
    float v = theta / M_PI;
    int x = std::clamp((int)(u * _envMapWidth), 0, _envMapWidth - 1);
    int y = std::clamp((int)(v * _envMapHeight), 0, _envMapHeight - 1);
    size_t idx = (y * _envMapWidth + x) * 3;
    GfVec3f color(_envMapPixels[idx], _envMapPixels[idx+1], _envMapPixels[idx+2]);
    for (const auto& lightPair : lights) {
        HdGeminiLight* light = lightPair.second;
        if (light->GetLightType() == HdPrimTypeTokens->domeLight) {
            return GfCompMult(color, light->GetColor()) * light->GetIntensity();
        }
    }
    return color;
}

GfVec3f HdGeminiRenderer::_TraceRay(const GfVec3f& rayOrigin, const GfVec3f& rayDir, int depth, bool isInteractive, HdRenderThread* renderThread, const std::map<SdfPath, HdGeminiLight*>& lights, uint32_t& rng) const
{
    if (depth > (isInteractive ? 0 : 3) || renderThread->IsStopRequested()) return GfVec3f(0.0f);

    HitRecord hit;
    if (!this->_IntersectTLAS(0, rayOrigin, rayDir, hit, renderThread)) {
        return _SampleEnvironment(rayDir, lights);
    }

    GfVec3f hitPos = rayOrigin + rayDir * hit.t;
    GfVec3f shadowOrigin = hitPos + hit.normal * 1e-4f;
    GfVec3f result(0.0f);

    if (!lights.empty()) {
        auto it = lights.begin();
        std::advance(it, (size_t)(RandomFloat(rng) * lights.size()));
        HdGeminiLight* light = it->second;
        GfVec3f lDir;
        float lightDist = 1e30f;
        float lightPdf = 1.0f / (float)lights.size();
        GfVec3f lColor(0.0f);

        if (light->GetLightType() == HdPrimTypeTokens->distantLight) {
            lDir = GfMatrix4f(light->GetTransform()).TransformDir(GfVec3f(0, 0, -1)).GetNormalized();
            lColor = light->GetColor() * light->GetIntensity();
        } else if (light->GetLightType() == HdPrimTypeTokens->domeLight && !_envMapRowCdf.empty()) {
            float u1 = RandomFloat(rng);
            float u2 = RandomFloat(rng);
            auto itY = std::lower_bound(_envMapRowCdf.begin(), _envMapRowCdf.end(), u1);
            int y = std::clamp((int)std::distance(_envMapRowCdf.begin(), itY) - 1, 0, _envMapHeight - 1);
            const float* colCdf = &_envMapColCdf[y * (_envMapWidth + 1)];
            auto itX = std::lower_bound(colCdf, colCdf + _envMapWidth + 1, u2);
            int x = std::clamp((int)std::distance(colCdf, itX) - 1, 0, _envMapWidth - 1);
            float theta = M_PI * (float)(y + 0.5f) / (float)_envMapHeight;
            float phi = 2.0f * M_PI * (float)(x + 0.5f) / (float)_envMapWidth;
            lDir = GfVec3f(std::sin(theta) * std::cos(phi), std::cos(theta), std::sin(theta) * std::sin(phi));
            size_t idx = (y * _envMapWidth + x) * 3;
            GfVec3f texColor(_envMapPixels[idx], _envMapPixels[idx+1], _envMapPixels[idx+2]);
            lColor = GfCompMult(texColor, light->GetColor()) * light->GetIntensity();
            if (_envMapTotalLuminance > 0) {
                float lum = 0.2126f * texColor[0] + 0.7152f * texColor[1] + 0.0722f * texColor[2];
                float pdf = lum / (_envMapTotalLuminance * (M_PI / (float)_envMapHeight) * (2.0f * M_PI / (float)_envMapWidth));
                lightPdf *= std::max(pdf, 1e-6f);
            }
        } else if (light->GetLightType() == HdPrimTypeTokens->rectLight) {
            float u = RandomFloat(rng) - 0.5f;
            float v = RandomFloat(rng) - 0.5f;
            GfVec3f lPosLocal(u * light->GetWidth(), v * light->GetHeight(), 0.0f);
            GfVec3f lPosWorld = GfMatrix4f(light->GetTransform()).Transform(lPosLocal);
            GfVec3f toLight = lPosWorld - hitPos;
            lightDist = toLight.GetLength();
            lDir = toLight / lightDist;
            float area = light->GetWidth() * light->GetHeight();
            if (area > 0) {
                GfVec3f lNormal = GfMatrix4f(light->GetTransform()).TransformDir(GfVec3f(0, 0, -1)).GetNormalized();
                float cosThetaL = std::max(0.0f, GfDot(lNormal, -lDir));
                if (cosThetaL > 0) {
                    lightPdf *= (lightDist * lightDist) / (area * cosThetaL);
                    lColor = light->GetColor() * light->GetIntensity();
                } else {
                    lightDist = -1.0f; // mark as skipped
                }
            }
        } else {
            GfVec3f lPos = GfMatrix4f(light->GetTransform()).ExtractTranslation();
            GfVec3f toLight = lPos - hitPos;
            lightDist = toLight.GetLength();
            lDir = toLight / lightDist;
            lColor = light->GetColor() * light->GetIntensity();
        }

        if (lightDist > 0) {
            float nDotL = std::max(0.0f, GfDot(hit.normal, lDir));
            if (nDotL > 0) {
                HitRecord shadowHit;
                shadowHit.t = lightDist - 1e-3f;
                if (!this->_IntersectTLAS(0, shadowOrigin, lDir, shadowHit, renderThread)) {
                    GfVec3f bsdf = hit.baseColor / (float)M_PI;
                    result += GfCompMult(bsdf, lColor) * (nDotL / (lightPdf + 1e-6f));
                }
            }
        }
    }

    if (!isInteractive && depth < 3) {
        GfVec3f bounceDir = AlignToNormal(SampleCosineHemisphere(RandomFloat(rng), RandomFloat(rng)), hit.normal);
        float nDotL = std::max(0.0f, GfDot(hit.normal, bounceDir));
        float pdf = nDotL / (float)M_PI;
        if (pdf > 1e-6f) {
            GfVec3f indirect = _TraceRay(shadowOrigin, bounceDir, depth + 1, isInteractive, renderThread, lights, rng);
            GfVec3f bsdf = hit.baseColor / (float)M_PI;
            result += GfCompMult(bsdf, indirect) * (nDotL / pdf);
        }
    } else if (isInteractive) {
        result += hit.baseColor * 0.2f;
    }
    return result;
}

void
HdGeminiRenderer::_RenderTiles(HdRenderThread *renderThread, HdGeminiRenderDelegate* delegate)
{
    unsigned int width = _dataWindow.GetWidth();
    unsigned int height = _dataWindow.GetHeight();
    if (width == 0 || height == 0 || _aovBindings.empty()) return;
    HdGeminiRenderBuffer* colorBuffer = static_cast<HdGeminiRenderBuffer*>(_aovBindings[0].renderBuffer);
    GfVec3f cameraPosWorld(_inverseViewMatrix.Transform(GfVec3f(0, 0, 0)));
    std::lock_guard<std::mutex> lock(delegate->GetSceneLock());
    const auto& lights = delegate->GetLights();
    int res = _resolutionLevel;
    bool isInteractive = (res > 1);
    const int bucketSize = 16;
    size_t numBucketsX = (width + bucketSize - 1) / bucketSize;
    size_t numBucketsY = (height + bucketSize - 1) / bucketSize;
    size_t numBuckets = numBucketsX * numBucketsY;

    WorkParallelForN(numBuckets, [&](size_t b_start, size_t b_end) {
        for (size_t b = b_start; b < b_end; ++b) {
            if (renderThread->IsStopRequested()) return;
            size_t bx = b % numBucketsX;
            size_t by = b / numBucketsX;
            size_t startX = (bx * bucketSize / res) * res;
            size_t startY = (by * bucketSize / res) * res;
            size_t endX = std::min(startX + bucketSize, (size_t)width);
            size_t endY = std::min(startY + bucketSize, (size_t)height);
            for (size_t y = startY; y < endY; y += res) {
                for (size_t x = startX; x < endX; x += res) {
                    if (renderThread->IsStopRequested()) return;
                    uint32_t rng = (uint32_t)(y * width + x) ^ (uint32_t)(_frameCount * 12345);
                    float ndcX = (2.0f * (x + res * RandomFloat(rng)) / width) - 1.0f;
                    float ndcY = (2.0f * (y + res * RandomFloat(rng)) / height) - 1.0f;
                    GfVec3f nearPlanePointCam(_inverseProjMatrix.Transform(GfVec3f(ndcX, ndcY, -1.0f)));
                    GfVec3f nearPlanePointWorld(_inverseViewMatrix.Transform(nearPlanePointCam));
                    GfVec3f rayDirWorld = (nearPlanePointWorld - cameraPosWorld).GetNormalized();
                    GfVec3f hitColor = _TraceRay(cameraPosWorld, rayDirWorld, 0, isInteractive, renderThread, lights, rng);
                    if (isInteractive) {
                        GfVec4f finalColor(hitColor[0], hitColor[1], hitColor[2], 1.0f);
                        for (int dy = 0; dy < res && y + dy < height; ++dy) {
                            for (int dx = 0; dx < res && x + dx < width; ++dx) {
                                colorBuffer->Write(GfVec3i(x + dx, y + dy, 0), 4, finalColor.data());
                            }
                        }
                    } else {
                        colorBuffer->WriteSample(GfVec3i(x, y, 0), GfVec4f(hitColor[0], hitColor[1], hitColor[2], 1.0f));
                    }
                }
                std::this_thread::yield();
            }
            if (!renderThread->IsStopRequested()) colorBuffer->ResolveBucket(startX, startY, endX, endY);
        }
    });
}

void
HdGeminiRenderer::Clear()
{
    _resolutionLevel = 4;
    _frameCount = 0;
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
    if (clearValue.IsHolding<GfVec4f>()) return clearValue.UncheckedGet<GfVec4f>();
    if (clearValue.IsHolding<GfVec3f>()) {
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
