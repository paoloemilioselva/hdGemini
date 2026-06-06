#include "renderer.h"
#include "renderDelegate.h"
#include "renderBuffer.h"
#include "mesh.h"
#include "instancer.h"
#include "light.h"
#include "mesh.h"
#include "curves.h"
#include "material.h"
#include "volume.h"
#include "field.h"
#include "qmc.h"
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
#ifdef HDGEMINI_HAS_NANOVDB
#include <nanovdb/NanoVDB.h>
#endif
#include <cmath>
#include <algorithm>
#include <random>
#include <thread>
#include <chrono>
#include <iomanip>
#include <ctime>

#ifdef _WIN32
#include <windows.h>
#include <dbghelp.h>
#pragma comment(lib, "dbghelp.lib")

static CRITICAL_SECTION _geminiCrashLock;
static bool _geminiCrashLockInit = false;
static volatile LONG _geminiCrashCount = 0;

static void _GeminiPrintStackTrace(PEXCEPTION_POINTERS exInfo) {
    // Only let the first crash print fully; others just note their existence
    LONG crashNum = InterlockedIncrement(&_geminiCrashCount);
    
    EnterCriticalSection(&_geminiCrashLock);
    
    if (crashNum > 1) {
        std::cerr << "[Gemini] Additional crash #" << crashNum 
                  << " on thread " << GetCurrentThreadId()
                  << " at 0x" << std::hex << exInfo->ExceptionRecord->ExceptionAddress 
                  << std::dec << std::endl;
        LeaveCriticalSection(&_geminiCrashLock);
        return;
    }

    HANDLE process = GetCurrentProcess();
    
    SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
    SymInitialize(process, NULL, TRUE);

    std::cerr << "\n[Gemini] *** ACCESS VIOLATION (0xC0000005) ***" << std::endl;
    std::cerr << "[Gemini] Thread ID: " << GetCurrentThreadId() << std::endl;
    std::cerr << "[Gemini] Crash address: 0x" << std::hex 
              << exInfo->ExceptionRecord->ExceptionAddress << std::dec << std::endl;
    
    // Print faulting instruction pointer and what memory was accessed
    if (exInfo->ExceptionRecord->NumberParameters >= 2) {
        ULONG_PTR accessType = exInfo->ExceptionRecord->ExceptionInformation[0];
        ULONG_PTR accessAddr = exInfo->ExceptionRecord->ExceptionInformation[1];
        std::cerr << "[Gemini] " << (accessType == 0 ? "READ" : (accessType == 1 ? "WRITE" : "EXECUTE"))
                  << " access to address: 0x" << std::hex << accessAddr << std::dec << std::endl;
    }

    // Use RtlCaptureStackBackTrace - much more reliable on x64 than StackWalk64
    void* stack[64];
    USHORT frames = CaptureStackBackTrace(0, 64, stack, NULL);
    
    std::cerr << "[Gemini] Stack trace (" << frames << " frames):" << std::endl;

    char symbolBuf[sizeof(SYMBOL_INFO) + 1024];
    SYMBOL_INFO* symbol = (SYMBOL_INFO*)symbolBuf;
    symbol->SizeOfStruct = sizeof(SYMBOL_INFO);
    symbol->MaxNameLen = 1024;

    IMAGEHLP_LINE64 line = {};
    line.SizeOfStruct = sizeof(IMAGEHLP_LINE64);

    for (USHORT i = 0; i < frames; ++i) {
        DWORD64 addr = (DWORD64)stack[i];
        
        // Get module name via VirtualQuery + GetModuleFileName (always works)
        char modulePath[MAX_PATH] = "<unknown>";
        char moduleName[MAX_PATH] = "<unknown>";
        MEMORY_BASIC_INFORMATION mbi = {};
        if (VirtualQuery((void*)addr, &mbi, sizeof(mbi))) {
            HMODULE hMod = (HMODULE)mbi.AllocationBase;
            if (GetModuleFileNameA(hMod, modulePath, MAX_PATH)) {
                // Extract just the filename
                const char* lastSlash = strrchr(modulePath, '\\');
                if (!lastSlash) lastSlash = strrchr(modulePath, '/');
                strncpy(moduleName, lastSlash ? lastSlash + 1 : modulePath, MAX_PATH - 1);
            }
        }

        DWORD64 displacement64 = 0;
        DWORD displacement32 = 0;
        if (SymFromAddr(process, addr, &displacement64, symbol)) {
            if (SymGetLineFromAddr64(process, addr, &displacement32, &line)) {
                std::cerr << "  #" << i << " " << moduleName << "!" << symbol->Name
                          << " [" << line.FileName << ":" << line.LineNumber << "]" << std::endl;
            } else {
                std::cerr << "  #" << i << " " << moduleName << "!" << symbol->Name
                          << " +0x" << std::hex << displacement64 << std::dec << std::endl;
            }
        } else {
            DWORD64 moduleBase = 0;
            if (VirtualQuery((void*)addr, &mbi, sizeof(mbi))) {
                moduleBase = (DWORD64)mbi.AllocationBase;
            }
            std::cerr << "  #" << i << " " << moduleName << " +0x" << std::hex 
                      << (addr - moduleBase) << std::dec << std::endl;
        }
    }
    std::cerr << "[Gemini] *** END STACK TRACE ***\n" << std::endl;
    SymCleanup(process);
    LeaveCriticalSection(&_geminiCrashLock);
}

static LONG WINAPI _GeminiVectoredHandler(PEXCEPTION_POINTERS exInfo) {
    if (exInfo->ExceptionRecord->ExceptionCode == EXCEPTION_ACCESS_VIOLATION) {
        _GeminiPrintStackTrace(exInfo);
        return EXCEPTION_CONTINUE_SEARCH;
    }
    return EXCEPTION_CONTINUE_SEARCH;
}

static bool _geminiVehInstalled = false;
static void _GeminiInstallCrashHandler() {
    if (!_geminiVehInstalled) {
        InitializeCriticalSection(&_geminiCrashLock);
        _geminiCrashLockInit = true;
        AddVectoredExceptionHandler(1, _GeminiVectoredHandler);
        _geminiVehInstalled = true;
    }
}
#endif // _WIN32

#ifdef HDGEMINI_HAS_OIDN
#ifndef SYCL_LANGUAGE_VERSION
#define SYCL_LANGUAGE_VERSION 202001L
#endif
#include <OpenImageDenoise/oidn.hpp>
#endif

#ifndef M_PI
#define M_PI 3.14159265358979323846f
#endif

PXR_NAMESPACE_USING_DIRECTIVE

#define RAY_EPSILON(p) (std::max(1e-4f, std::max({std::abs((p)[0]), std::abs((p)[1]), std::abs((p)[2])}) * 2e-5f))

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

static bool
IntersectAABB2(const GfVec3f& rayOrigin, const GfVec3f& rayDir, const GfRange3f& range, float& tMinHit, float& tMaxHit)
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
    tMaxHit = tmax;
    return true;
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

static GfVec3f SampleGGX(float u1, float u2, float roughness) {
    float alpha = std::max(0.001f, roughness * roughness);
    float alpha2 = alpha * alpha;
    float phi = 2.0f * (float)M_PI * u1;
    float cosTheta = std::sqrt((1.0f - u2) / (1.0f + (alpha2 - 1.0f) * u2));
    float sinTheta = std::sqrt(std::max(0.0f, 1.0f - cosTheta * cosTheta));
    return GfVec3f(sinTheta * std::cos(phi), cosTheta, sinTheta * std::sin(phi));
}

static GfVec3f AlignToNormal(const GfVec3f& sample, const GfVec3f& normal) {
    GfVec3f up = std::abs(normal[1]) < 0.999f ? GfVec3f(0, 1, 0) : GfVec3f(1, 0, 0);
    GfVec3f tangent = GfCross(up, normal).GetNormalized();
    GfVec3f bitangent = GfCross(normal, tangent);
    return sample[0] * tangent + sample[1] * normal + sample[2] * bitangent;
}

