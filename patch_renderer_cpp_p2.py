import sys
import re

with open("renderer.cpp", "r") as f:
    content = f.read()

# 1. Destructor
dtor_find = """HdGeminiRenderer::~HdGeminiRenderer()
{
#ifdef HDGEMINI_HAS_SYCL
    if (_syclQueue) {
        if (_rayBuffer) sycl::free(_rayBuffer, *_syclQueue);
        delete _syclQueue;
    }
#endif
}"""
dtor_replace = """HdGeminiRenderer::~HdGeminiRenderer()
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
}"""
content = content.replace(dtor_find, dtor_replace)

# 2. Clear()
clear_find = """#ifdef HDGEMINI_HAS_SYCL
        if (_syclQueue) {
            size_t newSize = width * height;
            if (_rayBufferSize < newSize) {
                if (_rayBuffer) sycl::free(_rayBuffer, *_syclQueue);
                _rayBuffer = sycl::malloc_shared<RayState>(newSize, *_syclQueue);
                _rayBufferSize = newSize;
            }
        }
#endif"""
clear_replace = """#ifdef HDGEMINI_HAS_SYCL
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
#endif"""
content = content.replace(clear_find, clear_replace)

# 3. _PrepareScene
prep_scene_find = """    _BuildTLAS(renderThread);
}"""
prep_scene_replace = """#ifdef HDGEMINI_HAS_SYCL
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
        } else {
            _hasDomeLight = false;
        }
    }
#endif

    _BuildTLAS(renderThread);
}"""
content = content.replace(prep_scene_find, prep_scene_replace)

# 4. Remove Phase 1 SYCL from _RenderTiles
rt_start = content.find("#ifdef HDGEMINI_HAS_SYCL\n    if (_syclQueue && _rayBuffer && !isInteractive) {")
rt_end = content.find("    WorkParallelForN(numBuckets, [&](size_t b_start, size_t b_end) {", rt_start)
if rt_start != -1 and rt_end != -1:
    new_rt = """#ifdef HDGEMINI_HAS_SYCL
    if (_syclQueue && _rayBuffer && !isInteractive) {
        _RenderTilesSYCL(renderThread, delegate);
        return;
    }
#endif\n\n"""
    content = content[:rt_start] + new_rt + content[rt_end:]

# 5. Fix _RenderTiles CPU loop to NOT use SYCL
loop_find = """                    bool useSYCLBuffer = false;
#ifdef HDGEMINI_HAS_SYCL
                    if (_syclQueue && _rayBuffer && !isInteractive) {
                        useSYCLBuffer = true;
                    }
#endif
                    
                    if (useSYCLBuffer) {
#ifdef HDGEMINI_HAS_SYCL
                        size_t idx = y * width + x;
                        RayState& rs = _rayBuffer[idx];
                        if (!rs.active) continue;
                        rayOriginWorld = GfVec3f(rs.origin[0], rs.origin[1], rs.origin[2]);
                        rayDirWorld = GfVec3f(rs.dir[0], rs.dir[1], rs.dir[2]);
                        rng = rs.rng;
                        lambda = rs.lambda;
                        exposureMultiplier = rs.exposureMultiplier;
#endif
                    } else {"""
loop_replace = """                    bool useSYCLBuffer = false;
                    if (useSYCLBuffer) {
                    } else {"""
content = content.replace(loop_find, loop_replace)

with open("renderer.cpp", "w") as f:
    f.write(content)
print("renderer.cpp Phase 2 scaffolding complete")