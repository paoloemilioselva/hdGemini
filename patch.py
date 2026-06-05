import re

def patch_file(filepath):
    with open(filepath, 'r') as f:
        content = f.read()
    
    # 1. Variables
    variables_insert = """
    // ReSTIR GI Variables
    bool useRestirGI = (_enableRestirGI && pixelX >= 0 && pixelY >= 0 && depth == 0);
    GfVec3f giPrimaryPos, giPrimaryNormal;
    float giPrimaryDepth = 0.0f;
    int giPrevX = pixelX, giPrevY = pixelY;
    bool giFoundPrev = false;
    GfVec3f giVirtualLightPos, giVirtualLightNormal;
    SampledSpectrum giVirtualRadiance(0.0f);
    SampledSpectrum giPrimaryThroughput(0.0f);
    bool hasGiData = false;
    SampledSpectrum totalRadianceBeforeBounce(0.0f);
"""
    content = content.replace("GfVec3f currentRayOrigin = rayOrigin;\n    GfVec3f currentRayDir = rayDir;", "GfVec3f currentRayOrigin = rayOrigin;\n    GfVec3f currentRayDir = rayDir;\n" + variables_insert)
    
    # 2. totalRadianceBeforeBounce
    content = content.replace("while (distRemaining > 0.0f && bounce < 8) {", "while (distRemaining > 0.0f && bounce < 8) {\n        totalRadianceBeforeBounce = totalRadiance;")
    
    # 3. bounce == 0 capture
    bounce0_capture = """
        if (bounce == 0 && useRestirGI) {
            giPrimaryPos = hitPos;
            giPrimaryNormal = shadingNormal;
            giPrimaryDepth = hit.t;
            giPrimaryThroughput = throughput;
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
        """
    content = content.replace("GfVec3f shadingNormal = hit.smoothNormal;\n        if (isInside) shadingNormal = -shadingNormal;\n\n        // --- Volumetric SSS (Random Walk for Objects) ---", "GfVec3f shadingNormal = hit.smoothNormal;\n        if (isInside) shadingNormal = -shadingNormal;\n" + bounce0_capture + "\n        // --- Volumetric SSS (Random Walk for Objects) ---")
    
    # 4. intercept bounce == 1 End of Direct Lighting
    interception = """
        // --- End of Direct Lighting ---
        if (useRestirGI && bounce == 1) {
            giVirtualLightPos = hitPos;
            giVirtualLightNormal = shadingNormal;
            
            SampledSpectrum bounceRad = totalRadiance - totalRadianceBeforeBounce;
            for(int i=0; i<SPECTRUM_SAMPLES; ++i) {
                giVirtualRadiance[i] = bounceRad[i] / std::max(throughput[i], 1e-6f);
            }
            totalRadiance = totalRadianceBeforeBounce; // Undo addition to totalRadiance
            hasGiData = true;
            break; // Stop path tracing base path
        }
        """
    content = content.replace("int numPhotonsTraced = 10000;\n            float area = M_PI * r * r;\n            SampledSpectrum causticRadiance = causticFlux / (area * numPhotonsTraced);\n            totalRadiance += throughput * causticRadiance;\n        }\n\n        // --- Indirect Path Selection (BSDF Sampling) ---", "int numPhotonsTraced = 10000;\n            float area = M_PI * r * r;\n            SampledSpectrum causticRadiance = causticFlux / (area * numPhotonsTraced);\n            totalRadiance += throughput * causticRadiance;\n        }\n" + interception + "\n        // --- Indirect Path Selection (BSDF Sampling) ---")
    
    # 5. GI ReSTIR logic at the end of the function
    restir_logic = """
    // --- ReSTIR GI ---
    if (useRestirGI && hasGiData && !_giPrevTemporalReservoirs.empty()) {
        GIReservoir r;
        
        // Target weight of base sample: luminance(giPrimaryThroughput * bsdf(X1) * giVirtualRadiance)
        // Wait, primaryThroughput already has bsdf(X1) multiplied into it at the end of bounce 0!
        // So throughput at bounce 1 is exactly primaryThroughput * bsdf(X1).
        // Since we extracted it at bounce 0, giPrimaryThroughput is JUST the exposure/attenuation up to the hit.
        // We need f(X1) * G(X1, X2). This is exactly what was used to scale throughput between bounce 0 and 1.
        // So the target weight is exactly luminance(throughput(at bounce 1) * giVirtualRadiance).
        // Wait, throughput was updated. Is it available here?
        // No, we broke the loop. But throughput is still intact at bounce 1!
        
        float lumBase = 0.2126f * giVirtualRadiance[0] + 0.7152f * giVirtualRadiance[1] + 0.0722f * giVirtualRadiance[2];
        float p_hat_base = lumBase * (throughput[0] + throughput[1] + throughput[2]) / 3.0f; // Approx
        
        float randValBase = qmc::SampleDimension(sampleIdx, qmcDim++, rng);
        r.Update(giVirtualLightPos, giVirtualLightNormal, GfVec3f(giVirtualRadiance[0], giVirtualRadiance[1], giVirtualRadiance[2]), (p_hat_base > 0.0f) ? 1.0f : 0.0f, randValBase); // M=1, w=1
        
        // Temporal Reuse
        if (giFoundPrev) {
            GIReservoir prevRes = _giPrevTemporalReservoirs[giPrevY * _lastWidth + giPrevX];
            float depthDiff = std::abs(prevRes.depth - giPrimaryDepth);
            float normalDot = GfDot(prevRes.normal, giPrimaryNormal);
            
            if (depthDiff < 0.1f * giPrimaryDepth && normalDot > 0.8f && prevRes.M > 0) {
                // Evaluate prev sample at current X1.
                // We need bsdf(X1 -> prevRes.virtualLightPos). We use a diffuse approximation for GI spatial/temporal reuse to save time.
                GfVec3f dir = prevRes.virtualLightPos - giPrimaryPos;
                float distSq = GfDot(dir, dir);
                if (distSq > 1e-4f) {
                    float dist = std::sqrt(distSq);
                    dir /= dist;
                    float nDotL = std::max(0.0f, GfDot(giPrimaryNormal, dir));
                    if (nDotL > 0.0f) {
                        float lumPrev = 0.2126f * prevRes.virtualLightRadiance[0] + 0.7152f * prevRes.virtualLightRadiance[1] + 0.0722f * prevRes.virtualLightRadiance[2];
                        float p_hat_prev = lumPrev * nDotL / distSq;
                        
                        // Blind spatial/temporal reuse (no visibility check for temporal history to save time)
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
                            // Visibility check
                            GfVec3f shadowOrigin = giPrimaryPos + giPrimaryNormal * RAY_EPSILON(giPrimaryPos);
                            float t_vis = 1e30f;
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
                float lumFinal = 0.2126f * r.virtualLightRadiance[0] + 0.7152f * r.virtualLightRadiance[1] + 0.0722f * r.virtualLightRadiance[2];
                p_hat_final = lumFinal * nDotL / finalDistSq;
            }
        }
        
        r.W = (p_hat_final > 0.0f) ? (r.w_sum / (p_hat_final * r.M)) : 0.0f;
        
        if (p_hat_final > 0.0f) {
            SampledSpectrum giResRadiance = RGBToSpectrum(r.virtualLightRadiance, lambda);
            // Diffuse approximation for the primary hit
            SampledSpectrum diffAlbedo = RGBToSpectrum(giPrimaryAlbedo, lambda) * (1.0f / (float)M_PI);
            for(int i=0; i<SPECTRUM_SAMPLES; ++i) {
                totalRadiance[i] += giPrimaryThroughput[i] * diffAlbedo[i] * giResRadiance[i] * (p_hat_final * finalDistSq / std::max(1e-6f, GfDot(giPrimaryNormal, finalDir))) * r.W;
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
    """
    
    content = content.replace("return transmittance;\n}\n\nSampledSpectrum HdGeminiRenderer::_TraceRay(const GfVec3f& rayOrigin, const GfVec3f& rayDir, int depth, bool isInteractive, HdRenderThread* renderThread, uint32_t sampleIdx, uint32_t& qmcDim, uint32_t& rng, const SampledWavelengths& lambda, GfVec3f* outAlbedo, GfVec3f* outNormal, float* outDepth, float exposureMultiplier, int pixelX, int pixelY) const\n{\n    SampledSpectrum throughput(exposureMultiplier);\n    SampledSpectrum totalRadiance(0.0f);", 
        "return transmittance;\n}\n\nSampledSpectrum HdGeminiRenderer::_TraceRay(const GfVec3f& rayOrigin, const GfVec3f& rayDir, int depth, bool isInteractive, HdRenderThread* renderThread, uint32_t sampleIdx, uint32_t& qmcDim, uint32_t& rng, const SampledWavelengths& lambda, GfVec3f* outAlbedo, GfVec3f* outNormal, float* outDepth, float exposureMultiplier, int pixelX, int pixelY) const\n{\n    SampledSpectrum throughput(exposureMultiplier);\n    SampledSpectrum totalRadiance(0.0f);")
    
    content = content.replace("return totalRadiance;\n}\n\nvoid HdGeminiRenderer::Render(HdRenderThread *renderThread, HdGeminiRenderDelegate* delegate)", restir_logic + "\n    return totalRadiance;\n}\n\nvoid HdGeminiRenderer::Render(HdRenderThread *renderThread, HdGeminiRenderDelegate* delegate)")

    with open(filepath, 'w') as f:
        f.write(content)

patch_file("C:/Users/paolo/Desktop/code/hdGemini/renderer.cpp")
