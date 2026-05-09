import re

with open('renderer.cpp', 'r') as f:
    code = f.read()

# Task 1: Update Sampling Methods
code = code.replace(
    'GfVec3f HdGeminiRenderer::_SampleEnvironment(const GfVec3f& rayDir) const',
    'SampledSpectrum HdGeminiRenderer::_SampleEnvironment(const GfVec3f& rayDir, const SampledWavelengths& lambda) const'
)
code = code.replace(
    'return GfVec3f(0.0f);',
    'return RGBToSpectrum(GfVec3f(0.0f), lambda);'
)
code = code.replace(
    'return GfCompMult(color, light->GetColor()) * light->GetIntensity();',
    'return RGBToSpectrum(GfCompMult(color, light->GetColor()) * light->GetIntensity(), lambda);'
)
code = code.replace(
    'return color;\n}',
    'return RGBToSpectrum(color, lambda);\n}'
)

code = code.replace(
    'GfVec3f HdGeminiRenderer::_SampleTexture(const SdfAssetPath& path, const GfVec2f& uv, bool forceLinear) const',
    'SampledSpectrum HdGeminiRenderer::_SampleTexture(const SdfAssetPath& path, const GfVec2f& uv, const SampledWavelengths& lambda, bool forceLinear) const'
)
code = code.replace(
    'return GfVec3f(1.0f);',
    'return RGBToSpectrum(GfVec3f(1.0f), lambda);'
)
code = code.replace(
    'return _SampleTextureData(data, uv);',
    'return _SampleTextureData(data, uv, lambda);'
)
code = code.replace(
    'return _SampleTextureData(_textureCache[cacheKey], uv);',
    'return _SampleTextureData(_textureCache[cacheKey], uv, lambda);'
)

code = code.replace(
    'GfVec3f HdGeminiRenderer::_SampleTextureData(const TextureData& data, const GfVec2f& uv) const',
    'SampledSpectrum HdGeminiRenderer::_SampleTextureData(const TextureData& data, const GfVec2f& uv, const SampledWavelengths& lambda) const'
)
code = code.replace(
    'return (p00 * (1-fx) + p10 * fx) * (1-fy) + (p01 * (1-fx) + p11 * fx) * fy;',
    'return RGBToSpectrum((p00 * (1-fx) + p10 * fx) * (1-fy) + (p01 * (1-fx) + p11 * fx) * fy, lambda);'
)

# Task 2: _TraceRay signature and initial variables
code = code.replace(
    'GfVec3f HdGeminiRenderer::_TraceRay(const GfVec3f& rayOrigin, const GfVec3f& rayDir, int depth, bool isInteractive, HdRenderThread* renderThread, uint32_t& rng, GfVec3f* outAlbedo, GfVec3f* outNormal) const',
    'SampledSpectrum HdGeminiRenderer::_TraceRay(const GfVec3f& rayOrigin, const GfVec3f& rayDir, int depth, bool isInteractive, HdRenderThread* renderThread, uint32_t& rng, const SampledWavelengths& lambda, GfVec3f* outAlbedo, GfVec3f* outNormal) const'
)

code = code.replace(
    'GfVec3f throughput(1.0f);',
    'SampledSpectrum throughput(1.0f);'
)
code = code.replace(
    'GfVec3f totalRadiance(0.0f);',
    'SampledSpectrum totalRadiance(0.0f);'
)

# Replace env sampling call
code = code.replace(
    'GfVec3f env = _SampleEnvironment(currentRayDir);',
    'SampledSpectrum env = _SampleEnvironment(currentRayDir, lambda);'
)
code = code.replace(
    'if (bounce == 0 && outAlbedo) *outAlbedo = env;',
    'if (bounce == 0 && outAlbedo) *outAlbedo = SpectrumToRGB(env, lambda);'
)
code = code.replace(
    'totalRadiance += GfVec3f(0.0f);',
    'totalRadiance += SampledSpectrum(0.0f);'
)
code = code.replace(
    'totalRadiance += GfCompMult(throughput, env);',
    'totalRadiance += throughput * env;'
)

# Textures
code = code.replace(
    'hit.baseColor = GfCompMult(hit.baseColor, _SampleTexture(hit.diffuseTexture, hit.uv));',
    'hit.baseColor = GfCompMult(hit.baseColor, SpectrumToRGB(_SampleTexture(hit.diffuseTexture, hit.uv, lambda), lambda));'
)
code = code.replace(
    'hit.metallic = _SampleTexture(hit.metallicTexture, hit.uv, true)[0];',
    'hit.metallic = SpectrumToRGB(_SampleTexture(hit.metallicTexture, hit.uv, lambda, true), lambda)[0];'
)
code = code.replace(
    'hit.roughness = _SampleTexture(hit.roughnessTexture, hit.uv, true)[0];',
    'hit.roughness = SpectrumToRGB(_SampleTexture(hit.roughnessTexture, hit.uv, lambda, true), lambda)[0];'
)
code = code.replace(
    'GfVec3f nTex = _SampleTexture(hit.normalTexture, hit.uv, true);',
    'GfVec3f nTex = SpectrumToRGB(_SampleTexture(hit.normalTexture, hit.uv, lambda, true), lambda);'
)

