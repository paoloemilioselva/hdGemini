#ifndef HD_GEMINI_QMC_H
#define HD_GEMINI_QMC_H

#include <cstdint>

namespace qmc {

// Radical inverse in base 2 (Van der Corput sequence)
inline float RadicalInverseBase2(uint32_t bits) {
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10f; // / 0x100000000
}

// Fast hash function (PCG-style)
inline uint32_t Hash(uint32_t x) {
    x ^= x >> 16;
    x *= 0x7feb352d;
    x ^= x >> 15;
    x *= 0x846ca68b;
    x ^= x >> 16;
    return x;
}

// Get the d-th dimension for the i-th sample
inline float SampleDimension(uint32_t sampleIndex, uint32_t dimension, uint32_t scramble = 0) {
    if (dimension == 0) {
        return RadicalInverseBase2(sampleIndex ^ Hash(scramble));
    }
    
    // Halton sequence for dims 1-31
    static const uint32_t Primes[32] = {
        2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37, 41, 43, 47, 53,
        59, 61, 67, 71, 73, 79, 83, 89, 97, 101, 103, 107, 109, 113, 127, 131
    };
    
    if (dimension < 32) {
        uint32_t base = Primes[dimension];
        float f = 1.0f;
        float invBase = 1.0f / base;
        float result = 0.0f;
        uint32_t i = sampleIndex ^ Hash(scramble + dimension);
        while (i > 0) {
            f *= invBase;
            result += f * (i % base);
            i /= base;
        }
        return result;
    }
    
    // Fallback hash for high dimensions
    uint32_t h = Hash(sampleIndex ^ Hash(dimension) ^ scramble);
    return float(h) * 2.3283064365386963e-10f;
}

} // namespace qmc

#endif // HD_GEMINI_QMC_H
