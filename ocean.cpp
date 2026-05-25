#include "ocean.h"
#include <cmath>
#include <random>
#include <tbb/parallel_for.h>
#include <tbb/blocked_range.h>
#include <tbb/blocked_range2d.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

HdGeminiOcean::HdGeminiOcean() {}
HdGeminiOcean::~HdGeminiOcean() {}

float HdGeminiOcean::Phillips(float kx, float kz, float amplitude) const {
    float k_length2 = kx * kx + kz * kz;
    if (k_length2 < 1e-6f) return 0.0f;

    float k_length = std::sqrt(k_length2);
    float k_length4 = k_length2 * k_length2;

    GfVec2f windDir = _params.windDirection.GetLength() > 1e-5f ? _params.windDirection.GetNormalized() : GfVec2f(1.0f, 0.0f);
    GfVec2f k_hat(kx / k_length, kz / k_length);
    float k_dot_w = k_hat[0] * windDir[0] + k_hat[1] * windDir[1];
    float k_dot_w2 = k_dot_w * k_dot_w;

    float L = (_params.windSpeed * _params.windSpeed) / 9.81f;
    float L2 = L * L;

    float damping = 0.001f;
    float l2 = L2 * damping * damping;

    return amplitude * std::exp(-1.0f / (k_length2 * L2)) / k_length4 * k_dot_w2 * std::exp(-k_length2 * l2);
}

void HdGeminiOcean::ComputeH0() {
    int N = _params.gridSize;
    for (int c = 0; c < 3; ++c) {
        _h0[c].resize(N * N);
        _h0_minus[c].resize(N * N);
    }
    std::mt19937 gen(12345);
    std::normal_distribution<float> dist(0.0f, 1.0f);

    for (int c = 0; c < 3; ++c) {
        float currentSize = _params.size[c];
        float currentAmp = _params.amplitude[c];
        
        for (int z = 0; z < N; ++z) {
            for (int x = 0; x < N; ++x) {
                float nx = x - N / 2.0f;
                float nz = z - N / 2.0f;
                
                float kx = (2.0f * M_PI * nx) / currentSize;
                float kz = (2.0f * M_PI * nz) / currentSize;

                float P = Phillips(kx, kz, currentAmp);
                float sqrt_P = std::sqrt(P / 2.0f);

                float p_minus = Phillips(-kx, -kz, currentAmp);
                float sqrt_P_minus = std::sqrt(p_minus / 2.0f);

                std::complex<float> h0(dist(gen) * sqrt_P, dist(gen) * sqrt_P);
                std::complex<float> h0_minus(dist(gen) * sqrt_P_minus, dist(gen) * sqrt_P_minus);

                _h0[c][z * N + x] = h0;
                _h0_minus[c][z * N + x] = std::conj(h0_minus);
            }
        }
    }
}

void HdGeminiOcean::Init(const HdGeminiOceanParams& params) {
    _params = params;
    int N = _params.gridSize;

    ComputeH0();

    _h_kt_dz.resize(N * N);
    _h_kt_dx.resize(N * N);
    _h_kt_dy.resize(N * N);
    for (int c = 0; c < 3; ++c) {
        _displacementMap[c].resize(N * N);
    }

    int log2N = (int)std::log2(N);
    _bitReversedIndices.resize(N);
    for (int i = 0; i < N; ++i) {
        int reversed = 0;
        for (int j = 0; j < log2N; ++j) {
            if ((i >> j) & 1) reversed |= (1 << (log2N - 1 - j));
        }
        _bitReversedIndices[i] = reversed;
    }

    _initialized = true;
    _lastTime = -1.0f;
}

