#include "renderer.h"
#include "renderDelegate.h"
#include "renderBuffer.h"
#include "mesh.h"
#include "instancer.h"
#include "light.h"
#include "material.h"
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

#ifdef HDGEMINI_HAS_OIDN
#include <OpenImageDenoise/oidn.hpp>
#endif

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

static float FresnelDielectric(float cosThetaI, float ior) {
    cosThetaI = std::clamp(cosThetaI, -1.0f, 1.0f);
    float etaI = 1.0f, etaT = ior;
    if (cosThetaI > 0) std::swap(etaI, etaT);
    float sinThetaT = etaI / etaT * std::sqrt(std::max(0.0f, 1.0f - cosThetaI * cosThetaI));
    if (sinThetaT >= 1.0f) return 1.0f;
    float cosThetaT = std::sqrt(std::max(0.0f, 1.0f - sinThetaT * sinThetaT));
    cosThetaI = std::abs(cosThetaI);
    float rParl = ((etaT * cosThetaI) - (etaI * cosThetaT)) / ((etaT * cosThetaI) + (etaI * cosThetaT));
    float rPerp = ((etaI * cosThetaI) - (etaT * cosThetaT)) / ((etaI * cosThetaI) + (etaT * cosThetaT));
    return (rParl * rParl + rPerp * rPerp) / 2.0f;
}