# Beer's Law (around 707)
old_beers = '''        if (isInside && hit.transmissionDepth > 0.0f && !hit.thinWalled) {
            GfVec3f scatter = hit.transmissionScatter;
            if (scatter[0] <= 0 && scatter[1] <= 0 && scatter[2] <= 0) scatter = hit.transmissionColor;
            GfVec3f sigma_a(
                -std::log(std::clamp(scatter[0], 1e-5f, 1.0f)) / hit.transmissionDepth,
                -std::log(std::clamp(scatter[1], 1e-5f, 1.0f)) / hit.transmissionDepth,
                -std::log(std::clamp(scatter[2], 1e-5f, 1.0f)) / hit.transmissionDepth
            );
            throughput[0] *= std::exp(-sigma_a[0] * hit.t);
            throughput[1] *= std::exp(-sigma_a[1] * hit.t);
            throughput[2] *= std::exp(-sigma_a[2] * hit.t);
        }'''
new_beers = '''        if (isInside && hit.transmissionDepth > 0.0f && !hit.thinWalled) {
            SampledSpectrum transSpec = RGBToSpectrum(hit.transmissionColor, lambda);
            SampledSpectrum sigma_a;
            for(int i=0; i<SPECTRUM_SAMPLES; ++i) {
                sigma_a[i] = -std::log(std::max(transSpec[i], 1e-4f)) / hit.transmissionDepth;
                throughput[i] *= std::exp(-sigma_a[i] * hit.t);
            }
        }'''
code = code.replace(old_beers, new_beers)

# Emission
code = code.replace(
    'totalRadiance += GfCompMult(throughput, hit.emission);',
    'totalRadiance += throughput * RGBToSpectrum(hit.emission, lambda);'
)

# MIS Light sampling
code = code.replace(
    '''            if (lightDist > 0 && (lColor[0] > 0 || lColor[1] > 0 || lColor[2] > 0)) {
                float nDotL = std::max(0.0f, GfDot(shadingNormal, lDir));
                if (nDotL > 0) {
                    HitRecord shadowHit;
                    shadowHit.t = lightDist - 1e-3f;
                    GfVec3f shadowOrigin = hitPos + shadingNormal * 1e-4f;
                    if (!this->_IntersectTLAS(shadowOrigin, lDir, shadowHit, renderThread)) {
                        GfVec3f finalDiffuse = hit.baseColor * (1.0f - hit.subsurface) + hit.subsurfaceColor * hit.subsurface;
                        GfVec3f diffuseWeight = finalDiffuse * (1.0f - hit.metallic) * (1.0f - hit.transmission);
                        GfVec3f bsdf = diffuseWeight / (float)M_PI;
                        totalRadiance += GfCompMult(throughput, GfCompMult(bsdf, lColor)) * (nDotL / (lightPdf + 1e-6f));
                    }
                }
            }''',
    '''            if (lightDist > 0 && (lColor[0] > 0 || lColor[1] > 0 || lColor[2] > 0)) {
                float nDotL = std::max(0.0f, GfDot(shadingNormal, lDir));
                if (nDotL > 0) {
                    HitRecord shadowHit;
                    shadowHit.t = lightDist - 1e-3f;
                    GfVec3f shadowOrigin = hitPos + shadingNormal * 1e-4f;
                    if (!this->_IntersectTLAS(shadowOrigin, lDir, shadowHit, renderThread)) {
                        SampledSpectrum specLColor = RGBToSpectrum(lColor, lambda);
                        GfVec3f finalDiffuse = hit.baseColor * (1.0f - hit.subsurface) + hit.subsurfaceColor * hit.subsurface;
                        GfVec3f diffuseWeight = finalDiffuse * (1.0f - hit.metallic) * (1.0f - hit.transmission);
                        SampledSpectrum bsdf = RGBToSpectrum(diffuseWeight / (float)M_PI, lambda);
                        totalRadiance += throughput * bsdf * specLColor * (nDotL / (lightPdf + 1e-6f));
                    }
                }
            }'''
)

# Coat layer
code = code.replace(
    'throughput = GfCompMult(throughput, hit.coatColor);',
    'throughput = throughput * RGBToSpectrum(hit.coatColor, lambda);'
)

# Sheen layer
code = code.replace(
    'throughput = GfCompMult(throughput, hit.sheenColor);',
    'throughput = throughput * RGBToSpectrum(hit.sheenColor, lambda);'
)

# Reflection layer
code = code.replace(
    'throughput = GfCompMult(throughput, reflTint);',
    'throughput = throughput * RGBToSpectrum(reflTint, lambda);'
)

# Transmission layer
code = code.replace(
    'throughput = GfCompMult(throughput, hit.transmissionColor);',
    'throughput = throughput * RGBToSpectrum(hit.transmissionColor, lambda);'
)

# Diffuse layer
code = code.replace(
    'throughput = GfCompMult(throughput, finalDiffuse);',
    'throughput = throughput * RGBToSpectrum(finalDiffuse, lambda);'
)

# Russian Roulette
code = code.replace(
    'float p = std::max(throughput[0], std::max(throughput[1], throughput[2]));',
    'float p = throughput.Max();'
)

# Task 3: _RenderTiles
code = code.replace(
    '''                    GfVec3f albedo(0.0f), normal(0.0f);
                    GfVec3f hitColor = _TraceRay(rayOriginWorld, rayDirWorld, 0, isInteractive, renderThread, rng, &albedo, &normal);''',
    '''                    GfVec3f albedo(0.0f), normal(0.0f);
                    float u_lambda = RandomFloat(rng);
                    SampledWavelengths lambda = SampledWavelengths::SampleUniform(u_lambda);
                    SampledSpectrum hitSpectrum = _TraceRay(rayOriginWorld, rayDirWorld, 0, isInteractive, renderThread, rng, lambda, &albedo, &normal);
                    GfVec3f hitColor = SpectrumToRGB(hitSpectrum, lambda);'''
)

with open('renderer.cpp', 'w') as f:
    f.write(code)

print("Refactoring script executed.")