static float FresnelDielectric(float cosThetaI, float etaI, float etaT) {
    cosThetaI = std::clamp(cosThetaI, -1.0f, 1.0f);
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
    , _resolutionLevel(_initialResolutionLevel)
    , _frameCount(0)
{
#ifdef _WIN32
    _GeminiInstallCrashHandler();
#endif
#ifdef HDGEMINI_HAS_SYCL
    try {
        auto async_handler = [](sycl::exception_list exceptions) {
            for (std::exception_ptr const& e : exceptions) {
                try {
                    std::rethrow_exception(e);
                } catch (sycl::exception const& ex) {
                    std::cerr << "[Gemini] SYCL Async Exception: " << ex.what() << std::endl;
                }
            }
        };
        _syclQueue = new sycl::queue(sycl::default_selector_v, async_handler);
        _syclDeviceName = _syclQueue->get_device().get_info<sycl::info::device::name>();
        auto t1 = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        std::cout << "[Gemini] [" << std::put_time(std::localtime(&t1), "%T") << "] SYCL queue initialized on: " << _syclDeviceName << std::endl;
    } catch (sycl::exception const& e) {
        auto t2 = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        std::cerr << "[Gemini] [" << std::put_time(std::localtime(&t2), "%T") << "] SYCL initialization failed: " << e.what() << std::endl;
    }
#endif
#ifdef HDGEMINI_HAS_OIDN
    {
        auto t3 = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        std::cout << "[Gemini] [" << std::put_time(std::localtime(&t3), "%T") << "] Renderer initialized WITH Open Image Denoise (OIDN) support." << std::endl;
    }
#else
    {
        auto t4 = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        std::cout << "[Gemini] [" << std::put_time(std::localtime(&t4), "%T") << "] Renderer initialized WITHOUT Open Image Denoise (OIDN) support." << std::endl;
    }
#endif
}

HdGeminiRenderer::~HdGeminiRenderer()
{
#ifdef HDGEMINI_HAS_SYCL
    if (_syclQueue) {
        if (_rayBuffer) sycl::free(_rayBuffer, *_syclQueue);
        if (_hitBuffer) sycl::free(_hitBuffer, *_syclQueue);
        if (_shadowRayBuffer) sycl::free(_shadowRayBuffer, *_syclQueue);
        if (_lightBuffer) sycl::free(_lightBuffer, *_syclQueue);
        if (_usmEnvMapPixels) sycl::free(_usmEnvMapPixels, *_syclQueue);
        if (_usmEnvMapRowCdf) sycl::free(_usmEnvMapRowCdf, *_syclQueue);
        if (_usmEnvMapColCdf) sycl::free(_usmEnvMapColCdf, *_syclQueue);
        delete _syclQueue;
    }
#endif
}

void
HdGeminiRenderer::SetCamera(const GfMatrix4d& viewMatrix, const GfMatrix4d& projMatrix)
{
    _viewMatrix = viewMatrix;
    _projMatrix = projMatrix;
    _inverseViewMatrix = viewMatrix.GetInverse();
    _inverseProjMatrix = projMatrix.GetInverse();
    _frameCount = 0;
    _isConverged = false;
}

void HdGeminiRenderer::SetHydraCameraParams(float fStop, float focalLength, float focusDistance, float iso, float shutterSpeed) {
    _hydraFStop = fStop;
    _hydraFocalLength = focalLength;
    _hydraFocusDistance = focusDistance;
    _hydraIso = iso;
    _hydraShutterSpeed = shutterSpeed;
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
            else if (binding.aovName == HdAovTokens->depth) _depthBuffer = rb;
            else if (binding.aovName == HdGeminiAovTokens->albedo) _albedoBuffer = rb;
            else if (binding.aovName == HdGeminiAovTokens->normal) _normalBuffer = rb;
        }
    }

    _PrepareScene(renderThread, delegate);
    if (renderThread->IsStopRequested()) return;

    auto start_time = std::chrono::high_resolution_clock::now();
    if (!_aiGenerationPending) {
        _RenderTiles(renderThread, delegate);
    }
    auto end_time = std::chrono::high_resolution_clock::now();
    _lastProgressionTimeMs = std::chrono::duration<float, std::milli>(end_time - start_time).count();

    auto now = std::chrono::high_resolution_clock::now();
    float timeSinceLastUpdate = std::chrono::duration<float>(now - _lastStatsUpdateTime).count();
    if (timeSinceLastUpdate > 0.5f) { // Update rays/sec twice a second
        long long currentRays = _rayCount.load(std::memory_order_relaxed);
        _raysPerSecond = (currentRays - _lastRayCount) / timeSinceLastUpdate;
        _lastRayCount = currentRays;
        _lastStatsUpdateTime = now;
    }

    if (renderThread->IsStopRequested()) return;

    if (_enableOnScreenStats) {
        _DrawStats();
    }

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
        
        if (!_aiGenerationPending) {
            _frameCount++;
            if (_targetSampleCount > 0) {
                int currentProgress = (_frameCount * 100) / _targetSampleCount;
                int previousProgress = ((_frameCount - 1) * 100) / _targetSampleCount;
                if ((currentProgress / 10) > (previousProgress / 10)) {
                    std::cout << "ALF_PROGRESS " << (currentProgress / 10) * 10 << "%" << std::endl;
                }
            }
        }
        
        if (_frameCount >= _targetSampleCount && !_aiGenerationPending) {
            if (_enableDenoiser || _enableFireflyFilter || _enableChromaticityBlur) {
                _Denoise();
            }
            if (_enableLensFlare || _chromaticAberration > 0.0f) {
                _ApplyPostProcess();
            }
            if (!_isConverged) {
                _renderEndTime = std::chrono::high_resolution_clock::now();
                
                if (_enableGenAi && !_genAiPrompt.empty()) {
                    FILE* f = fopen("sd_request.bin", "wb");
                    if (f) {
                        int magic = 0x12345678;
                        fwrite(&magic, 4, 1, f);
                        int w = _colorBuffer ? _colorBuffer->GetWidth() : 1;
                        int h = _colorBuffer ? _colorBuffer->GetHeight() : 1;
                        fwrite(&w, 4, 1, f);
                        fwrite(&h, 4, 1, f);
                        int promptLen = (int)_genAiPrompt.length();
                        fwrite(&promptLen, 4, 1, f);
                        fwrite(_genAiPrompt.c_str(), 1, promptLen, f);
                        fwrite(&_genAiStrength, 4, 1, f);
                        if (_colorBuffer) {
                            std::vector<float> avgColor;
                            _colorBuffer->GetAveragedFloatBuffer(avgColor);
                            fwrite(avgColor.data(), 4, w * h * 4, f);
                            
                            int hasDepth = _depthBuffer ? 1 : 0;
                            int hasNormal = _normalBuffer ? 1 : 0;
                            int hasAlbedo = _albedoBuffer ? 1 : 0;
                            
                            fwrite(&hasDepth, 4, 1, f);
                            fwrite(&hasNormal, 4, 1, f);
                            fwrite(&hasAlbedo, 4, 1, f);
                            
                            if (hasDepth) {
                                std::vector<float> avgDepth;
                                _depthBuffer->GetAveragedFloatBuffer(avgDepth);
                                fwrite(avgDepth.data(), 4, w * h * 4, f);
                            }
                            if (hasNormal) {
                                std::vector<float> avgNormal;
                                _normalBuffer->GetAveragedFloatBuffer(avgNormal);
                                fwrite(avgNormal.data(), 4, w * h * 4, f);
                            }
                            if (hasAlbedo) {
                                std::vector<float> avgAlbedo;
                                _albedoBuffer->GetAveragedFloatBuffer(avgAlbedo);
                                fwrite(avgAlbedo.data(), 4, w * h * 4, f);
                            }
                        }
                        fclose(f);
                        _aiGenerationPending = true;
                    }
                }
                
                if (!_aiGenerationPending) {
                    _isConverged = true;
                    for (auto const& binding : _aovBindings) {
                        if (binding.renderBuffer) {
                            static_cast<HdGeminiRenderBuffer*>(binding.renderBuffer)->SetConverged(true);
                        }
                    }
                }
            }
        }
        
        // ALWAYS check for AI response, even if we didn't just request it!
        // This allows the standalone Python UI to push updates manually.
        FILE* f = fopen("sd_response.bin", "rb");
        if (f) {
            unsigned int magic = 0;
            if (fread(&magic, 4, 1, f) == 1 && magic == 0x87654321) {
                if (_colorBuffer && _colorBuffer->GetFloatBufferPointer()) {
                    int w = _colorBuffer->GetWidth();
                    int h = _colorBuffer->GetHeight();
                    std::vector<float> tempBuf(w * h * 4);
                    if (fread(tempBuf.data(), 4, w * h * 4, f) == (size_t)(w * h * 4)) {
                        _colorBuffer->SetFromFloatBuffer(tempBuf.data());
                    }
                }
                fclose(f);
                std::remove("sd_response.bin");
                _aiGenerationPending = false;
                _isConverged = true;
                for (auto const& binding : _aovBindings) {
                    if (binding.renderBuffer) {
                        static_cast<HdGeminiRenderBuffer*>(binding.renderBuffer)->SetConverged(true);
                    }
                }
            } else {
                fclose(f);
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
    
    _hasDomeLight = foundDome;

    _lightPowerCdf.clear();
    _lightPowerTotal = 0.0f;
    for (const auto& light : _activeLights) {
        float power = 0.0f;
        if (light->GetLightType() == HdPrimTypeTokens->distantLight) {
            power = light->GetIntensity() * std::max({light->GetColor()[0], light->GetColor()[1], light->GetColor()[2]}) * 1000.0f;
        } else if (light->GetLightType() == HdPrimTypeTokens->sphereLight) {
            GfMatrix4f transform = GfMatrix4f(light->GetTransform());
            float scale = transform.TransformDir(GfVec3f(1, 0, 0)).GetLength();
            float r = light->GetRadius() * scale;
            power = light->GetIntensity() * std::max({light->GetColor()[0], light->GetColor()[1], light->GetColor()[2]}) * (4.0f * (float)M_PI * r * r);
        } else if (light->GetLightType() == HdPrimTypeTokens->diskLight) {
            GfMatrix4f transform = GfMatrix4f(light->GetTransform());
            float scale = transform.TransformDir(GfVec3f(1, 0, 0)).GetLength();
            float r = light->GetRadius() * scale;
            power = light->GetIntensity() * std::max({light->GetColor()[0], light->GetColor()[1], light->GetColor()[2]}) * ((float)M_PI * r * r);
        } else if (light->GetLightType() == HdPrimTypeTokens->cylinderLight) {
            GfMatrix4f transform = GfMatrix4f(light->GetTransform());
            float scaleR = transform.TransformDir(GfVec3f(1, 0, 0)).GetLength();
            float scaleL = transform.TransformDir(GfVec3f(0, 0, 1)).GetLength();
            float r = light->GetRadius() * scaleR;
            float l = light->GetLength() * scaleL;
            power = light->GetIntensity() * std::max({light->GetColor()[0], light->GetColor()[1], light->GetColor()[2]}) * (2.0f * (float)M_PI * r * l);
        } else if (light->GetLightType() == HdPrimTypeTokens->rectLight) {
            GfMatrix4f transform = GfMatrix4f(light->GetTransform());
            float scaleX = transform.TransformDir(GfVec3f(1, 0, 0)).GetLength();
            float scaleY = transform.TransformDir(GfVec3f(0, 1, 0)).GetLength();
            float area = (light->GetWidth() * scaleX) * (light->GetHeight() * scaleY);
            power = light->GetIntensity() * std::max({light->GetColor()[0], light->GetColor()[1], light->GetColor()[2]}) * area;
        } else if (light->GetLightType() == HdPrimTypeTokens->domeLight) {
            power = light->GetIntensity() * std::max({light->GetColor()[0], light->GetColor()[1], light->GetColor()[2]}) * 10000.0f;
        }
        _lightPowerTotal += std::max(power, 1e-4f);
        _lightPowerCdf.push_back(_lightPowerTotal);
    }
    if (_lightPowerTotal > 0.0f) {
        for (auto& v : _lightPowerCdf) v /= _lightPowerTotal;
    }

    for (auto const& item : meshes) {
        if (renderThread->IsStopRequested()) return;
        HdGeminiMesh* mesh = item.second;
        if (!mesh->IsVisible()) continue;
        
        if (mesh->IsOcean()) {
            GfMatrix4f viewProj(GfMatrix4f(_viewMatrix) * GfMatrix4f(_projMatrix));
            GfVec3f camPos(GfVec3f(_viewMatrix.GetInverse().ExtractTranslation()));
            mesh->UpdateOcean(viewProj, camPos, _time);
        }

        if (!mesh->GetInstancerId().IsEmpty()) {
            HdGeminiInstancer* instancer = delegate->GetInstancer(mesh->GetInstancerId());
            if (instancer) {
                VtMatrix4dArray transforms = instancer->ComputeInstanceTransforms(mesh->GetId());
                for (const auto& t : transforms) {
                    for (const auto& subset : mesh->GetSubsets()) {
                        SceneInstance inst;
                        inst.type = SceneInstance::Type::Mesh;
                        inst.mesh = mesh;
                        inst.subset = &subset;
                        inst.material = delegate->GetMaterial(subset.materialId);
                        inst.transform = mesh->GetTransform() * GfMatrix4f(t);
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
            SceneInstance inst;
            inst.type = SceneInstance::Type::Mesh;
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

    const auto& basisCurves = delegate->GetBasisCurves();
    for (auto const& item : basisCurves) {
        if (renderThread->IsStopRequested()) return;
        HdGeminiBasisCurves* curves = item.second;
        if (!curves->IsVisible()) continue;

        if (!curves->GetInstancerId().IsEmpty()) {
            HdGeminiInstancer* instancer = delegate->GetInstancer(curves->GetInstancerId());
            if (instancer) {
                VtMatrix4dArray transforms = instancer->ComputeInstanceTransforms(curves->GetId());
                for (const auto& t : transforms) {
                    for (const auto& subset : curves->GetSubsets()) {
                        SceneInstance inst;
                        inst.type = SceneInstance::Type::BasisCurves;
                        inst.curves = curves;
                        inst.curveSubset = &subset;
                        inst.material = delegate->GetMaterial(subset.materialId);
                        inst.transform = curves->GetTransform() * GfMatrix4f(t);
                        inst.invTransform = inst.transform.GetInverse();
                        inst.bounds = TransformBounds(subset.range, inst.transform);
                        inst.centroid = (inst.bounds.GetMin() + inst.bounds.GetMax()) * 0.5f;
                        _instances.push_back(inst);
                    }
                }
            }
            continue;
        }

        for (const auto& subset : curves->GetSubsets()) {
            SceneInstance inst;
            inst.type = SceneInstance::Type::BasisCurves;
            inst.curves = curves;
            inst.curveSubset = &subset;
            inst.material = delegate->GetMaterial(subset.materialId);
            inst.transform = curves->GetTransform();
            inst.invTransform = inst.transform.GetInverse();
            inst.bounds = TransformBounds(subset.range, inst.transform);
            inst.centroid = (inst.bounds.GetMin() + inst.bounds.GetMax()) * 0.5f;
            _instances.push_back(inst);
        }
    }

    const auto& volumes = delegate->GetVolumes();
    for (auto const& item : volumes) {
        if (renderThread->IsStopRequested()) return;
        HdGeminiVolume* volume = item.second;
        
        SceneInstance inst;
        inst.type = SceneInstance::Type::Volume;
        inst.volume = volume;
        inst.transform = GfMatrix4f(volume->GetTransform());
        inst.invTransform = inst.transform.GetInverse();
        GfRange3d extents = volume->GetExtents();
        GfRange3f extents3f(GfVec3f(extents.GetMin()), GfVec3f(extents.GetMax()));
        inst.bounds = TransformBounds(extents3f, inst.transform);
        inst.centroid = (inst.bounds.GetMin() + inst.bounds.GetMax()) * 0.5f;
        
        // Grab the density grid
        HdGeminiField* densityField = volume->GetField(TfToken("density"));
        if (densityField) {
#ifdef HDGEMINI_HAS_NANOVDB
            inst.densityGrid = densityField->GetNanoVDBGrid();
#endif
        }
        
        _instances.push_back(inst);
    }
    
    if (_oceanEnable) {
        bool rebuildTopology = false;
        if (!_globalOcean) {
            _globalOcean = std::make_unique<HdGeminiOcean>();
            _oceanParams.waterHeight = _oceanWaterHeight;
            _oceanParams.metersPerUnit = _metersPerUnit;
            _globalOcean->Init(_oceanParams);
            rebuildTopology = true;
        } else if (_globalOcean->GetParams() != _oceanParams || _oceanParams.waterHeight != _oceanWaterHeight || _oceanParams.metersPerUnit != _metersPerUnit) {
            _oceanParams.waterHeight = _oceanWaterHeight;
            _oceanParams.metersPerUnit = _metersPerUnit;
            _globalOcean->Init(_oceanParams);
            rebuildTopology = true;
        }
        
        _globalOcean->Update(_time);
        
        static GfMatrix4d lastOceanView = _viewMatrix;
        static GfMatrix4d lastOceanProj = _projMatrix;
        if (_oceanParams.continuousDicing) {
            if (lastOceanView != _viewMatrix || lastOceanProj != _projMatrix) {
                rebuildTopology = true;
            }
        }
        
        if (rebuildTopology || _globalOceanBasePoints.empty()) {
            _globalOceanBasePoints.clear();
            _globalOceanIndices.clear();
            _globalOceanUvs.clear();
            _globalOcean->GenerateGridTopology(_viewMatrix, _projMatrix, _lastWidth, _lastHeight, _globalOceanBasePoints, _globalOceanIndices, _globalOceanUvs, _globalOceanColors);
            lastOceanView = _viewMatrix;
            lastOceanProj = _projMatrix;
        }

        std::vector<GfVec3f> points, normals, colors;
        GfVec3f camPos(GfVec3f(_viewMatrix.GetInverse().ExtractTranslation()));
        _globalOcean->DisplaceGrid(_globalOceanBasePoints, _globalOceanColors, camPos, points, normals, colors);
        
        if (!_globalOceanBvh) _globalOceanBvh = std::make_unique<BVH>();
        VtVec3fArray vtPoints(points.begin(), points.end());
        VtVec3iArray vtIndices(_globalOceanIndices.begin(), _globalOceanIndices.end());
        VtVec2fArray vtUvs(_globalOceanUvs.begin(), _globalOceanUvs.end());
        VtVec3fArray vtNormals(normals.begin(), normals.end());
        VtVec3fArray vtColors(colors.begin(), colors.end());
        std::vector<int> matIndices(_globalOceanIndices.size(), -1);
        _globalOceanBvh->Build(vtPoints, vtIndices, vtUvs, vtNormals, vtColors, matIndices);
        
        SceneInstance inst;
        inst.type = SceneInstance::Type::Ocean;
        inst.dynamicBvh = _globalOceanBvh.get();
        inst.transform = GfMatrix4f(1.0f);
        inst.invTransform = GfMatrix4f(1.0f);
        inst.bounds = inst.dynamicBvh->GetBounds();
        inst.centroid = (inst.bounds.GetMin() + inst.bounds.GetMax()) * 0.5f;
        _instances.push_back(inst);
    }

    _BuildTLAS(renderThread);
#ifdef HDGEMINI_HAS_SYCL
    if (_syclQueue) {
        size_t numLights = _activeLights.size();
        if (_lightBufferSize < numLights) {
            if (_lightBuffer) sycl::free(_lightBuffer, *_syclQueue);
            _lightBuffer = sycl::malloc_shared<SYCLLightData>(numLights, *_syclQueue);
            _lightBufferSize = numLights;
        }
        _numActiveLights = numLights;
        _hasDomeLight = false;
        
        for (size_t i = 0; i < numLights; ++i) {
            HdGeminiLight* l = _activeLights[i];
            SYCLLightData& sd = _lightBuffer[i];
            
            if (l->GetLightType() == HdPrimTypeTokens->distantLight) sd.type = 1;
            else if (l->GetLightType() == HdPrimTypeTokens->domeLight) { sd.type = 2; _hasDomeLight = true; }
            else if (l->GetLightType() == HdPrimTypeTokens->rectLight) sd.type = 3;
            else sd.type = 4;
            
            const double* tr = l->GetTransform().GetArray();
            for(int j=0; j<16; ++j) sd.transform[j] = (float)tr[j];
            
            GfVec3f c = l->GetColor();
            sd.color[0] = c[0]; sd.color[1] = c[1]; sd.color[2] = c[2];
            sd.intensity = l->GetIntensity();
            sd.width = l->GetWidth();
            sd.height = l->GetHeight();
            sd.shapingConeAngle = l->GetShapingConeAngle();
            sd.shapingConeSoftness = l->GetShapingConeSoftness();
        }

        if (foundDome && !_envMapPixels.empty()) {
            if (_usmEnvMapPixelsSize < _envMapPixels.size()) {
                if (_usmEnvMapPixels) sycl::free(_usmEnvMapPixels, *_syclQueue);
                _usmEnvMapPixels = sycl::malloc_shared<float>(_envMapPixels.size(), *_syclQueue);
                _usmEnvMapPixelsSize = _envMapPixels.size();
            }
            std::copy(_envMapPixels.begin(), _envMapPixels.end(), _usmEnvMapPixels);
            
            if (_usmEnvMapRowCdfSize < _envMapRowCdf.size()) {
                if (_usmEnvMapRowCdf) sycl::free(_usmEnvMapRowCdf, *_syclQueue);
                _usmEnvMapRowCdf = sycl::malloc_shared<float>(_envMapRowCdf.size(), *_syclQueue);
                _usmEnvMapRowCdfSize = _envMapRowCdf.size();
            }
            std::copy(_envMapRowCdf.begin(), _envMapRowCdf.end(), _usmEnvMapRowCdf);

            if (_usmEnvMapColCdfSize < _envMapColCdf.size()) {
                if (_usmEnvMapColCdf) sycl::free(_usmEnvMapColCdf, *_syclQueue);
                _usmEnvMapColCdf = sycl::malloc_shared<float>(_envMapColCdf.size(), *_syclQueue);
                _usmEnvMapColCdfSize = _envMapColCdf.size();
            }
            std::copy(_envMapColCdf.begin(), _envMapColCdf.end(), _usmEnvMapColCdf);
            std::copy(_envMapColCdf.begin(), _envMapColCdf.end(), _usmEnvMapColCdf);
        }
    }
#endif

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

bool HdGeminiRenderer::_IntersectTLAS(const GfVec3f& rayOrigin, const GfVec3f& rayDir, HitRecord& hit, HdRenderThread* renderThread, uint32_t sampleIdx, uint32_t& qmcDim, uint32_t& rng) const
{
    _rayCount.fetch_add(1, std::memory_order_relaxed);
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
                
                if (inst.type == SceneInstance::Type::Volume) {
#ifdef HDGEMINI_HAS_NANOVDB
                    if (inst.densityGrid) {
                        float tMinAabb, tMaxAabb;
                        if (IntersectAABB2(objRayOrigin, objRayDir, inst.bounds, tMinAabb, tMaxAabb)) {
                            float t = std::max(0.0f, tMinAabb);
                            float step = _volumeStepSize;
                            const nanovdb::FloatGrid* grid = static_cast<const nanovdb::FloatGrid*>(inst.densityGrid);
                            auto acc = grid->getAccessor();
                            
                            while (t < tMaxAabb && t < hit.t) {
                                t += step;
                                if (t >= tMaxAabb || t >= hit.t) break;
                                
                                GfVec3f pos = objRayOrigin + objRayDir * t;
                                nanovdb::Vec3f ipos = grid->worldToIndexF(nanovdb::Vec3f(pos[0], pos[1], pos[2]));
                                nanovdb::Coord coord((int)std::floor(ipos[0]), (int)std::floor(ipos[1]), (int)std::floor(ipos[2]));
                                float density = acc.getValue(coord);
                                float sigma_t = density * _volumeDensityScale;
                                
                                if (sigma_t > 0.0f) {
                                    float p_scatter = 1.0f - std::exp(-sigma_t * step);
                                    if (qmc::SampleDimension(sampleIdx, qmcDim++, rng) < p_scatter) {
                                        hit.t = t;
                                        hit.isVolumeHit = true;
                                        hit.densityGrid = inst.densityGrid;
                                        hit.normal = GfVec3f(0.0f); // Volumes have no normal
                                        hit.smoothNormal = GfVec3f(0.0f);
                                        wasHit = true;
                                        break;
                                    }
                                }
                            }
                        }
                    }
#endif
                } else if (inst.type == SceneInstance::Type::Mesh) {
                    GfVec3f instNormal;
                    GfVec2f instUv;
                    GfVec3f instSmoothNormal;
                    GfVec3f instDpdu, instDpdv;
                    GfVec3f instSmoothColor;
                    int matIdx = -1;
                    if (inst.subset->bvh.Intersect(objRayOrigin, objRayDir, instT, instNormal, instUv, instSmoothNormal, instDpdu, instDpdv, instSmoothColor, matIdx)) {
                    if (instT < hit.t) {
                        hit.t = instT;
                        hit.hitMesh = inst.mesh;
                        GfMatrix4f invTransp = inst.invTransform.GetTranspose();
                        hit.normal = invTransp.TransformDir(instNormal).GetNormalized();
                        hit.smoothNormal = invTransp.TransformDir(instSmoothNormal).GetNormalized();
                        hit.dpdu = inst.transform.TransformDir(instDpdu).GetNormalized();
                        hit.dpdv = inst.transform.TransformDir(instDpdv).GetNormalized();
                        hit.uv = instUv;
                        hit.baseColor = instSmoothColor; // Use interpolated vertex color
                        
                        if (inst.material && !inst.material->GetId().IsEmpty()) {
                            hit.baseColor = inst.material->GetDiffuseColor();
                            hit.metallic = inst.material->GetMetallic();
                            hit.roughness = inst.material->GetRoughness();
                            hit.specularColor = inst.material->GetSpecularColor();
                            hit.specular = inst.material->GetSpecular();
                            hit.opacity = inst.material->GetOpacity();
                            hit.ior = inst.material->GetIor();
                            
                            hit.transmission = inst.material->GetTransmission();
                            hit.transmissionColor = inst.material->GetTransmissionColor();
                            hit.emission = inst.material->GetEmissionColor() * inst.material->GetEmission();
                            hit.diffuseTexture = &inst.material->GetDiffuseTexture();
                            hit.normalTexture = &inst.material->GetNormalTexture();
                            hit.metallicTexture = &inst.material->GetMetallicTexture();
                            hit.roughnessTexture = &inst.material->GetRoughnessTexture();
                            hit.opacityTexture = &inst.material->GetOpacityTexture();
                            hit.transmissionTexture = &inst.material->GetTransmissionTexture();
                            hit.metallicTextureChannel = inst.material->GetMetallicTextureChannel();
                            hit.roughnessTextureChannel = inst.material->GetRoughnessTextureChannel();
                            hit.opacityTextureChannel = inst.material->GetOpacityTextureChannel();
                            hit.transmissionTextureChannel = inst.material->GetTransmissionTextureChannel();

                            hit.coat = inst.material->GetCoat();
                            hit.coatColor = inst.material->GetCoatColor();
                            hit.coatRoughness = inst.material->GetCoatRoughness();
                            hit.coatIor = inst.material->GetCoatIor();
                            hit.transmissionDepth = inst.material->GetTransmissionDepth();
                            hit.transmissionScatter = inst.material->GetTransmissionScatter();
                            hit.sheen = inst.material->GetSheen();
                            hit.sheenColor = inst.material->GetSheenColor();
                            hit.sheenRoughness = inst.material->GetSheenRoughness();
                            hit.subsurface = inst.material->GetSubsurface();
                            hit.subsurfaceColor = inst.material->GetSubsurfaceColor();
                            hit.subsurfaceRadius = inst.material->GetSubsurfaceRadius();
                            hit.subsurfaceScale = inst.material->GetSubsurfaceScale();
                            hit.subsurfaceAnisotropy = inst.material->GetSubsurfaceAnisotropy();
                            hit.thinWalled = inst.material->GetThinWalled();
                            hit.diffuseRoughness = inst.material->GetDiffuseRoughness();
                        } else {
                            // Reset material properties to defaults so we don't inherit from a farther hit
                            hit.metallic = 0.0f;
                            hit.roughness = 1.0f;
                            hit.specularColor = GfVec3f(0.0f);
                            hit.specular = 0.0f;
                            hit.opacity = 1.0f;
                            hit.ior = 1.5f;
                            
                            hit.transmission = 0.0f;
                            hit.transmissionColor = GfVec3f(1.0f);
                            hit.emission = GfVec3f(0.0f);
                            hit.diffuseTexture = nullptr;
                            hit.normalTexture = nullptr;
                            hit.metallicTexture = nullptr;
                            hit.roughnessTexture = nullptr;
                            hit.opacityTexture = nullptr;
                            hit.transmissionTexture = nullptr;
                            hit.metallicTextureChannel = 0;
                            hit.roughnessTextureChannel = 0;
                            hit.opacityTextureChannel = 0;
                            hit.transmissionTextureChannel = 0;

                            hit.coat = 0.0f;
                            hit.coatColor = GfVec3f(1.0f);
                            hit.coatRoughness = 0.0f;
                            hit.coatIor = 1.5f;
                            hit.transmissionDepth = 0.0f;
                            hit.transmissionScatter = GfVec3f(0.0f);
                            hit.sheen = 0.0f;
                            hit.sheenColor = GfVec3f(1.0f);
                            hit.sheenRoughness = 0.2f;
                            hit.subsurface = 0.0f;
                            hit.subsurfaceColor = GfVec3f(1.0f);
                            hit.subsurfaceRadius = GfVec3f(1.0f);
                            hit.subsurfaceScale = 1.0f;
                            hit.subsurfaceAnisotropy = 0.0f;
                            hit.thinWalled = false;
                            hit.diffuseRoughness = 0.0f;
                        }
                        hit.hit = true;
                        wasHit = true;
                    }
                }
                } // end else if Mesh
                else if (inst.type == SceneInstance::Type::Ocean) {
                    GfVec3f instNormal;
                    GfVec2f instUv;
                    GfVec3f instSmoothNormal;
                    GfVec3f instDpdu, instDpdv;
                    GfVec3f instSmoothColor;
                    int matIdx = -1;
                    if (inst.dynamicBvh && inst.dynamicBvh->Intersect(objRayOrigin, objRayDir, instT, instNormal, instUv, instSmoothNormal, instDpdu, instDpdv, instSmoothColor, matIdx)) {
                        if (instT < hit.t) {
                            hit.t = instT;
                            GfMatrix4f invTransp = inst.invTransform.GetTranspose();
                            hit.normal = invTransp.TransformDir(instNormal).GetNormalized();
                            hit.smoothNormal = invTransp.TransformDir(instSmoothNormal).GetNormalized();
                            hit.dpdu = inst.transform.TransformDir(instDpdu).GetNormalized();
                            hit.dpdv = inst.transform.TransformDir(instDpdv).GetNormalized();
                            hit.uv = instUv;
                            hit.isOcean = true;
                            
                            if (inst.material && !inst.material->GetId().IsEmpty()) {
                                hit.baseColor = inst.material->GetDiffuseColor();
                                hit.metallic = inst.material->GetMetallic();
                                hit.roughness = inst.material->GetRoughness();
                                hit.specularColor = inst.material->GetSpecularColor();
                                hit.specular = inst.material->GetSpecular();
                                hit.opacity = inst.material->GetOpacity();
                                hit.ior = inst.material->GetIor();
                                hit.transmission = inst.material->GetTransmission();
                                hit.transmissionColor = inst.material->GetTransmissionColor();
                                hit.emission = inst.material->GetEmissionColor() * inst.material->GetEmission();
                                hit.diffuseTexture = &inst.material->GetDiffuseTexture();
                                hit.normalTexture = &inst.material->GetNormalTexture();
                                hit.metallicTexture = &inst.material->GetMetallicTexture();
                                hit.roughnessTexture = &inst.material->GetRoughnessTexture();
                                hit.opacityTexture = &inst.material->GetOpacityTexture();
                                hit.transmissionTexture = &inst.material->GetTransmissionTexture();
                                hit.metallicTextureChannel = inst.material->GetMetallicTextureChannel();
                                hit.roughnessTextureChannel = inst.material->GetRoughnessTextureChannel();
                                hit.opacityTextureChannel = inst.material->GetOpacityTextureChannel();
                                hit.transmissionTextureChannel = inst.material->GetTransmissionTextureChannel();

                                hit.coat = inst.material->GetCoat();
                                hit.coatColor = inst.material->GetCoatColor();
                                hit.coatRoughness = inst.material->GetCoatRoughness();
                                hit.coatIor = inst.material->GetCoatIor();
                                hit.transmissionDepth = inst.material->GetTransmissionDepth();
                                hit.transmissionScatter = inst.material->GetTransmissionScatter();
                                hit.sheen = inst.material->GetSheen();
                                hit.sheenColor = inst.material->GetSheenColor();
                                hit.sheenRoughness = inst.material->GetSheenRoughness();
                                hit.subsurface = inst.material->GetSubsurface();
                                hit.subsurfaceColor = inst.material->GetSubsurfaceColor();
                                hit.subsurfaceRadius = inst.material->GetSubsurfaceRadius();
                                hit.subsurfaceScale = inst.material->GetSubsurfaceScale();
                                hit.subsurfaceAnisotropy = inst.material->GetSubsurfaceAnisotropy();
                                hit.thinWalled = inst.material->GetThinWalled();
                                hit.diffuseRoughness = inst.material->GetDiffuseRoughness();
                            } else {
                                bool isShaderDisabled = inst.mesh ? inst.mesh->GetOceanParams().disableShader : _oceanParams.disableShader;
                                if (isShaderDisabled) {
                                    hit.baseColor = instSmoothColor;
                                    if (instSmoothColor == GfVec3f(0.0f)) hit.baseColor = GfVec3f(0.8f, 0.8f, 0.8f);
                                    hit.metallic = 0.0f;
                                    hit.roughness = 1.0f;
                                    hit.specularColor = GfVec3f(0.0f);
                                    hit.specular = 0.0f;
                                    hit.opacity = 1.0f;
                                    hit.ior = 1.0f;
                                    hit.transmission = 0.0f;
                                    hit.transmissionColor = GfVec3f(0.0f);
                                    hit.emission = GfVec3f(0.0f);
                                    hit.diffuseTexture = nullptr;
                                    hit.normalTexture = nullptr;
                                    hit.metallicTexture = nullptr;
                                    hit.roughnessTexture = nullptr;
                                    hit.opacityTexture = nullptr;
                                    hit.transmissionTexture = nullptr;
                                    hit.metallicTextureChannel = 0;
                                    hit.roughnessTextureChannel = 0;
                                    hit.opacityTextureChannel = 0;
                                    hit.transmissionTextureChannel = 0;
                                    hit.coat = 0.0f;
                                    hit.coatColor = GfVec3f(1.0f);
                                    hit.coatRoughness = 0.0f;
                                    hit.coatIor = 1.5f;
                                    hit.transmissionDepth = 0.0f;
                                    hit.transmissionScatter = GfVec3f(0.0f);
                                    hit.sheen = 0.0f;
                                    hit.sheenColor = GfVec3f(1.0f);
                                    hit.sheenRoughness = 0.2f;
                                    hit.subsurface = 0.0f;
                                    hit.subsurfaceColor = GfVec3f(1.0f);
                                    hit.subsurfaceRadius = GfVec3f(1.0f);
                                    hit.subsurfaceScale = 1.0f;
                                    hit.subsurfaceAnisotropy = 0.0f;
                                    hit.thinWalled = false;
                                    hit.diffuseRoughness = 0.0f;
                                } else {
                                    float foamVis = inst.mesh ? inst.mesh->GetOceanParams().foamVisibility : _oceanParams.foamVisibility;
                                    float foam = std::clamp(instSmoothColor[0] * foamVis, 0.0f, 1.0f);

                                    hit.baseColor = GfVec3f(0.05f, 0.15f, 0.25f);
                                    hit.baseColor = hit.baseColor * (1.0f - foam) + GfVec3f(1.0f) * foam;
                                    hit.metallic = 0.0f;
                                    hit.roughness = 0.02f * (1.0f - foam) + 0.8f * foam;
                                    hit.specularColor = GfVec3f(1.0f);
                                    hit.specular = 1.0f;
                                    hit.opacity = 1.0f;
                                    hit.ior = 1.33f;
                                    hit.transmission = 1.0f - foam * 0.8f;
                                    hit.transmissionColor = GfVec3f(0.8f, 0.9f, 0.95f);
                                    hit.emission = GfVec3f(0.0f);
                                    hit.diffuseTexture = nullptr;
                                    hit.normalTexture = nullptr;
                                    hit.metallicTexture = nullptr;
                                    hit.roughnessTexture = nullptr;
                                    hit.opacityTexture = nullptr;
                                    hit.transmissionTexture = nullptr;
                                    hit.metallicTextureChannel = 0;
                                    hit.roughnessTextureChannel = 0;
                                    hit.opacityTextureChannel = 0;
                                    hit.transmissionTextureChannel = 0;
                                    hit.coat = 1.0f * (1.0f - foam); // Coat on water, not foam
                                    hit.coatColor = GfVec3f(1.0f);
                                    hit.coatRoughness = 0.0f;
                                    hit.coatIor = 1.5f;
                                    hit.transmissionDepth = inst.mesh ? inst.mesh->GetOceanParams().scatteringDepth : _oceanParams.scatteringDepth;
                                    hit.transmissionScatter = inst.mesh ? inst.mesh->GetOceanParams().scatteringColor : _oceanParams.scatteringColor;
                                    hit.sheen = 0.0f;
                                    hit.sheenColor = GfVec3f(1.0f);
                                    hit.sheenRoughness = 0.2f;
                                    hit.subsurface = 0.0f;
                                    hit.subsurfaceColor = GfVec3f(1.0f);
                                    hit.subsurfaceRadius = GfVec3f(1.0f);
                                    hit.subsurfaceScale = 1.0f;
                                    hit.subsurfaceAnisotropy = 0.0f;
                                    hit.thinWalled = false;
                                    hit.diffuseRoughness = 0.0f;
                                }
                            }
                            hit.hit = true;
                            wasHit = true;
                        }
                    }
                } // end else if Ocean
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
    GfVec3f localDir = rayDir;
    const HdGeminiLight* domeLight = nullptr;
    for (const auto& light : _activeLights) {
        if (light->GetLightType() == HdPrimTypeTokens->domeLight) {
            localDir = GfMatrix4f(light->GetTransform()).GetInverse().TransformDir(rayDir).GetNormalized();
            domeLight = light;
            break;
        }
    }
    float theta = std::acos(std::clamp(localDir[1], -1.0f, 1.0f));
    float phi = std::atan2(localDir[0], -localDir[2]);
    if (phi < 0) phi += 2.0f * M_PI;
    float u = phi / (2.0f * M_PI);
    float v = theta / M_PI;
    int x = std::clamp((int)(u * _envMapWidth), 0, _envMapWidth - 1);
    int y = std::clamp((int)(v * _envMapHeight), 0, _envMapHeight - 1);
    size_t idx = (y * _envMapWidth + x) * 3;
    GfVec3f color(_envMapPixels[idx], _envMapPixels[idx+1], _envMapPixels[idx+2]);
    if (domeLight) {
        return GfCompMult(color, domeLight->GetColor()) * domeLight->GetIntensity();
    }
    return color;
}

GfVec4f HdGeminiRenderer::_SampleTexture(const SdfAssetPath& path, const GfVec2f& uv, bool forceLinear) const
{
    if (path.GetAssetPath().empty()) return GfVec4f(1.0f);

    std::string assetPath = path.GetAssetPath();
    std::string resolvedPath = path.GetResolvedPath();

    size_t udimPos = assetPath.find("<UDIM>");
    if (udimPos != std::string::npos) {
        int uIndex = std::clamp((int)std::floor(uv[0]), 0, 9);
        int vIndex = std::max(0, (int)std::floor(uv[1]));
        int udim = 1001 + uIndex + (vIndex * 10);
        std::string udimStr = std::to_string(udim);
        
        assetPath.replace(udimPos, 6, udimStr);
        
        size_t resUdimPos = resolvedPath.find("<UDIM>");
        if (resUdimPos != std::string::npos) {
            resolvedPath.replace(resUdimPos, 6, udimStr);
        }
    }

    std::string cacheKey = assetPath + (forceLinear ? "_lin" : "_srgb");

    {
        std::lock_guard<std::mutex> lock(_textureMutex);
        auto it = _textureCache.find(cacheKey);
        if (it != _textureCache.end()) {
            const TextureData& data = it->second;
            if (data.pixels.empty()) return GfVec4f(1.0f);
            return _SampleTextureData(data, uv);
        }
    }

    // Not in cache, load it (hold lock for loading to prevent redundant loads)
    std::lock_guard<std::mutex> lock(_textureMutex);
    
    // Check again in case another thread loaded it while we were waiting for the lock
    auto it = _textureCache.find(cacheKey);
    if (it != _textureCache.end()) {
        const TextureData& data = it->second;
        if (data.pixels.empty()) return GfVec4f(1.0f);
        return _SampleTextureData(data, uv);
    }

    if (path.GetAssetPath().find("<UDIM>") != std::string::npos) {
        HDGEMINI_LOG << "[Gemini]   UDIM detected! Original: " << path.GetAssetPath() << " -> Resolved: " << assetPath << std::endl;
    } else {
        HDGEMINI_LOG << "[Gemini]   Loading texture: " << assetPath << std::endl;
    }

    HioImageSharedPtr image = HioImage::OpenForReading(resolvedPath.empty() ? assetPath : resolvedPath);
    if (!image) {
        image = HioImage::OpenForReading(assetPath);
    }
    if (!image) {
        HDGEMINI_LOG << "[Gemini]   Failed to open texture: " << (resolvedPath.empty() ? assetPath : resolvedPath) << std::endl;
        _textureCache[cacheKey] = TextureData();
        return GfVec4f(-1.0f);
    }

    TextureData data;
    data.width = image->GetWidth();
    data.height = image->GetHeight();
    HDGEMINI_LOG << "[Gemini]   Texture loaded: " << data.width << "x" << data.height << std::endl;
    data.pixels.assign(data.width * data.height * 4, 0.0f);

    HioFormat format = image->GetFormat();
    int channels = HioGetComponentCount(format);
    if (channels <= 0 || data.width == 0 || data.height == 0) {
        HDGEMINI_LOG << "[Gemini]   Invalid texture channels or dimensions: " << assetPath << std::endl;
        _textureCache[cacheKey] = TextureData();
        return GfVec4f(-1.0f);
    }
    bool isFloat = (format == HioFormatFloat32 || format == HioFormatFloat32Vec2 || format == HioFormatFloat32Vec3 || format == HioFormatFloat32Vec4 ||
                    format == HioFormatFloat16 || format == HioFormatFloat16Vec2 || format == HioFormatFloat16Vec3 || format == HioFormatFloat16Vec4);
    bool isOriginalSrgb = (format == HioFormatUNorm8srgb || format == HioFormatUNorm8Vec2srgb || format == HioFormatUNorm8Vec3srgb || format == HioFormatUNorm8Vec4srgb);
    bool applySrgb = isOriginalSrgb && !forceLinear;

    HioImage::StorageSpec spec;
    spec.format = format;
    spec.width = data.width;
    spec.height = data.height;

    if (isFloat) {
        // Just read as Float32 regardless of original float type (HioImage converts internally for floats usually, but let's be safe and read as Float32)
        spec.format = channels == 4 ? HioFormatFloat32Vec4 : (channels == 3 ? HioFormatFloat32Vec3 : (channels == 2 ? HioFormatFloat32Vec2 : HioFormatFloat32));
        std::vector<float> rawData(data.width * data.height * channels);
        spec.data = rawData.data();
        if (!image->Read(spec)) {
            HDGEMINI_LOG << "[Gemini]   Failed to read texture pixels (float): " << path.GetAssetPath() << std::endl;
        } else {
            for (int i = 0; i < data.width * data.height; ++i) {
                data.pixels[i*4+0] = rawData[i*channels+0];
                data.pixels[i*4+1] = channels > 1 ? rawData[i*channels+1] : rawData[i*channels+0];
                data.pixels[i*4+2] = channels > 2 ? rawData[i*channels+2] : rawData[i*channels+0];
                data.pixels[i*4+3] = channels > 3 ? rawData[i*channels+3] : 1.0f;
            }
        }
    } else {
        // Read as UNorm8 using original format
        spec.format = channels == 4 ? (isOriginalSrgb ? HioFormatUNorm8Vec4srgb : HioFormatUNorm8Vec4) :
                      (channels == 3 ? (isOriginalSrgb ? HioFormatUNorm8Vec3srgb : HioFormatUNorm8Vec3) :
                      (channels == 2 ? (isOriginalSrgb ? HioFormatUNorm8Vec2srgb : HioFormatUNorm8Vec2) : 
                      (isOriginalSrgb ? HioFormatUNorm8srgb : HioFormatUNorm8)));
                      
        std::vector<unsigned char> rawData(data.width * data.height * channels);
        spec.data = rawData.data();
        if (!image->Read(spec)) {
            HDGEMINI_LOG << "[Gemini]   Failed to read texture pixels (unorm8): " << path.GetAssetPath() << std::endl;
        } else {
            for (int i = 0; i < data.width * data.height; ++i) {
                float r = rawData[i*channels+0] / 255.0f;
                float g = channels > 1 ? rawData[i*channels+1] / 255.0f : r;
                float b = channels > 2 ? rawData[i*channels+2] / 255.0f : r;
                if (applySrgb) {
                    r = std::pow(r, 2.2f);
                    g = std::pow(g, 2.2f);
                    b = std::pow(b, 2.2f);
                }
                data.pixels[i*4+0] = r;
                data.pixels[i*4+1] = g;
                data.pixels[i*4+2] = b;
                data.pixels[i*4+3] = channels > 3 ? rawData[i*channels+3] / 255.0f : 1.0f;
            }
        }
    }

    _textureCache[cacheKey] = std::move(data);
    return _SampleTextureData(_textureCache[cacheKey], uv);
}

GfVec4f HdGeminiRenderer::_SampleTextureData(const TextureData& data, const GfVec2f& uv) const {
    if (data.pixels.empty()) return GfVec4f(-1.0f);

    float u = uv[0] - std::floor(uv[0]);
    float v = 1.0f - (uv[1] - std::floor(uv[1])); // Flip V for OpenGL/USD convention

    float px = u * (data.width - 1);
    float py = v * (data.height - 1);
    int x0 = std::max(0, std::min((int)std::floor(px), data.width - 1));
    int y0 = std::max(0, std::min((int)std::floor(py), data.height - 1));
    int x1 = std::max(0, std::min(x0 + 1, data.width - 1));
    int y1 = std::max(0, std::min(y0 + 1, data.height - 1));
    float fx = px - x0;
    float fy = py - y0;

    auto getPixel = [&](int x, int y) {
        size_t idx = (y * data.width + x) * 4;
        return GfVec4f(data.pixels[idx], data.pixels[idx+1], data.pixels[idx+2], data.pixels[idx+3]);
    };

    GfVec4f p00 = getPixel(x0, y0);
    GfVec4f p10 = getPixel(x1, y0);
    GfVec4f p01 = getPixel(x0, y1);
    GfVec4f p11 = getPixel(x1, y1);

    return (p00 * (1-fx) + p10 * fx) * (1-fy) + (p01 * (1-fx) + p11 * fx) * fy;
}

static GfVec2f _RaySphereIntersect(const GfVec3f& r0, const GfVec3f& rd, float sr) {
    float a = GfDot(rd, rd);
    float b = 2.0f * GfDot(rd, r0);
    float c = GfDot(r0, r0) - (sr * sr);
    float d = (b * b) - 4.0f * a * c;
    if (d < 0.0f) return GfVec2f(1e5f, -1e5f);
    return GfVec2f((-b - std::sqrt(d)) / (2.0f * a), (-b + std::sqrt(d)) / (2.0f * a));
}

GfVec3f HdGeminiRenderer::_SamplePhysicalSky(const GfVec3f& rayDir, const GfVec3f& sunDir, bool includeSun) const {
    GfVec3f dir = rayDir.GetNormalized();
    GfVec3f sDir = sunDir.GetNormalized();

    float R_planet = 6371e3f;
    float R_atmos = 6471e3f;

    GfVec3f rayOrigin(0.0f, R_planet + 100.0f, 0.0f);

    GfVec2f isect = _RaySphereIntersect(rayOrigin, dir, R_atmos);
    if (isect[1] < 0.0f) return GfVec3f(0.0f);

    float tMin = std::max(0.0f, isect[0]);
    float tMax = isect[1];

    GfVec2f isectPlanet = _RaySphereIntersect(rayOrigin, dir, R_planet);
    if (isectPlanet[0] >= 0.0f && isectPlanet[0] < tMax) {
        tMax = isectPlanet[0];
    }

    const int numSteps = 16;
    const int numLightSteps = 8;
    float stepSize = (tMax - tMin) / (float)numSteps;

    GfVec3f betaR(5.5e-6f, 13.0e-6f, 22.4e-6f);
    float betaM = 21e-6f;
    float hR = 8000.0f;
    float hM = 1200.0f;

    float currentT = tMin;
    float opticalDepthR = 0.0f;
    float opticalDepthM = 0.0f;

    GfVec3f totalR(0.0f);
    GfVec3f totalM(0.0f);

    float mu = GfDot(dir, sDir);
    float phaseR = 3.0f / (16.0f * (float)M_PI) * (1.0f + mu * mu);
    float g = 0.76f;
    float phaseM = 3.0f / (8.0f * (float)M_PI) * ((1.0f - g * g) * (1.0f + mu * mu)) / ((2.0f + g * g) * std::pow(1.0f + g * g - 2.0f * g * mu, 1.5f));

    for (int i = 0; i < numSteps; ++i) {
        float midT = currentT + stepSize * 0.5f;
        GfVec3f samplePos = rayOrigin + dir * midT;
        float height = samplePos.GetLength() - R_planet;

        if (height < 0.0f) break;

        float hr = std::exp(-height / hR) * stepSize;
        float hm = std::exp(-height / hM) * stepSize;
        opticalDepthR += hr;
        opticalDepthM += hm;

        GfVec2f isectSun = _RaySphereIntersect(samplePos, sDir, R_atmos);
        float sunTMax = isectSun[1];
        float sunStepSize = sunTMax / (float)numLightSteps;
        float sunCurrentT = 0.0f;
        float sunOpticalDepthR = 0.0f;
        float sunOpticalDepthM = 0.0f;

        bool inShadow = false;
        GfVec2f sunIsectPlanet = _RaySphereIntersect(samplePos, sDir, R_planet);
        if (sunIsectPlanet[0] > 0.0f) {
            inShadow = true;
        } else {
            for (int j = 0; j < numLightSteps; ++j) {
                float sunMidT = sunCurrentT + sunStepSize * 0.5f;
                GfVec3f sunSamplePos = samplePos + sDir * sunMidT;
                float sunHeight = sunSamplePos.GetLength() - R_planet;
                if (sunHeight < 0.0f) break;
                sunOpticalDepthR += std::exp(-sunHeight / hR) * sunStepSize;
                sunOpticalDepthM += std::exp(-sunHeight / hM) * sunStepSize;
                sunCurrentT += sunStepSize;
            }
        }

        if (!inShadow) {
            GfVec3f tau = betaR * (opticalDepthR + sunOpticalDepthR) + GfVec3f(betaM * 1.1f) * (opticalDepthM + sunOpticalDepthM);
            GfVec3f attenuation(std::exp(-tau[0]), std::exp(-tau[1]), std::exp(-tau[2]));
            totalR += attenuation * hr;
            totalM += attenuation * hm;
        }
        currentT += stepSize;
    }

    GfVec3f sunIntensity(120000.0f * std::exp2(_physicalSkySunExposure));
    GfVec3f color = GfCompMult(GfCompMult(totalR, betaR) * phaseR + totalM * betaM * phaseM, sunIntensity);
    color = color * std::exp2(_physicalSkySkyExposure);

    if (isectPlanet[0] >= 0.0f && isectPlanet[0] < tMax + stepSize) {
         color = color * 0.5f;
    }

    float sunAngularRadius = 0.00465f;
    if (includeSun && std::acos(std::clamp(mu, -1.0f, 1.0f)) < sunAngularRadius && isectPlanet[0] < 0.0f) {
        GfVec3f tau = betaR * opticalDepthR + GfVec3f(betaM * 1.1f) * opticalDepthM;
        GfVec3f attenuation(std::exp(-tau[0]), std::exp(-tau[1]), std::exp(-tau[2]));
        color += GfCompMult(sunIntensity, attenuation); 
    }

    return color;
}

GfVec3f HdGeminiRenderer::_GetSunTransmittance(const GfVec3f& sunDir) const {
    if (sunDir[1] < 0.0f) return GfVec3f(0.0f);
    float R_planet = 6371e3f;
    float R_atmos = 6471e3f;
    GfVec3f rayOrigin(0.0f, R_planet + 100.0f, 0.0f);
    GfVec2f isectSun = _RaySphereIntersect(rayOrigin, sunDir, R_atmos);
    
    float sunTMax = isectSun[1];
    const int numLightSteps = 16;
    float sunStepSize = sunTMax / (float)numLightSteps;
    float sunCurrentT = 0.0f;
    float sunOpticalDepthR = 0.0f;
    float sunOpticalDepthM = 0.0f;

    float hR = 8000.0f;
    float hM = 1200.0f;
    GfVec3f betaR(5.5e-6f, 13.0e-6f, 22.4e-6f);
    float betaM = 21e-6f;

    for (int j = 0; j < numLightSteps; ++j) {
        float sunMidT = sunCurrentT + sunStepSize * 0.5f;
        GfVec3f sunSamplePos = rayOrigin + sunDir * sunMidT;
        float sunHeight = sunSamplePos.GetLength() - R_planet;
        if (sunHeight < 0.0f) break;
        sunOpticalDepthR += std::exp(-sunHeight / hR) * sunStepSize;
        sunOpticalDepthM += std::exp(-sunHeight / hM) * sunStepSize;
        sunCurrentT += sunStepSize;
    }

    GfVec3f tau = betaR * sunOpticalDepthR + GfVec3f(betaM * 1.1f) * sunOpticalDepthM;
    return GfVec3f(std::exp(-tau[0]), std::exp(-tau[1]), std::exp(-tau[2]));
}

bool HdGeminiRenderer::_IsPointInsideOcean(const GfVec3f& pos) const {
    if (_oceanEnable && _globalOcean) {
        GfVec3f queryPos = pos;
        queryPos[1] = _oceanWaterHeight;
        if (!_oceanParams.repeat) {
            float halfSize = _oceanParams.size * 0.5f;
            if (pos[0] >= -halfSize && pos[0] <= halfSize && pos[2] >= -halfSize && pos[2] <= halfSize) {
                if (pos[1] < _globalOcean->GetDisplacedPosition(queryPos)[1]) return true;
            }
        } else {
            if (pos[1] < _globalOcean->GetDisplacedPosition(queryPos)[1]) return true;
        }
    }

    for (const auto& inst : _instances) {
        if (inst.type == SceneInstance::Type::Ocean && inst.mesh && inst.mesh->GetOceanSimulator()) {
            GfVec3f localPos = inst.invTransform.Transform(pos);
            GfVec3f queryPos = localPos;
            queryPos[1] = inst.mesh->GetOceanParams().waterHeight;
            if (!inst.mesh->GetOceanParams().repeat) {
                float halfSize = inst.mesh->GetOceanParams().size * 0.5f;
                if (localPos[0] < -halfSize || localPos[0] > halfSize || localPos[2] < -halfSize || localPos[2] > halfSize) continue;
            }
            if (localPos[1] < inst.mesh->GetOceanSimulator()->GetDisplacedPosition(queryPos)[1]) return true;
        }
    }
    return false;
}

static float PowerHeuristic(float f, float g) {
    float f2 = f * f;
    float g2 = g * g;
    return f2 / (f2 + g2);
}

SampledSpectrum HdGeminiRenderer::_TraceShadowRay(const GfVec3f& rayOrigin, const GfVec3f& rayDir, float maxDist, bool currentlyInside, float currentTransmissionDepth, const GfVec3f& currentTransmissionColor, const GfVec3f& currentTransmissionScatter, HdRenderThread* renderThread, uint32_t sampleIdx, uint32_t& qmcDim, uint32_t& rng, const SampledWavelengths& lambda) const {
    SampledSpectrum transmittance;
    for(int i=0; i<SPECTRUM_SAMPLES; ++i) transmittance[i] = 1.0f;

    GfVec3f p = rayOrigin;
    float distRemaining = maxDist;
    float transDepth = currentTransmissionDepth;
    GfVec3f transColor = currentTransmissionColor;
    GfVec3f transScatter = currentTransmissionScatter;
    int bounce = 0;

    while (distRemaining > 0.0f && bounce < 8) {
        bounce++;
        HitRecord sHit;
        sHit.t = distRemaining;
        if (!_IntersectTLAS(p, rayDir, sHit, renderThread, sampleIdx, qmcDim, rng)) {
            if (currentlyInside && transDepth > 0.0f) {
                float distToExit = distRemaining;
                if (_oceanEnable && rayDir[1] > 0.0f) {
                    if (p[1] < _oceanWaterHeight) {
                        float tToSurface = (_oceanWaterHeight - p[1]) / rayDir[1];
                        distToExit = std::min(distRemaining, tToSurface);
                    } else {
                        // Already above the mathematical water height.
                        // Prevent infinite attenuation in the sky.
                        distToExit = 0.0f;
                    }
                }
                
                if (distToExit > 0.0f) {
                    SampledSpectrum transSpec = RGBToSpectrum(transColor, lambda);
                    for(int i=0; i<SPECTRUM_SAMPLES; ++i) {
                        float scaledDepth = transDepth / _metersPerUnit;
                        float sigma_a = -std::log(std::max(transSpec[i], 1e-4f)) / scaledDepth;
                        float sigma_s = transScatter[i] / scaledDepth;
                        float sigma_t = sigma_a + sigma_s;
                        transmittance[i] *= std::exp(-sigma_t * distToExit);
                    }
                }
            }
            break; 
        }

        if (currentlyInside && transDepth > 0.0f) {
            SampledSpectrum transSpec = RGBToSpectrum(transColor, lambda);
            for(int i=0; i<SPECTRUM_SAMPLES; ++i) {
                float scaledDepth = transDepth / _metersPerUnit;
                float sigma_a = -std::log(std::max(transSpec[i], 1e-4f)) / scaledDepth;
                float sigma_s = transScatter[i] / scaledDepth;
                float sigma_t = sigma_a + sigma_s;
                transmittance[i] *= std::exp(-sigma_t * sHit.t);
            }
        }

        if (sHit.transmission <= 0.01f && sHit.subsurface <= 0.01f) {
            for(int i=0; i<SPECTRUM_SAMPLES; ++i) transmittance[i] = 0.0f;
            break;
        }

        SampledSpectrum hitTrans = RGBToSpectrum(sHit.baseColor * sHit.transmission + sHit.subsurfaceColor * sHit.subsurface, lambda);
        for(int i=0; i<SPECTRUM_SAMPLES; ++i) transmittance[i] *= hitTrans[i];

        p = p + rayDir * sHit.t + rayDir * RAY_EPSILON(p);
        distRemaining -= sHit.t;
        
        if (!sHit.thinWalled) {
            if (GfDot(rayDir, sHit.smoothNormal) < 0.0f) {
                // Entering the object
                currentlyInside = true;
                transDepth = sHit.transmissionDepth;
                transColor = sHit.transmissionColor;
                transScatter = sHit.transmissionScatter;
            } else {
                // Exiting the object
                currentlyInside = false;
            }
        }
    }

    return transmittance;
}

SampledSpectrum HdGeminiRenderer::_TraceRay(const GfVec3f& rayOrigin, const GfVec3f& rayDir, int depth, bool isInteractive, HdRenderThread* renderThread, uint32_t sampleIdx, uint32_t& qmcDim, uint32_t& rng, const SampledWavelengths& lambda, GfVec3f* outAlbedo, GfVec3f* outNormal, float* outDepth, float exposureMultiplier, int pixelX, int pixelY) const
{
    SampledSpectrum throughput(exposureMultiplier);
    SampledSpectrum totalRadiance(0.0f);
    GfVec3f currentRayOrigin = rayOrigin;
    GfVec3f currentRayDir = rayDir;

    // ReSTIR GI Variables
    bool useRestirGI = (_enableRestirGI && pixelX >= 0 && pixelY >= 0 && depth == 0);
    GfVec3f giPrimaryPos, giPrimaryNormal;
    GfVec3f giPrimaryAlbedo(0.5f);
    float giPrimaryDepth = 0.0f;
    int giPrevX = pixelX, giPrevY = pixelY;
    bool giFoundPrev = false;
    bool giPrimaryCaptured = false;
    int giPrimaryBounce = -1;
    GfVec3f giVirtualLightPos, giVirtualLightNormal;
    SampledSpectrum giVirtualRadiance(0.0f);
    SampledSpectrum giPrimaryThroughput(0.0f);
    bool hasGiData = false;
    SampledSpectrum totalRadianceBeforeBounce(0.0f);


    auto EvaluateLight = [&](const LightSample& ls, const GfVec3f& targetPos, GfVec3f& lDir, float& lightDist, GfVec3f& lColor, float& lightPdf) {
        HdGeminiLight* light = _activeLights[ls.lightIdx];
        lightPdf = (_lightPowerCdf[ls.lightIdx] - (ls.lightIdx > 0 ? _lightPowerCdf[ls.lightIdx - 1] : 0.0f));
        lightPdf = std::max(lightPdf, 1e-6f);
        
        lightDist = 1e30f;
        lColor = GfVec3f(0.0f);
        
        if (light->GetLightType() == HdPrimTypeTokens->distantLight) {
            lDir = GfMatrix4f(light->GetTransform()).TransformDir(GfVec3f(0, 0, -1)).GetNormalized();
            lColor = light->GetColor() * light->GetIntensity();
        } else if (light->GetLightType() == HdPrimTypeTokens->domeLight && !_envMapRowCdf.empty()) {
            float u1 = ls.u;
            float u2 = ls.v;
            auto itY = std::lower_bound(_envMapRowCdf.begin(), _envMapRowCdf.end(), u1);
            int y = std::clamp((int)std::distance(_envMapRowCdf.begin(), itY) - 1, 0, _envMapHeight - 1);
            const float* colCdf = &_envMapColCdf[y * (_envMapWidth + 1)];
            auto itX = std::lower_bound(colCdf, colCdf + _envMapWidth + 1, u2);
            int x = std::clamp((int)std::distance(colCdf, itX) - 1, 0, _envMapWidth - 1);
            float theta = M_PI * (float)(y + 0.5f) / (float)_envMapHeight;
            float phi = 2.0f * M_PI * (float)(x + 0.5f) / (float)_envMapWidth;
            float sinThetaL = std::max(1e-6f, std::sin(theta));
            GfVec3f localDir(sinThetaL * std::sin(phi), std::cos(theta), -sinThetaL * std::cos(phi));
            lDir = GfMatrix4f(light->GetTransform()).TransformDir(localDir).GetNormalized();
            size_t idx = (y * _envMapWidth + x) * 3;
            GfVec3f texColor(_envMapPixels[idx], _envMapPixels[idx+1], _envMapPixels[idx+2]);
            lColor = GfCompMult(texColor, light->GetColor()) * light->GetIntensity();
            if (_envMapTotalLuminance > 0) {
                float lum = 0.2126f * texColor[0] + 0.7152f * texColor[1] + 0.0722f * texColor[2];
                float pdf = lum / (_envMapTotalLuminance * (M_PI / (float)_envMapHeight) * (2.0f * M_PI / (float)_envMapWidth) * sinThetaL);
                lightPdf *= std::max(pdf, 1e-6f);
            }
        } else if (light->GetLightType() == HdPrimTypeTokens->rectLight) {
            GfVec3f lPosLocal((ls.u - 0.5f) * light->GetWidth(), (ls.v - 0.5f) * light->GetHeight(), 0.0f);
            GfVec3f lPosWorld = GfMatrix4f(light->GetTransform()).Transform(lPosLocal);
            GfVec3f toLight = lPosWorld - targetPos;
            lightDist = toLight.GetLength();
            lDir = toLight / lightDist;
            GfMatrix4f transform = GfMatrix4f(light->GetTransform());
            float scaleX = transform.TransformDir(GfVec3f(1, 0, 0)).GetLength();
            float scaleY = transform.TransformDir(GfVec3f(0, 1, 0)).GetLength();
            float area = (light->GetWidth() * scaleX) * (light->GetHeight() * scaleY);
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
        } else if (light->GetLightType() == HdPrimTypeTokens->sphereLight) {
            GfMatrix4f transform = GfMatrix4f(light->GetTransform());
            float scale = transform.TransformDir(GfVec3f(1, 0, 0)).GetLength();
            float r = light->GetRadius() * scale;
            if (r > 0) {
                float phi = ls.u * 2.0f * (float)M_PI;
                float cosTheta = 1.0f - 2.0f * ls.v;
                float sinTheta = std::sqrt(std::max(0.0f, 1.0f - cosTheta * cosTheta));
                GfVec3f lPosLocal(r * sinTheta * std::cos(phi), r * sinTheta * std::sin(phi), r * cosTheta);
                GfVec3f lPosWorld = transform.Transform(lPosLocal);
                GfVec3f toLight = lPosWorld - targetPos;
                lightDist = toLight.GetLength();
                lDir = toLight / lightDist;
                
                GfVec3f lNormal = transform.TransformDir(lPosLocal).GetNormalized();
                float cosThetaL = std::max(0.0f, GfDot(lNormal, -lDir));
                if (cosThetaL > 0) {
                    float area = 4.0f * (float)M_PI * r * r;
                    lightPdf *= (lightDist * lightDist) / (area * cosThetaL);
                    lColor = light->GetColor() * light->GetIntensity();
                } else {
                    lightDist = -1.0f;
                }
            }
        } else if (light->GetLightType() == HdPrimTypeTokens->diskLight) {
            GfMatrix4f transform = GfMatrix4f(light->GetTransform());
            float scale = transform.TransformDir(GfVec3f(1, 0, 0)).GetLength();
            float r = light->GetRadius() * scale;
            if (r > 0) {
                float rSample = r * std::sqrt(ls.u);
                float theta = 2.0f * (float)M_PI * ls.v;
                GfVec3f lPosLocal(rSample * std::cos(theta), rSample * std::sin(theta), 0.0f);
                GfVec3f lPosWorld = transform.Transform(lPosLocal);
                GfVec3f toLight = lPosWorld - targetPos;
                lightDist = toLight.GetLength();
                lDir = toLight / lightDist;
                
                GfVec3f lNormal = transform.TransformDir(GfVec3f(0, 0, -1)).GetNormalized();
                float cosThetaL = std::max(0.0f, GfDot(lNormal, -lDir));
                if (cosThetaL > 0) {
                    float area = (float)M_PI * r * r;
                    lightPdf *= (lightDist * lightDist) / (area * cosThetaL);
                    lColor = light->GetColor() * light->GetIntensity();
                } else {
                    lightDist = -1.0f;
                }
            }
        } else if (light->GetLightType() == HdPrimTypeTokens->cylinderLight) {
            GfMatrix4f transform = GfMatrix4f(light->GetTransform());
            float scaleR = transform.TransformDir(GfVec3f(1, 0, 0)).GetLength();
            float scaleL = transform.TransformDir(GfVec3f(0, 0, 1)).GetLength();
            float r = light->GetRadius() * scaleR;
            float l = light->GetLength() * scaleL;
            if (r > 0 && l > 0) {
                float phi = ls.u * 2.0f * (float)M_PI;
                float z = (ls.v - 0.5f) * l;
                GfVec3f lPosLocal(r * std::cos(phi), r * std::sin(phi), z);
                GfVec3f lPosWorld = transform.Transform(lPosLocal);
                GfVec3f toLight = lPosWorld - targetPos;
                lightDist = toLight.GetLength();
                lDir = toLight / lightDist;
                
                GfVec3f lNormal = transform.TransformDir(GfVec3f(lPosLocal[0], lPosLocal[1], 0.0f)).GetNormalized();
                float cosThetaL = std::max(0.0f, GfDot(lNormal, -lDir));
                if (cosThetaL > 0) {
                    float area = 2.0f * (float)M_PI * r * l;
                    lightPdf *= (lightDist * lightDist) / (area * cosThetaL);
                    lColor = light->GetColor() * light->GetIntensity();
                } else {
                    lightDist = -1.0f;
                }
            }
        } else {
            // Point light fallback for any unhandled lights
            GfVec3f lPos = GfMatrix4f(light->GetTransform()).ExtractTranslation();
            GfVec3f toLight = lPos - targetPos;
            lightDist = toLight.GetLength();
            lDir = toLight / lightDist;
            lightPdf *= (lightDist * lightDist);
            lColor = light->GetColor() * light->GetIntensity();
        }

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
                            factor = factor * factor * (3.0f - 2.0f * factor);
                            lColor *= factor;
                        }
                    }
                }
            }
        }
    };

    int reflectionBounces = 0;
    int refractionBounces = 0;
    
    struct PathVertex {
        GfVec3f pos;
        GfVec3f dir;
        SampledSpectrum throughput;
        SampledSpectrum totalRadianceBefore;
    };
    PathVertex pathHistory[16];
    int pathHistoryCount = 0;
    
    struct MediumState {
        float ior = 1.0f;
        GfVec3f scatterColor = GfVec3f(0.0f);
        float depth = 0.0f;
        GfVec3f transmissionColor = GfVec3f(1.0f);
    };
    
    // Nested Dielectrics tracking stack
    MediumState mediumStack[16];
    int mediumStackCount = 0;
    mediumStack[mediumStackCount++] = MediumState{1.0f, GfVec3f(0.0f), 0.0f, GfVec3f(1.0f)};
    
    // Perform a BVH-accurate test to see if the camera is underwater
    if (_oceanEnable) {
        GfVec3f ro = currentRayOrigin;
        GfVec3f rd(0.0f, 1.0f, 0.0f);
        uint32_t dummyQmc = 0, dummyRng = rng;
        while (true) {
            HitRecord hit;
            if (!_IntersectTLAS(ro, rd, hit, renderThread, sampleIdx, dummyQmc, dummyRng)) {
                break;
            }
            if (hit.isOcean) {
                // If we hit the ocean from below (smoothNormal points roughly up +Y, rd is up +Y -> dot > 0)
                if (GfDot(hit.smoothNormal, rd) > 0.0f) {
                    if (mediumStackCount < 16) mediumStack[mediumStackCount++] = MediumState{hit.ior, hit.transmissionScatter, hit.transmissionDepth, hit.transmissionColor};
                }
                break; // First ocean hit determines if we are above or below
            }
            ro = ro + rd * (hit.t + RAY_EPSILON(ro));
        }
        
        if (mediumStackCount == 1 && currentRayOrigin[1] < _oceanWaterHeight) {
            mediumStack[mediumStackCount++] = MediumState{1.33f, _oceanParams.scatteringColor, _oceanParams.scatteringDepth, GfVec3f(0.8f, 0.9f, 0.95f)};
        }
    }

    const int maxDepth = isInteractive ? 2 : 32;

    float azimuthRad = _physicalSkyAzimuth * (float)(M_PI / 180.0);
    float altitudeRad = _physicalSkyAltitude * (float)(M_PI / 180.0);
    GfVec3f physicalSunDir = GfVec3f(
        std::cos(altitudeRad) * std::sin(azimuthRad),
        std::sin(altitudeRad),
        std::cos(altitudeRad) * std::cos(azimuthRad)
    ).GetNormalized();

    float lastBsdfPdf = 0.0f;

    for (int bounce = 0; bounce < maxDepth; ++bounce) {
        totalRadianceBeforeBounce = totalRadiance;
        if (renderThread->IsStopRequested()) break;

        HitRecord hit;
        bool intersected = this->_IntersectTLAS(currentRayOrigin, currentRayDir, hit, renderThread, sampleIdx, qmcDim, rng);
        
        if (_renderLightGeometry || bounce > 0) {
            float lightT = hit.t;
            HdGeminiLight* intersectedLight = nullptr;
            GfVec3f lightNormal;
            GfVec3f lightColor;
            
            for (const auto& light : _activeLights) {
                GfMatrix4f invTransform = GfMatrix4f(light->GetTransform()).GetInverse();
                GfVec3f localOrigin = invTransform.Transform(currentRayOrigin);
                GfVec3f localDir = invTransform.TransformDir(currentRayDir);
                
                if (light->GetLightType() == HdPrimTypeTokens->rectLight) {
                    if (std::abs(localDir[2]) > 1e-6f) {
                        float t = -localOrigin[2] / localDir[2];
                        if (t > 0.0f && t < lightT) {
                            float localX = localOrigin[0] + t * localDir[0];
                            float localY = localOrigin[1] + t * localDir[1];
                            GfMatrix4f transform = GfMatrix4f(light->GetTransform());
                            float scale = transform.TransformDir(GfVec3f(1, 0, 0)).GetLength();
                            float w = (light->GetWidth() * scale) / 2.0f;
                            float h = (light->GetHeight() * scale) / 2.0f;
                            if (localX >= -w && localX <= w && localY >= -h && localY <= h) {
                                lightT = t;
                                intersectedLight = light;
                                GfVec3f n(0, 0, localDir[2] > 0.0f ? -1.0f : 1.0f);
                                lightNormal = GfMatrix4f(light->GetTransform()).TransformDir(n).GetNormalized();
                                if (localDir[2] > 0.0f) {
                                    lightColor = light->GetColor() * light->GetIntensity();
                                } else {
                                    lightColor = GfVec3f(0.01f);
                                }
                            }
                        }
                    }
                } else if (light->GetLightType() == HdPrimTypeTokens->diskLight) {
                    if (std::abs(localDir[2]) > 1e-6f) {
                        float t = -localOrigin[2] / localDir[2];
                        if (t > 0.0f && t < lightT) {
                            float localX = localOrigin[0] + t * localDir[0];
                            float localY = localOrigin[1] + t * localDir[1];
                            GfMatrix4f transform = GfMatrix4f(light->GetTransform());
                            float scale = transform.TransformDir(GfVec3f(1, 0, 0)).GetLength();
                            float r = light->GetRadius() * scale;
                            if (localX * localX + localY * localY <= r * r) {
                                lightT = t;
                                intersectedLight = light;
                                GfVec3f n(0, 0, localDir[2] > 0.0f ? -1.0f : 1.0f);
                                lightNormal = GfMatrix4f(light->GetTransform()).TransformDir(n).GetNormalized();
                                if (localDir[2] > 0.0f) {
                                    lightColor = light->GetColor() * light->GetIntensity();
                                } else {
                                    lightColor = GfVec3f(0.01f);
                                }
                            }
                        }
                    }
                } else if (light->GetLightType() == HdPrimTypeTokens->sphereLight) {
                    GfMatrix4f transform = GfMatrix4f(light->GetTransform());
                    float scale = transform.TransformDir(GfVec3f(1, 0, 0)).GetLength();
                    float r = light->GetRadius() * scale;
                    GfVec2f isect = _RaySphereIntersect(localOrigin, localDir, r);
                    float t = isect[0] > 0.0f ? isect[0] : isect[1];
                    if (t > 0.0f && t < lightT) {
                        lightT = t;
                        intersectedLight = light;
                        GfVec3f hitLocal = localOrigin + t * localDir;
                        lightNormal = transform.TransformDir(hitLocal).GetNormalized();
                        lightColor = light->GetColor() * light->GetIntensity();
                    }
                } else if (light->GetLightType() == HdPrimTypeTokens->cylinderLight) {
                    GfMatrix4f transform = GfMatrix4f(light->GetTransform());
                    float scaleR = transform.TransformDir(GfVec3f(1, 0, 0)).GetLength();
                    float scaleL = transform.TransformDir(GfVec3f(0, 0, 1)).GetLength();
                    float r = light->GetRadius() * scaleR;
                    float l = light->GetLength() * scaleL;
                    
                    float a = localDir[0]*localDir[0] + localDir[1]*localDir[1];
                    float b = 2.0f * (localOrigin[0]*localDir[0] + localOrigin[1]*localDir[1]);
                    float c = localOrigin[0]*localOrigin[0] + localOrigin[1]*localOrigin[1] - r*r;
                    
                    float d = b*b - 4.0f*a*c;
                    if (d >= 0.0f && a > 1e-6f) {
                        float t0 = (-b - std::sqrt(d)) / (2.0f*a);
                        float t1 = (-b + std::sqrt(d)) / (2.0f*a);
                        float t = t0 > 0.0f ? t0 : t1;
                        if (t > 0.0f && t < lightT) {
                            float z = localOrigin[2] + t * localDir[2];
                            if (z >= -l/2.0f && z <= l/2.0f) {
                                lightT = t;
                                intersectedLight = light;
                                GfVec3f hitLocal(localOrigin[0] + t*localDir[0], localOrigin[1] + t*localDir[1], 0.0f);
                                lightNormal = transform.TransformDir(hitLocal).GetNormalized();
                                lightColor = light->GetColor() * light->GetIntensity();
                            }
                        }
                    }
                }
            }
            if (intersectedLight) {
                hit.t = lightT;
                hit.baseColor = GfVec3f(0.0f);
                hit.emission = lightColor;
                hit.normal = lightNormal;
                hit.smoothNormal = lightNormal;
                hit.roughness = 1.0f;
                hit.metallic = 0.0f;
                hit.specular = 0.0f;
                hit.isVolumeHit = false;
                hit.thinWalled = true;
                hit.transmission = 0.0f;
                intersected = true;
                
                auto it = std::find(_activeLights.begin(), _activeLights.end(), intersectedLight);
                if (it != _activeLights.end()) {
                    int idx = (int)std::distance(_activeLights.begin(), it);
                    float prob = _lightPowerCdf[idx] - (idx > 0 ? _lightPowerCdf[idx-1] : 0.0f);
                    GfMatrix4f transform = GfMatrix4f(intersectedLight->GetTransform());
                    float scaleX = transform.TransformDir(GfVec3f(1, 0, 0)).GetLength();
                    float scaleY = transform.TransformDir(GfVec3f(0, 1, 0)).GetLength();
                    float area = (intersectedLight->GetWidth() * scaleX) * (intersectedLight->GetHeight() * scaleY);
                    float cosThetaL = std::max(0.0f, GfDot(lightNormal, -currentRayDir));
                    if (cosThetaL > 0.0f && area > 0.0f) {
                        hit.emissionPdf = std::max(prob, 1e-6f) * (lightT * lightT) / (area * cosThetaL);
                    }
                }
            }
        }

        if (intersected && bounce == 0 && outDepth) *outDepth = hit.t;
        
        if (!intersected) {
            if (_oceanEnable && currentRayOrigin[1] < _oceanWaterHeight && currentRayDir[1] > 0.0f) {
                float tToSurface = (_oceanWaterHeight - currentRayOrigin[1]) / currentRayDir[1];
                hit.t = tToSurface;
                hit.isVolumeHit = false;
                hit.smoothNormal = GfVec3f(0.0f, 1.0f, 0.0f);
                hit.thinWalled = false;
                hit.transmission = 1.0f;
                hit.baseColor = GfVec3f(1.0f);
                hit.ior = 1.0f;
                hit.transmissionDepth = 0.0f;
                if (bounce == 0 && outDepth) *outDepth = hit.t;
                intersected = true;
            } else if (mediumStackCount > 0 && mediumStack[mediumStackCount-1].depth > 0.0f) {
                for(int i=0; i<SPECTRUM_SAMPLES; ++i) throughput[i] = 0.0f;
            }
        }

        if (!intersected) {
            GfVec3f envRGB;
            if (_enablePhysicalSky) {
                bool includeSun = (bounce == 0 || lastBsdfPdf <= 0.0f);
                envRGB = _SamplePhysicalSky(currentRayDir, physicalSunDir, includeSun);
            } else {
                envRGB = _SampleEnvironment(currentRayDir);
            }
            SampledSpectrum env = RGBToSpectrum(envRGB, lambda);
            if (bounce == 0 && outAlbedo) *outAlbedo = envRGB;
            
            if (bounce == 0 && !_renderIblBackground) {
                // Do not add the environment map to the final pixel if background rendering is disabled for primary rays.
                totalRadiance += SampledSpectrum(0.0f);
            } else if (bounce == 0) {
                totalRadiance += throughput * env;
            } else {
                float lightPdf = 0.0f;
                if (_hasDomeLight && !_envMapRowCdf.empty() && _envMapTotalLuminance > 0) {
                    float lum = 0.2126f * envRGB[0] + 0.7152f * envRGB[1] + 0.0722f * envRGB[2];
                    // Fix 4: include sinθ Jacobian for equirectangular mapping
                    GfVec3f localDir = currentRayDir;
                    for (const auto& light : _activeLights) {
                        if (light->GetLightType() == HdPrimTypeTokens->domeLight) {
                            localDir = GfMatrix4f(light->GetTransform()).GetInverse().TransformDir(currentRayDir).GetNormalized();
                            break;
                        }
                    }
                    float thetaEnv = std::acos(std::clamp(localDir[1], -1.0f, 1.0f));
                    float sinThetaEnv = std::max(1e-6f, std::sin(thetaEnv));
                    float envPdf = lum / (_envMapTotalLuminance * (M_PI / (float)_envMapHeight) * (2.0f * M_PI / (float)_envMapWidth) * sinThetaEnv);
                    lightPdf = std::max(envPdf, 1e-6f) / (float)_activeLights.size();
                }
                float misWeight = (lastBsdfPdf <= 0.0f) ? 1.0f : PowerHeuristic(lastBsdfPdf, lightPdf);
                totalRadiance += throughput * env * misWeight;
            }
            break;
        }

        if (hit.diffuseTexture && !hit.diffuseTexture->GetAssetPath().empty()) {
            GfVec4f texVal = _SampleTexture(*hit.diffuseTexture, hit.uv);
            if (texVal[0] >= 0.0f) {
                hit.baseColor = GfCompMult(hit.baseColor, GfVec3f(texVal[0], texVal[1], texVal[2]));
            }
        }

        if (hit.metallicTexture && !hit.metallicTexture->GetAssetPath().empty()) {
            GfVec4f texVal = _SampleTexture(*hit.metallicTexture, hit.uv, true);
            if (texVal[0] >= 0.0f) {
                hit.metallic = texVal[hit.metallicTextureChannel];
            }
        }

        if (hit.roughnessTexture && !hit.roughnessTexture->GetAssetPath().empty()) {
            GfVec4f texVal = _SampleTexture(*hit.roughnessTexture, hit.uv, true);
            if (texVal[0] >= 0.0f) {
                hit.roughness = texVal[hit.roughnessTextureChannel];
            }
        }

        if (hit.opacityTexture && !hit.opacityTexture->GetAssetPath().empty()) {
            GfVec4f texVal = _SampleTexture(*hit.opacityTexture, hit.uv, true);
            if (texVal[0] >= 0.0f) {
                hit.opacity = texVal[hit.opacityTextureChannel];
            }
        }

        if (hit.transmissionTexture && !hit.transmissionTexture->GetAssetPath().empty()) {
            GfVec4f texVal = _SampleTexture(*hit.transmissionTexture, hit.uv, true);
            if (texVal[0] >= 0.0f) {
                hit.transmission = texVal[hit.transmissionTextureChannel];
            }
        }

        if (hit.normalTexture && !hit.normalTexture->GetAssetPath().empty()) {
            GfVec4f texVal = _SampleTexture(*hit.normalTexture, hit.uv, true);
            if (texVal[0] >= 0.0f) {
                GfVec3f nTex = GfVec3f(texVal[0], texVal[1], texVal[2]) * 2.0f - GfVec3f(1.0f);
                
                GfVec3f n = hit.smoothNormal;
                GfVec3f t = hit.dpdu;
                GfVec3f b = hit.dpdv;
                
                t = (t - n * GfDot(t, n)).GetNormalized();
                b = (b - n * GfDot(b, n) - t * GfDot(b, t)).GetNormalized();
                
                hit.smoothNormal = (t * nTex[0] + b * nTex[1] + n * nTex[2]).GetNormalized();
            }
        }

        if (bounce == 0) {
            if (outAlbedo) {
                *outAlbedo = hit.baseColor;
                if (hit.transmission > 0.0f) {
                    *outAlbedo = hit.baseColor * (1.0f - hit.transmission) + hit.transmissionColor * hit.transmission;
                }
            }
            if (outNormal) *outNormal = hit.smoothNormal;
            if (outAlbedo && std::isnan((*outAlbedo)[0])) *outAlbedo = GfVec3f(0.0f);
        }

        GfVec3f hitPos = currentRayOrigin + currentRayDir * hit.t;

        if (hit.isVolumeHit) {
            // Volume scattering event (isotropic)
            float cosTheta = 1.0f - 2.0f * qmc::SampleDimension(sampleIdx, qmcDim++, rng);
            float sinTheta = std::sqrt(std::max(0.0f, 1.0f - cosTheta * cosTheta));
            float phi = 2.0f * (float)M_PI * qmc::SampleDimension(sampleIdx, qmcDim++, rng);
            currentRayDir = GfVec3f(sinTheta * std::cos(phi), sinTheta * std::sin(phi), cosTheta);
            currentRayOrigin = hitPos;
            
            // Assume purely white albedo for volume scattering
            lastBsdfPdf = 1.0f / (4.0f * (float)M_PI);
            continue;
        }

        // Handle transparency/opacity
        if (hit.opacity < 0.999f && qmc::SampleDimension(sampleIdx, qmcDim++, rng) > hit.opacity) {
            currentRayOrigin = hitPos + currentRayDir * RAY_EPSILON(hitPos);
            bounce--; // Don't count as a bounce
            continue;
        }

        bool isInside = GfDot(hit.smoothNormal, currentRayDir) > 0;
        float etaI_fresnel = mediumStack[mediumStackCount-1].ior;
        float etaT_fresnel = hit.ior;
        if (isInside) {
            etaT_fresnel = (mediumStackCount > 1) ? mediumStack[mediumStackCount - 2].ior : 1.0f;
        }

        // --- Medium Volumetric Scattering ---
        const MediumState& currentMedium = mediumStack[mediumStackCount-1];        bool shadowCurrentlyInside = mediumStackCount > 0;
        float shadowTransDepth = currentMedium.depth;
        GfVec3f shadowTransColor = currentMedium.transmissionColor;
        GfVec3f shadowTransScatter = currentMedium.scatterColor;
        if (currentMedium.depth > 0.0f) {
            GfVec3f d_mfp = GfVec3f(currentMedium.depth / _metersPerUnit);
            
            GfVec3f sigma_a_rgb(
                -std::log(std::max(currentMedium.transmissionColor[0], 1e-4f)) / d_mfp[0],
                -std::log(std::max(currentMedium.transmissionColor[1], 1e-4f)) / d_mfp[1],
                -std::log(std::max(currentMedium.transmissionColor[2], 1e-4f)) / d_mfp[2]
            );
            
            GfVec3f sigma_s_rgb = GfVec3f(
                currentMedium.scatterColor[0] / d_mfp[0],
                currentMedium.scatterColor[1] / d_mfp[1],
                currentMedium.scatterColor[2] / d_mfp[2]
            );
            
            GfVec3f sigma_t_rgb = sigma_a_rgb + sigma_s_rgb;
            
            SampledSpectrum sigma_t = RGBToSpectrum(sigma_t_rgb, lambda);
            SampledSpectrum sigma_s = RGBToSpectrum(sigma_s_rgb, lambda);
            
            float ext = std::max(sigma_t[0], 1e-4f);
            float d = -std::log(std::max(qmc::SampleDimension(sampleIdx, qmcDim++, rng), 1e-6f)) / ext;
            
            if (d < hit.t) {
                // Volumetric Scatter
                currentRayOrigin = currentRayOrigin + currentRayDir * d;
                for(int i=0; i<SPECTRUM_SAMPLES; ++i) {
                    throughput[i] *= (1.0f / ext) * std::exp(-(sigma_t[i] - ext) * d);
                }
                
                // --- Volumetric Next Event Estimation (NEE) ---
                if (!_activeLights.empty()) {
                    LightSample ls = {0};
                    ls.lightIdx = 0;
                    ls.u = qmc::SampleDimension(sampleIdx, qmcDim++, rng);
                    ls.v = qmc::SampleDimension(sampleIdx, qmcDim++, rng);
                    
                    float uLight = qmc::SampleDimension(sampleIdx, qmcDim++, rng);
                    auto itLight = std::lower_bound(_lightPowerCdf.begin(), _lightPowerCdf.end(), uLight);
                    ls.lightIdx = std::clamp((int)std::distance(_lightPowerCdf.begin(), itLight), 0, (int)_activeLights.size() - 1);

                    GfVec3f lDir;
                    float lightDist;
                    GfVec3f lColor;
                    float lightPdf;
                    EvaluateLight(ls, currentRayOrigin, lDir, lightDist, lColor, lightPdf);
                    
                    if (lightDist > 0.0f && lightPdf > 0.0f && (lColor[0]>0 || lColor[1]>0 || lColor[2]>0)) {
                        SampledSpectrum shadowTransmittance = _TraceShadowRay(currentRayOrigin, lDir, lightDist - RAY_EPSILON(currentRayOrigin), shadowCurrentlyInside, shadowTransDepth, shadowTransColor, shadowTransScatter, renderThread, sampleIdx, qmcDim, rng, lambda);
                        
                        bool hasTransmittance = false;
                        for(int i=0; i<SPECTRUM_SAMPLES; ++i) if (shadowTransmittance[i] > 0.0f) hasTransmittance = true;
                        
                        if (hasTransmittance) {
                            float g = 0.8f;
                            float cosThetaL = GfDot(currentRayDir, lDir);
                            float denom = 1.0f + g * g - 2.0f * g * cosThetaL;
                            float phase = (1.0f / (4.0f * (float)M_PI)) * (1.0f - g * g) / (denom * std::sqrt(denom));
                            
                            SampledSpectrum L_scatter;
                            SampledSpectrum specLColor = RGBToSpectrum(lColor, lambda);
                            for(int i=0; i<SPECTRUM_SAMPLES; ++i) {
                                L_scatter[i] = (throughput[i] * sigma_s[i] * specLColor[i] * phase * shadowTransmittance[i]) / lightPdf;
                            }
                            totalRadiance += L_scatter;
                        }
                    }
                }
                
                // Henyey-Greenstein phase function for water
                float g = 0.8f;
                float sqrTerm = (1.0f - g * g) / (1.0f - g + 2.0f * g * qmc::SampleDimension(sampleIdx, qmcDim++, rng));
                float cosTheta = (1.0f + g * g - sqrTerm * sqrTerm) / (2.0f * g);
                float sinTheta = std::sqrt(std::max(0.0f, 1.0f - cosTheta * cosTheta));
                float phi = 2.0f * (float)M_PI * qmc::SampleDimension(sampleIdx, qmcDim++, rng);
                
                GfVec3f up = std::abs(currentRayDir[2]) < 0.999f ? GfVec3f(0,0,1) : GfVec3f(1,0,0);
                GfVec3f t = GfCross(up, currentRayDir).GetNormalized();
                GfVec3f b = GfCross(currentRayDir, t);
                
                currentRayDir = (t * std::cos(phi) * sinTheta + b * std::sin(phi) * sinTheta + currentRayDir * cosTheta).GetNormalized();
                
                for(int i=0; i<SPECTRUM_SAMPLES; ++i) {
                    throughput[i] *= sigma_s[i];
                }
                continue; // Proceed directly to next scattering event
            } else {
                // Reached boundary, attenuate by transmittance
                for(int i=0; i<SPECTRUM_SAMPLES; ++i) {
                    throughput[i] *= std::exp(-(sigma_t[i] - ext) * hit.t);
                }
            }
        } else if (isInside && hit.transmissionDepth > 0.0f && !hit.thinWalled) {
            SampledSpectrum transSpec = RGBToSpectrum(hit.transmissionColor, lambda);
            SampledSpectrum sigma_a;
            for(int i=0; i<SPECTRUM_SAMPLES; ++i) {
                float scaledDepth = hit.transmissionDepth / _metersPerUnit;
                sigma_a[i] = -std::log(std::max(transSpec[i], 1e-4f)) / scaledDepth;
                throughput[i] *= std::exp(-sigma_a[i] * hit.t);
            }
        }

        GfVec3f shadingNormal = hit.smoothNormal;
        if (isInside) shadingNormal = -shadingNormal;

        // Capture the first non-transmissive hit as the GI primary (skip glass surfaces)
        if (useRestirGI && !giPrimaryCaptured && hit.transmission < 0.5f) {
            giPrimaryCaptured = true;
            giPrimaryBounce = bounce;
            giPrimaryPos = hitPos;
            giPrimaryNormal = shadingNormal;
            giPrimaryDepth = hit.t;
            giPrimaryThroughput = throughput;
            giPrimaryAlbedo = hit.baseColor;
            if (hit.hitMesh && hit.hit) {
                GfVec3f localPos = hit.hitMesh->GetTransform().GetInverse().Transform(hitPos);
                GfVec3f prevWorldPos = hit.hitMesh->GetPreviousTransform().Transform(localPos);
                GfVec3d prevCamPos = _previousViewMatrix.Transform(GfVec3d(prevWorldPos[0], prevWorldPos[1], prevWorldPos[2]));
                if (prevCamPos[2] < 0.0) {
                    GfVec3d prevNdcPos = _previousProjMatrix.Transform(prevCamPos);
                    if (prevNdcPos[0] >= -1.0 && prevNdcPos[0] <= 1.0 && prevNdcPos[1] >= -1.0 && prevNdcPos[1] <= 1.0) {
                        giPrevX = (int)((prevNdcPos[0] + 1.0) * 0.5 * _lastWidth);
                        giPrevY = (int)((prevNdcPos[1] + 1.0) * 0.5 * _lastHeight);
                        if (giPrevX >= 0 && giPrevX < _lastWidth && giPrevY >= 0 && giPrevY < _lastHeight) {
                            giFoundPrev = true;
                        }
                    }
                }
            }
        }
        
        // --- Volumetric SSS (Random Walk for Objects) ---
        if (isInside && hit.subsurface > 0.0f && !hit.thinWalled) {
            GfVec3f d_mfp = GfCompMult(hit.subsurfaceRadius, GfVec3f(hit.subsurfaceScale));
            d_mfp[0] = std::max(d_mfp[0], 1e-4f);
            d_mfp[1] = std::max(d_mfp[1], 1e-4f);
            d_mfp[2] = std::max(d_mfp[2], 1e-4f);
            
            GfVec3f sigma_t_rgb(1.0f / d_mfp[0], 1.0f / d_mfp[1], 1.0f / d_mfp[2]);
            GfVec3f sigma_s_rgb = GfCompMult(hit.subsurfaceColor, sigma_t_rgb);
            
            SampledSpectrum sigma_t = RGBToSpectrum(sigma_t_rgb, lambda);
            SampledSpectrum sigma_s = RGBToSpectrum(sigma_s_rgb, lambda);
            
            float ext = std::max(sigma_t[0], 1e-4f);
            float d = -std::log(std::max(qmc::SampleDimension(sampleIdx, qmcDim++, rng), 1e-6f)) / ext;
            
            if (d < hit.t) {
                // Volumetric Scatter
                currentRayOrigin = currentRayOrigin + currentRayDir * d;
                
                // Isotropic or Henyey-Greenstein phase function
                float cosTheta = 1.0f - 2.0f * qmc::SampleDimension(sampleIdx, qmcDim++, rng);
                float sinTheta = std::sqrt(std::max(0.0f, 1.0f - cosTheta * cosTheta));
                float phi = 2.0f * (float)M_PI * qmc::SampleDimension(sampleIdx, qmcDim++, rng);
                GfVec3f w(sinTheta * std::cos(phi), sinTheta * std::sin(phi), cosTheta);
                
                if (std::abs(hit.subsurfaceAnisotropy) > 0.001f) {
                    float g = hit.subsurfaceAnisotropy;
                    float sqrTerm = (1.0f - g * g) / (1.0f - g + 2.0f * g * qmc::SampleDimension(sampleIdx, qmcDim++, rng));
                    cosTheta = (1.0f + g * g - sqrTerm * sqrTerm) / (2.0f * g);
                    sinTheta = std::sqrt(std::max(0.0f, 1.0f - cosTheta * cosTheta));
                    
                    GfVec3f up = std::abs(currentRayDir[2]) < 0.999f ? GfVec3f(0,0,1) : GfVec3f(1,0,0);
                    GfVec3f t = GfCross(up, currentRayDir).GetNormalized();
                    GfVec3f b = GfCross(currentRayDir, t);
                    
                    w = (t * std::cos(phi) * sinTheta + b * std::sin(phi) * sinTheta + currentRayDir * cosTheta).GetNormalized();
                }
                currentRayDir = w;
                
                for(int i=0; i<SPECTRUM_SAMPLES; ++i) {
                    throughput[i] *= (sigma_s[i] / std::max(sigma_t[i], 1e-6f));
                }
                continue; // Proceed directly to next scattering event
            } else {
                // Reached boundary, attenuate by transmittance
                for(int i=0; i<SPECTRUM_SAMPLES; ++i) {
                    throughput[i] *= std::exp(-sigma_t[i] * hit.t);
                }
            }
        }
        if (bounce == 0) {
            totalRadiance += throughput * RGBToSpectrum(hit.emission, lambda);
        } else {
            float misWeight = (lastBsdfPdf <= 0.0f) ? 1.0f : PowerHeuristic(lastBsdfPdf, hit.emissionPdf);
            totalRadiance += throughput * RGBToSpectrum(hit.emission, lambda) * misWeight;
        }

        // --- Apply dispersion (Fix 5: moved before direct lighting) ---
        float iorBase = hit.ior;
        float CauchyC = 10000.0f;
        float CauchyB = iorBase - CauchyC / (589.3f * 589.3f);
        hit.ior = CauchyB + CauchyC / (lambda.lambda[0] * lambda.lambda[0]);



        // --- Direct Lighting (Light Sampling) ---
        if (!_activeLights.empty()) {
            LightSample ls = {0}; auto EvaluatePHat = [&](const GfVec3f& lDir, const GfVec3f& lColor) -> float {
                float nDotL = std::max(0.0f, GfDot(shadingNormal, lDir));
                if (nDotL <= 0.0f) return 0.0f;
                
                float effectiveSubsurface = hit.subsurface;
                GfVec3f finalDiffuse = hit.baseColor * (1.0f - effectiveSubsurface) + hit.subsurfaceColor * effectiveSubsurface;
                float effectiveTransmission = hit.transmission * (1.0f - hit.subsurface);
                GfVec3f diffuseBase = finalDiffuse * (1.0f - hit.metallic) * (1.0f - effectiveTransmission) / (float)M_PI;
                GfVec3f F0 = hit.specular * hit.specularColor * (1.0f - hit.metallic) + hit.baseColor * hit.metallic;
                GfVec3f combined = diffuseBase + F0;
                
                GfVec3f radiance = GfCompMult(combined, lColor) * nDotL;
                return 0.2126f * radiance[0] + 0.7152f * radiance[1] + 0.0722f * radiance[2];
            };

            GfVec3f lDir;
            float lightDist = 1e30f;
            GfVec3f lColor(0.0f);
            float lightPdf = 1.0f;
            float restirWeight = 1.0f;
            HdGeminiLight* light = nullptr;
            
            bool useRestir = (pixelX >= 0 && pixelY >= 0 && depth == 0 && _enableRestirDI);
            Reservoir temporalReservoirLocal;
            Reservoir* temporalReservoir = nullptr;
            
            if (useRestir) {
                int prevX = pixelX;
                int prevY = pixelY;
                bool foundPrev = false;

                if (hit.hitMesh && hit.hit) {
                    GfVec3f localPos = hit.hitMesh->GetTransform().GetInverse().Transform(hitPos);
                    GfVec3f prevWorldPos = hit.hitMesh->GetPreviousTransform().Transform(localPos);

                    GfVec3d prevCamPos = _previousViewMatrix.Transform(GfVec3d(prevWorldPos[0], prevWorldPos[1], prevWorldPos[2]));
                    
                    if (prevCamPos[2] < 0.0) {
                        GfVec3d prevNdcPos = _previousProjMatrix.Transform(prevCamPos);

                        if (prevNdcPos[0] >= -1.0 && prevNdcPos[0] <= 1.0 && prevNdcPos[1] >= -1.0 && prevNdcPos[1] <= 1.0) {
                            prevX = (int)((prevNdcPos[0] + 1.0) * 0.5 * _lastWidth);
                            prevY = (int)((prevNdcPos[1] + 1.0) * 0.5 * _lastHeight);
                            
                            if (prevX >= 0 && prevX < _lastWidth && prevY >= 0 && prevY < _lastHeight) {
                                foundPrev = true;
                            }
                        }
                    }
                }
                
                if (foundPrev && !_prevTemporalReservoirs.empty()) {
                    Reservoir prevRes = _prevTemporalReservoirs[prevY * _lastWidth + prevX];
                    
                    float depthDiff = std::abs(prevRes.depth - hit.t);
                    float normalDot = GfDot(prevRes.normal, hit.normal);
                    
                    if (depthDiff < 0.1f * hit.t && normalDot > 0.8f) {
                        temporalReservoirLocal = prevRes;
                    }
                }
                temporalReservoir = &temporalReservoirLocal;

                Reservoir risReservoir;
                const int M_init = 8;
                for (int m = 0; m < M_init; ++m) {
                    float uLight = qmc::SampleDimension(sampleIdx, qmcDim++, rng);
                    auto itLight = std::lower_bound(_lightPowerCdf.begin(), _lightPowerCdf.end(), uLight);
                    int lightIdx = std::min((int)std::distance(_lightPowerCdf.begin(), itLight), (int)_activeLights.size() - 1);
                    
                    LightSample cand;
                    cand.lightIdx = lightIdx;
                    cand.u = qmc::SampleDimension(sampleIdx, qmcDim++, rng);
                    cand.v = qmc::SampleDimension(sampleIdx, qmcDim++, rng);
                    
                    GfVec3f cLDir; float cLDist; GfVec3f cLColor; float cLPdf;
                    EvaluateLight(cand, hitPos, cLDir, cLDist, cLColor, cLPdf);
                    float p_hat = EvaluatePHat(cLDir, cLColor);
                    
                    float weight = (cLPdf > 0.0f) ? (p_hat / cLPdf) : 0.0f;
                    float randVal = qmc::SampleDimension(sampleIdx, qmcDim++, rng);
                    risReservoir.Update(cand, weight, randVal);
                }
                
                float p_hat_ris = 0.0f;
                if (risReservoir.sample.lightIdx != -1) {
                    GfVec3f cLDir; float cLDist; GfVec3f cLColor; float cLPdf;
                    EvaluateLight(risReservoir.sample, hitPos, cLDir, cLDist, cLColor, cLPdf);
                    p_hat_ris = EvaluatePHat(cLDir, cLColor);
                    risReservoir.W = (p_hat_ris > 0.0f) ? (risReservoir.w_sum / (p_hat_ris * risReservoir.M)) : 0.0f;
                }
                
                Reservoir combined;
                float randVal1 = qmc::SampleDimension(sampleIdx, qmcDim++, rng);
                combined.Update(risReservoir.sample, p_hat_ris * risReservoir.W * risReservoir.M, randVal1);
                combined.M += risReservoir.M;
                
                float p_hat_temporal = 0.0f;
                if (temporalReservoir->sample.lightIdx != -1 && temporalReservoir->M > 0) {
                    temporalReservoir->M = std::min(temporalReservoir->M, 20 * M_init);
                    GfVec3f cLDir; float cLDist; GfVec3f cLColor; float cLPdf;
                    EvaluateLight(temporalReservoir->sample, hitPos, cLDir, cLDist, cLColor, cLPdf);
                    p_hat_temporal = EvaluatePHat(cLDir, cLColor);
                    
                    float randVal2 = qmc::SampleDimension(sampleIdx, qmcDim++, rng);
                    combined.Update(temporalReservoir->sample, p_hat_temporal * temporalReservoir->W * temporalReservoir->M, randVal2);
                    combined.M += temporalReservoir->M;
                }
                
                float p_hat_final = 0.0f;
                if (combined.sample.lightIdx != -1) {
                    EvaluateLight(combined.sample, hitPos, lDir, lightDist, lColor, lightPdf);
                    p_hat_final = EvaluatePHat(lDir, lColor);
                    combined.W = (p_hat_final > 0.0f) ? (combined.w_sum / (p_hat_final * combined.M)) : 0.0f;
                    light = _activeLights[combined.sample.lightIdx];
                }
                
                *temporalReservoir = combined;
                temporalReservoir->depth = hit.t;
                temporalReservoir->normal = hit.normal;
                
                if (!_temporalReservoirs.empty() && pixelY >= 0 && pixelX >= 0) {
                    size_t idx = (size_t)pixelY * _lastWidth + pixelX;
                    if (idx < _temporalReservoirs.size()) {
                        _temporalReservoirs[idx] = *temporalReservoir;
                    }
                }
                
                restirWeight = combined.W;
            } else {
                float uLight = qmc::SampleDimension(sampleIdx, qmcDim++, rng);
                auto itLight = std::lower_bound(_lightPowerCdf.begin(), _lightPowerCdf.end(), uLight);
                int lightIdx = std::min((int)std::distance(_lightPowerCdf.begin(), itLight), (int)_activeLights.size() - 1);
                LightSample cand;
                cand.lightIdx = lightIdx;
                cand.u = qmc::SampleDimension(sampleIdx, qmcDim++, rng);
                cand.v = qmc::SampleDimension(sampleIdx, qmcDim++, rng);
                EvaluateLight(cand, hitPos, lDir, lightDist, lColor, lightPdf);
                light = _activeLights[cand.lightIdx];
            }

            if (light && lightDist > 0 && (lColor[0] > 0 || lColor[1] > 0 || lColor[2] > 0)) {
                float nDotL = std::max(0.0f, GfDot(shadingNormal, lDir));
                if (nDotL > 0) {
                    GfVec3f shadowOrigin = hitPos + shadingNormal * RAY_EPSILON(hitPos);
                    SampledSpectrum shadowTransmittance = _TraceShadowRay(shadowOrigin, lDir, lightDist - 1e-3f, shadowCurrentlyInside, shadowTransDepth, shadowTransColor, shadowTransScatter, renderThread, sampleIdx, qmcDim, rng, lambda);
                    
                    bool hasTransmittance = false;
                    for(int i=0; i<SPECTRUM_SAMPLES; ++i) if (shadowTransmittance[i] > 0.0f) hasTransmittance = true;
                    
                    if (hasTransmittance) {
                        SampledSpectrum specLColor = RGBToSpectrum(lColor, lambda);
                        for(int i=0; i<SPECTRUM_SAMPLES; ++i) specLColor[i] *= shadowTransmittance[i];

                        
                        GfVec3f v = -currentRayDir;
                        GfVec3f l = lDir;
                        GfVec3f h = (v + l).GetNormalized();
                        float nDotL_eval = std::max(0.001f, nDotL);
                        float nDotV_eval = std::max(0.001f, GfDot(shadingNormal, v));
                        float nDotH = std::max(0.001f, GfDot(shadingNormal, h));
                        float lDotH = std::max(0.001f, GfDot(l, h));

                        // Specular GGX Evaluation
                        float alpha = std::max(0.001f, hit.roughness * hit.roughness);
                        float alpha2 = alpha * alpha;
                        float D = alpha2 / (float)(M_PI * std::pow(nDotH * nDotH * (alpha2 - 1.0f) + 1.0f, 2.0f));
                        // Fix 6: Exact Smith-GGX geometry term
                        float G_l = 2.0f * nDotL_eval / (nDotL_eval + std::sqrt(alpha2 + (1.0f - alpha2) * nDotL_eval * nDotL_eval));
                        float G_v = 2.0f * nDotV_eval / (nDotV_eval + std::sqrt(alpha2 + (1.0f - alpha2) * nDotV_eval * nDotV_eval));
                        float G = G_l * G_v;
                        
                        float f0_ior = (hit.ior - 1.0f) / (hit.ior + 1.0f);
                        f0_ior *= f0_ior;
                        GfVec3f F0 = hit.specular * hit.specularColor * (1.0f - hit.metallic) * f0_ior + hit.baseColor * hit.metallic;
                        GfVec3f F = F0 + (GfVec3f(1.0f) - F0) * std::pow(1.0f - lDotH, 5.0f);
                        GfVec3f specBsdf = (F * D * G) / (4.0f * nDotL_eval * nDotV_eval);

                        // Fix 8 & 19: apply (1-F) and always blend subsurface color for visual approximation
                        float effectiveSubsurface = hit.subsurface;
                        GfVec3f finalDiffuse = hit.baseColor * (1.0f - effectiveSubsurface) + hit.subsurfaceColor * effectiveSubsurface;
                        float effectiveTransmission = hit.transmission * (1.0f - hit.subsurface);
                        GfVec3f diffuseBase = finalDiffuse * (1.0f - hit.metallic) * (1.0f - effectiveTransmission) / (float)M_PI;
                        GfVec3f diffBsdf = GfCompMult(diffuseBase, GfVec3f(1.0f) - F);

                        // Fix 7: Apply coat layer attenuation to direct lighting
                        float coatAtten = 1.0f;
                        GfVec3f coatSpecDirect(0.0f);
                        if (hit.coat > 0.0f && !isInside) {
                            float coatF = hit.coat * FresnelDielectric(GfDot(-currentRayDir, shadingNormal), mediumStack[mediumStackCount-1].ior, hit.coatIor);
                            coatAtten = 1.0f - coatF;
                            // Coat specular lobe
                            float coatAlpha = std::max(0.001f, hit.coatRoughness * hit.coatRoughness);
                            float coatAlpha2 = coatAlpha * coatAlpha;
                            float D_coat = coatAlpha2 / (float)(M_PI * std::pow(nDotH * nDotH * (coatAlpha2 - 1.0f) + 1.0f, 2.0f));
                            float G_coat_l = 2.0f * nDotL_eval / (nDotL_eval + std::sqrt(coatAlpha2 + (1.0f - coatAlpha2) * nDotL_eval * nDotL_eval));
                            float G_coat_v = 2.0f * nDotV_eval / (nDotV_eval + std::sqrt(coatAlpha2 + (1.0f - coatAlpha2) * nDotV_eval * nDotV_eval));
                            float G_coat = G_coat_l * G_coat_v;
                            coatSpecDirect = GfVec3f(coatF * D_coat * G_coat / (4.0f * nDotL_eval * nDotV_eval));
                            coatSpecDirect = GfCompMult(coatSpecDirect, hit.coatColor);
                        }

                        GfVec3f combinedBsdf = (diffBsdf + specBsdf) * coatAtten + coatSpecDirect;
                        SampledSpectrum bsdf = RGBToSpectrum(combinedBsdf, lambda);

                        // PDF for MIS
                        float fresnel_eval = FresnelDielectric(nDotV_eval, etaI_fresnel, etaT_fresnel);
                        float reflectProb = fresnel_eval * hit.specular;
                        if (hit.metallic > 0.0f) reflectProb = std::max(reflectProb, hit.metallic);
                        
                        float diffPdf = nDotL_eval / (float)M_PI;
                        float specPdf = (D * nDotH) / (4.0f * lDotH);
                        float bsdfPdf = reflectProb * specPdf + (1.0f - reflectProb) * (1.0f - hit.transmission) * diffPdf;
                        
                        bool isDeltaLight = (light->GetLightType() == HdPrimTypeTokens->distantLight);
                        float misWeight = isDeltaLight ? 1.0f : PowerHeuristic(lightPdf, bsdfPdf);
                        
                        if (useRestir) {
                            totalRadiance += throughput * bsdf * specLColor * restirWeight;
                        } else {
                            totalRadiance += throughput * bsdf * specLColor * (nDotL / (lightPdf + 1e-6f)) * misWeight;
                        }
                    }
                }
            }
        }

        // --- Physical Sun Direct Lighting ---
        if (_enablePhysicalSky && physicalSunDir[1] > -0.05f) {
            GfVec3f sunIntensity(120000.0f * std::exp2(_physicalSkySunExposure));
            GfVec3f sunColor = GfCompMult(_GetSunTransmittance(physicalSunDir), sunIntensity); // Sun intensity multiplier
            if (sunColor[0] > 0 || sunColor[1] > 0 || sunColor[2] > 0) {
                float nDotL = std::max(0.0f, GfDot(shadingNormal, physicalSunDir));
                if (nDotL > 0) {
                    GfVec3f shadowOrigin = hitPos + shadingNormal * RAY_EPSILON(hitPos);
                    SampledSpectrum shadowTransmittance = _TraceShadowRay(shadowOrigin, physicalSunDir, 1e30f, shadowCurrentlyInside, shadowTransDepth, shadowTransColor, shadowTransScatter, renderThread, sampleIdx, qmcDim, rng, lambda);
                    
                    bool hasTransmittance = false;
                    for(int i=0; i<SPECTRUM_SAMPLES; ++i) if (shadowTransmittance[i] > 0.0f) hasTransmittance = true;
                    
                    if (hasTransmittance) {
                        SampledSpectrum specLColor = RGBToSpectrum(sunColor, lambda);
                        for(int i=0; i<SPECTRUM_SAMPLES; ++i) specLColor[i] *= shadowTransmittance[i];
                        
                        GfVec3f v = -currentRayDir;
                        GfVec3f l = physicalSunDir;
                        GfVec3f h = (v + l).GetNormalized();
                        float nDotL_eval = std::max(0.001f, nDotL);
                        float nDotV_eval = std::max(0.001f, GfDot(shadingNormal, v));
                        float nDotH = std::max(0.001f, GfDot(shadingNormal, h));
                        float lDotH = std::max(0.001f, GfDot(l, h));

                        float alpha = std::max(0.001f, hit.roughness * hit.roughness);
                        float alpha2 = alpha * alpha;
                        float D = alpha2 / (float)(M_PI * std::pow(nDotH * nDotH * (alpha2 - 1.0f) + 1.0f, 2.0f));
                        // Fix 6: Exact Smith-GGX geometry term (sun)
                        float G_l = 2.0f * nDotL_eval / (nDotL_eval + std::sqrt(alpha2 + (1.0f - alpha2) * nDotL_eval * nDotL_eval));
                        float G_v = 2.0f * nDotV_eval / (nDotV_eval + std::sqrt(alpha2 + (1.0f - alpha2) * nDotV_eval * nDotV_eval));
                        float G = G_l * G_v;
                        
                        float f0_ior = (hit.ior - 1.0f) / (hit.ior + 1.0f);
                        f0_ior *= f0_ior;
                        GfVec3f F0 = hit.specular * hit.specularColor * (1.0f - hit.metallic) * f0_ior + hit.baseColor * hit.metallic;
                        GfVec3f F = F0 + (GfVec3f(1.0f) - F0) * std::pow(1.0f - lDotH, 5.0f);
                        GfVec3f specBsdf = (F * D * G) / (4.0f * nDotL_eval * nDotV_eval);

                        // Always blend subsurface color for visual approximation
                        float effectiveSubsurface = hit.subsurface;
                        GfVec3f finalDiffuse = hit.baseColor * (1.0f - effectiveSubsurface) + hit.subsurfaceColor * effectiveSubsurface;
                        float effectiveTransmission = hit.transmission * (1.0f - hit.subsurface);
                        GfVec3f diffuseBase = finalDiffuse * (1.0f - hit.metallic) * (1.0f - effectiveTransmission) / (float)M_PI;
                        GfVec3f diffBsdf = GfCompMult(diffuseBase, GfVec3f(1.0f) - F);

                        SampledSpectrum bsdf = RGBToSpectrum(diffBsdf + specBsdf, lambda);
                        totalRadiance += throughput * bsdf * specLColor * nDotL; // PDF is 1 for directional sun (Delta light MIS = 1)
                    }
                }
            }
        }

        // --- Coat Layer (Fix 2: energy conservation) ---
        if (hit.coat > 0.0f && !isInside) {
            float coatFresnel = hit.coat * FresnelDielectric(GfDot(currentRayDir, shadingNormal), mediumStack[mediumStackCount-1].ior, hit.coatIor);
            if (qmc::SampleDimension(sampleIdx, qmcDim++, rng) < coatFresnel) {
                if (reflectionBounces >= (isInteractive ? 1 : _maxReflectionBounces)) break;
                reflectionBounces++;
                GfVec3f reflectDir;
                if (hit.coatRoughness > 0.0f) {
                    float u1 = qmc::SampleDimension(sampleIdx, qmcDim++, rng);
                    float u2 = qmc::SampleDimension(sampleIdx, qmcDim++, rng);
                    GfVec3f h = AlignToNormal(SampleGGX(u1, u2, hit.coatRoughness), shadingNormal);
                    reflectDir = (currentRayDir - 2.0f * GfDot(currentRayDir, h) * h).GetNormalized();
                    if (GfDot(reflectDir, shadingNormal) < 0) {
                        reflectDir = (currentRayDir - 2.0f * GfDot(currentRayDir, shadingNormal) * shadingNormal).GetNormalized();
                    }
                } else {
                    reflectDir = (currentRayDir - 2.0f * GfDot(currentRayDir, shadingNormal) * shadingNormal).GetNormalized();
                }
                currentRayDir = reflectDir;
                currentRayOrigin = hitPos + shadingNormal * RAY_EPSILON(hitPos);
                throughput = throughput * RGBToSpectrum(hit.coatColor, lambda);
                lastBsdfPdf = 0.0f;
                continue;
            }
            // Coat was NOT chosen — attenuate base layer throughput
            throughput = throughput * (1.0f - coatFresnel) * RGBToSpectrum(hit.coatColor, lambda);
        }

        // --- Sheen Layer (Fix 3: energy conservation) ---
        if (hit.sheen > 0.0f && !isInside) {
            float cosTheta = std::max(0.0f, GfDot(-currentRayDir, shadingNormal));
            float sheenFresnel = hit.sheen * std::pow(1.0f - cosTheta, 5.0f);
            if (qmc::SampleDimension(sampleIdx, qmcDim++, rng) < sheenFresnel) {
                if (reflectionBounces >= (isInteractive ? 1 : _maxReflectionBounces)) break;
                reflectionBounces++;
                GfVec3f reflectDir;
                if (hit.sheenRoughness > 0.0f) {
                    float u1 = qmc::SampleDimension(sampleIdx, qmcDim++, rng);
                    float u2 = qmc::SampleDimension(sampleIdx, qmcDim++, rng);
                    GfVec3f h = AlignToNormal(SampleGGX(u1, u2, hit.sheenRoughness), shadingNormal);
                    reflectDir = (currentRayDir - 2.0f * GfDot(currentRayDir, h) * h).GetNormalized();
                    if (GfDot(reflectDir, shadingNormal) < 0) {
                        reflectDir = (currentRayDir - 2.0f * GfDot(currentRayDir, shadingNormal) * shadingNormal).GetNormalized();
                    }
                } else {
                    reflectDir = (currentRayDir - 2.0f * GfDot(currentRayDir, shadingNormal) * shadingNormal).GetNormalized();
                }
                currentRayDir = reflectDir;
                currentRayOrigin = hitPos + shadingNormal * RAY_EPSILON(hitPos);
                throughput = throughput * RGBToSpectrum(hit.sheenColor, lambda);
                lastBsdfPdf = 0.0f;
                continue;
            }
            // Sheen was NOT chosen — attenuate base layer throughput
            throughput = throughput * (1.0f - sheenFresnel);
        }

        // (Fix 5: dispersion moved before direct lighting)

        // --- SPPM Caustic Gathering ---
        if (_enableSPPM && !_photonMap.photons.empty() && (reflectionBounces > 0 || refractionBounces > 0 || hit.roughness > 0.1f) && hit.metallic < 1.0f && hit.transmission < 1.0f) {
            float r = _photonMap.searchRadius;
            SampledSpectrum causticFlux(0.0f);
            
            // Spatial Hash Lookup
            for (int dx = -1; dx <= 1; ++dx) {
                for (int dy = -1; dy <= 1; ++dy) {
                    for (int dz = -1; dz <= 1; ++dz) {
                        GfVec3f neighbor = hitPos + GfVec3f(dx * r, dy * r, dz * r);
                        uint64_t nh = _photonMap.Hash(neighbor);
                        auto it = _photonMap.spatialHash.find(nh);
                        if (it != _photonMap.spatialHash.end()) {
                            for (int idx : it->second) {
                                const Photon& p = _photonMap.photons[idx];
                                GfVec3f diff = p.pos - hitPos;
                                if (GfDot(diff, diff) <= r * r) {
                                    // Diffuse evaluation
                                    float nDotWi = std::max(0.0f, GfDot(shadingNormal, p.wi));
                                    if (nDotWi > 0) {
                                        SampledSpectrum pPower = RGBToSpectrum(p.powerRGB, lambda);
                                        float effectiveSubsurface = hit.subsurface;
                                        GfVec3f finalDiffuse = hit.baseColor * (1.0f - effectiveSubsurface) + hit.subsurfaceColor * effectiveSubsurface;
                                        causticFlux += pPower * RGBToSpectrum(finalDiffuse, lambda) * (float)(1.0 / M_PI);
                                    }
                                }
                            }
                        }
                    }
                }
            }
            
            int numPhotonsTraced = 10000;
            float area = M_PI * r * r;
            SampledSpectrum causticRadiance = causticFlux / (area * numPhotonsTraced);
            totalRadiance += throughput * causticRadiance;
        }

        // --- End of Direct Lighting / ReSTIR GI Virtual Light Capture ---
        // Capture on the bounce AFTER the primary was captured
        if (useRestirGI && giPrimaryCaptured && !hasGiData && bounce == giPrimaryBounce + 1) {
            if (hit.transmission < 0.5f) {
                giVirtualLightPos = hitPos;
                giVirtualLightNormal = shadingNormal;
                
                SampledSpectrum bounceRad = totalRadiance - totalRadianceBeforeBounce;
                for(int i=0; i<SPECTRUM_SAMPLES; ++i) {
                    float val = bounceRad[i] / std::max(throughput[i], 1e-6f);
                    giVirtualRadiance[i] = (std::isnan(val) || std::isinf(val)) ? 0.0f : val;
                }
                totalRadiance = totalRadianceBeforeBounce; // Undo addition to totalRadiance
                hasGiData = true;
                break; // Stop path tracing base path
            } else {
                // If the bounce after primary is transmissive, we cannot straight-line connect!
                // Disable ReSTIR GI for this ray and continue normally.
                useRestirGI = false;
            }
        }
        
        // --- Indirect Path Selection (BSDF Sampling) ---
        float fresnel = FresnelDielectric(GfDot(currentRayDir, hit.smoothNormal), etaI_fresnel, etaT_fresnel);
        float reflectProb = fresnel * hit.specular;
        if (hit.metallic > 0.0f) reflectProb = std::max(reflectProb, hit.metallic);
        reflectProb = std::min(reflectProb, 1.0f);
        
        float effectiveTransmission = hit.transmission * (1.0f - hit.subsurface);
        
        float randVal = qmc::SampleDimension(sampleIdx, qmcDim++, rng);

        if (randVal < reflectProb) {
            // Reflection (Fix 1: apply GGX importance sampling weight)
            if (reflectionBounces >= (isInteractive ? 1 : _maxReflectionBounces)) break;
            reflectionBounces++;
            GfVec3f reflectDir;
            float ggxWeight = 1.0f;
            if (hit.roughness > 0.0f) {
                float u1 = qmc::SampleDimension(sampleIdx, qmcDim++, rng);
                float u2 = qmc::SampleDimension(sampleIdx, qmcDim++, rng);
                GfVec3f h = AlignToNormal(SampleGGX(u1, u2, hit.roughness), shadingNormal);
                reflectDir = (currentRayDir - 2.0f * GfDot(currentRayDir, h) * h).GetNormalized();
                
                float nDotH = std::max(0.001f, GfDot(shadingNormal, h));
                float vDotH = std::max(0.001f, std::abs(GfDot(-currentRayDir, h)));
                float nDotV = std::max(0.001f, std::abs(GfDot(shadingNormal, -currentRayDir)));
                float nDotL = std::max(0.001f, GfDot(shadingNormal, reflectDir));
                float alpha = std::max(0.001f, hit.roughness * hit.roughness);
                float alpha2 = alpha * alpha;
                float D = alpha2 / (float)(M_PI * std::pow(nDotH * nDotH * (alpha2 - 1.0f) + 1.0f, 2.0f));
                float specPdf = (D * nDotH) / (4.0f * std::max(vDotH, 1e-6f));
                lastBsdfPdf = specPdf * reflectProb;
                
                // Fix 1: Smith-GGX geometry weight for importance sampling
                float G1_v = 2.0f * nDotV / (nDotV + std::sqrt(alpha2 + (1.0f - alpha2) * nDotV * nDotV));
                float G1_l = 2.0f * nDotL / (nDotL + std::sqrt(alpha2 + (1.0f - alpha2) * nDotL * nDotL));
                ggxWeight = (G1_v * G1_l * vDotH) / (nDotV * nDotH);
                ggxWeight = std::min(ggxWeight, 10.0f); // Clamp to avoid fireflies
                
                if (GfDot(reflectDir, shadingNormal) <= 0.0f) {
                    break;
                }
            } else {
                reflectDir = (currentRayDir - 2.0f * GfDot(currentRayDir, shadingNormal) * shadingNormal).GetNormalized();
                lastBsdfPdf = 0.0f;
            }
            currentRayDir = reflectDir;
            currentRayOrigin = hitPos + shadingNormal * RAY_EPSILON(hitPos);
            
            GfVec3f reflTint = hit.specularColor * (1.0f - hit.metallic) + hit.baseColor * hit.metallic;
            throughput = throughput * RGBToSpectrum(reflTint, lambda) * ggxWeight;
        } else {
            // Fix 27: guard against divide-by-zero
            float remainingProb = (reflectProb >= 1.0f) ? 0.0f : (randVal - reflectProb) / (1.0f - reflectProb);
            
            // Metals absorb non-reflected energy
            if (remainingProb > (1.0f - hit.metallic)) {
                break;
            }
            remainingProb = remainingProb / std::max(1.0f - hit.metallic, 1e-6f);
            
            float sssProb = hit.subsurface;
            float transProb = hit.transmission * (1.0f - hit.subsurface);
            float diffProb = 1.0f - sssProb - transProb;
            
            if (sssProb + transProb > 1e-6f && remainingProb < sssProb + transProb) {
                // Refraction (Both Transmission and SSS refract into the volume)
                float etaI = mediumStack[mediumStackCount-1].ior;
                float etaT = hit.ior;
                if (isInside) {
                    etaT = (mediumStackCount > 1) ? mediumStack[mediumStackCount - 2].ior : 1.0f;
                }
                
                float eta = etaI / etaT;
                GfVec3f n = shadingNormal;
                float cosThetaI = GfDot(currentRayDir, n); // < 0
                float k = 1.0f - eta * eta * (1.0f - cosThetaI * cosThetaI);
                
                if (k >= 0) {
                    if (refractionBounces >= (isInteractive ? 1 : _maxRefractionBounces)) break;
                    refractionBounces++;
                    GfVec3f refractDir = (eta * currentRayDir - (eta * cosThetaI + std::sqrt(k)) * n).GetNormalized();
                    
                    // Microfacet refraction direction
                    bool transmitted = true;
                    if (hit.roughness > 0.0f) {
                        float u1 = qmc::SampleDimension(sampleIdx, qmcDim++, rng);
                        float u2 = qmc::SampleDimension(sampleIdx, qmcDim++, rng);
                        GfVec3f h = AlignToNormal(SampleGGX(u1, u2, hit.roughness), n);
                        float cosThetaI_h = GfDot(currentRayDir, h); // < 0
                        float k_h = 1.0f - eta * eta * (1.0f - cosThetaI_h * cosThetaI_h);
                        if (k_h >= 0.0f) {
                            refractDir = (eta * currentRayDir - (eta * cosThetaI_h + std::sqrt(k_h)) * h).GetNormalized();
                        } else {
                            refractDir = (currentRayDir - 2.0f * cosThetaI_h * h).GetNormalized(); // TIR on microfacet
                            transmitted = false;
                        }
                    }
                    
                    if (transmitted) {
                        currentRayDir = refractDir;
                        currentRayOrigin = hitPos - n * RAY_EPSILON(hitPos);
                        if (isInside) {
                            if (mediumStackCount > 1) mediumStackCount--;
                        } else {
                            if (remainingProb >= sssProb) {
                                // It is Transmission
                                if (mediumStackCount < 16) mediumStack[mediumStackCount++] = MediumState{hit.ior, hit.transmissionScatter, hit.transmissionDepth, hit.transmissionColor};
                            } else {
                                // It is SSS
                                // For SSS, subsurfaceColor acts as scatter color, subsurfaceScale acts as depth
                                if (mediumStackCount < 16) mediumStack[mediumStackCount++] = MediumState{hit.ior, hit.subsurfaceColor, hit.subsurfaceScale, hit.baseColor};
                            }
                        }
                        
                        if (remainingProb >= sssProb) {
                            // It is Transmission
                            throughput = throughput * RGBToSpectrum(hit.transmissionColor, lambda);
                        }
                    } else {
                        // TIR on microfacet
                        currentRayDir = refractDir;
                        currentRayOrigin = hitPos + n * RAY_EPSILON(hitPos);
                        // iorStack remains unchanged
                    }
                    
                    lastBsdfPdf = 0.0f;
                } else {
                    // Total Internal Reflection
                    if (reflectionBounces >= (isInteractive ? 1 : _maxReflectionBounces)) break;
                    reflectionBounces++;
                    GfVec3f reflectDir = (currentRayDir - 2.0f * cosThetaI * n).GetNormalized();
                    currentRayDir = reflectDir;
                    currentRayOrigin = hitPos + n * RAY_EPSILON(hitPos);
                    lastBsdfPdf = 0.0f;
                }
            } else {
                // Diffuse
                float u1 = qmc::SampleDimension(sampleIdx, qmcDim++, rng);
                float u2 = qmc::SampleDimension(sampleIdx, qmcDim++, rng);
                
                GfVec3f diffuseDir;
                float pdfBsdf;
                float pdfGuide = 0.0f;
                
                uint32_t h = _guidingGrid.Hash(hitPos);
                const GuidingVoxel& voxel = _guidingGrid.voxels[h];
                
                if (_enablePathGuiding && voxel.totalRadiance > 0) {
                    float u3 = qmc::SampleDimension(sampleIdx, qmcDim++, rng);
                    float guideProb = 0.5f;
                    
                    if (qmc::SampleDimension(sampleIdx, qmcDim++, rng) < guideProb) {
                        diffuseDir = voxel.Sample(u1, u2, u3);
                        if (GfDot(diffuseDir, shadingNormal) <= 0) {
                            diffuseDir = AlignToNormal(SampleCosineHemisphere(u1, u2), shadingNormal);
                        }
                    } else {
                        diffuseDir = AlignToNormal(SampleCosineHemisphere(u1, u2), shadingNormal);
                    }
                    
                    float nDotL = std::max(0.0f, GfDot(shadingNormal, diffuseDir));
                    pdfBsdf = nDotL / (float)M_PI;
                    pdfGuide = voxel.Pdf(diffuseDir);
                    
                    float combinedPdf = guideProb * pdfGuide + (1.0f - guideProb) * pdfBsdf;
                    if (combinedPdf < 1e-6f || nDotL <= 0) break;
                    
                    lastBsdfPdf = combinedPdf * (1.0f - reflectProb) * diffProb;
                    
                    float fresnelOut = FresnelDielectric(GfDot(diffuseDir, shadingNormal), etaI_fresnel, etaT_fresnel);
                    float diffFresnelAtten = 1.0f - fresnelOut * hit.specular;
                    GfVec3f diffuseBase = hit.baseColor * diffFresnelAtten / (float)M_PI;
                    
                    throughput = throughput * RGBToSpectrum(diffuseBase, lambda) * (nDotL / combinedPdf);
                    currentRayDir = diffuseDir;
                } else {
                    diffuseDir = AlignToNormal(SampleCosineHemisphere(u1, u2), shadingNormal);
                    float nDotL = std::max(0.0f, GfDot(shadingNormal, diffuseDir));
                    pdfBsdf = nDotL / (float)M_PI;
                    if (pdfBsdf < 1e-6f) break;
                    
                    lastBsdfPdf = pdfBsdf * (1.0f - reflectProb) * diffProb;
                    
                    float fresnelOut = FresnelDielectric(GfDot(diffuseDir, shadingNormal), etaI_fresnel, etaT_fresnel);
                    float diffFresnelAtten = 1.0f - fresnelOut * hit.specular;
                    
                    GfVec3f diffuseBase = hit.baseColor * diffFresnelAtten / (float)M_PI;
                    throughput = throughput * RGBToSpectrum(diffuseBase, lambda) * (float)M_PI;
                    currentRayDir = diffuseDir;
                }
                currentRayOrigin = hitPos + shadingNormal * RAY_EPSILON(hitPos);
            }
        }

        // --- Russian Roulette ---
        if (bounce > 3) {
            float p = throughput.Max();
            if (qmc::SampleDimension(sampleIdx, qmcDim++, rng) > p) break;
            throughput = throughput * (1.0f / p);
        }

        if (_enablePathGuiding) {
            PathVertex pv;
            pv.pos = hitPos;
            pv.dir = currentRayDir;
            pv.throughput = throughput;
            pv.totalRadianceBefore = totalRadiance;
            if (pathHistoryCount < 16) pathHistory[pathHistoryCount++] = pv;
        }
    }
    
    if (_enablePathGuiding && pathHistoryCount > 0) {
        for (int i = 0; i < pathHistoryCount; ++i) {
            const auto& pv = pathHistory[i];
            SampledSpectrum addedRadiance = totalRadiance - pv.totalRadianceBefore;
            GfVec3f returningL(0.0f);
            for(int i=0; i<SPECTRUM_SAMPLES; ++i) {
                if (pv.throughput[i] > 1e-6f) {
                    returningL[i] = addedRadiance[i] / pv.throughput[i];
                }
            }
            float lum = 0.2126f * returningL[0] + 0.7152f * returningL[1] + 0.0722f * returningL[2];
            if (lum > 0) {
                uint32_t h = _guidingGrid.Hash(pv.pos);
                _guidingGrid.voxels[h].Deposit(pv.dir, lum);
            }
        }
    }

    // --- ReSTIR GI ---
    if (useRestirGI && hasGiData && !_giPrevTemporalReservoirs.empty()) {
        GIReservoir r;
        
        // Target weight of base sample
        float lumBase = 0.2126f * giVirtualRadiance[0] + 0.7152f * giVirtualRadiance[1] + 0.0722f * giVirtualRadiance[2];
        float p_hat_base = lumBase * (throughput[0] + throughput[1] + throughput[2]) / 3.0f;
        
        float randValBase = qmc::SampleDimension(sampleIdx, qmcDim++, rng);
        r.Update(giVirtualLightPos, giVirtualLightNormal, GfVec3f(giVirtualRadiance[0], giVirtualRadiance[1], giVirtualRadiance[2]), (p_hat_base > 0.0f) ? 1.0f : 0.0f, randValBase);
        
        // Temporal Reuse
        if (giFoundPrev) {
            GIReservoir prevRes = _giPrevTemporalReservoirs[giPrevY * _lastWidth + giPrevX];
            float depthDiff = std::abs(prevRes.depth - giPrimaryDepth);
            float normalDot = GfDot(prevRes.normal, giPrimaryNormal);
            
            if (depthDiff < 0.1f * giPrimaryDepth && normalDot > 0.8f && prevRes.M > 0) {
                GfVec3f dir = prevRes.virtualLightPos - giPrimaryPos;
                float distSq = GfDot(dir, dir);
                if (distSq > 1e-4f) {
                    float dist = std::sqrt(distSq);
                    dir /= dist;
                    float nDotL = std::max(0.0f, GfDot(giPrimaryNormal, dir));
                    if (nDotL > 0.0f) {
                        float lumPrev = 0.2126f * prevRes.virtualLightRadiance[0] + 0.7152f * prevRes.virtualLightRadiance[1] + 0.0722f * prevRes.virtualLightRadiance[2];
                        float p_hat_prev = lumPrev * nDotL / distSq;
                        
                        float randValTemp = qmc::SampleDimension(sampleIdx, qmcDim++, rng);
                        r.Update(prevRes.virtualLightPos, prevRes.virtualLightNormal, prevRes.virtualLightRadiance, p_hat_prev * prevRes.W * std::min(prevRes.M, 20), randValTemp);
                    }
                }
            }
        }
        
        // Spatial Reuse
        const int spatialNeighbors = 3;
        for (int i = 0; i < spatialNeighbors; ++i) {
            float u1 = qmc::SampleDimension(sampleIdx, qmcDim++, rng);
            float u2 = qmc::SampleDimension(sampleIdx, qmcDim++, rng);
            int nx = pixelX + (int)((u1 - 0.5f) * 30.0f);
            int ny = pixelY + (int)((u2 - 0.5f) * 30.0f);
            if (nx >= 0 && nx < _lastWidth && ny >= 0 && ny < _lastHeight) {
                GIReservoir neighborRes = _giPrevTemporalReservoirs[ny * _lastWidth + nx];
                if (neighborRes.M > 0) {
                    GfVec3f dir = neighborRes.virtualLightPos - giPrimaryPos;
                    float distSq = GfDot(dir, dir);
                    if (distSq > 1e-4f) {
                        float dist = std::sqrt(distSq);
                        dir /= dist;
                        float nDotL = std::max(0.0f, GfDot(giPrimaryNormal, dir));
                        if (nDotL > 0.0f) {
                            // Visibility check for spatial reuse
                            GfVec3f shadowOrigin = giPrimaryPos + giPrimaryNormal * RAY_EPSILON(giPrimaryPos);
                            float shadowTransDepth = 0; GfVec3f sc(0); GfVec3f tc(0);
                            SampledSpectrum shadowVis = _TraceShadowRay(shadowOrigin, dir, dist - 1e-3f, false, shadowTransDepth, tc, sc, renderThread, sampleIdx, qmcDim, rng, lambda);
                            
                            if (shadowVis[0] > 0 || shadowVis[1] > 0 || shadowVis[2] > 0) {
                                float lumNeighbor = 0.2126f * neighborRes.virtualLightRadiance[0] + 0.7152f * neighborRes.virtualLightRadiance[1] + 0.0722f * neighborRes.virtualLightRadiance[2];
                                float p_hat_neighbor = lumNeighbor * nDotL / distSq;
                                float randValSpat = qmc::SampleDimension(sampleIdx, qmcDim++, rng);
                                r.Update(neighborRes.virtualLightPos, neighborRes.virtualLightNormal, neighborRes.virtualLightRadiance, p_hat_neighbor * neighborRes.W, randValSpat);
                            }
                        }
                    }
                }
            }
        }
        
        // Final evaluation
        float p_hat_final = 0.0f;
        GfVec3f finalDir = r.virtualLightPos - giPrimaryPos;
        float finalDistSq = GfDot(finalDir, finalDir);
        if (finalDistSq > 1e-4f) {
            float finalDist = std::sqrt(finalDistSq);
            finalDir /= finalDist;
            float nDotL = std::max(0.0f, GfDot(giPrimaryNormal, finalDir));
            if (nDotL > 0.0f) {
                // Final visibility check
                GfVec3f shadowOrigin = giPrimaryPos + giPrimaryNormal * RAY_EPSILON(giPrimaryPos);
                float shadowTransDepth = 0; GfVec3f sc(0); GfVec3f tc(0);
                SampledSpectrum shadowVis = _TraceShadowRay(shadowOrigin, finalDir, finalDist - 1e-3f, false, shadowTransDepth, tc, sc, renderThread, sampleIdx, qmcDim, rng, lambda);
                
                if (shadowVis[0] > 0 || shadowVis[1] > 0 || shadowVis[2] > 0) {
                    float lumFinal = 0.2126f * r.virtualLightRadiance[0] + 0.7152f * r.virtualLightRadiance[1] + 0.0722f * r.virtualLightRadiance[2];
                    p_hat_final = lumFinal * nDotL / finalDistSq;
                }
            }
        }
        
        r.W = (p_hat_final > 0.0f) ? (r.w_sum / (p_hat_final * r.M)) : 0.0f;
        if (std::isnan(r.W) || std::isinf(r.W)) r.W = 0.0f;
        r.W = std::min(r.W, 50.0f); // Clamp to prevent firefly explosions
        
        if (p_hat_final > 0.0f && r.W > 0.0f) {
            SampledSpectrum giResRadiance = RGBToSpectrum(r.virtualLightRadiance, lambda);
            // Diffuse BRDF approximation: albedo / pi
            SampledSpectrum diffAlbedo = RGBToSpectrum(giPrimaryAlbedo, lambda) * (1.0f / (float)M_PI);
            // Geometric term G = nDotL / distSq
            float nDotL_final = std::max(0.0f, GfDot(giPrimaryNormal, finalDir));
            float G = nDotL_final / std::max(finalDistSq, 1e-4f);
            G = std::min(G, 100.0f); // Bound the geometry term singularity to prevent massive fireflies
            
            // RIS estimator: throughput * f * L * G * W
            for(int i=0; i<SPECTRUM_SAMPLES; ++i) {
                float added = giPrimaryThroughput[i] * diffAlbedo[i] * giResRadiance[i] * G * r.W;
                if (!std::isnan(added) && !std::isinf(added)) {
                    totalRadiance[i] += std::min(added, 500.0f); // Hard clamp to avoid denoiser destruction
                }
            }
        }
        
        r.depth = giPrimaryDepth;
        r.normal = giPrimaryNormal;
        
        if (pixelX >= 0 && pixelY >= 0) {
            size_t idx = (size_t)pixelY * _lastWidth + pixelX;
            if (idx < _giTemporalReservoirs.size()) {
                _giTemporalReservoirs[idx] = r;
            }
        }
    }
    
    return totalRadiance;
}

void HdGeminiRenderer::_TracePhoton(class HdRenderThread* renderThread, uint32_t sampleIdx, uint32_t& rng, const SampledWavelengths& lambda)
{
    if (_activeLights.empty() || !_enableSPPM) return;
    
    uint32_t qmcDim = 0;
    float uLight = qmc::SampleDimension(sampleIdx, qmcDim++, rng);
    auto itLight = std::lower_bound(_lightPowerCdf.begin(), _lightPowerCdf.end(), uLight);
    int lightIdx = std::min((int)std::distance(_lightPowerCdf.begin(), itLight), (int)_activeLights.size() - 1);
    HdGeminiLight* light = _activeLights[lightIdx];
    
    float lightPdf = (_lightPowerCdf[lightIdx] - (lightIdx > 0 ? _lightPowerCdf[lightIdx - 1] : 0.0f));
    lightPdf = std::max(lightPdf, 1e-6f);
    
    GfVec3f origin(0.0f), dir(0.0f);
    GfVec3f powerRGB(0.0f);
    
    if (light->GetLightType() == HdPrimTypeTokens->rectLight) {
        float u = qmc::SampleDimension(sampleIdx, qmcDim++, rng) - 0.5f;
        float v = qmc::SampleDimension(sampleIdx, qmcDim++, rng) - 0.5f;
        GfVec3f lPosLocal(u * light->GetWidth(), v * light->GetHeight(), 0.0f);
        origin = GfMatrix4f(light->GetTransform()).Transform(lPosLocal);
        
        float u1 = qmc::SampleDimension(sampleIdx, qmcDim++, rng);
        float u2 = qmc::SampleDimension(sampleIdx, qmcDim++, rng);
        float r = std::sqrt(u1);
        float theta = 2.0f * M_PI * u2;
        GfVec3f localDir(r * std::cos(theta), r * std::sin(theta), -std::sqrt(std::max(0.0f, 1.0f - u1)));
        
        dir = GfMatrix4f(light->GetTransform()).TransformDir(localDir).GetNormalized();
        
        GfMatrix4f transform = GfMatrix4f(light->GetTransform());
        float scaleX = transform.TransformDir(GfVec3f(1, 0, 0)).GetLength();
        float scaleY = transform.TransformDir(GfVec3f(0, 1, 0)).GetLength();
        float area = (light->GetWidth() * scaleX) * (light->GetHeight() * scaleY);
        powerRGB = light->GetColor() * light->GetIntensity() * area * (float)M_PI / lightPdf;
    } else if (light->GetLightType() == HdPrimTypeTokens->distantLight || light->GetLightType() == HdPrimTypeTokens->domeLight) {
        return; // Skip infinite lights for standard photon mapping to avoid tracing useless photons
    } else {
        origin = GfMatrix4f(light->GetTransform()).ExtractTranslation();
        float u1 = qmc::SampleDimension(sampleIdx, qmcDim++, rng);
        float u2 = qmc::SampleDimension(sampleIdx, qmcDim++, rng);
        float z = 1.0f - 2.0f * u1;
        float r = std::sqrt(std::max(0.0f, 1.0f - z * z));
        float phi = 2.0f * M_PI * u2;
        dir = GfVec3f(r * std::cos(phi), r * std::sin(phi), z);
        
        powerRGB = light->GetColor() * light->GetIntensity() * 4.0f * (float)M_PI / lightPdf;
    }
    
    SampledSpectrum power = RGBToSpectrum(powerRGB, lambda);
    bool hasSpecularBounce = false;
    
    for (int bounce = 0; bounce < 10; ++bounce) {
        if (renderThread->IsStopRequested()) break;
        
        HitRecord hit;
        if (!this->_IntersectTLAS(origin, dir, hit, renderThread, sampleIdx, qmcDim, rng)) break;
        
        GfVec3f hitPos = origin + dir * hit.t;
        GfVec3f shadingNormal = hit.normal;
        if (GfDot(dir, shadingNormal) > 0) {
            shadingNormal = -shadingNormal;
        }
        
        if (hasSpecularBounce && hit.metallic < 1.0f && hit.transmission < 1.0f) {
            Photon p;
            p.pos = hitPos;
            p.wi = -dir;
            p.powerRGB = SpectrumToRGB(power, lambda);
            
            #pragma omp critical
            {
                _photonMap.AddPhoton(p);
            }
        }
        
        bool isSpecular = (hit.transmission > 0.5f || hit.roughness < 0.1f || hit.metallic > 0.5f);
        if (!isSpecular) {
            break;
        }
        
        float ior = hit.ior;
        bool isInside = (GfDot(dir, hit.normal) > 0);
        float n1 = 1.0f;
        float n2 = ior;
        if (isInside) std::swap(n1, n2);
        
        float cosThetaI = std::abs(GfDot(dir, hit.normal));
        float fresnel = FresnelDielectric(cosThetaI, n1, n2);
        if (hit.metallic > 0.0f) fresnel = 1.0f;
        
        float uBounce = qmc::SampleDimension(sampleIdx, qmcDim++, rng);
        if (uBounce < fresnel) {
            dir = dir - 2.0f * GfDot(dir, shadingNormal) * shadingNormal;
            dir = dir.GetNormalized();
            origin = hitPos + shadingNormal * RAY_EPSILON(hitPos);
            power = power * RGBToSpectrum(hit.baseColor, lambda);
            hasSpecularBounce = true;
        } else if (hit.transmission > 0.0f) {
            float eta = n1 / n2;
            float sin2ThetaT = eta * eta * (1.0f - cosThetaI * cosThetaI);
            if (sin2ThetaT >= 1.0f) break;
            
            float cosThetaT = std::sqrt(1.0f - sin2ThetaT);
            dir = eta * dir + (eta * cosThetaI - cosThetaT) * shadingNormal;
            dir = dir.GetNormalized();
            origin = hitPos - shadingNormal * RAY_EPSILON(hitPos);
            power = power * RGBToSpectrum(hit.transmissionColor, lambda);
            hasSpecularBounce = true;
        } else {
            break;
        }
    }
}

void
HdGeminiRenderer::_Denoise()
{
#ifdef HDGEMINI_HAS_OIDN
    unsigned int width = _dataWindow.GetWidth();
    unsigned int height = _dataWindow.GetHeight();
    if (width == 0 || height == 0 || !_colorBuffer) return;
    if (_accumHeroRGB.empty() || _accumDiffRGB.empty()) return;

    std::cout << "[Gemini] Running Full Spectral Demultiplexing Denoiser on frame " << _frameCount << "..." << std::endl;

    std::vector<float> albedo, normal;
    if (_albedoBuffer) _albedoBuffer->GetFloatBuffer(albedo);
    if (_normalBuffer) _normalBuffer->GetFloatBuffer(normal);

    std::vector<float> heroOutput(width * height * 3);
    std::vector<float> diffOutput(width * height * 3);

    float invSamples = 1.0f / (float)std::max(1, _frameCount);
    for(size_t i=0; i<width*height; ++i) {
        heroOutput[i*3+0] = _accumHeroRGB[i][0] * invSamples;
        heroOutput[i*3+1] = _accumHeroRGB[i][1] * invSamples;
        heroOutput[i*3+2] = _accumHeroRGB[i][2] * invSamples;
        
        diffOutput[i*3+0] = _accumDiffRGB[i][0] * invSamples;
        diffOutput[i*3+1] = _accumDiffRGB[i][1] * invSamples;
        diffOutput[i*3+2] = _accumDiffRGB[i][2] * invSamples;
    }

    auto getLuminance = [](float r, float g, float b) {
        return 0.2126f * r + 0.7152f * g + 0.0722f * b;
    };
    auto sanitize = [](float& val) {
        if (std::isnan(val) || std::isinf(val)) val = 0.0f;
    };

    // 1. Recombine for OIDN directly (eliminating manual blur/firefly passes)
    std::vector<float> prefiltered(width * height * 3);
    for(size_t i=0; i<width*height; ++i) {
        float r = std::max(0.0f, heroOutput[i*3+0] + diffOutput[i*3+0]);
        float g = std::max(0.0f, heroOutput[i*3+1] + diffOutput[i*3+1]);
        float b = std::max(0.0f, heroOutput[i*3+2] + diffOutput[i*3+2]);
        sanitize(r); sanitize(g); sanitize(b);
        // Clamp to prevent overflow in OIDN's internal fp16 network. 65000 is too high and causes black screen!
        prefiltered[i*3+0] = std::min(r, 4000.0f);
        prefiltered[i*3+1] = std::min(g, 4000.0f);
        prefiltered[i*3+2] = std::min(b, 4000.0f);
    }

    if (!albedo.empty()) {
        if (albedo.size() != width * height * 3) {
            albedo.clear();
        } else {
            for(size_t i=0; i<albedo.size(); ++i) {
                sanitize(albedo[i]);
                albedo[i] = std::max(0.0f, std::min(albedo[i], 1.0f));
            }
        }
    }
    if (!normal.empty()) {
        if (normal.size() != width * height * 3) {
            normal.clear();
        } else {
            for(size_t i=0; i<normal.size(); ++i) {
                sanitize(normal[i]);
                normal[i] = std::max(-1.0f, std::min(normal[i], 1.0f));
            }
        }
    }

    std::vector<float> output = prefiltered;

    if (_enableDenoiser) {
        try {
            oidn::DeviceRef device = oidn::newDevice(oidn::DeviceType::CPU);
            device.setErrorFunction([](void* userPtr, oidn::Error error, const char* message) {
                std::cerr << "[Gemini] OIDN Fatal Error (" << (int)error << "): " << (message ? message : "Unknown") << std::endl;
            });
            device.commit();

            oidn::FilterRef filter = device.newFilter("RT");
            filter.setImage("color", prefiltered.data(), oidn::Format::Float3, width, height);
            if (!albedo.empty()) filter.setImage("albedo", albedo.data(), oidn::Format::Float3, width, height);
            if (!normal.empty()) filter.setImage("normal", normal.data(), oidn::Format::Float3, width, height);
            filter.setImage("output", output.data(), oidn::Format::Float3, width, height);
            filter.set("hdr", true);
            filter.commit();
            filter.execute();

            const char* errorMessage;
            if (device.getError(errorMessage) != oidn::Error::None) {
                 std::cerr << "[Gemini] OIDN Error: " << errorMessage << std::endl;
            }
        } catch (std::exception& e) {
            std::cerr << "[Gemini] OIDN Exception: " << e.what() << std::endl;
        }
    }

    for (unsigned int y = 0; y < height; ++y) {
        for (unsigned int x = 0; x < width; ++x) {
            size_t idx = (y * width + x) * 3;
            float pixel[4] = { output[idx], output[idx+1], output[idx+2], 1.0f };
            _colorBuffer->Write(GfVec3i(x, y, 0), 4, pixel);
        }
    }
    _colorBuffer->Resolve();
#endif
}

void
HdGeminiRenderer::_ApplyPostProcess()
{
    unsigned int width = _dataWindow.GetWidth();
    unsigned int height = _dataWindow.GetHeight();
    if (width == 0 || height == 0 || !_colorBuffer) return;

    std::vector<float> color(width * height * 3, 0.0f);
    const float* mappedColor = (const float*)_colorBuffer->Map();
    if (mappedColor) {
        for (unsigned int i = 0; i < width * height; ++i) {
            color[i*3+0] = mappedColor[i*4+0];
            color[i*3+1] = mappedColor[i*4+1];
            color[i*3+2] = mappedColor[i*4+2];
        }
    }
    _colorBuffer->Unmap();

    std::vector<float> finalColor = color;

    if (_enableLensFlare) {
        int downscale = std::max(1, (int)std::min(width, height) / 512);
        int w = width / downscale;
        int h = height / downscale;
        std::vector<float> bright(w * h * 3, 0.0f);
        float threshold = 2.0f;
        std::vector<std::pair<int, int>> brightPixels;
        
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                float r=0, g=0, b=0;
                int count=0;
                for (int dy=0; dy<downscale; ++dy) {
                    for (int dx=0; dx<downscale; ++dx) {
                        int ix = x*downscale + dx;
                        int iy = y*downscale + dy;
                        if (ix < (int)width && iy < (int)height) {
                            size_t c_idx = (iy * width + ix) * 3;
                            r += color[c_idx];
                            g += color[c_idx+1];
                            b += color[c_idx+2];
                            count++;
                        }
                    }
                }
                if (count > 0) { r/=count; g/=count; b/=count; }
                float lum = 0.2126f * r + 0.7152f * g + 0.0722f * b;
                if (lum > threshold) {
                    size_t b_idx = (y * w + x) * 3;
                    bright[b_idx] = r - threshold;
                    bright[b_idx+1] = g - threshold;
                    bright[b_idx+2] = b - threshold;
                    brightPixels.push_back({x, y});
                }
            }
        }

        std::vector<float> bokeh(w * h * 3, 0.0f);
        int blades = _bokehBlades;
        int R = std::max(1, std::min(w, h) / 30);
        float pi = 3.14159265f;
        float d_angle = (blades >= 3) ? (pi * 2.0f / blades) : 0.0f;
        float area = pi * R * R;
        
        for (auto p : brightPixels) {
            int cx = p.first;
            int cy = p.second;
            size_t cidx = (cy * w + cx) * 3;
            float cr = bright[cidx], cg = bright[cidx+1], cb = bright[cidx+2];
            
            for (int dy = -R; dy <= R; ++dy) {
                for (int dx = -R; dx <= R; ++dx) {
                    int nx = cx + dx;
                    int ny = cy + dy;
                    if (nx >= 0 && nx < w && ny >= 0 && ny < h) {
                        float dist;
                        if (blades < 3) {
                            dist = std::sqrt((float)(dx*dx + dy*dy));
                        } else {
                            float a = std::atan2((float)dy, (float)dx) + pi;
                            float r_dist = std::sqrt((float)(dx*dx + dy*dy));
                            float v = a - std::floor(a / d_angle) * d_angle - d_angle * 0.5f;
                            dist = r_dist * std::cos(v);
                        }
                        
                        if (dist <= R) {
                            float d_norm = dist / (float)R;
                            float d2 = d_norm * d_norm;
                            float weight = std::max(0.0f, 1.0f - d2 * d2) / area;
                            
                            size_t nidx = (ny * w + nx) * 3;
                            bokeh[nidx] += cr * weight;
                            bokeh[nidx+1] += cg * weight;
                            bokeh[nidx+2] += cb * weight;
                        }
                    }
                }
            }
        }

        std::vector<float> flare(w * h * 3, 0.0f);
        int ghosts = 5;
        float ghostDispersal = 0.4f;
        float cx_screen = w * 0.5f;
        float cy_screen = h * 0.5f;
        
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                float dx = x - cx_screen;
                float dy = y - cy_screen;
                float r=0, g=0, b=0;
                
                for (int i = 0; i < ghosts; ++i) {
                    float offset = (float)i * ghostDispersal - 1.0f; 
                    float sx = cx_screen + dx * offset;
                    float sy = cy_screen + dy * offset;
                    
                    float ca_shift = 0.02f * w;
                    float dirX = (dx != 0 || dy != 0) ? dx / std::sqrt(dx*dx+dy*dy) : 0;
                    float dirY = (dx != 0 || dy != 0) ? dy / std::sqrt(dx*dx+dy*dy) : 0;
                    
                    auto Sample = [&](float px, float py) -> GfVec3f {
                        if (px >= 0 && px < w-1 && py >= 0 && py < h-1) {
                            int ix = (int)px; int iy = (int)py;
                            float fx = px - ix; float fy = py - iy;
                            auto C = [&](int xx, int yy) {
                                size_t idx = (yy*w+xx)*3;
                                return GfVec3f(bokeh[idx], bokeh[idx+1], bokeh[idx+2]);
                            };
                            return C(ix, iy) * (1-fx)*(1-fy) + C(ix+1, iy) * fx*(1-fy) +
                                   C(ix, iy+1) * (1-fx)*fy + C(ix+1, iy+1) * fx*fy;
                        }
                        return GfVec3f(0);
                    };
                    
                    r += Sample(sx + dirX * ca_shift, sy + dirY * ca_shift)[0] * 0.2f;
                    g += Sample(sx, sy)[1] * 0.2f;
                    b += Sample(sx - dirX * ca_shift, sy - dirY * ca_shift)[2] * 0.2f;
                }
                
                size_t idx = (y * w + x) * 3;
                flare[idx] = r;
                flare[idx+1] = g;
                flare[idx+2] = b;
            }
        }
        
        std::vector<float> starburst(w * h * 3, 0.0f);
        if (blades >= 3) {
            float spikeLength = std::max(w, h) * 0.2f;
            float spikeIntensity = 2.0f / (spikeLength * blades);
            
            for (auto p : brightPixels) {
                int cx = p.first;
                int cy = p.second;
                size_t cidx = (cy * w + cx) * 3;
                float cr = bright[cidx], cg = bright[cidx+1], cb = bright[cidx+2];
                
                for (int i = 0; i < blades; ++i) {
                    float angle = i * (2.0f * pi) / blades + pi * 0.5f;
                    float s_dx = std::cos(angle);
                    float s_dy = std::sin(angle);
                    
                    for (float d = -spikeLength; d <= spikeLength; d += 1.0f) {
                        if (d == 0.0f) continue;
                        int nx = cx + s_dx * d;
                        int ny = cy + s_dy * d;
                        if (nx >= 0 && nx < w && ny >= 0 && ny < h) {
                            float weight = (1.0f - std::abs(d) / spikeLength);
                            weight = weight * weight * spikeIntensity;
                            size_t nidx = (ny * w + nx) * 3;
                            starburst[nidx] += cr * weight;
                            starburst[nidx+1] += cg * weight;
                            starburst[nidx+2] += cb * weight;
                        }
                    }
                }
            }
        }
        
        for (unsigned int y = 0; y < height; ++y) {
            for (unsigned int x = 0; x < width; ++x) {
                int sx = std::clamp((int)(x / downscale), 0, w - 1);
                int sy = std::clamp((int)(y / downscale), 0, h - 1);
                size_t sidx = (sy * w + sx) * 3;
                size_t idx = (y * width + x) * 3;
                
                finalColor[idx] += flare[sidx] * 0.5f + starburst[sidx] * 0.5f + bokeh[sidx] * 0.2f;
                finalColor[idx+1] += flare[sidx+1] * 0.5f + starburst[sidx+1] * 0.5f + bokeh[sidx+1] * 0.2f;
                finalColor[idx+2] += flare[sidx+2] * 0.5f + starburst[sidx+2] * 0.5f + bokeh[sidx+2] * 0.2f;
            }
        }
    }

    if (_chromaticAberration > 0.0f) {
        std::vector<float> caColor = finalColor;
        float maxDist = std::sqrt((float)(width * width + height * height)) * 0.5f;
        for (unsigned int y = 0; y < height; ++y) {
            for (unsigned int x = 0; x < width; ++x) {
                float dx = (float)x - width * 0.5f;
                float dy = (float)y - height * 0.5f;
                float dist = std::sqrt(dx * dx + dy * dy);
                float dirX = (dist > 0.0f) ? dx / dist : 0.0f;
                float dirY = (dist > 0.0f) ? dy / dist : 0.0f;

                float shift = _chromaticAberration * (dist / maxDist);

                int rx = std::clamp((int)(x - dirX * shift), 0, (int)width - 1);
                int ry = std::clamp((int)(y - dirY * shift), 0, (int)height - 1);
                int bx = std::clamp((int)(x + dirX * shift), 0, (int)width - 1);
                int by = std::clamp((int)(y + dirY * shift), 0, (int)height - 1);

                size_t idx = (y * width + x) * 3;
                size_t ridx = (ry * width + rx) * 3;
                size_t bidx = (by * width + bx) * 3;

                caColor[idx] = finalColor[ridx]; // R shifted inwards
                // caColor[idx+1] is unchanged G
                caColor[idx+2] = finalColor[bidx+2]; // B shifted outwards
            }
        }
        finalColor = caColor;
    }

    for (unsigned int y = 0; y < height; ++y) {
        for (unsigned int x = 0; x < width; ++x) {
            size_t idx = (y * width + x) * 3;
            float pixel[4] = { 
                finalColor[idx], 
                finalColor[idx+1], 
                finalColor[idx+2], 
                1.0f 
            };
            _colorBuffer->Write(GfVec3i(x, y, 0), 4, pixel);
        }
    }
    _colorBuffer->Resolve();
}