HdGeminiRenderer::HdGeminiRenderer()
    : _viewMatrix(1.0)
    , _projMatrix(1.0)
    , _inverseViewMatrix(1.0)
    , _inverseProjMatrix(1.0)
    , _resolutionLevel(4)
    , _frameCount(0)
{
#ifdef HDGEMINI_HAS_OIDN
    std::cout << "[Gemini] Renderer initialized WITH Open Image Denoise (OIDN) support." << std::endl;
#else
    std::cout << "[Gemini] Renderer initialized WITHOUT Open Image Denoise (OIDN) support." << std::endl;
#endif
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
    _colorBuffer = nullptr;
    _albedoBuffer = nullptr;
    _normalBuffer = nullptr;

    for (auto const& binding : _aovBindings) {
        if (binding.renderBuffer) {
            HdGeminiRenderBuffer* rb = static_cast<HdGeminiRenderBuffer*>(binding.renderBuffer);
            rb->SetConverged(false);
            if (binding.aovName == HdAovTokens->color) _colorBuffer = rb;
            else if (binding.aovName == HdGeminiAovTokens->albedo) _albedoBuffer = rb;
            else if (binding.aovName == HdGeminiAovTokens->normal) _normalBuffer = rb;
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
        
        if (_frameCount >= _targetSampleCount) {
            if (_enableDenoiser) {
                _Denoise();
            }
            _isConverged = true;
            for (auto const& binding : _aovBindings) {
                if (binding.renderBuffer) {
                    static_cast<HdGeminiRenderBuffer*>(binding.renderBuffer)->SetConverged(true);
                }
            }
        }
    }
}

void
HdGeminiRenderer::_PrepareScene(HdRenderThread *renderThread, HdGeminiRenderDelegate* delegate)
{
    _instances.clear();
    _activeLights.clear();
    
    std::lock_guard<std::recursive_mutex> lock(delegate->GetSceneLock());
    const auto& meshes = delegate->GetMeshes();
    const auto& lights = delegate->GetLights();

    bool foundDome = false;
    for (const auto& lightPair : lights) {
        HdGeminiLight* light = lightPair.second;
        _activeLights.push_back(light);
        if (light->GetLightType() == HdPrimTypeTokens->domeLight) {
            if (light->GetTextureFile() != _lastEnvMapPath) {
                HDGEMINI_LOG << "[Gemini]   Found dome light with env map: " << light->GetTextureFile().GetAssetPath() << std::endl;
                SdfAssetPath texPath = light->GetTextureFile();
                if (!texPath.GetAssetPath().empty()) {
                    HioImageSharedPtr image = HioImage::OpenForReading(texPath.GetResolvedPath());
                    if (image) {
                        _envMapWidth = image->GetWidth();
                        _envMapHeight = image->GetHeight();
                        _envMapPixels.assign(_envMapWidth * _envMapHeight * 3, 0.0f);
                        HioImage::StorageSpec spec;
                        spec.format = HioFormatFloat32Vec3;
                        spec.width = _envMapWidth;
                        spec.height = _envMapHeight;
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
            } else {
                foundDome = true;
            }
            break;
        }
    }
    if (!foundDome) {
        if (!_envMapPixels.empty()) HDGEMINI_LOG << "[Gemini]   Dome light removed. Clearing env map." << std::endl;
        _envMapPixels.clear();
        _envMapWidth = _envMapHeight = 0;
        _envMapRowCdf.clear();
        _envMapColCdf.clear();
        _envMapTotalLuminance = 0.0f;
        _lastEnvMapPath = SdfAssetPath();
    }

    for (auto const& item : meshes) {
        if (renderThread->IsStopRequested()) return;
        HdGeminiMesh* mesh = item.second;
        if (!mesh->IsVisible()) continue;

        if (!mesh->GetInstancerId().IsEmpty()) {
            HdGeminiInstancer* instancer = delegate->GetInstancer(mesh->GetInstancerId());
            if (instancer) {
                VtMatrix4dArray transforms = instancer->ComputeInstanceTransforms(mesh->GetId());
                for (const auto& t : transforms) {
                    for (const auto& subset : mesh->GetSubsets()) {
                        MeshInstance inst;
                        inst.mesh = mesh;
                        inst.subset = &subset;
                        inst.material = delegate->GetMaterial(subset.materialId);
                        inst.transform = GfMatrix4f(t) * mesh->GetTransform();
                        inst.invTransform = inst.transform.GetInverse();
                        inst.bounds = TransformBounds(subset.range, inst.transform);
                        inst.centroid = (inst.bounds.GetMin() + inst.bounds.GetMax()) * 0.5f;
                        _instances.push_back(inst);
                    }
                }
            }
            continue;
        }

        for (const auto& subset : mesh->GetSubsets()) {
            MeshInstance inst;
            inst.mesh = mesh;
            inst.subset = &subset;
            inst.material = delegate->GetMaterial(subset.materialId);
            inst.transform = mesh->GetTransform();
            inst.invTransform = inst.transform.GetInverse();
            inst.bounds = TransformBounds(subset.range, inst.transform);
            inst.centroid = (inst.bounds.GetMin() + inst.bounds.GetMax()) * 0.5f;
            _instances.push_back(inst);
        }
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

    int leftChildIdx = (int)_tlasNodes.size();
    _tlasNodes.push_back(TLASNode());
    _tlasNodes.push_back(TLASNode());
    _tlasNodes[nodeIdx].leftChild = leftChildIdx;
    _tlasNodes[nodeIdx].instanceCount = 0;

    _tlasNodes[nodeIdx].bounds.SetEmpty();
    for (int i = start; i < end; ++i) {
        _tlasNodes[nodeIdx].bounds.ExtendBy(_instances[_tlasInstanceIndices[i]].bounds.GetMin());
        _tlasNodes[nodeIdx].bounds.ExtendBy(_instances[_tlasInstanceIndices[i]].bounds.GetMax());
    }

    int count = end - start;
    if (count <= 2) {
        _tlasNodes[nodeIdx].leftChild = -start - 1;
        _tlasNodes[nodeIdx].instanceCount = count;
        return;
    }

    GfVec3f size = _tlasNodes[nodeIdx].bounds.GetSize();
    int axis = 0;
    if (size[1] > size[0]) axis = 1;
    if (size[2] > size[axis]) axis = 2;

    float splitPos = _tlasNodes[nodeIdx].bounds.GetMin()[axis] + size[axis] * 0.5f;

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

    _SubdivideTLAS(leftChildIdx, start, i, renderThread);
    _SubdivideTLAS(leftChildIdx + 1, i, end, renderThread);
}

bool HdGeminiRenderer::_IntersectTLAS(const GfVec3f& rayOrigin, const GfVec3f& rayDir, HitRecord& hit, HdRenderThread* renderThread) const
{
    if (_tlasNodes.empty() || renderThread->IsStopRequested()) return false;

    int stack[64];
    int stackPtr = 0;
    stack[stackPtr++] = 0;

    bool wasHit = false;

    while (stackPtr > 0) {
        if (renderThread->IsStopRequested()) return false;
        int nodeIdx = stack[--stackPtr];
        const TLASNode& node = _tlasNodes[nodeIdx];
        
        float tAabb;
        if (!IntersectAABB(rayOrigin, rayDir, node.bounds, tAabb) || tAabb > hit.t) continue;

        if (node.leftChild < 0) {
            int start = -node.leftChild - 1;
            for (int i = 0; i < node.instanceCount; ++i) {
                const auto& inst = _instances[_tlasInstanceIndices[start + i]];
                GfVec3f objRayOrigin = inst.invTransform.Transform(rayOrigin);
                GfVec3f objRayDir = inst.invTransform.TransformDir(rayDir);
                float instT = hit.t;
                GfVec3f instNormal;
                GfVec2f instUv;
                GfVec3f instSmoothNormal;
                GfVec3f instSmoothColor;
                int matIdx = -1;
                if (inst.subset->bvh.Intersect(objRayOrigin, objRayDir, instT, instNormal, instUv, instSmoothNormal, instSmoothColor, matIdx)) {
                    if (instT < hit.t) {
                        hit.t = instT;
                        hit.normal = inst.transform.TransformDir(instNormal).GetNormalized();
                        hit.smoothNormal = inst.transform.TransformDir(instSmoothNormal).GetNormalized();
                        hit.uv = instUv;
                        hit.baseColor = instSmoothColor; // Use interpolated vertex color
                        
                        if (inst.material) {
                            hit.baseColor = GfCompMult(hit.baseColor, inst.material->GetDiffuseColor());
                            hit.metallic = inst.material->GetMetallic();
                            hit.roughness = inst.material->GetRoughness();
                            hit.opacity = inst.material->GetOpacity();
                            hit.ior = inst.material->GetIor();
                            hit.transmission = inst.material->GetTransmission();
                            hit.transmissionColor = inst.material->GetTransmissionColor();
                            hit.emission = inst.material->GetEmissionColor() * inst.material->GetEmission();
                            hit.diffuseTexture = inst.material->GetDiffuseTexture();
                        }
                        hit.hit = true;
                        wasHit = true;
                    }
                }
            }
        } else {
            stack[stackPtr++] = node.leftChild + 1;
            stack[stackPtr++] = node.leftChild;
        }
    }
    return wasHit;
}

GfVec3f HdGeminiRenderer::_SampleEnvironment(const GfVec3f& rayDir) const
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
    for (const auto& light : _activeLights) {
        if (light->GetLightType() == HdPrimTypeTokens->domeLight) {
            return GfCompMult(color, light->GetColor()) * light->GetIntensity();
        }
    }
    return color;
}

GfVec3f HdGeminiRenderer::_SampleTexture(const SdfAssetPath& path, const GfVec2f& uv) const
{
    if (path.GetAssetPath().empty()) return GfVec3f(1.0f);

    {
        std::lock_guard<std::mutex> lock(_textureMutex);
        auto it = _textureCache.find(path.GetAssetPath());
        if (it != _textureCache.end()) {
            const TextureData& data = it->second;
            if (data.pixels.empty()) return GfVec3f(1.0f);
            return _SampleTextureData(data, uv);
        }
    }

    // Not in cache, load it (hold lock for loading to prevent redundant loads)
    std::lock_guard<std::mutex> lock(_textureMutex);
    
    // Check again in case another thread loaded it while we were waiting for the lock
    auto it = _textureCache.find(path.GetAssetPath());
    if (it != _textureCache.end()) {
        const TextureData& data = it->second;
        if (data.pixels.empty()) return GfVec3f(1.0f);
        return _SampleTextureData(data, uv);
    }

    HDGEMINI_LOG << "[Gemini]   Loading texture: " << path.GetAssetPath() << std::endl;
    HioImageSharedPtr image = HioImage::OpenForReading(path.GetResolvedPath());
    if (!image) {
        HDGEMINI_LOG << "[Gemini]   Failed to open texture: " << path.GetResolvedPath() << std::endl;
        _textureCache[path.GetAssetPath()] = TextureData();
        return GfVec3f(1.0f);
    }

    TextureData data;
    data.width = image->GetWidth();
    data.height = image->GetHeight();
    HDGEMINI_LOG << "[Gemini]   Texture loaded: " << data.width << "x" << data.height << std::endl;
    data.pixels.assign(data.width * data.height * 3, 0.0f);

    HioImage::StorageSpec spec;
    spec.format = HioFormatFloat32Vec3;
    spec.width = data.width;
    spec.height = data.height;
    spec.data = data.pixels.data();
    if (!image->Read(spec)) {
        HDGEMINI_LOG << "[Gemini]   Failed to read texture pixels: " << path.GetAssetPath() << std::endl;
    }

    _textureCache[path.GetAssetPath()] = std::move(data);
    return _SampleTextureData(_textureCache[path.GetAssetPath()], uv);
}

GfVec3f HdGeminiRenderer::_SampleTextureData(const TextureData& data, const GfVec2f& uv) const
{
    if (data.pixels.empty()) return GfVec3f(1.0f);

    float u = uv[0] - std::floor(uv[0]);
    float v = 1.0f - (uv[1] - std::floor(uv[1])); // Flip V for standard image coords

    float px = u * (data.width - 1);
    float py = v * (data.height - 1);
    int x0 = (int)std::floor(px);
    int y0 = (int)std::floor(py);
    int x1 = std::min(x0 + 1, data.width - 1);
    int y1 = std::min(y0 + 1, data.height - 1);
    float fx = px - x0;
    float fy = py - y0;

    auto getPixel = [&](int x, int y) {
        size_t idx = (y * data.width + x) * 3;
        return GfVec3f(data.pixels[idx], data.pixels[idx+1], data.pixels[idx+2]);
    };

    GfVec3f p00 = getPixel(x0, y0);
    GfVec3f p10 = getPixel(x1, y0);
    GfVec3f p01 = getPixel(x0, y1);
    GfVec3f p11 = getPixel(x1, y1);

    return (p00 * (1-fx) + p10 * fx) * (1-fy) + (p01 * (1-fx) + p11 * fx) * fy;
}

static float PowerHeuristic(float f, float g) {
    float f2 = f * f;
    float g2 = g * g;
    return f2 / (f2 + g2);
}

GfVec3f HdGeminiRenderer::_TraceRay(const GfVec3f& rayOrigin, const GfVec3f& rayDir, int depth, bool isInteractive, HdRenderThread* renderThread, uint32_t& rng, GfVec3f* outAlbedo, GfVec3f* outNormal) const
{
    GfVec3f throughput(1.0f);
    GfVec3f totalRadiance(0.0f);
    GfVec3f currentRayOrigin = rayOrigin;
    GfVec3f currentRayDir = rayDir;

    const int maxDepth = isInteractive ? 2 : 8;

    for (int bounce = 0; bounce < maxDepth; ++bounce) {
        if (renderThread->IsStopRequested()) break;

        HitRecord hit;
        if (!this->_IntersectTLAS(currentRayOrigin, currentRayDir, hit, renderThread)) {
            GfVec3f env = _SampleEnvironment(currentRayDir);
            if (bounce == 0 && outAlbedo) *outAlbedo = env;
            totalRadiance += GfCompMult(throughput, env);
            break;
        }

        if (!hit.diffuseTexture.GetAssetPath().empty()) {
            hit.baseColor = GfCompMult(hit.baseColor, _SampleTexture(hit.diffuseTexture, hit.uv));
        }

        if (bounce == 0) {
            if (outAlbedo) *outAlbedo = hit.baseColor;
            if (outNormal) *outNormal = hit.smoothNormal;
        }

        GfVec3f hitPos = currentRayOrigin + currentRayDir * hit.t;

        // Handle transparency/opacity
        if (hit.opacity < 0.999f && RandomFloat(rng) > hit.opacity) {
            currentRayOrigin = hitPos + currentRayDir * 1e-4f;
            bounce--; // Don't count as a bounce
            continue;
        }

        GfVec3f shadingNormal = hit.smoothNormal;
        if (GfDot(shadingNormal, currentRayDir) > 0) shadingNormal = -shadingNormal;

        totalRadiance += GfCompMult(throughput, hit.emission);

        // --- Direct Lighting (Light Sampling) ---
        if (!_activeLights.empty()) {
            size_t lightIdx = std::min((size_t)(RandomFloat(rng) * _activeLights.size()), _activeLights.size() - 1);
            HdGeminiLight* light = _activeLights[lightIdx];
            
            GfVec3f lDir;
            float lightDist = 1e30f;
            float lightPdf = 1.0f / (float)_activeLights.size();
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
                        lightDist = -1.0f;
                    }
                }
            } else {
                GfVec3f lPos = GfMatrix4f(light->GetTransform()).ExtractTranslation();
                GfVec3f toLight = lPos - hitPos;
                lightDist = toLight.GetLength();
                lDir = toLight / lightDist;
                lightPdf *= (lightDist * lightDist);
                lColor = light->GetColor() * light->GetIntensity();
            }

            // Apply shaping parameters (cone angle & softness) for local lights
            if (lightDist > 0 && light->GetLightType() != HdPrimTypeTokens->domeLight && light->GetLightType() != HdPrimTypeTokens->distantLight) {
                float coneAngle = light->GetShapingConeAngle();
                if (coneAngle < 180.0f) {
                    GfVec3f lNormal = GfMatrix4f(light->GetTransform()).TransformDir(GfVec3f(0, 0, -1)).GetNormalized();
                    float cosTheta = GfDot(lNormal, -lDir);
                    float coneAngleRad = coneAngle * (float)(M_PI / 180.0);
                    float cosConeAngle = std::cos(coneAngleRad);

                    if (cosTheta <= cosConeAngle) {
                        lColor = GfVec3f(0.0f);
                    } else {
                        float softness = light->GetShapingConeSoftness();
                        if (softness > 0.0f) {
                            float innerAngleRad = coneAngleRad * (1.0f - softness);
                            float cosInnerAngle = std::cos(innerAngleRad);
                            if (cosTheta < cosInnerAngle) {
                                float factor = (cosTheta - cosConeAngle) / (cosInnerAngle - cosConeAngle);
                                // smoothstep
                                factor = factor * factor * (3.0f - 2.0f * factor);
                                lColor *= factor;
                            }
                        }
                    }
                }
            }

            if (lightDist > 0 && (lColor[0] > 0 || lColor[1] > 0 || lColor[2] > 0)) {
                float nDotL = std::max(0.0f, GfDot(shadingNormal, lDir));
                if (nDotL > 0) {
                    HitRecord shadowHit;
                    shadowHit.t = lightDist - 1e-3f;
                    GfVec3f shadowOrigin = hitPos + shadingNormal * 1e-4f;
                    if (!this->_IntersectTLAS(shadowOrigin, lDir, shadowHit, renderThread)) {
                        GfVec3f bsdf = hit.baseColor / (float)M_PI;
                        totalRadiance += GfCompMult(throughput, GfCompMult(bsdf, lColor)) * (nDotL / (lightPdf + 1e-6f));
                    }
                }
            }
        }

        // --- Indirect Path Selection (BSDF Sampling) ---
        float fresnel = FresnelDielectric(GfDot(currentRayDir, shadingNormal), hit.ior);
        float reflectProb = fresnel;
        if (hit.metallic > 0.0f) reflectProb = std::max(reflectProb, hit.metallic);
        
        float randVal = RandomFloat(rng);

        if (randVal < reflectProb) {
            // Reflection
            GfVec3f reflectDir = (currentRayDir - 2.0f * GfDot(currentRayDir, shadingNormal) * shadingNormal).GetNormalized();
            if (hit.roughness > 0.0f) {
                reflectDir = AlignToNormal(SampleCosineHemisphere(RandomFloat(rng), RandomFloat(rng)), reflectDir);
                if (GfDot(reflectDir, shadingNormal) < 0) reflectDir = (reflectDir - 2.0f * GfDot(reflectDir, shadingNormal) * shadingNormal).GetNormalized();
            }
            currentRayDir = reflectDir;
            currentRayOrigin = hitPos + shadingNormal * 1e-4f;
        } else {
            float remainingProb = (randVal - reflectProb) / (1.0f - reflectProb);
            if (hit.transmission > 1e-6f && remainingProb < hit.transmission) {
                // Refraction
                float etaI = 1.0f, etaT = hit.ior;
                GfVec3f n = shadingNormal;
                float cosThetaI = GfDot(currentRayDir, n);
                if (cosThetaI > 0) {
                    std::swap(etaI, etaT);
                    n = -n;
                    cosThetaI = -cosThetaI;
                }
                float eta = etaI / etaT;
                float k = 1.0f - eta * eta * (1.0f - cosThetaI * cosThetaI);
                if (k >= 0) {
                    GfVec3f refractDir = (eta * currentRayDir - (eta * cosThetaI + std::sqrt(k)) * n).GetNormalized();
                    currentRayDir = refractDir;
                    currentRayOrigin = hitPos - n * 1e-4f;
                    throughput = GfCompMult(throughput, hit.transmissionColor);
                } else {
                    // Total Internal Reflection
                    GfVec3f reflectDir = (currentRayDir - 2.0f * GfDot(currentRayDir, n) * n).GetNormalized();
                    currentRayDir = reflectDir;
                    currentRayOrigin = hitPos + n * 1e-4f;
                }
            } else {
                // Diffuse
                GfVec3f diffuseDir = AlignToNormal(SampleCosineHemisphere(RandomFloat(rng), RandomFloat(rng)), shadingNormal);
                float nDotL = std::max(0.0f, GfDot(shadingNormal, diffuseDir));
                float pdf = nDotL / (float)M_PI;
                if (pdf < 1e-6f) break;
                
                throughput = GfCompMult(throughput, hit.baseColor); 
                currentRayDir = diffuseDir;
                currentRayOrigin = hitPos + shadingNormal * 1e-4f;
            }
        }

        // --- Russian Roulette ---
        if (bounce > 3) {
            float p = std::max(throughput[0], std::max(throughput[1], throughput[2]));
            if (RandomFloat(rng) > p) break;
            throughput /= p;
        }
    }

    return totalRadiance;
}

