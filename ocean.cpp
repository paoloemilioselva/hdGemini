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

float HdGeminiOcean::Phillips(float kx, float kz) const {
    float k_length2 = kx * kx + kz * kz;
    if (k_length2 < 1e-6f) return 0.0f;

    float k_length = std::sqrt(k_length2);
    float k_length4 = k_length2 * k_length2;

    GfVec2f windDir = _params.windDirection.GetLength() > 1e-5f ? _params.windDirection.GetNormalized() : GfVec2f(1.0f, 0.0f);
    float k_dot_w = (kx * windDir[0] + kz * windDir[1]);
    float k_dot_w2 = k_dot_w * k_dot_w;

    float L = (_params.windSpeed * _params.windSpeed) / 9.81f;
    float L2 = L * L;

    float damping = 0.001f;
    float l2 = L2 * damping * damping;

    float blend = std::clamp((2.0f * (float)M_PI / k_length - 1.0f) / 4.0f, 0.0f, 1.0f);
    float amplitude = _params.amplitudeFine + blend * (_params.amplitude - _params.amplitudeFine);

    return amplitude * std::exp(-1.0f / (k_length2 * L2)) / k_length4 * k_dot_w2 * std::exp(-k_length2 * l2);
}

void HdGeminiOcean::ComputeH0() {
    int N = _params.fftResolution;
    _h0.resize(N * N);
    _h0_minus.resize(N * N);
    std::mt19937 gen(12345);
    std::normal_distribution<float> dist(0.0f, 1.0f);

    for (int z = 0; z < N; ++z) {
        for (int x = 0; x < N; ++x) {
            float kx = (2.0f * M_PI * x) / _params.size;
            float kz = (2.0f * M_PI * z) / _params.size;
            
            float nx = x - N / 2.0f;
            float nz = z - N / 2.0f;
            
            kx = (2.0f * M_PI * nx) / _params.size;
            kz = (2.0f * M_PI * nz) / _params.size;

            float P = Phillips(kx, kz);
            float sqrt_P = std::sqrt(P / 2.0f);

            float p_minus = Phillips(-kx, -kz);
            float sqrt_P_minus = std::sqrt(p_minus / 2.0f);

            std::complex<float> h0(dist(gen) * sqrt_P, dist(gen) * sqrt_P);
            std::complex<float> h0_minus(dist(gen) * sqrt_P_minus, dist(gen) * sqrt_P_minus);

            _h0[z * N + x] = h0;
            _h0_minus[z * N + x] = std::conj(h0_minus);
        }
    }
}

void HdGeminiOcean::Init(const HdGeminiOceanParams& params) {
    _params = params;
    int N = _params.fftResolution;

    ComputeH0();

    _h_kt_dz.resize(N * N);
    _h_kt_dx.resize(N * N);
    _h_kt_dy.resize(N * N);
    _displacementMap.resize(N * N);
    _normalMap.resize(N * N);

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
    int N = _params.fftResolution;
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

    int N = _params.fftResolution;

    tbb::parallel_for(tbb::blocked_range2d<int>(0, N, 0, N), [&](const tbb::blocked_range2d<int>& r) {
        for (int z = r.rows().begin(); z != r.rows().end(); ++z) {
            for (int x = r.cols().begin(); x != r.cols().end(); ++x) {
                float nx = x - N / 2.0f;
                float nz = z - N / 2.0f;
                float kx = (2.0f * M_PI * nx) / _params.size;
                float kz = (2.0f * M_PI * nz) / _params.size;

                float k_length = std::sqrt(kx * kx + kz * kz);
                float omega = std::sqrt(9.81f * k_length);
                
                int idx = z * N + x;
                std::complex<float> h0 = _h0[idx];
                std::complex<float> h0_minus = _h0_minus[idx];

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
                
                float dx = _h_kt_dx[idx].real() * sign * _params.choppiness;
                float dy = _h_kt_dz[idx].real() * sign; // Y is up!
                float dz = _h_kt_dy[idx].real() * sign * _params.choppiness;

                _displacementMap[idx] = GfVec3f(dx, dy, dz);
            }
        }
    });

    // Compute normals using finite differences on the fully displaced grid (Jacobian included)
    tbb::parallel_for(tbb::blocked_range2d<int>(0, N, 0, N), [&](const tbb::blocked_range2d<int>& r) {
        for (int z = r.rows().begin(); z != r.rows().end(); ++z) {
            for (int x = r.cols().begin(); x != r.cols().end(); ++x) {
                int idx = z * N + x;
                int x0 = (x - 1 + N) % N;
                int x1 = (x + 1) % N;
                int z0 = (z - 1 + N) % N;
                int z1 = (z + 1) % N;
                
                GfVec3f p00 = GfVec3f((x0 - x) * _params.size / N, 0, (z - z) * _params.size / N) + _displacementMap[z * N + x0];
                GfVec3f p10 = GfVec3f((x1 - x) * _params.size / N, 0, (z - z) * _params.size / N) + _displacementMap[z * N + x1];
                GfVec3f p01 = GfVec3f((x - x) * _params.size / N, 0, (z0 - z) * _params.size / N) + _displacementMap[z0 * N + x];
                GfVec3f p11 = GfVec3f((x - x) * _params.size / N, 0, (z1 - z) * _params.size / N) + _displacementMap[z1 * N + x];
                
                GfVec3f tx = p10 - p00;
                GfVec3f tz = p11 - p01;
                
                _normalMap[idx] = GfCross(tz, tx).GetNormalized();
            }
        }
    });

}

