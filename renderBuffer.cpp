#include "renderBuffer.h"
#include <pxr/base/gf/vec3i.h>
#include <pxr/base/gf/vec4f.h>
#include <pxr/base/gf/vec2i.h>
#include <pxr/base/gf/half.h>
#include <cstring>
#include <algorithm>

PXR_NAMESPACE_USING_DIRECTIVE

HdGeminiRenderBuffer::HdGeminiRenderBuffer(SdfPath const& id)
    : HdRenderBuffer(id)
    , _width(0), _height(0), _format(HdFormatInvalid), _multiSampled(false)
{
    _converged.store(false);
}

bool
HdGeminiRenderBuffer::Allocate(GfVec3i const& dimensions,
                               HdFormat format,
                               bool multiSampled)
{
    std::lock_guard<std::mutex> lock(_bufferMutex);
    _width = dimensions[0];
    _height = dimensions[1];
    _format = format;
    _multiSampled = multiSampled;
    size_t numPixels = _width * _height;
    size_t formatSize = HdDataSizeOfFormat(format);
    _buffer.resize(numPixels * formatSize);
    _renderBuffer.resize(numPixels * formatSize);
    _accumBuffer.assign(numPixels * 4, 0.0f);
    _sumSquaredBuffer.assign(numPixels * 4, 0.0f);
    _sampleCount.assign(numPixels, 0);
    return true;
}

void
HdGeminiRenderBuffer::_Deallocate()
{
    std::lock_guard<std::mutex> lock(_bufferMutex);
    _width = 0;
    _height = 0;
    _format = HdFormatInvalid;
    _buffer.clear();
    _renderBuffer.clear();
    _accumBuffer.clear();
    _sampleCount.clear();
}

template<typename T>
static void _WriteOutput(HdFormat format, uint8_t *dst,
                         size_t valueComponents, T const* value)
{
    HdFormat componentFormat = HdGetComponentFormat(format);
    size_t componentCount = HdGetComponentCount(format);

    for (size_t c = 0; c < componentCount; ++c) {
        if (componentFormat == HdFormatInt32) {
            ((int32_t*)dst)[c] = (c < valueComponents) ? (int32_t)(value[c]) : 0;
        } else if (componentFormat == HdFormatFloat16) {
            ((uint16_t*)dst)[c] = (c < valueComponents) ? GfHalf(value[c]).bits() : 0;
        } else if (componentFormat == HdFormatFloat32) {
            ((float*)dst)[c] = (c < valueComponents) ? (float)(value[c]) : 0.0f;
        } else if (componentFormat == HdFormatUNorm8) {
            ((uint8_t*)dst)[c] = (c < valueComponents) ? (uint8_t)std::clamp(value[c] * 255.0f, 0.0f, 255.0f) : 0;
        } else if (componentFormat == HdFormatSNorm8) {
            ((int8_t*)dst)[c] = (c < valueComponents) ? (int8_t)std::clamp(value[c] * 127.0f, -128.0f, 127.0f) : 0;
        }
    }
}

void
HdGeminiRenderBuffer::Write(GfVec3i const& pixel, size_t numComponents, float const* value)
{
    std::lock_guard<std::mutex> lock(_bufferMutex);
    if (pixel[0] < 0 || pixel[0] >= (int)_width || 
        pixel[1] < 0 || pixel[1] >= (int)_height) return;
    size_t idx = pixel[1] * _width + pixel[0];
    size_t formatSize = HdDataSizeOfFormat(_format);
    if ((idx + 1) * formatSize > _renderBuffer.size()) return;
    uint8_t *dst = &_renderBuffer[idx * formatSize];
    _WriteOutput(_format, dst, numComponents, value);
}

void
HdGeminiRenderBuffer::Write(GfVec3i const& pixel, size_t numComponents, int const* value)
{
    std::lock_guard<std::mutex> lock(_bufferMutex);
    if (pixel[0] < 0 || pixel[0] >= (int)_width || 
        pixel[1] < 0 || pixel[1] >= (int)_height) return;
    size_t idx = pixel[1] * _width + pixel[0];
    size_t formatSize = HdDataSizeOfFormat(_format);
    if ((idx + 1) * formatSize > _renderBuffer.size()) return;
    uint8_t *dst = &_renderBuffer[idx * formatSize];
    _WriteOutput(_format, dst, numComponents, value);
}

void
HdGeminiRenderBuffer::GetFloatBuffer(std::vector<float>& outFloats) const
{
    std::lock_guard<std::mutex> lock((const_cast<HdGeminiRenderBuffer*>(this))->_bufferMutex);
    outFloats.resize(_width * _height * 3);
    for (size_t i = 0; i < _width * _height; ++i) {
        float invCount = (_sampleCount[i] > 0) ? (1.0f / (float)_sampleCount[i]) : 1.0f;
        outFloats[i * 3 + 0] = _accumBuffer[i * 4 + 0] * invCount;
        outFloats[i * 3 + 1] = _accumBuffer[i * 4 + 1] * invCount;
        outFloats[i * 3 + 2] = _accumBuffer[i * 4 + 2] * invCount;
    }
}