void
HdGeminiRenderer::_RenderTiles(HdRenderThread *renderThread, HdGeminiRenderDelegate* delegate)
{
    unsigned int width = _dataWindow.GetWidth();
    unsigned int height = _dataWindow.GetHeight();
    if (width == 0 || height == 0 || !_colorBuffer) return;
    
    GfVec3f cameraPosWorld(_inverseViewMatrix.Transform(GfVec3f(0, 0, 0)));
    
    float activeFocalLength = std::max(0.01f, _enablePhysicalCamera ? _focalLength : _hydraFocalLength);
    float activeFStop = std::max(0.01f, _enablePhysicalCamera ? _fStop : _hydraFStop);
    float activeFocusDistance = std::max(0.01f, _enablePhysicalCamera ? _focusDistance : _hydraFocusDistance);
    float activeIso = std::max(0.01f, _enablePhysicalCamera ? _iso : _hydraIso);
    float activeShutterSpeed = std::max(0.0001f, _enablePhysicalCamera ? _shutterSpeed : _hydraShutterSpeed);
    
    float lensWaveHeight = -1e30f;
    if (_oceanEnable && _globalOcean) {
        GfVec3f nearPlaneCenterCam = GfVec3f(_inverseProjMatrix.Transform(GfVec3d(0, 0, -1.0)));
        GfVec3f nearPlaneCenterWorld = GfVec3f(_inverseViewMatrix.Transform(GfVec3d(nearPlaneCenterCam)));
        GfVec3f queryPos = nearPlaneCenterWorld;
        queryPos[1] = _oceanWaterHeight;
        lensWaveHeight = _globalOcean->GetDisplacedPosition(queryPos)[1];
    }
    
    std::lock_guard<std::recursive_mutex> lock(delegate->GetSceneLock());
    int res = _resolutionLevel;
    bool isInteractive = (res > 1);
    const int bucketSize = 16;
    size_t numBucketsX = (width + bucketSize - 1) / bucketSize;
    size_t numBucketsY = (height + bucketSize - 1) / bucketSize;
    size_t numBuckets = numBucketsX * numBucketsY;

#ifdef HDGEMINI_HAS_SYCL
    if (_enableSycl && _syclQueue && _rayBuffer && !isInteractive) {
        _RenderTilesSYCL(renderThread, delegate);
        return;
    }
#endif

    if (width != _lastWidth || height != _lastHeight || _colorBufferVersion != _colorBuffer->GetVersion()) {
        _buckets.clear();
        for (size_t by = 0; by < numBucketsY; ++by) {
            for (size_t bx = 0; bx < numBucketsX; ++bx) {
                RenderBucket b;
                b.startX = (uint32_t)(bx * bucketSize);
                b.startY = (uint32_t)(by * bucketSize);
                b.endX = std::min(b.startX + bucketSize, (uint32_t)width);
                b.endY = std::min(b.startY + bucketSize, (uint32_t)height);
                b.activePixels = (b.endX - b.startX) * (b.endY - b.startY);
                b.maxVariance = 1e5f;
                _buckets.push_back(b);
            }
        }
        _lastWidth = width;
        _lastHeight = height;
        _colorBufferVersion = _colorBuffer->GetVersion();
        
        if (_enableRestirDI) {
            _temporalReservoirs.assign(width * height, Reservoir{});
            _prevTemporalReservoirs.assign(width * height, Reservoir{});
        }
        _giTemporalReservoirs.assign(width * height, GIReservoir{});
        _giPrevTemporalReservoirs.assign(width * height, GIReservoir{});
        _photonMap.Clear();
        _sppmPasses = 0;
        
        _accumHeroRGB.assign(width * height, GfVec3f(0.0f));
        _accumDiffRGB.assign(width * height, GfVec3f(0.0f));
    }
    
    if (!_enableRestirDI && !_temporalReservoirs.empty()) {
        _temporalReservoirs.clear();
        _prevTemporalReservoirs.clear();
    } else if (_enableRestirDI && _temporalReservoirs.size() != width * height) {
        _temporalReservoirs.assign(width * height, Reservoir{});
        _prevTemporalReservoirs.assign(width * height, Reservoir{});
    } else if (_enableRestirDI) {
        std::swap(_temporalReservoirs, _prevTemporalReservoirs);
        _temporalReservoirs.assign(width * height, Reservoir{});
    }
    
    if (_giTemporalReservoirs.size() != width * height) {
        _giTemporalReservoirs.assign(width * height, GIReservoir{});
        _giPrevTemporalReservoirs.assign(width * height, GIReservoir{});
    } else {
        std::swap(_giTemporalReservoirs, _giPrevTemporalReservoirs);
        _giTemporalReservoirs.assign(width * height, GIReservoir{});
    }
    if (_enableSPPM && !_activeLights.empty()) {
        _sppmPasses++;
        _photonMap.searchRadius = std::max(0.001f, 0.1f / std::sqrt((float)_sppmPasses));
        _photonMap.Clear();
        
        int numPhotons = 10000;
        WorkParallelForN(numPhotons, [&](size_t start, size_t end) {
            for (size_t i = start; i < end; ++i) {
                uint32_t rng = (uint32_t)i + _sppmPasses * 1337;
                SampledWavelengths lambda = SampledWavelengths::SampleUniform(0.5f);
                _TracePhoton(renderThread, (uint32_t)i, rng, lambda);
            }
        });
    }

    std::vector<size_t> activeBucketIndices;
    activeBucketIndices.reserve(_buckets.size());
    for (size_t i = 0; i < _buckets.size(); ++i) {
        if (!_enableAdaptiveSampling || isInteractive || _buckets[i].activePixels > 0) {
            activeBucketIndices.push_back(i);
        }
    }

    std::sort(activeBucketIndices.begin(), activeBucketIndices.end(), [&](size_t a, size_t b) {
        return _buckets[a].maxVariance > _buckets[b].maxVariance;
    });

    WorkParallelForN(activeBucketIndices.size(), [&](size_t b_start, size_t b_end) {
        for (size_t idx = b_start; idx < b_end; ++idx) {
            if (renderThread->IsStopRequested()) return;
            size_t b = activeBucketIndices[idx];
            RenderBucket& bucket = _buckets[b];
            
            uint32_t activePixels = 0;
            float maxVariance = 0.0f;

            size_t startX = (bucket.startX / res) * res;
            size_t startY = (bucket.startY / res) * res;
            size_t endX = bucket.endX;
            size_t endY = bucket.endY;

            for (size_t y = startY; y < endY; y += res) {
                for (size_t x = startX; x < endX; x += res) {
                    if (renderThread->IsStopRequested()) return;
                    GfVec3f rayOriginWorld;
                    GfVec3f rayDirWorld;
                    uint32_t rng;
                    SampledWavelengths lambda;
                    float exposureMultiplier;
                    
                    bool useSYCLBuffer = false;
                    uint32_t sampleIdx = _colorBuffer->GetPixelSampleCount(GfVec3i(x, y, 0));
                    uint32_t qmcDim = 0;
                    
                    if (useSYCLBuffer) {
                    } else {
                        rng = (uint32_t)(y * width + x) ^ (uint32_t)(_frameCount * 12345);
                        
                        if (!isInteractive && _enableAdaptiveSampling && sampleIdx >= _adaptiveMinSamples) {
                            float variance = _colorBuffer->GetPixelVariance(GfVec3i(x, y, 0));
                            maxVariance = std::max(maxVariance, variance);
                            if (variance < _adaptiveVarianceThreshold) {
                                continue;
                            }
                        }
                        activePixels++;
                        
                        float px = (float)x;
                        float py = (float)y;
                        
                        if (isInteractive) {
                             px += res * 0.5f;
                             py += res * 0.5f;
                        } else {
                            float u1 = qmc::SampleDimension(sampleIdx, qmcDim++, rng);
                            float u2 = qmc::SampleDimension(sampleIdx, qmcDim++, rng);
                            
                            if (_antiAliasingFilter == 0) { // None
                                px += 0.5f; py += 0.5f;
                            } else if (_antiAliasingFilter == 1) { // Box
                                px += u1; py += u2;
                            } else if (_antiAliasingFilter == 2) { // Tent
                                auto tent = [](float u) {
                                    return u < 0.5f ? std::sqrt(2.0f * u) - 1.0f : 1.0f - std::sqrt(2.0f - 2.0f * u);
                                };
                                px += 0.5f + tent(u1); py += 0.5f + tent(u2);
                            } else if (_antiAliasingFilter == 3) { // Gaussian
                                u1 = std::max(1e-6f, u1);
                                float r = std::sqrt(-2.0f * std::log(u1));
                                float theta = 2.0f * (float)M_PI * u2;
                                float sigma = 0.5f;
                                px += 0.5f + r * std::cos(theta) * sigma; py += 0.5f + r * std::sin(theta) * sigma;
                            }
                        }

                        float ndcX = (2.0f * px / width) - 1.0f;
                        float ndcY = (2.0f * py / height) - 1.0f;
                        
                        if (_lensDistortion != 0.0f) {
                            float r2 = ndcX * ndcX + ndcY * ndcY;
                            float f = 1.0f + _lensDistortion * r2;
                            ndcX *= f; ndcY *= f;
                        }

                        GfVec3f nearPlanePointCam = GfVec3f(_inverseProjMatrix.Transform(GfVec3d(ndcX, ndcY, -1.0)));
                        
                        if (_enableDoF) {
                            float apertureRadius = (activeFocalLength * 0.001f / _metersPerUnit) / (2.0f * activeFStop);
                            float lensU, lensV;
                            
                            float d1 = qmc::SampleDimension(sampleIdx, qmcDim++, rng);
                            float d2 = qmc::SampleDimension(sampleIdx, qmcDim++, rng);
                            
                            if (_bokehBlades < 3) {
                                float r = std::sqrt(d1);
                                float theta = 2.0f * M_PI * d2;
                                lensU = r * std::cos(theta); lensV = r * std::sin(theta);
                            } else {
                                float theta = 2.0f * M_PI * d2;
                                float r = std::sqrt(d1);
                                float sectorAngle = 2.0f * M_PI / _bokehBlades;
                                float sector = std::floor(theta / sectorAngle);
                                float angleInSector = theta - sector * sectorAngle;
                                float d = std::cos(sectorAngle / 2.0f) / std::cos(sectorAngle / 2.0f - angleInSector);
                                lensU = r * d * std::cos(theta); lensV = r * d * std::sin(theta);
                            }
                            lensU *= apertureRadius; lensV *= apertureRadius;

                            GfVec3f lensPointCam(lensU, lensV, 0.0f);
                            float t_focus = activeFocusDistance / std::max(1e-5f, std::abs(nearPlanePointCam[2]));
                            GfVec3f focalPointCam = nearPlanePointCam * t_focus;
                            
                            GfVec3f lensPointWorld = GfVec3f(_inverseViewMatrix.Transform(GfVec3d(lensPointCam)));
                            GfVec3f focalPointWorld = GfVec3f(_inverseViewMatrix.Transform(GfVec3d(focalPointCam)));
                            
                            rayDirWorld = (focalPointWorld - lensPointWorld).GetNormalized();
                            
                            GfVec3f rayDirCam = (focalPointCam - lensPointCam).GetNormalized();
                            float t_near = nearPlanePointCam[2] / rayDirCam[2];
                            GfVec3f nearIntersectCam = lensPointCam + rayDirCam * t_near;
                            rayOriginWorld = GfVec3f(_inverseViewMatrix.Transform(GfVec3d(nearIntersectCam)));
                        } else {
                            GfVec3f nearPlanePointWorld = GfVec3f(_inverseViewMatrix.Transform(GfVec3d(nearPlanePointCam)));
                            rayDirWorld = (nearPlanePointWorld - cameraPosWorld).GetNormalized();
                            rayOriginWorld = nearPlanePointWorld;
                        }
                        
                        float u_lambda = qmc::SampleDimension(sampleIdx, qmcDim++, rng);
                        lambda = SampledWavelengths::SampleUniform(u_lambda);
                        
                        exposureMultiplier = (activeIso / 100.0f) * activeShutterSpeed / (activeFStop * activeFStop) * 100.0f;
                        
                        if (lensWaveHeight > -1e29f) {
                            float distToWater = rayOriginWorld[1] - lensWaveHeight;
                            if (distToWater > 0.0f && distToWater < _meniscusSize) {
                                float factor = 1.0f - (distToWater / _meniscusSize);
                                rayDirWorld[1] -= factor * _meniscusBend;
                                rayDirWorld.Normalize();
                            } else if (distToWater <= 0.0f && distToWater > -_meniscusSize) {
                                float factor = 1.0f - (std::abs(distToWater) / _meniscusSize);
                                rayDirWorld[1] += factor * _meniscusBend;
                                rayDirWorld.Normalize();
                            }
                        }
                    }
                    
                    GfVec3f albedo(0.0f), normal(0.0f);
                    float hitDepth = 1.0f;
                    
                    SampledSpectrum hitSpectrum = _TraceRay(rayOriginWorld, rayDirWorld, 0, isInteractive, renderThread, sampleIdx, qmcDim, rng, lambda, &albedo, &normal, &hitDepth, exposureMultiplier, x, y);
                    
                    if (lensWaveHeight > -1e29f) {
                        float distToWater = rayOriginWorld[1] - lensWaveHeight;
                        if (distToWater > 0.0f && distToWater < _meniscusSize) {
                            float factor = 1.0f - (distToWater / _meniscusSize);
                            SampledSpectrum tintSpec = RGBToSpectrum(GfVec3f(1.0f) - factor * (GfVec3f(1.0f) - _meniscusTint), lambda);
                            hitSpectrum = hitSpectrum * tintSpec;
                        } else if (distToWater <= 0.0f && distToWater > -_meniscusSize) {
                            float factor = 1.0f - (std::abs(distToWater) / _meniscusSize);
                            SampledSpectrum tintSpec = RGBToSpectrum(GfVec3f(1.0f) - factor * (GfVec3f(1.0f) - _meniscusTint), lambda);
                            hitSpectrum = hitSpectrum * tintSpec;
                        }
                    }
                    
                    SampledSpectrum heroSpec;
                    for(int i=0; i<SPECTRUM_SAMPLES; ++i) heroSpec[i] = hitSpectrum[0];
                    SampledSpectrum diffSpec;
                    for(int i=0; i<SPECTRUM_SAMPLES; ++i) diffSpec[i] = hitSpectrum[i] - hitSpectrum[0];

                    GfVec3f heroRGB = SpectrumToRGB(heroSpec, lambda);
                    GfVec3f diffRGB = SpectrumToRGB(diffSpec, lambda);
                    GfVec3f hitColor = heroRGB + diffRGB; // Exactly equal to SpectrumToRGB(hitSpectrum, lambda)

                    if (isInteractive) {
                        GfVec4f finalColor(hitColor[0], hitColor[1], hitColor[2], 1.0f);
                        GfVec4f finalAlbedo(albedo[0], albedo[1], albedo[2], 1.0f);
                        GfVec4f finalNormal(normal[0], normal[1], normal[2], 1.0f);
                        for (int dy = 0; dy < res && y + dy < height; ++dy) {
                            for (int dx = 0; dx < res && x + dx < width; ++dx) {
                                if (_renderAlbedoOnly) {
                                    _colorBuffer->Write(GfVec3i(x + dx, y + dy, 0), 4, finalAlbedo.data());
                                } else {
                                    _colorBuffer->Write(GfVec3i(x + dx, y + dy, 0), 4, finalColor.data());
                                }
                                if (_albedoBuffer) _albedoBuffer->Write(GfVec3i(x + dx, y + dy, 0), 4, finalAlbedo.data());
                                if (_normalBuffer) _normalBuffer->Write(GfVec3i(x + dx, y + dy, 0), 4, finalNormal.data());
                                if (_depthBuffer) _depthBuffer->Write(GfVec3i(x + dx, y + dy, 0), 1, &hitDepth);
                            }
                        }
                    } else {
                        size_t idx = y * width + x;
                        if (idx < _accumHeroRGB.size()) {
                            _accumHeroRGB[idx] += heroRGB;
                            _accumDiffRGB[idx] += diffRGB;
                        }

                        if (_renderAlbedoOnly) {
                            _colorBuffer->WriteSample(GfVec3i(x, y, 0), GfVec4f(albedo[0], albedo[1], albedo[2], 1.0f));
                        } else {
                            _colorBuffer->WriteSample(GfVec3i(x, y, 0), GfVec4f(hitColor[0], hitColor[1], hitColor[2], 1.0f));
                        }
                        if (_albedoBuffer) _albedoBuffer->WriteSample(GfVec3i(x, y, 0), GfVec4f(albedo[0], albedo[1], albedo[2], 1.0f));
                        if (_normalBuffer) _normalBuffer->WriteSample(GfVec3i(x, y, 0), GfVec4f(normal[0], normal[1], normal[2], 1.0f));
                        if (_depthBuffer) _depthBuffer->WriteSample(GfVec3i(x, y, 0), GfVec4f(hitDepth, 0.0f, 0.0f, 1.0f));
                    }
                }
                std::this_thread::yield();
            }
            if (!renderThread->IsStopRequested()) _colorBuffer->ResolveBucket(startX, startY, endX, endY);
            if (!renderThread->IsStopRequested() && _albedoBuffer) _albedoBuffer->ResolveBucket(startX, startY, endX, endY);
            if (!renderThread->IsStopRequested() && _normalBuffer) _normalBuffer->ResolveBucket(startX, startY, endX, endY);
            if (!renderThread->IsStopRequested() && _depthBuffer) _depthBuffer->ResolveBucket(startX, startY, endX, endY);

            if (_enableOnScreenStats && !renderThread->IsStopRequested()) {
                unsigned int statsStartY = std::max(0, (int)height - 60);
                if (endY > statsStartY && startX < 800) {
                    std::lock_guard<std::mutex> lock(_statsMutex);
                    _DrawStats();
                    _colorBuffer->ResolveBucket(0, statsStartY, std::min(800u, width), height);
                }
            }

            bucket.activePixels = activePixels;
            bucket.maxVariance = maxVariance;
        }
    });
}

