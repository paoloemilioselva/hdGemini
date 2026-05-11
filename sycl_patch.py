import sys

with open("renderer.cpp", "r") as f:
    content = f.read()

# 1. Update Clear()
clear_find = """    if (width > 0 && height > 0) {
        _accumHeroRGB.assign(width * height, GfVec3f(0.0f));
        _accumDiffRGB.assign(width * height, GfVec3f(0.0f));
    }"""
clear_replace = """    if (width > 0 && height > 0) {
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
        }
#endif
    }"""
content = content.replace(clear_find, clear_replace)

# 2. Update _RenderTiles()
render_find = """    WorkParallelForN(numBuckets, [&](size_t b_start, size_t b_end) {"""
render_replace = """#ifdef HDGEMINI_HAS_SYCL
    if (_syclQueue && _rayBuffer && !isInteractive) {
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
            cgh.parallel_for<class GenerateRays>(sycl::range<2>(width, height), [=](sycl::item<2> item) {
                int x = item.get_id(0);
                int y = item.get_id(1);
                size_t idx = y * width + x;
                
                uint32_t rng = (uint32_t)(y * width + x) ^ (uint32_t)(frameCount * 12345);
                
                auto randFloat = [&]() {
                    rng = rng * 1664525 + 1013904223;
                    return (float)rng / (float)0xFFFFFFFF;
                };

                float px = (float)x;
                float py = (float)y;
                
                if (antiAliasingFilter == 0) {
                    px += 0.5f; py += 0.5f;
                } else if (antiAliasingFilter == 1) {
                    px += randFloat(); py += randFloat();
                } else if (antiAliasingFilter == 2) {
                    auto tent = [](float u) {
                        return u < 0.5f ? sycl::sqrt(2.0f * u) - 1.0f : 1.0f - sycl::sqrt(2.0f - 2.0f * u);
                    };
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
                for(int i=0; i<4; ++i) {
                    for(int j=0; j<4; ++j) {
                        nearCam[i] += clip[j] * invProj[j*4 + i];
                    }
                }
                float invW = 1.0f / nearCam[3];
                float nearPlanePointCam[3] = {nearCam[0]*invW, nearCam[1]*invW, nearCam[2]*invW};
                
                float rayOrigin[3] = {camPos[0], camPos[1], camPos[2]};
                float rayDir[3];

                if (enableDoF) {
                    float lensU, lensV;
                    if (bokehBlades < 3) {
                        float r = sycl::sqrt(randFloat());
                        float theta = 2.0f * 3.14159265f * randFloat();
                        lensU = r * sycl::cos(theta); lensV = r * sycl::sin(theta);
                    } else {
                        float theta = 2.0f * 3.14159265f * randFloat();
                        float r = sycl::sqrt(randFloat());
                        float sectorAngle = 2.0f * 3.14159265f / bokehBlades;
                        float sector = sycl::floor(theta / sectorAngle);
                        float angleInSector = theta - sector * sectorAngle;
                        float d = sycl::cos(sectorAngle / 2.0f) / sycl::cos(sectorAngle / 2.0f - angleInSector);
                        lensU = r * d * sycl::cos(theta); lensV = r * d * sycl::sin(theta);
                    }
                    lensU *= apertureRadius; lensV *= apertureRadius;

                    float lensCam[4] = {lensU, lensV, 0.0f, 1.0f};
                    float focalCam[4] = {nearPlanePointCam[0]*focusDist, nearPlanePointCam[1]*focusDist, nearPlanePointCam[2]*focusDist, 1.0f};

                    float lensWorld[4] = {0,0,0,0};
                    float focalWorld[4] = {0,0,0,0};

                    for(int i=0; i<4; ++i) {
                        for(int j=0; j<4; ++j) {
                            lensWorld[i] += lensCam[j] * invView[j*4 + i];
                            focalWorld[i] += focalCam[j] * invView[j*4 + i];
                        }
                    }
                    
                    rayOrigin[0] = lensWorld[0]; rayOrigin[1] = lensWorld[1]; rayOrigin[2] = lensWorld[2];
                    float dx = focalWorld[0] - lensWorld[0]; float dy = focalWorld[1] - lensWorld[1]; float dz = focalWorld[2] - lensWorld[2];
                    float len = sycl::sqrt(dx*dx + dy*dy + dz*dz);
                    rayDir[0] = dx/len; rayDir[1] = dy/len; rayDir[2] = dz/len;
                } else {
                    float nearCam4[4] = {nearPlanePointCam[0], nearPlanePointCam[1], nearPlanePointCam[2], 1.0f};
                    float nearWorld[4] = {0,0,0,0};
                    for(int i=0; i<4; ++i) {
                        for(int j=0; j<4; ++j) {
                            nearWorld[i] += nearCam4[j] * invView[j*4 + i];
                        }
                    }
                    float dx = nearWorld[0] - camPos[0]; float dy = nearWorld[1] - camPos[1]; float dz = nearWorld[2] - camPos[2];
                    float len = sycl::sqrt(dx*dx + dy*dy + dz*dz);
                    rayDir[0] = dx/len; rayDir[1] = dy/len; rayDir[2] = dz/len;
                }

                rayBuf[idx].origin[0] = rayOrigin[0]; rayBuf[idx].origin[1] = rayOrigin[1]; rayBuf[idx].origin[2] = rayOrigin[2];
                rayBuf[idx].dir[0] = rayDir[0]; rayBuf[idx].dir[1] = rayDir[1]; rayBuf[idx].dir[2] = rayDir[2];
                rayBuf[idx].rng = rng;
                rayBuf[idx].x = x;
                rayBuf[idx].y = y;
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
            });
        });
        _syclQueue->wait();
    }
#endif

    WorkParallelForN(numBuckets, [&](size_t b_start, size_t b_end) {"""