void
HdGeminiRenderer::_Denoise()
{
#ifdef HDGEMINI_HAS_OIDN
    unsigned int width = _dataWindow.GetWidth();
    unsigned int height = _dataWindow.GetHeight();
    if (width == 0 || height == 0 || !_colorBuffer) return;

    std::cout << "[Gemini] Running OIDN Denoiser on frame " << _frameCount << "..." << std::endl;

    std::vector<float> color, albedo, normal;
    _colorBuffer->GetFloatBuffer(color);
    if (_albedoBuffer) _albedoBuffer->GetFloatBuffer(albedo);
    if (_normalBuffer) _normalBuffer->GetFloatBuffer(normal);

    std::vector<float> output(width * height * 3);

    try {
        oidn::DeviceRef device = oidn::newDevice(oidn::DeviceType::CPU);
        device.commit();

        oidn::FilterRef filter = device.newFilter("RT");
        filter.setImage("color", color.data(), oidn::Format::Float3, width, height);
        if (!albedo.empty()) filter.setImage("albedo", albedo.data(), oidn::Format::Float3, width, height);
        if (!normal.empty()) filter.setImage("normal", normal.data(), oidn::Format::Float3, width, height);
        filter.setImage("output", output.data(), oidn::Format::Float3, width, height);
        filter.set("hdr", true);
        filter.commit();
        filter.execute();

        const char* errorMessage;
        if (device.getError(errorMessage) != oidn::Error::None) {
             std::cerr << "[Gemini] OIDN Error: " << errorMessage << std::endl;
             return;
        }

        // Write back to color buffer
        for (unsigned int y = 0; y < height; ++y) {
            for (unsigned int x = 0; x < width; ++x) {
                size_t idx = (y * width + x) * 3;
                float pixel[4] = { output[idx], output[idx+1], output[idx+2], 1.0f };
                _colorBuffer->Write(GfVec3i(x, y, 0), 4, pixel);
            }
        }
        _colorBuffer->Resolve();

    } catch (std::exception& e) {
        std::cerr << "[Gemini] OIDN Exception: " << e.what() << std::endl;
    }
#endif
}