void
HdGeminiRenderer::Clear()
{
    _resolutionLevel = 2;
    _frameCount = 0;
    _isConverged = false;
    _renderStartTime = std::chrono::high_resolution_clock::now();
    
    unsigned int width = _dataWindow.GetWidth();
    unsigned int height = _dataWindow.GetHeight();
    if (width > 0 && height > 0) {
        _accumHeroRGB.assign(width * height, GfVec3f(0.0f));
        _accumDiffRGB.assign(width * height, GfVec3f(0.0f));
#ifdef HDGEMINI_HAS_SYCL
        if (_syclQueue) {
            size_t newSize = width * height;
            if (_rayBufferSize < newSize) {
                if (_rayBuffer) sycl::free(_rayBuffer, *_syclQueue);
                _rayBuffer = sycl::malloc_shared<RayState>(newSize, *_syclQueue);
                _rayBufferSize = newSize;
            }
            if (_hitBufferSize < newSize) {
                if (_hitBuffer) sycl::free(_hitBuffer, *_syclQueue);
                _hitBuffer = sycl::malloc_shared<HitState>(newSize, *_syclQueue);
                _hitBufferSize = newSize;
            }
            if (_shadowRayBufferSize < newSize) {
                if (_shadowRayBuffer) sycl::free(_shadowRayBuffer, *_syclQueue);
                _shadowRayBuffer = sycl::malloc_shared<ShadowRay>(newSize, *_syclQueue);
                _shadowRayBufferSize = newSize;
            }
        }
#endif
    }

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

void
HdGeminiRenderer::ReapplyPostProcess()
{
    if (!_colorBuffer || _frameCount == 0) return;
    if (_accumHeroRGB.empty() || _accumDiffRGB.empty()) return;

    unsigned int width = _dataWindow.GetWidth();
    unsigned int height = _dataWindow.GetHeight();

    float invSamples = 1.0f / (float)std::max(1, _frameCount);
    for (unsigned int y = 0; y < height; ++y) {
        for (unsigned int x = 0; x < width; ++x) {
            size_t idx = y * width + x;
            GfVec3f hero = _accumHeroRGB[idx] * invSamples;
            GfVec3f diff = _accumDiffRGB[idx] * invSamples;
            GfVec3f finalRGB = hero + diff;
            auto sanitize = [](float& val) {
                if (std::isnan(val) || std::isinf(val)) val = 0.0f;
            };
            sanitize(finalRGB[0]); sanitize(finalRGB[1]); sanitize(finalRGB[2]);
            float pixel[4] = { finalRGB[0], finalRGB[1], finalRGB[2], 1.0f };
            _colorBuffer->Write(GfVec3i(x, y, 0), 4, pixel);
        }
    }
    _colorBuffer->Resolve();

    if (_enableDenoiser || _enableFireflyFilter || _enableChromaticityBlur) {
        _Denoise();
    }
    if (_enableLensFlare || _chromaticAberration > 0.0f) {
        _ApplyPostProcess();
    }
    
    if (_enableOnScreenStats) {
        _DrawStats();
    }

    _isConverged = true;
    for (auto const& binding : _aovBindings) {
        if (binding.renderBuffer) {
            static_cast<HdGeminiRenderBuffer*>(binding.renderBuffer)->SetConverged(true);
        }
    }
    
    _previousViewMatrix = _viewMatrix;
    _previousProjMatrix = _projMatrix;
    _previousInverseViewMatrix = _inverseViewMatrix;
    _previousInverseProjMatrix = _inverseProjMatrix;
}

#ifdef HDGEMINI_HAS_SYCL
void HdGeminiRenderer::_RenderTilesSYCL(HdRenderThread *renderThread, HdGeminiRenderDelegate* delegate)
{
    unsigned int width = _dataWindow.GetWidth();
    unsigned int height = _dataWindow.GetHeight();
    if (width == 0 || height == 0 || !_colorBuffer) return;
    
    GfVec3f cameraPosWorld(_inverseViewMatrix.Transform(GfVec3f(0, 0, 0)));
    
    float lensWaveHeight = -1e30f;
    if (_oceanEnable && _globalOcean) {
        GfVec3f nearPlaneCenterCam = GfVec3f(_inverseProjMatrix.Transform(GfVec3d(0, 0, -1.0)));
        GfVec3f nearPlaneCenterWorld = GfVec3f(_inverseViewMatrix.Transform(GfVec3d(nearPlaneCenterCam)));
        GfVec3f queryPos = nearPlaneCenterWorld;
        queryPos[1] = _oceanWaterHeight;
        lensWaveHeight = _globalOcean->GetDisplacedPosition(queryPos)[1];
    }
    
    std::lock_guard<std::recursive_mutex> lock(delegate->GetSceneLock());
    
    // --- GPU Ray Generation Phase ---
    float invWidth = 1.0f / width;
    float invHeight = 1.0f / height;
    float lensDistortion = _lensDistortion;
    bool enableDoF = _enableDoF;
    float focalLength = std::max(0.01f, _enablePhysicalCamera ? _focalLength : _hydraFocalLength);
    float fStop = std::max(0.01f, _enablePhysicalCamera ? _fStop : _hydraFStop);
    float focusDist = std::max(0.01f, _enablePhysicalCamera ? _focusDistance : _hydraFocusDistance);
    int bokehBlades = _bokehBlades;
    float iso = std::max(0.01f, _enablePhysicalCamera ? _iso : _hydraIso);
    float shutterSpeed = std::max(0.0001f, _enablePhysicalCamera ? _shutterSpeed : _hydraShutterSpeed);
    
    float meniscusSize = _meniscusSize;
    float meniscusBend = _meniscusBend;
    float metersPerUnit = _metersPerUnit;
    float meniscusTint[3] = {_meniscusTint[0], _meniscusTint[1], _meniscusTint[2]};
    int antiAliasingFilter = _antiAliasingFilter;
    uint32_t frameCount = _frameCount;
    
    double invProj[16], invView[16];
    const double* pd = _inverseProjMatrix.GetArray();
    const double* vd = _inverseViewMatrix.GetArray();
    for(int i=0; i<16; ++i) { invProj[i] = pd[i]; invView[i] = vd[i]; }
    
    float camPos[3] = {cameraPosWorld[0], cameraPosWorld[1], cameraPosWorld[2]};
    RayState* rayBuf = _rayBuffer;

    _syclQueue->submit([&](sycl::handler& cgh) {
        cgh.parallel_for<class GenerateRaysSYCL>(sycl::range<1>(width * height), [=](sycl::item<1> item) {
            size_t idx = item.get_id(0);
            int x = idx % width;
            int y = idx / width;
            
            uint32_t rng = (uint32_t)(y * width + x) ^ (uint32_t)(frameCount * 12345);
            auto randFloat = [&]() {
                rng = rng * 1664525 + 1013904223;
                return (float)rng / (float)0xFFFFFFFF;
            };

            float px = (float)x; float py = (float)y;
            if (antiAliasingFilter == 0) {
                px += 0.5f; py += 0.5f;
            } else if (antiAliasingFilter == 1) {
                px += randFloat(); py += randFloat();
            } else if (antiAliasingFilter == 2) {
                auto tent = [](float u) { return u < 0.5f ? sycl::sqrt(2.0f * u) - 1.0f : 1.0f - sycl::sqrt(2.0f - 2.0f * u); };
                px += 0.5f + tent(randFloat()); py += 0.5f + tent(randFloat());
            } else if (antiAliasingFilter == 3) {
                float u1 = sycl::fmax(1e-6f, randFloat());
                float u2 = randFloat();
                float r = sycl::sqrt(-2.0f * sycl::log(u1));
                float theta = 2.0f * 3.14159265f * u2;
                float sigma = 0.5f;
                px += 0.5f + r * sycl::cos(theta) * sigma; py += 0.5f + r * sycl::sin(theta) * sigma;
            }

            float ndcX = (2.0f * px * invWidth) - 1.0f;
            float ndcY = (2.0f * py * invHeight) - 1.0f;
            if (lensDistortion != 0.0f) {
                float r2 = ndcX * ndcX + ndcY * ndcY;
                float f = 1.0f + lensDistortion * r2;
                ndcX *= f; ndcY *= f;
            }

            float clip[4] = {ndcX, ndcY, -1.0f, 1.0f};
            float nearCam[4] = {0,0,0,0};
            for(int i=0; i<4; ++i) { for(int j=0; j<4; ++j) { nearCam[i] += clip[j] * invProj[j*4 + i]; } }
            float invW = 1.0f / nearCam[3];
            float nearPlanePointCam[3] = {nearCam[0]*invW, nearCam[1]*invW, nearCam[2]*invW};
            
            float rayOrigin[3] = {camPos[0], camPos[1], camPos[2]};
            float rayDir[3];

            if (enableDoF) {
                float lensU, lensV;
                if (bokehBlades < 3) {
                    float r = sycl::sqrt(randFloat()); float theta = 2.0f * 3.14159265f * randFloat();
                    lensU = r * sycl::cos(theta); lensV = r * sycl::sin(theta);
                } else {
                    float theta = 2.0f * 3.14159265f * randFloat(); float r = sycl::sqrt(randFloat());
                    float sectorAngle = 2.0f * 3.14159265f / bokehBlades;
                    float sector = sycl::floor(theta / sectorAngle); float angleInSector = theta - sector * sectorAngle;
                    float d = sycl::cos(sectorAngle / 2.0f) / sycl::cos(sectorAngle / 2.0f - angleInSector);
                    lensU = r * d * sycl::cos(theta); lensV = r * d * sycl::sin(theta);
                }
                float scaledFocalLength = focalLength * 0.001f / metersPerUnit;
                lensU *= scaledFocalLength / (2.0f * fStop); lensV *= scaledFocalLength / (2.0f * fStop);

                float lensCam[4] = {lensU, lensV, 0.0f, 1.0f};
                float t_focus = focusDist / sycl::max(1e-5f, sycl::fabs(nearPlanePointCam[2]));
                float focalCam[4] = {nearPlanePointCam[0]*t_focus, nearPlanePointCam[1]*t_focus, nearPlanePointCam[2]*t_focus, 1.0f};
                float lensWorld[4] = {0,0,0,0}; float focalWorld[4] = {0,0,0,0};
                for(int i=0; i<4; ++i) { for(int j=0; j<4; ++j) { lensWorld[i] += lensCam[j] * invView[j*4 + i]; focalWorld[i] += focalCam[j] * invView[j*4 + i]; } }
                
                float dx = focalWorld[0] - lensWorld[0]; float dy = focalWorld[1] - lensWorld[1]; float dz = focalWorld[2] - lensWorld[2];
                float len = sycl::sqrt(dx*dx + dy*dy + dz*dz);
                rayDir[0] = dx/len; rayDir[1] = dy/len; rayDir[2] = dz/len;
                
                float dxCam = focalCam[0] - lensCam[0]; float dyCam = focalCam[1] - lensCam[1]; float dzCam = focalCam[2] - lensCam[2];
                float lenCam = sycl::sqrt(dxCam*dxCam + dyCam*dyCam + dzCam*dzCam);
                dxCam /= lenCam; dyCam /= lenCam; dzCam /= lenCam;
                
                float t_near = nearPlanePointCam[2] / dzCam;
                float nearIntersectCam[4] = {lensCam[0] + dxCam * t_near, lensCam[1] + dyCam * t_near, lensCam[2] + dzCam * t_near, 1.0f};
                
                float nearIntersectWorld[4] = {0,0,0,0};
                for(int i=0; i<4; ++i) { for(int j=0; j<4; ++j) { nearIntersectWorld[i] += nearIntersectCam[j] * invView[j*4 + i]; } }
                
                rayOrigin[0] = nearIntersectWorld[0]; rayOrigin[1] = nearIntersectWorld[1]; rayOrigin[2] = nearIntersectWorld[2];
            } else {
                float nearCam4[4] = {nearPlanePointCam[0], nearPlanePointCam[1], nearPlanePointCam[2], 1.0f};
                float nearWorld[4] = {0,0,0,0};
                for(int i=0; i<4; ++i) { for(int j=0; j<4; ++j) { nearWorld[i] += nearCam4[j] * invView[j*4 + i]; } }
                float dx = nearWorld[0] - camPos[0]; float dy = nearWorld[1] - camPos[1]; float dz = nearWorld[2] - camPos[2];
                float len = sycl::sqrt(dx*dx + dy*dy + dz*dz);
                rayDir[0] = dx/len; rayDir[1] = dy/len; rayDir[2] = dz/len;
                rayOrigin[0] = nearWorld[0]; rayOrigin[1] = nearWorld[1]; rayOrigin[2] = nearWorld[2];
            }

            float u_lambda = randFloat();
            float lambda0 = 360.0f + u_lambda * (830.0f - 360.0f);
            for(int i=0; i<4; ++i) {
                float l = lambda0 + (i * (830.0f - 360.0f) / 4.0f);
                if (l > 830.0f) l -= (830.0f - 360.0f);
                rayBuf[idx].lambda.lambda[i] = l;
            }
            
            float tint[4] = {1.0f, 1.0f, 1.0f, 1.0f};
            if (lensWaveHeight > -1e29f) {
                float distToWater = rayOrigin[1] - lensWaveHeight;
                if (distToWater > 0.0f && distToWater < meniscusSize) {
                    float factor = 1.0f - (distToWater / meniscusSize);
                    rayDir[1] -= factor * meniscusBend;
                    float len = sycl::sqrt(rayDir[0]*rayDir[0] + rayDir[1]*rayDir[1] + rayDir[2]*rayDir[2]);
                    rayDir[0] /= len; rayDir[1] /= len; rayDir[2] /= len;
                    for(int i=0; i<4; ++i) {
                        float l = rayBuf[idx].lambda.lambda[i];
                        float tC = (l < 500) ? meniscusTint[2] : ((l < 600) ? meniscusTint[1] : meniscusTint[0]);
                        tint[i] = 1.0f - factor * (1.0f - tC);
                    }
                } else if (distToWater <= 0.0f && distToWater > -meniscusSize) {
                    float factor = 1.0f - (sycl::fabs(distToWater) / meniscusSize);
                    rayDir[1] += factor * meniscusBend;
                    float len = sycl::sqrt(rayDir[0]*rayDir[0] + rayDir[1]*rayDir[1] + rayDir[2]*rayDir[2]);
                    rayDir[0] /= len; rayDir[1] /= len; rayDir[2] /= len;
                    for(int i=0; i<4; ++i) {
                        float l = rayBuf[idx].lambda.lambda[i];
                        float tC = (l < 500) ? meniscusTint[2] : ((l < 600) ? meniscusTint[1] : meniscusTint[0]);
                        tint[i] = 1.0f - factor * (1.0f - tC);
                    }
                }
            }

            rayBuf[idx].origin[0] = rayOrigin[0]; rayBuf[idx].origin[1] = rayOrigin[1]; rayBuf[idx].origin[2] = rayOrigin[2];
            rayBuf[idx].dir[0] = rayDir[0]; rayBuf[idx].dir[1] = rayDir[1]; rayBuf[idx].dir[2] = rayDir[2];
            rayBuf[idx].rng = rng;
            rayBuf[idx].x = x; rayBuf[idx].y = y;
            rayBuf[idx].active = true;

            float exposure = (iso / 100.0f) * shutterSpeed / (fStop * fStop) * 100.0f;
            rayBuf[idx].exposureMultiplier = exposure;
            
            for(int i=0; i<4; ++i) {
                rayBuf[idx].throughput[i] = exposure * tint[i];
                rayBuf[idx].totalRadiance[i] = 0.0f;
            }
            rayBuf[idx].bounce = 0;
            rayBuf[idx].reflectionBounces = 0;
            rayBuf[idx].refractionBounces = 0;
            rayBuf[idx].isInside = false;
        });
    });
    _syclQueue->wait();

    // CPU-GPU Ping-Pong phase replaced with CPU _TraceRay evaluation
    // to utilize the full GGX/MIS light transport while still benefiting
    // from SYCL for the massive primary ray generation phase.
    size_t numRays = width * height;
    
    WorkParallelForN(numRays, [&](size_t start, size_t end) {
        for (size_t i = start; i < end; ++i) {
            if (renderThread->IsStopRequested()) return;
            
            RayState& rs = _rayBuffer[i];
            GfVec3f rayOriginWorld(rs.origin[0], rs.origin[1], rs.origin[2]);
            GfVec3f rayDirWorld(rs.dir[0], rs.dir[1], rs.dir[2]);
            uint32_t rng = rs.rng;
            
            GfVec3f albedo(0.0f), normal(0.0f);
            float hitDepth = 1.0f;
            uint32_t sampleIdx = 0; // GPU/SYCL not yet tracking variance count correctly via CPU loop
            uint32_t qmcDim = 0;
            SampledSpectrum hitSpectrum = _TraceRay(rayOriginWorld, rayDirWorld, 0, false, renderThread, sampleIdx, qmcDim, rng, rs.lambda, &albedo, &normal, &hitDepth, rs.exposureMultiplier, rs.x, rs.y);
            
            SampledSpectrum heroSpec;
            for(int j=0; j<4; ++j) heroSpec[j] = hitSpectrum[0];
            SampledSpectrum diffSpec;
            for(int j=0; j<4; ++j) diffSpec[j] = hitSpectrum[j] - hitSpectrum[0];
            
            GfVec3f heroRGB = SpectrumToRGB(heroSpec, rs.lambda);
            GfVec3f diffRGB = SpectrumToRGB(diffSpec, rs.lambda);
            
            _accumHeroRGB[i] += heroRGB;
            _accumDiffRGB[i] += diffRGB;
            
            GfVec3f hitColor = heroRGB + diffRGB;
            if (_renderAlbedoOnly) {
                _colorBuffer->WriteSampleLockFree(i, GfVec4f(albedo[0], albedo[1], albedo[2], 1.0f));
            } else {
                _colorBuffer->WriteSampleLockFree(i, GfVec4f(hitColor[0], hitColor[1], hitColor[2], 1.0f));
            }
            if (_albedoBuffer) _albedoBuffer->WriteSampleLockFree(i, GfVec4f(albedo[0], albedo[1], albedo[2], 1.0f));
            if (_normalBuffer) _normalBuffer->WriteSampleLockFree(i, GfVec4f(normal[0], normal[1], normal[2], 1.0f));
            if (_depthBuffer) _depthBuffer->WriteSampleLockFree(i, GfVec4f(hitDepth, 0.0f, 0.0f, 1.0f));
        }
        std::this_thread::yield();
    });

    if (!renderThread->IsStopRequested()) _colorBuffer->ResolveBucket(0, 0, width, height);
    if (!renderThread->IsStopRequested() && _albedoBuffer) _albedoBuffer->ResolveBucket(0, 0, width, height);
    if (!renderThread->IsStopRequested() && _normalBuffer) _normalBuffer->ResolveBucket(0, 0, width, height);
    if (!renderThread->IsStopRequested() && _depthBuffer) _depthBuffer->ResolveBucket(0, 0, width, height);
    
    _previousViewMatrix = _viewMatrix;
    _previousProjMatrix = _projMatrix;
    _previousInverseViewMatrix = _inverseViewMatrix;
    _previousInverseProjMatrix = _inverseProjMatrix;
}
#endif

// 8x8 font (ASCII 32-127)
static const unsigned char font8x8[96][8] = {
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00},{0x08,0x08,0x08,0x08,0x08,0x00,0x08,0x00},{0x14,0x14,0x14,0x00,0x00,0x00,0x00,0x00},
    {0x14,0x14,0x3e,0x14,0x3e,0x14,0x14,0x00},{0x08,0x1e,0x28,0x1c,0x0a,0x3c,0x08,0x00},{0x18,0x24,0x04,0x08,0x10,0x24,0x18,0x00},
    {0x10,0x28,0x28,0x10,0x2a,0x44,0x3a,0x00},{0x08,0x08,0x10,0x00,0x00,0x00,0x00,0x00},{0x08,0x10,0x20,0x20,0x20,0x10,0x08,0x00},
    {0x10,0x08,0x04,0x04,0x04,0x08,0x10,0x00},{0x00,0x08,0x2a,0x1c,0x2a,0x08,0x00,0x00},{0x00,0x08,0x08,0x3e,0x08,0x08,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x08,0x08,0x10},{0x00,0x00,0x00,0x3e,0x00,0x00,0x00,0x00},{0x00,0x00,0x00,0x00,0x00,0x08,0x08,0x00},
    {0x00,0x04,0x08,0x10,0x20,0x40,0x00,0x00},{0x1c,0x22,0x26,0x2a,0x32,0x22,0x1c,0x00},{0x08,0x18,0x28,0x08,0x08,0x08,0x3e,0x00},
    {0x1c,0x22,0x02,0x1c,0x20,0x20,0x3e,0x00},{0x3e,0x02,0x04,0x18,0x02,0x22,0x1c,0x00},{0x04,0x0c,0x14,0x24,0x3e,0x04,0x04,0x00},
    {0x3e,0x20,0x3c,0x02,0x02,0x22,0x1c,0x00},{0x1c,0x20,0x20,0x3c,0x22,0x22,0x1c,0x00},{0x3e,0x02,0x04,0x08,0x10,0x10,0x10,0x00},
    {0x1c,0x22,0x22,0x1c,0x22,0x22,0x1c,0x00},{0x1c,0x22,0x22,0x1e,0x02,0x02,0x1c,0x00},{0x00,0x00,0x08,0x00,0x00,0x08,0x00,0x00},
    {0x00,0x00,0x08,0x00,0x00,0x08,0x08,0x10},{0x04,0x08,0x10,0x20,0x10,0x08,0x04,0x00},{0x00,0x00,0x3e,0x00,0x3e,0x00,0x00,0x00},
    {0x10,0x08,0x04,0x02,0x04,0x08,0x10,0x00},{0x1c,0x22,0x02,0x0c,0x10,0x00,0x10,0x00},{0x1c,0x22,0x2a,0x3a,0x1a,0x02,0x1c,0x00},
    {0x08,0x14,0x22,0x22,0x3e,0x22,0x22,0x00},{0x3c,0x22,0x22,0x3c,0x22,0x22,0x3c,0x00},{0x1c,0x22,0x20,0x20,0x20,0x22,0x1c,0x00},
    {0x3c,0x22,0x22,0x22,0x22,0x22,0x3c,0x00},{0x3e,0x20,0x20,0x3c,0x20,0x20,0x3e,0x00},{0x3e,0x20,0x20,0x3c,0x20,0x20,0x20,0x00},
    {0x1c,0x22,0x20,0x2e,0x22,0x22,0x1c,0x00},{0x22,0x22,0x22,0x3e,0x22,0x22,0x22,0x00},{0x1c,0x08,0x08,0x08,0x08,0x08,0x1c,0x00},
    {0x0e,0x04,0x04,0x04,0x04,0x24,0x18,0x00},{0x22,0x24,0x28,0x30,0x28,0x24,0x22,0x00},{0x20,0x20,0x20,0x20,0x20,0x20,0x3e,0x00},
    {0x41,0x63,0x55,0x49,0x41,0x41,0x41,0x00},{0x41,0x61,0x51,0x49,0x45,0x43,0x41,0x00},{0x1c,0x22,0x22,0x22,0x22,0x22,0x1c,0x00},
    {0x3c,0x22,0x22,0x3c,0x20,0x20,0x20,0x00},{0x1c,0x22,0x22,0x22,0x2a,0x24,0x1a,0x00},{0x3c,0x22,0x22,0x3c,0x28,0x24,0x22,0x00},
    {0x1c,0x22,0x20,0x1c,0x02,0x22,0x1c,0x00},{0x3e,0x08,0x08,0x08,0x08,0x08,0x08,0x00},{0x22,0x22,0x22,0x22,0x22,0x22,0x1c,0x00},
    {0x22,0x22,0x22,0x22,0x22,0x14,0x08,0x00},{0x41,0x41,0x41,0x49,0x55,0x63,0x41,0x00},{0x22,0x22,0x14,0x08,0x14,0x22,0x22,0x00},
    {0x22,0x22,0x22,0x14,0x08,0x08,0x08,0x00},{0x3e,0x02,0x04,0x08,0x10,0x20,0x3e,0x00},{0x1e,0x10,0x10,0x10,0x10,0x10,0x1e,0x00},
    {0x00,0x40,0x20,0x10,0x08,0x04,0x00,0x00},{0x3c,0x04,0x04,0x04,0x04,0x04,0x3c,0x00},{0x08,0x14,0x22,0x00,0x00,0x00,0x00,0x00},
    {0x00,0x00,0x00,0x00,0x00,0x00,0x3e,0x00},{0x10,0x08,0x04,0x00,0x00,0x00,0x00,0x00},{0x00,0x00,0x1c,0x02,0x1e,0x22,0x1e,0x00},
    {0x20,0x20,0x3c,0x22,0x22,0x22,0x3c,0x00},{0x00,0x00,0x1c,0x20,0x20,0x20,0x1c,0x00},{0x02,0x02,0x1e,0x22,0x22,0x22,0x1e,0x00},
    {0x00,0x00,0x1c,0x22,0x3e,0x20,0x1c,0x00},{0x0c,0x12,0x10,0x3c,0x10,0x10,0x10,0x00},{0x00,0x00,0x1e,0x22,0x22,0x1e,0x02,0x1c},
    {0x20,0x20,0x3c,0x22,0x22,0x22,0x22,0x00},{0x08,0x00,0x18,0x08,0x08,0x08,0x1c,0x00},{0x08,0x00,0x18,0x08,0x08,0x08,0x08,0x30},
    {0x20,0x20,0x24,0x28,0x30,0x28,0x24,0x00},{0x18,0x08,0x08,0x08,0x08,0x08,0x1c,0x00},{0x00,0x00,0x34,0x4a,0x4a,0x4a,0x4a,0x00},
    {0x00,0x00,0x3c,0x22,0x22,0x22,0x22,0x00},{0x00,0x00,0x1c,0x22,0x22,0x22,0x1c,0x00},{0x00,0x00,0x3c,0x22,0x22,0x3c,0x20,0x20},
    {0x00,0x00,0x1e,0x22,0x22,0x1e,0x02,0x02},{0x00,0x00,0x2c,0x32,0x20,0x20,0x20,0x00},{0x00,0x00,0x1e,0x20,0x1c,0x02,0x3c,0x00},
    {0x10,0x3e,0x10,0x10,0x10,0x12,0x0c,0x00},{0x00,0x00,0x22,0x22,0x22,0x22,0x1e,0x00},{0x00,0x00,0x22,0x22,0x22,0x14,0x08,0x00},
    {0x00,0x00,0x41,0x49,0x49,0x49,0x36,0x00},{0x00,0x00,0x22,0x14,0x08,0x14,0x22,0x00},{0x00,0x00,0x22,0x22,0x22,0x1e,0x02,0x1c},
    {0x00,0x00,0x3e,0x04,0x08,0x10,0x3e,0x00},{0x0e,0x08,0x08,0x38,0x08,0x08,0x0e,0x00},{0x08,0x08,0x08,0x08,0x08,0x08,0x08,0x00},
    {0x38,0x08,0x08,0x0e,0x08,0x08,0x38,0x00},{0x20,0x10,0x08,0x00,0x00,0x00,0x00,0x00},{0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}
};