void
HdGeminiRenderBuffer::WriteSample(GfVec3i const& pixel, GfVec4f const& color)
{
    std::lock_guard<std::mutex> lock(_bufferMutex);
    if (pixel[0] < 0 || pixel[0] >= (int)_width ||
        pixel[1] < 0 || pixel[1] >= (int)_height) {
        return;
    }

    size_t idx = pixel[1] * _width + pixel[0];
    WriteSampleLockFree(idx, color);
}

void
HdGeminiRenderBuffer::WriteSampleLockFree(size_t idx, GfVec4f const& color)
{
    if (idx * 4 + 3 >= _accumBuffer.size()) return;

    _accumBuffer[idx * 4 + 0] += color[0];
    _accumBuffer[idx * 4 + 1] += color[1];
    _accumBuffer[idx * 4 + 2] += color[2];
    _accumBuffer[idx * 4 + 3] += color[3];
    
    _sumSquaredBuffer[idx * 4 + 0] += color[0] * color[0];
    _sumSquaredBuffer[idx * 4 + 1] += color[1] * color[1];
    _sumSquaredBuffer[idx * 4 + 2] += color[2] * color[2];
    _sumSquaredBuffer[idx * 4 + 3] += color[3] * color[3];
    
    _sampleCount[idx]++;

    float invCount = 1.0f / (float)_sampleCount[idx];
    float avg[4] = {
        _accumBuffer[idx * 4 + 0] * invCount,
        _accumBuffer[idx * 4 + 1] * invCount,
        _accumBuffer[idx * 4 + 2] * invCount,
        _accumBuffer[idx * 4 + 3] * invCount
    };

    size_t formatSize = HdDataSizeOfFormat(_format);
    uint8_t *dst = &_renderBuffer[idx * formatSize];
    _WriteOutput(_format, dst, 4, avg);
}

void
HdGeminiRenderBuffer::Resolve()
{
    // Copy from background render buffer to front display buffer
    if (_buffer.size() == _renderBuffer.size() && !_buffer.empty()) {
        std::memcpy(_buffer.data(), _renderBuffer.data(), _buffer.size());
    }
}

void
HdGeminiRenderBuffer::ResolveBucket(unsigned int startX, unsigned int startY, unsigned int endX, unsigned int endY)
{
    if (_buffer.empty() || _buffer.size() != _renderBuffer.size()) return;
    
    endX = std::min(endX, _width);
    endY = std::min(endY, _height);
    if (startX >= endX || startY >= endY) return;

    size_t formatSize = HdDataSizeOfFormat(_format);
    size_t rowBytes = (endX - startX) * formatSize;

    for (unsigned int y = startY; y < endY; ++y) {
        size_t idx = (y * _width + startX) * formatSize;
        std::memcpy(_buffer.data() + idx, _renderBuffer.data() + idx, rowBytes);
    }
}

void
HdGeminiRenderBuffer::Clear(size_t numComponents, float const* value)
{
    std::lock_guard<std::mutex> lock(_bufferMutex);
    _version++;
    // Do not visually clear the buffer to prevent black flashes, just reset the accumulators
    _accumBuffer.assign(_width * _height * 4, 0.0f);
    _sumSquaredBuffer.assign(_width * _height * 4, 0.0f);
    _sampleCount.assign(_width * _height, 0);
}

void
HdGeminiRenderBuffer::Clear(size_t numComponents, int const* value)
{
    std::lock_guard<std::mutex> lock(_bufferMutex);
    _version++;
    // Do not visually clear the buffer to prevent black flashes, just reset the accumulators
    _accumBuffer.assign(_width * _height * 4, 0.0f);
    _sumSquaredBuffer.assign(_width * _height * 4, 0.0f);
    _sampleCount.assign(_width * _height, 0);
}

float
HdGeminiRenderBuffer::GetPixelVariance(GfVec3i const& pixel) const
{
    if (pixel[0] < 0 || pixel[0] >= (int)_width ||
        pixel[1] < 0 || pixel[1] >= (int)_height) {
        return 0.0f;
    }
    size_t idx = pixel[1] * _width + pixel[0];
    int n = _sampleCount[idx];
    if (n < 2) return 1e5f; // High variance for very few samples
    
    // Variance = (E[X^2] - E[X]^2)
    float invN = 1.0f / (float)n;
    float ex2 = _sumSquaredBuffer[idx * 4 + 0] * invN + 
                _sumSquaredBuffer[idx * 4 + 1] * invN + 
                _sumSquaredBuffer[idx * 4 + 2] * invN;
    ex2 /= 3.0f;
    
    float ex0 = _accumBuffer[idx * 4 + 0] * invN;
    float ex1 = _accumBuffer[idx * 4 + 1] * invN;
    float ex2_mean = _accumBuffer[idx * 4 + 2] * invN;
    float ex_sq = (ex0 * ex0 + ex1 * ex1 + ex2_mean * ex2_mean) / 3.0f;
    
    float variance = ex2 - ex_sq;
    return std::max(0.0f, variance) * invN; // Standard error of the mean
}

int
HdGeminiRenderBuffer::GetPixelSampleCount(GfVec3i const& pixel) const
{
    if (pixel[0] < 0 || pixel[0] >= (int)_width ||
        pixel[1] < 0 || pixel[1] >= (int)_height) {
        return 0;
    }
    size_t idx = pixel[1] * _width + pixel[0];
    return _sampleCount[idx];
}
