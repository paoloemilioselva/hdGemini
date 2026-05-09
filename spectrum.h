#pragma once

#include <pxr/base/gf/vec3f.h>
#include <cmath>

PXR_NAMESPACE_USING_DIRECTIVE

#define SPECTRUM_SAMPLES 4
#define LAMBDA_MIN 360.0f
#define LAMBDA_MAX 830.0f

class SampledWavelengths {
public:
    float lambda[SPECTRUM_SAMPLES];

    SampledWavelengths() {
        for (int i = 0; i < SPECTRUM_SAMPLES; ++i) lambda[i] = 0.0f;
    }

    // u is a uniform random number in [0, 1)
    static SampledWavelengths SampleUniform(float u) {
        SampledWavelengths swl;
        swl.lambda[0] = LAMBDA_MIN + u * (LAMBDA_MAX - LAMBDA_MIN);
        for (int i = 1; i < SPECTRUM_SAMPLES; ++i) {
            swl.lambda[i] = swl.lambda[0] + (i * (LAMBDA_MAX - LAMBDA_MIN) / SPECTRUM_SAMPLES);
            if (swl.lambda[i] > LAMBDA_MAX) {
                swl.lambda[i] -= (LAMBDA_MAX - LAMBDA_MIN);
            }
        }
        return swl;
    }
};

class SampledSpectrum {
public:
    float values[SPECTRUM_SAMPLES];

    SampledSpectrum(float v = 0.0f) {
        for (int i = 0; i < SPECTRUM_SAMPLES; ++i) values[i] = v;
    }

    float& operator[](int i) { return values[i]; }
    const float& operator[](int i) const { return values[i]; }

    SampledSpectrum operator+(const SampledSpectrum& s) const {
        SampledSpectrum ret;
        for (int i = 0; i < SPECTRUM_SAMPLES; ++i) ret.values[i] = values[i] + s.values[i];
        return ret;
    }
    SampledSpectrum& operator+=(const SampledSpectrum& s) {
        for (int i = 0; i < SPECTRUM_SAMPLES; ++i) values[i] += s.values[i];
        return *this;
    }

    SampledSpectrum operator*(const SampledSpectrum& s) const {
        SampledSpectrum ret;
        for (int i = 0; i < SPECTRUM_SAMPLES; ++i) ret.values[i] = values[i] * s.values[i];
        return ret;
    }
    SampledSpectrum& operator*=(const SampledSpectrum& s) {
        for (int i = 0; i < SPECTRUM_SAMPLES; ++i) values[i] *= s.values[i];
        return *this;
    }
    
    SampledSpectrum operator*(float s) const {
        SampledSpectrum ret;
        for (int i = 0; i < SPECTRUM_SAMPLES; ++i) ret.values[i] = values[i] * s;
        return ret;
    }
    SampledSpectrum& operator*=(float s) {
        for (int i = 0; i < SPECTRUM_SAMPLES; ++i) values[i] *= s;
        return *this;
    }

    SampledSpectrum operator/(float s) const {
        SampledSpectrum ret;
        float inv = 1.0f / s;
        for (int i = 0; i < SPECTRUM_SAMPLES; ++i) ret.values[i] = values[i] * inv;
        return ret;
    }
    
    SampledSpectrum operator/(const SampledSpectrum& s) const {
        SampledSpectrum ret;
        for (int i = 0; i < SPECTRUM_SAMPLES; ++i) ret.values[i] = values[i] / (s.values[i] + 1e-8f);
        return ret;
    }

    bool IsBlack() const {
        for (int i = 0; i < SPECTRUM_SAMPLES; ++i) {
            if (values[i] > 0.0f) return false;
        }
        return true;
    }
    
    float Average() const {
        float sum = 0.0f;
        for (int i = 0; i < SPECTRUM_SAMPLES; ++i) sum += values[i];
        return sum / SPECTRUM_SAMPLES;
    }
    
    float Max() const {
        float m = values[0];
        for (int i = 1; i < SPECTRUM_SAMPLES; ++i) m = std::max(m, values[i]);
        return m;
    }
};

// Analytical CIE 1931 matching functions approximation
GfVec3f SpectrumToRGB(const SampledSpectrum& s, const SampledWavelengths& lambda);
SampledSpectrum RGBToSpectrum(const GfVec3f& rgb, const SampledWavelengths& lambda);