GfVec3f HdGeminiOcean::GetDisplacedPosition(const GfVec3f& basePos) const {
    if (!_initialized) return basePos;

    float u = basePos[0] / _params.size + 0.5f;
    float v = basePos[2] / _params.size + 0.5f;

    if (!_params.repeat && (u < 0.0f || u > 1.0f || v < 0.0f || v > 1.0f)) {
        return basePos;
    }

    u = u - std::floor(u);
    v = v - std::floor(v);

    int N = _params.fftResolution;
    float x = u * N;
    float z = v * N;
    
    int x0 = (int)std::floor(x) % N;
    int z0 = (int)std::floor(z) % N;
    if (x0 < 0) x0 += N;
    if (z0 < 0) z0 += N;
    
    int x1 = (x0 + 1) % N;
    int z1 = (z0 + 1) % N;

    float fx = x - std::floor(x);
    float fz = z - std::floor(z);

    GfVec3f d00 = _displacementMap[z0 * N + x0];
    GfVec3f d10 = _displacementMap[z0 * N + x1];
    GfVec3f d01 = _displacementMap[z1 * N + x0];
    GfVec3f d11 = _displacementMap[z1 * N + x1];

    GfVec3f d0 = d00 * (1.0f - fx) + d10 * fx;
    GfVec3f d1 = d01 * (1.0f - fx) + d11 * fx;

    return basePos + (d0 * (1.0f - fz) + d1 * fz);
}

GfVec3f HdGeminiOcean::GetNormal(const GfVec3f& basePos) const {
    if (!_initialized) return GfVec3f(0, 1, 0);

    float u = basePos[0] / _params.size + 0.5f;
    float v = basePos[2] / _params.size + 0.5f;

    if (!_params.repeat && (u < 0.0f || u > 1.0f || v < 0.0f || v > 1.0f)) {
        return GfVec3f(0, 1, 0);
    }

    u = u - std::floor(u);
    v = v - std::floor(v);

    int N = _params.fftResolution;
    float x = u * N;
    float z = v * N;
    
    int x0 = (int)std::floor(x) % N;
    int z0 = (int)std::floor(z) % N;
    if (x0 < 0) x0 += N;
    if (z0 < 0) z0 += N;
    
    int x1 = (x0 + 1) % N;
    int z1 = (z0 + 1) % N;

    float fx = x - std::floor(x);
    float fz = z - std::floor(z);

    GfVec3f n00 = _normalMap[z0 * N + x0];
    GfVec3f n10 = _normalMap[z0 * N + x1];
    GfVec3f n01 = _normalMap[z1 * N + x0];
    GfVec3f n11 = _normalMap[z1 * N + x1];

    GfVec3f n0 = n00 * (1.0f - fx) + n10 * fx;
    GfVec3f n1 = n01 * (1.0f - fx) + n11 * fx;

    return (n0 * (1.0f - fz) + n1 * fz).GetNormalized();
}
