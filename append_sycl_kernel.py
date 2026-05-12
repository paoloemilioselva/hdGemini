import sys

with open("renderer.cpp", "a") as f:
    f.write("""

#ifdef HDGEMINI_HAS_SYCL
void HdGeminiRenderer::_RenderTilesSYCL(HdRenderThread *renderThread, HdGeminiRenderDelegate* delegate)
{
    unsigned int width = _dataWindow.GetWidth();
    unsigned int height = _dataWindow.GetHeight();
    if (width == 0 || height == 0 || !_colorBuffer) return;
    
    GfVec3f cameraPosWorld(_inverseViewMatrix.Transform(GfVec3f(0, 0, 0)));
    std::lock_guard<std::recursive_mutex> lock(delegate->GetSceneLock());
    
    // --- GPU Ray Generation Phase ---
    float invWidth = 1.0f / width;
    float invHeight = 1.0f / height;
    float lensDistortion = _lensDistortion;
    bool enableDoF = _enableDoF;
    int bokehBlades = _bokehBlades;
    float apertureRadius = (_focalLength / 10.0f) / (2.0f * _fStop);
    float focusDist = _focusDistance;
    bool enablePhysicalCamera = _enablePhysicalCamera;
    float iso = _iso;
    float shutterSpeed = _shutterSpeed;
    float fStop = _fStop;
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
                lensU *= apertureRadius; lensV *= apertureRadius;

                float lensCam[4] = {lensU, lensV, 0.0f, 1.0f};
                float focalCam[4] = {nearPlanePointCam[0]*focusDist, nearPlanePointCam[1]*focusDist, nearPlanePointCam[2]*focusDist, 1.0f};
                float lensWorld[4] = {0,0,0,0}; float focalWorld[4] = {0,0,0,0};
                for(int i=0; i<4; ++i) { for(int j=0; j<4; ++j) { lensWorld[i] += lensCam[j] * invView[j*4 + i]; focalWorld[i] += focalCam[j] * invView[j*4 + i]; } }
                
                rayOrigin[0] = lensWorld[0]; rayOrigin[1] = lensWorld[1]; rayOrigin[2] = lensWorld[2];
                float dx = focalWorld[0] - lensWorld[0]; float dy = focalWorld[1] - lensWorld[1]; float dz = focalWorld[2] - lensWorld[2];
                float len = sycl::sqrt(dx*dx + dy*dy + dz*dz);
                rayDir[0] = dx/len; rayDir[1] = dy/len; rayDir[2] = dz/len;
            } else {
                float nearCam4[4] = {nearPlanePointCam[0], nearPlanePointCam[1], nearPlanePointCam[2], 1.0f};
                float nearWorld[4] = {0,0,0,0};
                for(int i=0; i<4; ++i) { for(int j=0; j<4; ++j) { nearWorld[i] += nearCam4[j] * invView[j*4 + i]; } }
                float dx = nearWorld[0] - camPos[0]; float dy = nearWorld[1] - camPos[1]; float dz = nearWorld[2] - camPos[2];
                float len = sycl::sqrt(dx*dx + dy*dy + dz*dz);
                rayDir[0] = dx/len; rayDir[1] = dy/len; rayDir[2] = dz/len;
            }

            rayBuf[idx].origin[0] = rayOrigin[0]; rayBuf[idx].origin[1] = rayOrigin[1]; rayBuf[idx].origin[2] = rayOrigin[2];
            rayBuf[idx].dir[0] = rayDir[0]; rayBuf[idx].dir[1] = rayDir[1]; rayBuf[idx].dir[2] = rayDir[2];
            rayBuf[idx].rng = rng;
            rayBuf[idx].x = x; rayBuf[idx].y = y;
            rayBuf[idx].active = true;

            float u_lambda = randFloat();
            float lambda0 = 360.0f + u_lambda * (830.0f - 360.0f);
            rayBuf[idx].lambda.lambda[0] = lambda0;
            for (int i = 1; i < 4; ++i) {
                float l = lambda0 + (i * (830.0f - 360.0f) / 4.0f);
                if (l > 830.0f) l -= (830.0f - 360.0f);
                rayBuf[idx].lambda.lambda[i] = l;
            }

            float exposure = 1.0f;
            if (enablePhysicalCamera) {
                exposure = (iso / 100.0f) * shutterSpeed / (fStop * fStop) * 100.0f;
            }
            rayBuf[idx].exposureMultiplier = exposure;
            
            for(int i=0; i<4; ++i) {
                rayBuf[idx].throughput[i] = exposure;
                rayBuf[idx].totalRadiance[i] = 0.0f;
            }
            rayBuf[idx].bounce = 0;
            rayBuf[idx].reflectionBounces = 0;
            rayBuf[idx].refractionBounces = 0;
            rayBuf[idx].isInside = false;
        });
    });
    _syclQueue->wait();

    // CPU-GPU Ping-Pong
    int maxDepth = 32;
    size_t numRays = width * height;
    
    // Convert Physical Sky sun dir
    float azimuthRad = _physicalSkyAzimuth * (float)(M_PI / 180.0);
    float altitudeRad = _physicalSkyAltitude * (float)(M_PI / 180.0);
    GfVec3f physicalSunDir = GfVec3f(
        std::cos(altitudeRad) * std::sin(azimuthRad),
        std::sin(altitudeRad),
        std::cos(altitudeRad) * std::cos(azimuthRad)
    ).GetNormalized();
    
    for (int bounce = 0; bounce < maxDepth; ++bounce) {
        if (renderThread->IsStopRequested()) break;

        // 1. CPU Intersect
        WorkParallelForN(numRays, [&](size_t start, size_t end) {
            for (size_t i = start; i < end; ++i) {
                RayState& rs = _rayBuffer[i];
                if (!rs.active) continue;
                HitRecord hit;
                bool wasHit = _IntersectTLAS(GfVec3f(rs.origin[0], rs.origin[1], rs.origin[2]), GfVec3f(rs.dir[0], rs.dir[1], rs.dir[2]), hit, renderThread);
                HitState& hs = _hitBuffer[i];
                hs.hit = wasHit;
                if (wasHit) {
                    hs.t = hit.t;
                    GfVec3f hp = GfVec3f(rs.origin[0], rs.origin[1], rs.origin[2]) + GfVec3f(rs.dir[0], rs.dir[1], rs.dir[2]) * hit.t;
                    hs.hitPos[0] = hp[0]; hs.hitPos[1] = hp[1]; hs.hitPos[2] = hp[2];
                    
                    if (!hit.diffuseTexture.GetAssetPath().empty()) hit.baseColor = GfCompMult(hit.baseColor, _SampleTexture(hit.diffuseTexture, hit.uv));
                    if (!hit.metallicTexture.GetAssetPath().empty()) hit.metallic = _SampleTexture(hit.metallicTexture, hit.uv, true)[0];
                    if (!hit.roughnessTexture.GetAssetPath().empty()) hit.roughness = _SampleTexture(hit.roughnessTexture, hit.uv, true)[0];
                    if (!hit.normalTexture.GetAssetPath().empty()) {
                        GfVec3f nTex = _SampleTexture(hit.normalTexture, hit.uv, true);
                        nTex = nTex * 2.0f - GfVec3f(1.0f);
                        GfVec3f n = hit.smoothNormal; GfVec3f t = hit.dpdu; GfVec3f b = hit.dpdv;
                        t = (t - n * GfDot(t, n)).GetNormalized();
                        b = (b - n * GfDot(b, n) - t * GfDot(b, t)).GetNormalized();
                        if (GfDot(GfCross(n, t), b) < 0.0f) b = -b;
                        hit.smoothNormal = (t * nTex[0] + b * nTex[1] + n * nTex[2]).GetNormalized();
                    }

                    hs.shadingNormal[0] = hit.smoothNormal[0]; hs.shadingNormal[1] = hit.smoothNormal[1]; hs.shadingNormal[2] = hit.smoothNormal[2];
                    hs.baseColor[0] = hit.baseColor[0]; hs.baseColor[1] = hit.baseColor[1]; hs.baseColor[2] = hit.baseColor[2];
                    hs.metallic = hit.metallic; hs.roughness = hit.roughness;
                    hs.specularColor[0] = hit.specularColor[0]; hs.specularColor[1] = hit.specularColor[1]; hs.specularColor[2] = hit.specularColor[2];
                    hs.specular = hit.specular; hs.opacity = hit.opacity; hs.ior = hit.ior;
                    hs.transmission = hit.transmission;
                    hs.transmissionColor[0] = hit.transmissionColor[0]; hs.transmissionColor[1] = hit.transmissionColor[1]; hs.transmissionColor[2] = hit.transmissionColor[2];
                    hs.emission[0] = hit.emission[0]; hs.emission[1] = hit.emission[1]; hs.emission[2] = hit.emission[2];
                    hs.coat = hit.coat; hs.coatColor[0] = hit.coatColor[0]; hs.coatColor[1] = hit.coatColor[1]; hs.coatColor[2] = hit.coatColor[2];
                    hs.coatRoughness = hit.coatRoughness; hs.coatIor = hit.coatIor;
                    hs.transmissionDepth = hit.transmissionDepth;
                    hs.transmissionScatter[0] = hit.transmissionScatter[0]; hs.transmissionScatter[1] = hit.transmissionScatter[1]; hs.transmissionScatter[2] = hit.transmissionScatter[2];
                    hs.sheen = hit.sheen; hs.sheenColor[0] = hit.sheenColor[0]; hs.sheenColor[1] = hit.sheenColor[1]; hs.sheenColor[2] = hit.sheenColor[2];
                    hs.sheenRoughness = hit.sheenRoughness;
                    hs.subsurface = hit.subsurface; hs.subsurfaceColor[0] = hit.subsurfaceColor[0]; hs.subsurfaceColor[1] = hit.subsurfaceColor[1]; hs.subsurfaceColor[2] = hit.subsurfaceColor[2];
                    hs.subsurfaceScale = hit.subsurfaceScale; hs.thinWalled = hit.thinWalled;

                    if (rs.bounce == 0) {
                        rs.albedo[0] = hit.baseColor[0]; rs.albedo[1] = hit.baseColor[1]; rs.albedo[2] = hit.baseColor[2];
                        rs.normal[0] = hit.smoothNormal[0]; rs.normal[1] = hit.smoothNormal[1]; rs.normal[2] = hit.smoothNormal[2];
                    }
                }
            }
        });
        
        if (renderThread->IsStopRequested()) break;

        // 2. GPU Shade
        HitState* hitBuf = _hitBuffer;
        ShadowRay* shadowBuf = _shadowRayBuffer;
        
        bool enableSubsurface = _enableSubsurface;
        int maxRefl = _maxReflectionBounces;
        int maxRefr = _maxRefractionBounces;

        _syclQueue->submit([&](sycl::handler& cgh) {
            cgh.parallel_for<class ShadeKernel>(sycl::range<1>(width * height), [=](sycl::item<1> item) {
                size_t idx = item.get_id(0);
                RayState& rs = rayBuf[idx];
                shadowBuf[idx].active = false;
                
                if (!rs.active) return;
                HitState& hs = hitBuf[idx];

                if (!hs.hit) {
                    rs.active = false;
                    return; // Environment map sampling omitted for simplicity in this port block.
                }

                // Simplified shading: just a basic diffuse reflection for this iteration
                // to validate the architecture without porting 300 lines of GGX math to raw SYCL.
                
                auto randFloat = [&]() {
                    rs.rng = rs.rng * 1664525 + 1013904223;
                    return (float)rs.rng / (float)0xFFFFFFFF;
                };
                
                float u1 = randFloat();
                float u2 = randFloat();
                float r = sycl::sqrt(u1);
                float theta = 2.0f * 3.14159265f * u2;
                
                float sample[3] = {r * sycl::cos(theta), sycl::sqrt(1.0f - u1), r * sycl::sin(theta)};
                
                float nx = hs.shadingNormal[0], ny = hs.shadingNormal[1], nz = hs.shadingNormal[2];
                float upX = 0, upY = 1, upZ = 0;
                if (sycl::fabs(ny) >= 0.999f) { upX = 1; upY = 0; }
                
                float tx = upY * nz - upZ * ny;
                float ty = upZ * nx - upX * nz;
                float tz = upX * ny - upY * nx;
                float tlen = sycl::sqrt(tx*tx + ty*ty + tz*tz);
                tx /= tlen; ty /= tlen; tz /= tlen;
                
                float bx = ny * tz - nz * ty;
                float by = nz * tx - nx * tz;
                float bz = nx * ty - ny * tx;
                
                rs.dir[0] = sample[0] * tx + sample[1] * nx + sample[2] * bx;
                rs.dir[1] = sample[0] * ty + sample[1] * ny + sample[2] * by;
                rs.dir[2] = sample[0] * tz + sample[1] * nz + sample[2] * bz;
                
                rs.origin[0] = hs.hitPos[0] + hs.shadingNormal[0] * 1e-4f;
                rs.origin[1] = hs.hitPos[1] + hs.shadingNormal[1] * 1e-4f;
                rs.origin[2] = hs.hitPos[2] + hs.shadingNormal[2] * 1e-4f;
                
                // Extremely simple emission + bounce
                for(int i=0; i<4; ++i) {
                    rs.totalRadiance[i] += rs.throughput[i] * hs.emission[0]; // simplistic RGB to Spec
                    rs.throughput[i] *= hs.baseColor[0]; // simplistic RGB to Spec
                }
                
                rs.bounce++;
                if (rs.bounce > 3) {
                    float p = sycl::fmax(rs.throughput[0], sycl::fmax(rs.throughput[1], rs.throughput[2]));
                    if (randFloat() > p) rs.active = false;
                    else {
                        for(int i=0; i<4; ++i) rs.throughput[i] /= p;
                    }
                }
            });
        });
        _syclQueue->wait();
    }
    
    // Splat
    WorkParallelForN(numRays, [&](size_t start, size_t end) {
        for (size_t i = start; i < end; ++i) {
            RayState& rs = _rayBuffer[i];
            
            SampledSpectrum totalSpec;
            for(int j=0; j<4; ++j) totalSpec[j] = rs.totalRadiance[j];
            
            SampledSpectrum heroSpec;
            for(int j=0; j<4; ++j) heroSpec[j] = totalSpec[0];
            SampledSpectrum diffSpec;
            for(int j=0; j<4; ++j) diffSpec[j] = totalSpec[j] - totalSpec[0];
            
            GfVec3f heroRGB = SpectrumToRGB(heroSpec, rs.lambda);
            GfVec3f diffRGB = SpectrumToRGB(diffSpec, rs.lambda);
            
            _accumHeroRGB[i] += heroRGB;
            _accumDiffRGB[i] += diffRGB;
            
            int x = i % width;
            int y = i / width;
            GfVec3f hitColor = heroRGB + diffRGB;
            _colorBuffer->WriteSample(GfVec3f(x, y, 0), GfVec4f(hitColor[0], hitColor[1], hitColor[2], 1.0f));
            if (_albedoBuffer) _albedoBuffer->WriteSample(GfVec3f(x, y, 0), GfVec4f(rs.albedo[0], rs.albedo[1], rs.albedo[2], 1.0f));
            if (_normalBuffer) _normalBuffer->WriteSample(GfVec3f(x, y, 0), GfVec4f(rs.normal[0], rs.normal[1], rs.normal[2], 1.0f));
        }
    });

#endif
}
""")
print("Done")