void HdGeminiOcean::PerformFFT2D(std::vector<std::complex<float>>& data) const {
    int N = _params.gridSize;
    int log2N = (int)std::log2(N);

    // 1D FFT over rows
    tbb::parallel_for(0, N, [&](int y) {
        int offset = y * N;
        std::vector<std::complex<float>> row(N);
        for (int i = 0; i < N; ++i) {
            row[_bitReversedIndices[i]] = data[offset + i];
        }
        
        for (int s = 1; s <= log2N; ++s) {
            int m = 1 << s;
            float theta = -2.0f * M_PI / m;
            std::complex<float> wm(std::cos(theta), std::sin(theta));
            for (int k = 0; k < N; k += m) {
                std::complex<float> w(1.0f, 0.0f);
                for (int j = 0; j < m / 2; ++j) {
                    std::complex<float> t = w * row[k + j + m / 2];
                    std::complex<float> u = row[k + j];
                    row[k + j] = u + t;
                    row[k + j + m / 2] = u - t;
                    w *= wm;
                }
            }
        }
        for (int i = 0; i < N; ++i) data[offset + i] = row[i];
    });

    // 1D FFT over columns
    tbb::parallel_for(0, N, [&](int x) {
        std::vector<std::complex<float>> col(N);
        for (int i = 0; i < N; ++i) {
            col[_bitReversedIndices[i]] = data[i * N + x];
        }
        
        for (int s = 1; s <= log2N; ++s) {
            int m = 1 << s;
            float theta = -2.0f * M_PI / m;
            std::complex<float> wm(std::cos(theta), std::sin(theta));
            for (int k = 0; k < N; k += m) {
                std::complex<float> w(1.0f, 0.0f);
                for (int j = 0; j < m / 2; ++j) {
                    std::complex<float> t = w * col[k + j + m / 2];
                    std::complex<float> u = col[k + j];
                    col[k + j] = u + t;
                    col[k + j + m / 2] = u - t;
                    w *= wm;
                }
            }
        }
        for (int i = 0; i < N; ++i) data[i * N + x] = col[i];
    });
}

void HdGeminiOcean::Update(float time) {
    if (!_initialized || std::abs(time - _lastTime) < 1e-4f) return;
    _lastTime = time;

    int N = _params.gridSize;

    for (int c = 0; c < 3; ++c) {
        float currentSize = _params.size[c];
        float currentChoppy = _params.choppiness[c];

        tbb::parallel_for(tbb::blocked_range2d<int>(0, N, 0, N), [&](const tbb::blocked_range2d<int>& r) {
            for (int z = r.rows().begin(); z != r.rows().end(); ++z) {
                for (int x = r.cols().begin(); x != r.cols().end(); ++x) {
                    float nx = x - N / 2.0f;
                    float nz = z - N / 2.0f;
                    float kx = (2.0f * M_PI * nx) / currentSize;
                    float kz = (2.0f * M_PI * nz) / currentSize;

                    float k_length = std::sqrt(kx * kx + kz * kz);
                    float omega = std::sqrt(9.81f * k_length);
                    
                    int idx = z * N + x;
                    std::complex<float> h0 = _h0[c][idx];
                    std::complex<float> h0_minus = _h0_minus[c][idx];

                    float phase = omega * time;
                    std::complex<float> exp_iwt(std::cos(phase), std::sin(phase));
                    std::complex<float> exp_minus_iwt(std::cos(-phase), std::sin(-phase));

                    std::complex<float> h_kt = h0 * exp_iwt + h0_minus * exp_minus_iwt;

                    float kx_norm = (k_length > 1e-6f) ? kx / k_length : 0.0f;
                    float kz_norm = (k_length > 1e-6f) ? kz / k_length : 0.0f;

                    _h_kt_dz[idx] = h_kt;
                    _h_kt_dx[idx] = std::complex<float>(0.0f, -kx_norm) * h_kt;
                    _h_kt_dy[idx] = std::complex<float>(0.0f, -kz_norm) * h_kt;
                }
            }
        });

        PerformFFT2D(_h_kt_dz);
        PerformFFT2D(_h_kt_dx);
        PerformFFT2D(_h_kt_dy);

        float sign_flip[2] = { 1.0f, -1.0f };
        
        tbb::parallel_for(tbb::blocked_range2d<int>(0, N, 0, N), [&](const tbb::blocked_range2d<int>& r) {
            for (int z = r.rows().begin(); z != r.rows().end(); ++z) {
                for (int x = r.cols().begin(); x != r.cols().end(); ++x) {
                    int idx = z * N + x;
                    float sign = sign_flip[(x + z) & 1];
                    
                    float dx = _h_kt_dx[idx].real() * sign * currentChoppy;
                    float dy = _h_kt_dz[idx].real() * sign;
                    float dz = _h_kt_dy[idx].real() * sign * currentChoppy;

                    _displacementMap[c][idx] = GfVec3f(dx, dy, dz);
                }
            }
        });
    }
}

