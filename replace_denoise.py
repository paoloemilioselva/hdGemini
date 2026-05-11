import sys

with open("renderer.cpp", "r") as f:
    content = f.read()

start_idx = content.find("void\nHdGeminiRenderer::_Denoise()")
end_idx = content.find("void\nHdGeminiRenderer::_ApplyPostProcess()")

if start_idx == -1 or end_idx == -1:
    print("Could not find _Denoise")
    sys.exit(1)

new_denoise = """void
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

    // 1. Firefly Rejection on Hero (Structural noise)
    if (_enableFireflyFilter) {
        std::vector<float> clampedHero = heroOutput;
        for (int y = 0; y < (int)height; ++y) {
            for (int x = 0; x < (int)width; ++x) {
                size_t idx = (y * width + x) * 3;
                float r = clampedHero[idx];
                float g = clampedHero[idx+1];
                float b = clampedHero[idx+2];
                float lum = getLuminance(r, g, b);

                float maxNeighborLum = 0.0f;
                for (int dy = -1; dy <= 1; ++dy) {
                    for (int dx = -1; dx <= 1; ++dx) {
                        if (dx == 0 && dy == 0) continue;
                        int nx = std::clamp(x + dx, 0, (int)width - 1);
                        int ny = std::clamp(y + dy, 0, (int)height - 1);
                        size_t nIdx = (ny * width + nx) * 3;
                        maxNeighborLum = std::max(maxNeighborLum, getLuminance(clampedHero[nIdx], clampedHero[nIdx+1], clampedHero[nIdx+2]));
                    }
                }

                float threshold = maxNeighborLum * 4.0f + 0.5f;
                if (lum > threshold && lum > 0.0f) {
                    float scale = threshold / lum;
                    heroOutput[idx] = r * scale;
                    heroOutput[idx+1] = g * scale;
                    heroOutput[idx+2] = b * scale;
                }
            }
        }
    }

    // 2. Spectral Difference Blur (Directly blurs chromatic variance)
    if (_enableChromaticityBlur) {
        std::vector<float> blurredDiff = diffOutput;
        for (int y = 0; y < (int)height; ++y) {
            for (int x = 0; x < (int)width; ++x) {
                size_t idx = (y * width + x) * 3;
                float sumR = 0.0f, sumG = 0.0f, sumB = 0.0f, weightSum = 0.0f;
                for (int dy = -2; dy <= 2; ++dy) {
                    for (int dx = -2; dx <= 2; ++dx) {
                        int nx = std::clamp(x + dx, 0, (int)width - 1);
                        int ny = std::clamp(y + dy, 0, (int)height - 1);
                        size_t nIdx = (ny * width + nx) * 3;
                        float w = std::exp(-(dx*dx + dy*dy) / 2.0f);
                        sumR += diffOutput[nIdx+0] * w;
                        sumG += diffOutput[nIdx+1] * w;
                        sumB += diffOutput[nIdx+2] * w;
                        weightSum += w;
                    }
                }
                blurredDiff[idx+0] = sumR / weightSum;
                blurredDiff[idx+1] = sumG / weightSum;
                blurredDiff[idx+2] = sumB / weightSum;
            }
        }
        diffOutput = blurredDiff;
    }

    // 3. Recombine for OIDN
    std::vector<float> prefiltered(width * height * 3);
    for(size_t i=0; i<width*height; ++i) {
        prefiltered[i*3+0] = std::max(0.0f, heroOutput[i*3+0] + diffOutput[i*3+0]);
        prefiltered[i*3+1] = std::max(0.0f, heroOutput[i*3+1] + diffOutput[i*3+1]);
        prefiltered[i*3+2] = std::max(0.0f, heroOutput[i*3+2] + diffOutput[i*3+2]);
    }

    std::vector<float> output = prefiltered;

    if (_enableDenoiser) {
        try {
            oidn::DeviceRef device = oidn::newDevice(oidn::DeviceType::CPU);
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

"""
content = content[:start_idx] + new_denoise + content[end_idx:]

start_idx2 = content.find("void\nHdGeminiRenderer::ReapplyPostProcess()")
if start_idx2 != -1:
    new_reapply = """void
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

    _isConverged = true;
    for (auto const& binding : _aovBindings) {
        if (binding.renderBuffer) {
            static_cast<HdGeminiRenderBuffer*>(binding.renderBuffer)->SetConverged(true);
        }
    }
}"""
    content = content[:start_idx2] + new_reapply

with open("renderer.cpp", "w") as f:
    f.write(content)
print("done")