void
HdGeminiRenderer::_RenderTiles(HdRenderThread *renderThread, HdGeminiRenderDelegate* delegate)
{
    unsigned int width = _dataWindow.GetWidth();
    unsigned int height = _dataWindow.GetHeight();
    if (width == 0 || height == 0 || !_colorBuffer) return;
    
    GfVec3f cameraPosWorld(_inverseViewMatrix.Transform(GfVec3f(0, 0, 0)));
    std::lock_guard<std::recursive_mutex> lock(delegate->GetSceneLock());
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
                    
                    GfVec3f albedo(0.0f), normal(0.0f);
                    GfVec3f hitColor = _TraceRay(cameraPosWorld, rayDirWorld, 0, isInteractive, renderThread, rng, &albedo, &normal);
                    
                    if (isInteractive) {
                        GfVec4f finalColor(hitColor[0], hitColor[1], hitColor[2], 1.0f);
                        for (int dy = 0; dy < res && y + dy < height; ++dy) {
                            for (int dx = 0; dx < res && x + dx < width; ++dx) {
                                _colorBuffer->Write(GfVec3i(x + dx, y + dy, 0), 4, finalColor.data());
                            }
                        }
                    } else {
                        _colorBuffer->WriteSample(GfVec3i(x, y, 0), GfVec4f(hitColor[0], hitColor[1], hitColor[2], 1.0f));
                        if (_albedoBuffer) _albedoBuffer->WriteSample(GfVec3i(x, y, 0), GfVec4f(albedo[0], albedo[1], albedo[2], 1.0f));
                        if (_normalBuffer) _normalBuffer->WriteSample(GfVec3i(x, y, 0), GfVec4f(normal[0], normal[1], normal[2], 1.0f));
                    }
                }
                std::this_thread::yield();
            }
            if (!renderThread->IsStopRequested()) _colorBuffer->ResolveBucket(startX, startY, endX, endY);
            if (!renderThread->IsStopRequested() && _albedoBuffer) _albedoBuffer->ResolveBucket(startX, startY, endX, endY);
            if (!renderThread->IsStopRequested() && _normalBuffer) _normalBuffer->ResolveBucket(startX, startY, endX, endY);
        }
    });
}

void
HdGeminiRenderer::Clear()
{
    _resolutionLevel = 4;
    _frameCount = 0;
    _isConverged = false;
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