content = content.replace(render_find, render_replace)


loop_find = """                    uint32_t rng = (uint32_t)(y * width + x) ^ (uint32_t)(_frameCount * 12345);
                    float px = (float)x;
                    float py = (float)y;
                    
                    if (isInteractive) {
                         px += res * 0.5f;
                         py += res * 0.5f;
                    } else {
                        if (_antiAliasingFilter == 0) { // None
                            px += 0.5f;
                            py += 0.5f;
                        } else if (_antiAliasingFilter == 1) { // Box
                            px += RandomFloat(rng);
                            py += RandomFloat(rng);
                        } else if (_antiAliasingFilter == 2) { // Tent
                            auto tent = [](float u) {
                                return u < 0.5f ? std::sqrt(2.0f * u) - 1.0f : 1.0f - std::sqrt(2.0f - 2.0f * u);
                            };
                            px += 0.5f + tent(RandomFloat(rng));
                            py += 0.5f + tent(RandomFloat(rng));
                        } else if (_antiAliasingFilter == 3) { // Gaussian
                            float u1 = std::max(1e-6f, RandomFloat(rng));
                            float u2 = RandomFloat(rng);
                            float r = std::sqrt(-2.0f * std::log(u1));
                            float theta = 2.0f * (float)M_PI * u2;
                            float sigma = 0.5f;
                            px += 0.5f + r * std::cos(theta) * sigma;
                            py += 0.5f + r * std::sin(theta) * sigma;
                        }
                    }

                    float ndcX = (2.0f * px / width) - 1.0f;
                    float ndcY = (2.0f * py / height) - 1.0f;
                    
                    if (_lensDistortion != 0.0f) {
                        float r2 = ndcX * ndcX + ndcY * ndcY;
                        float f = 1.0f + _lensDistortion * r2;
                        ndcX *= f;
                        ndcY *= f;
                    }

                    GfVec3f nearPlanePointCam = GfVec3f(_inverseProjMatrix.Transform(GfVec3d(ndcX, ndcY, -1.0)));
                    
                    GfVec3f rayOriginWorld = cameraPosWorld;
                    GfVec3f rayDirWorld;

                    if (_enableDoF) {
                        // Assuming focusDistance is in scene units, and focalLength is in mm.
                        // We use focalLength / 10.0f to get cm, assuming 1 unit = 1 cm.
                        float apertureRadius = (_focalLength / 10.0f) / (2.0f * _fStop);
                        
                        float lensU, lensV;
                        if (_bokehBlades < 3) {
                            float r = std::sqrt(RandomFloat(rng));
                            float theta = 2.0f * M_PI * RandomFloat(rng);
                            lensU = r * std::cos(theta);
                            lensV = r * std::sin(theta);
                        } else {
                            // Generate point in regular polygon
                            float theta = 2.0f * M_PI * RandomFloat(rng);
                            float r = std::sqrt(RandomFloat(rng));
                            float sectorAngle = 2.0f * M_PI / _bokehBlades;
                            float sector = std::floor(theta / sectorAngle);
                            float angleInSector = theta - sector * sectorAngle;
                            float d = std::cos(sectorAngle / 2.0f) / std::cos(sectorAngle / 2.0f - angleInSector);
                            lensU = r * d * std::cos(theta);
                            lensV = r * d * std::sin(theta);
                        }
                        lensU *= apertureRadius;
                        lensV *= apertureRadius;

                        GfVec3f lensPointCam(lensU, lensV, 0.0f);
                        GfVec3f focalPointCam = nearPlanePointCam * _focusDistance;
                        
                        GfVec3f lensPointWorld = GfVec3f(_inverseViewMatrix.Transform(GfVec3d(lensPointCam)));
                        GfVec3f focalPointWorld = GfVec3f(_inverseViewMatrix.Transform(GfVec3d(focalPointCam)));
                        
                        rayOriginWorld = lensPointWorld;
                        rayDirWorld = (focalPointWorld - lensPointWorld).GetNormalized();
                    } else {
                        GfVec3f nearPlanePointWorld = GfVec3f(_inverseViewMatrix.Transform(GfVec3d(nearPlanePointCam)));
                        rayDirWorld = (nearPlanePointWorld - cameraPosWorld).GetNormalized();
                    }
                    
                    GfVec3f albedo(0.0f), normal(0.0f);
                    float u_lambda = RandomFloat(rng);
                    SampledWavelengths lambda = SampledWavelengths::SampleUniform(u_lambda);
                    
                    float exposureMultiplier = 1.0f;
                    if (_enablePhysicalCamera) {
                        exposureMultiplier = (_iso / 100.0f) * _shutterSpeed / (_fStop * _fStop) * 100.0f;
                    }"""

