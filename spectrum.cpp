#include "spectrum.h"

// Wyman 2013 analytical approximation to CIE 1931 matching functions
static float xFit_1931(float wave) {
    float t1 = (wave - 442.0f) * ((wave < 442.0f) ? 0.0624f : 0.0374f);
    float t2 = (wave - 599.8f) * ((wave < 599.8f) ? 0.0264f : 0.0323f);
    float t3 = (wave - 501.1f) * ((wave < 501.1f) ? 0.0490f : 0.0382f);
    return 0.362f * std::exp(-0.5f * t1 * t1) +
           1.056f * std::exp(-0.5f * t2 * t2) -
           0.065f * std::exp(-0.5f * t3 * t3);
}

static float yFit_1931(float wave) {
    float t1 = (wave - 568.8f) * ((wave < 568.8f) ? 0.0213f : 0.0247f);
    float t2 = (wave - 530.9f) * ((wave < 530.9f) ? 0.0613f : 0.0322f);
    return 0.821f * std::exp(-0.5f * t1 * t1) +
           0.286f * std::exp(-0.5f * t2 * t2);
}

static float zFit_1931(float wave) {
    float t1 = (wave - 437.0f) * ((wave < 437.0f) ? 0.0845f : 0.0278f);
    float t2 = (wave - 459.0f) * ((wave < 459.0f) ? 0.0385f : 0.0725f);
    return 1.217f * std::exp(-0.5f * t1 * t1) +
           0.681f * std::exp(-0.5f * t2 * t2);
}

GfVec3f SpectrumToRGB(const SampledSpectrum& s, const SampledWavelengths& lambda) {
    float X = 0.0f;
    float Y = 0.0f;
    float Z = 0.0f;
    
    // Monte Carlo integration over the spectrum
    // The probability density of our uniform sampling is 1 / (LAMBDA_MAX - LAMBDA_MIN)
    // We multiply by (LAMBDA_MAX - LAMBDA_MIN) / SPECTRUM_SAMPLES to integrate.
    float weight = (LAMBDA_MAX - LAMBDA_MIN) / SPECTRUM_SAMPLES;
    // We also normalize by dividing by the integral of the y matching function (~106.85) to get relative luminance
    weight /= 106.856f; 

    for (int i = 0; i < SPECTRUM_SAMPLES; ++i) {
        float l = lambda.lambda[i];
        float val = s.values[i];
        X += val * xFit_1931(l);
        Y += val * yFit_1931(l);
        Z += val * zFit_1931(l);
    }
    
    X *= weight;
    Y *= weight;
    Z *= weight;

    // XYZ to Linear sRGB
    float r =  3.2404542f * X - 1.5371385f * Y - 0.4985314f * Z;
    float g = -0.9692660f * X + 1.8760108f * Y + 0.0415560f * Z;
    float b =  0.0556434f * X - 0.2040259f * Y + 1.0572252f * Z;

    // White balance to Illuminant E (so that a flat spectrum maps to RGB 1,1,1)
    r /= 1.198185f;
    g /= 0.950366f;
    b /= 0.907827f;

    return GfVec3f(std::max(0.0f, r), std::max(0.0f, g), std::max(0.0f, b));
}

// Simple Gaussian RGB to Spectrum Uplifting
static float Gaussian(float x, float center, float sigma) {
    float t = (x - center) / sigma;
    return std::exp(-0.5f * t * t);
}

SampledSpectrum RGBToSpectrum(const GfVec3f& rgb, const SampledWavelengths& lambda) {
    SampledSpectrum s;
    
    // White point mapping - if R=G=B, spectrum is flat
    float minRGB = std::min(rgb[0], std::min(rgb[1], rgb[2]));
    float white = minRGB;
    
    GfVec3f rem(rgb[0] - white, rgb[1] - white, rgb[2] - white);
    
    // Gaussian peaks for R, G, B
    const float R_peak = 615.0f;
    const float G_peak = 540.0f;
    const float B_peak = 460.0f;
    const float sigma = 20.0f;

    for (int i = 0; i < SPECTRUM_SAMPLES; ++i) {
        float l = lambda.lambda[i];
        
        float val = white; // Flat spectrum for achromatic part
        
        // Add peaks for chromatic part with optimized weights for round-tripping
        val += rem[0] * Gaussian(l, R_peak, sigma) * 1.295f; 
        val += rem[1] * Gaussian(l, G_peak, sigma) * 1.556f;
        val += rem[2] * Gaussian(l, B_peak, sigma) * 1.382f;
        
        s.values[i] = std::max(0.0f, val);
    }
    
    return s;
}