void HdGeminiRenderer::_DrawChar(int x, int y, char c, const GfVec4f& color, int scale)
{
    if (!_colorBuffer) return;
    unsigned int width = _dataWindow.GetWidth();
    unsigned int height = _dataWindow.GetHeight();
    
    if (c < 32 || c > 127) c = 32;
    const unsigned char* glyph = font8x8[c - 32];
    
    for (int cy = 0; cy < 8; ++cy) {
        for (int cx = 0; cx < 8; ++cx) {
            if (glyph[cy] & (1 << (7 - cx))) {
                for (int sy = 0; sy < scale; ++sy) {
                    for (int sx = 0; sx < scale; ++sx) {
                        int px = x + cx * scale + sx;
                        int py = height - 1 - (y + cy * scale + sy); // Y-down to Y-up
                        if (px >= 0 && px < (int)width && py >= 0 && py < (int)height) {
                            float p[4] = {color[0], color[1], color[2], color[3]};
                            _colorBuffer->Write(GfVec3i(px, py, 0), 4, p);
                        }
                    }
                }
            }
        }
    }
}

void HdGeminiRenderer::_DrawStats()
{
    std::string syclStatus = "OFF";
#ifdef HDGEMINI_HAS_SYCL
    if (_enableSycl && _syclQueue) syclStatus = "ON (" + _syclDeviceName + ")";
#endif

    char line1[256];
    snprintf(line1, sizeof(line1), "hdGemini | Frame: %d/%d | Res: 1/%d | SYCL: %s", 
             _frameCount, _targetSampleCount, _resolutionLevel, syclStatus.c_str());

    char line2[256];
    float mrps = _raysPerSecond / 1000000.0f;
    snprintf(line2, sizeof(line2), "Rays: %.2f M/s | Progression: %.1f ms", mrps, _lastProgressionTimeMs);

    float totalSeconds = 0.0f;
    if (!_isConverged) {
        totalSeconds = std::chrono::duration<float>(std::chrono::high_resolution_clock::now() - _renderStartTime).count();
    } else {
        totalSeconds = std::chrono::duration<float>(_renderEndTime - _renderStartTime).count();
    }
    
    char line3[256];
    snprintf(line3, sizeof(line3), "Total Time: %.2f s", totalSeconds);

    GfVec4f color(1.0f, 1.0f, 0.0f, 1.0f); // Yellow
    int scale = 1;
    int lineHeight = 10 * scale;
    
    auto drawText = [&](const char* text, int x, int y) {
        int cursorX = x;
        for (int i = 0; text[i] != '\0'; ++i) {
            _DrawChar(cursorX + 1, y + 1, text[i], GfVec4f(0, 0, 0, 1), scale);
            _DrawChar(cursorX, y, text[i], color, scale);
            cursorX += 8 * scale;
        }
    };

    drawText(line1, 10, 10);
    drawText(line2, 10, 10 + lineHeight);
    drawText(line3, 10, 10 + lineHeight * 2);
}