loop_replace = """                    GfVec3f rayOriginWorld;
                    GfVec3f rayDirWorld;
                    uint32_t rng;
                    SampledWavelengths lambda;
                    float exposureMultiplier;
                    
                    bool useSYCLBuffer = false;
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
                    } else {
                        rng = (uint32_t)(y * width + x) ^ (uint32_t)(_frameCount * 12345);
                        float px = (float)x;
                        float py = (float)y;
                        
                        if (isInteractive) {
                             px += res * 0.5f;
                             py += res * 0.5f;
                        } else {
                            if (_antiAliasingFilter == 0) { // None
                                px += 0.5f; py += 0.5f;
                            } else if (_antiAliasingFilter == 1) { // Box
                                px += RandomFloat(rng); py += RandomFloat(rng);
                            } else if (_antiAliasingFilter == 2) { // Tent
                                auto tent = [](float u) {
                                    return u < 0.5f ? std::sqrt(2.0f * u) - 1.0f : 1.0f - std::sqrt(2.0f - 2.0f * u);
                                };
                                px += 0.5f + tent(RandomFloat(rng)); py += 0.5f + tent(RandomFloat(rng));
                            } else if (_antiAliasingFilter == 3) { // Gaussian
                                float u1 = std::max(1e-6f, RandomFloat(rng));
                                float u2 = RandomFloat(rng);
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
                        
                        rayOriginWorld = cameraPosWorld;

                        if (_enableDoF) {
                            float apertureRadius = (_focalLength / 10.0f) / (2.0f * _fStop);
                            float lensU, lensV;
                            if (_bokehBlades < 3) {
                                float r = std::sqrt(RandomFloat(rng));
                                float theta = 2.0f * M_PI * RandomFloat(rng);
                                lensU = r * std::cos(theta); lensV = r * std::sin(theta);
                            } else {
                                float theta = 2.0f * M_PI * RandomFloat(rng);
                                float r = std::sqrt(RandomFloat(rng));
                                float sectorAngle = 2.0f * M_PI / _bokehBlades;
                                float sector = std::floor(theta / sectorAngle);
                                float angleInSector = theta - sector * sectorAngle;
                                float d = std::cos(sectorAngle / 2.0f) / std::cos(sectorAngle / 2.0f - angleInSector);
                                lensU = r * d * std::cos(theta); lensV = r * d * std::sin(theta);
                            }
                            lensU *= apertureRadius; lensV *= apertureRadius;

                            GfVec3f lensPointCam(lensU, lensV, 0.0f);
                            GfVec3f focalPointCam = nearPlanePointCam * _focusDistance;
                            
                            GfVec3f lensPointWorld = GfVec3f(_inverseViewMatrix.Transform(GfVec3d(lensPointCam)));
                            GfVec3f focalPointWorld = GfVec3f(_inverseViewMatrix.Transform(GfVec3d(focalPointCam)));
                            
                            rayOriginWorld = lensPointWorld;
                            rayDirWorld = (focalPointWorld - lensPointWorld).GetNormalized();
                        } else {
                            GfVec3f nearPlanePointWorld = GfVec3f(_inverseViewMatrix.Transform(GfVec3d(nearPlanePointCam)));
                            rayDirWorld = (nearPlanePointWorld - cameraPosWorld).GetNormalized();
                        }
                        
                        float u_lambda = RandomFloat(rng);
                        lambda = SampledWavelengths::SampleUniform(u_lambda);
                        
                        exposureMultiplier = 1.0f;
                        if (_enablePhysicalCamera) {
                            exposureMultiplier = (_iso / 100.0f) * _shutterSpeed / (_fStop * _fStop) * 100.0f;
                        }
                    }
                    
                    GfVec3f albedo(0.0f), normal(0.0f);"""

content = content.replace(loop_find, loop_replace)

with open("renderer.cpp", "w") as f:
    f.write(content)
print("done")