GfVec3f HdGeminiOcean::GetDisplacedPosition(const GfVec3f& basePos) const {
    if (!_initialized) return basePos;

    GfVec3f totalDisp(0.0f);
    int N = _params.gridSize;

    for (int c = 0; c < 3; ++c) {
        float size = _params.size[c];
        if (size <= 1e-5f) continue;
        
        float u = basePos[0] / size + 0.5f;
        float v = basePos[2] / size + 0.5f;

        if (!_params.repeat && (u < 0.0f || u > 1.0f || v < 0.0f || v > 1.0f)) {
            continue;
        }

        u = u - std::floor(u);
        v = v - std::floor(v);

        float x = u * N;
        float z = v * N;
        
        int x0 = (int)x;
        int z0 = (int)z;
        int x1 = (x0 + 1) % N;
        int z1 = (z0 + 1) % N;
        
        float fx = x - x0;
        float fz = z - z0;
        
        GfVec3f d00 = _displacementMap[c][z0 * N + x0];
        GfVec3f d10 = _displacementMap[c][z0 * N + x1];
        GfVec3f d01 = _displacementMap[c][z1 * N + x0];
        GfVec3f d11 = _displacementMap[c][z1 * N + x1];
        
        GfVec3f d0 = d00 * (1.0f - fx) + d10 * fx;
        GfVec3f d1 = d01 * (1.0f - fx) + d11 * fx;
        
        totalDisp += d0 * (1.0f - fz) + d1 * fz;
    }

    return basePos + totalDisp;
}

GfVec3f HdGeminiOcean::GetNormal(const GfVec3f& basePos) const {
    if (!_initialized) return GfVec3f(0, 1, 0);

    float delta = 0.1f;
    GfVec3f p0 = GetDisplacedPosition(basePos);
    GfVec3f px = GetDisplacedPosition(basePos + GfVec3f(delta, 0, 0));
    GfVec3f pz = GetDisplacedPosition(basePos + GfVec3f(0, 0, delta));

    GfVec3f tx = px - p0;
    GfVec3f tz = pz - p0;

    return GfCross(tz, tx).GetNormalized();
}

float HdGeminiOcean::GetFoam(const GfVec3f& basePos) const {
    if (!_initialized) return 0.0f;

    float delta = 0.1f;
    GfVec3f p0 = GetDisplacedPosition(basePos);
    GfVec3f px = GetDisplacedPosition(basePos + GfVec3f(delta, 0, 0));
    GfVec3f pz = GetDisplacedPosition(basePos + GfVec3f(0, 0, delta));

    float dDx_dx = (px[0] - p0[0]) / delta - 1.0f;
    float dDz_dx = (px[2] - p0[2]) / delta;
    float dDx_dz = (pz[0] - p0[0]) / delta;
    float dDz_dz = (pz[2] - p0[2]) / delta - 1.0f;

    float J = (1.0f + dDx_dx) * (1.0f + dDz_dz) - dDx_dz * dDz_dx;
    return std::max(0.0f, 1.0f - J) * _params.foamVisibility;
}
