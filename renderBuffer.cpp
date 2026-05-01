#include "renderBuffer.h"
#include <pxr/base/gf/vec3i.h>
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
    _width = dimensions[0];
    _height = dimensions[1];
    _format = format;
    _multiSampled = multiSampled;
    size_t size = _width * _height * HdDataSizeOfFormat(format);
    _buffer.resize(size);
    _renderBuffer.resize(size);
    _accumBuffer.assign(_width * _height * 4, 0.0f);
    _sampleCount.assign(_width * _height, 0);
    return true;
}

void
HdGeminiRenderBuffer::_Deallocate()
{
    _width = 0;
    _height = 0;
    _format = HdFormatInvalid;
    _buffer.clear();
    _renderBuffer.clear();
    _accumBuffer.clear();
    _sampleCount.clear();
}

void
HdGeminiRenderBuffer::WriteSample(GfVec3i const& pixel, GfVec4f const& color)
{
    if (pixel[0] < 0 || pixel[0] >= (int)_width ||
        pixel[1] < 0 || pixel[1] >= (int)_height) {
        return;
    }

    size_t idx = pixel[1] * _width + pixel[0];
    _accumBuffer[idx * 4 + 0] += color[0];
    _accumBuffer[idx * 4 + 1] += color[1];
    _accumBuffer[idx * 4 + 2] += color[2];
    _accumBuffer[idx * 4 + 3] += color[3];
    _sampleCount[idx]++;

    float invCount = 1.0f / _sampleCount[idx];
    GfVec4f avgColor(
        _accumBuffer[idx * 4 + 0] * invCount,
        _accumBuffer[idx * 4 + 1] * invCount,
        _accumBuffer[idx * 4 + 2] * invCount,
        _accumBuffer[idx * 4 + 3] * invCount
    );

    Write(pixel, 4, avgColor.data());
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
    if (pixel[0] >= (int)_width || pixel[1] >= (int)_height) return;
    size_t idx = pixel[1] * _width + pixel[0];
    size_t formatSize = HdDataSizeOfFormat(_format);
    uint8_t *dst = &_renderBuffer[idx * formatSize];
    _WriteOutput(_format, dst, numComponents, value);
}

void
HdGeminiRenderBuffer::Write(GfVec3i const& pixel, size_t numComponents, int const* value)
{
    if (pixel[0] >= (int)_width || pixel[1] >= (int)_height) return;
    size_t idx = pixel[1] * _width + pixel[0];
    size_t formatSize = HdDataSizeOfFormat(_format);
    uint8_t *dst = &_renderBuffer[idx * formatSize];
    _WriteOutput(_format, dst, numComponents, value);
}

void
HdGeminiRenderBuffer::Clear(size_t numComponents, float const* value)
{
    size_t formatSize = HdDataSizeOfFormat(_format);
    for (size_t i = 0; i < _width * _height; ++i) {
        uint8_t *dst = &_renderBuffer[i * formatSize];
        _WriteOutput(_format, dst, numComponents, value);
    }
    _accumBuffer.assign(_width * _height * 4, 0.0f);
    _sampleCount.assign(_width * _height, 0);
}

void
HdGeminiRenderBuffer::Clear(size_t numComponents, int const* value)
{
    size_t formatSize = HdDataSizeOfFormat(_format);
    for (size_t i = 0; i < _width * _height; ++i) {
        uint8_t *dst = &_renderBuffer[i * formatSize];
        _WriteOutput(_format, dst, numComponents, value);
    }
    _accumBuffer.assign(_width * _height * 4, 0.0f);
    _sampleCount.assign(_width * _height, 0);
}
