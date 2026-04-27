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
    _buffer.resize(_width * _height * HdDataSizeOfFormat(format));
    return true;
}

void
HdGeminiRenderBuffer::_Deallocate()
{
    _width = 0;
    _height = 0;
    _format = HdFormatInvalid;
    _buffer.clear();
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
    uint8_t *dst = &_buffer[idx * formatSize];
    _WriteOutput(_format, dst, numComponents, value);
}

void
HdGeminiRenderBuffer::Write(GfVec3i const& pixel, size_t numComponents, int const* value)
{
    if (pixel[0] >= (int)_width || pixel[1] >= (int)_height) return;
    size_t idx = pixel[1] * _width + pixel[0];
    size_t formatSize = HdDataSizeOfFormat(_format);
    uint8_t *dst = &_buffer[idx * formatSize];
    _WriteOutput(_format, dst, numComponents, value);
}

void
HdGeminiRenderBuffer::Clear(size_t numComponents, float const* value)
{
    size_t formatSize = HdDataSizeOfFormat(_format);
    for (size_t i = 0; i < _width * _height; ++i) {
        uint8_t *dst = &_buffer[i * formatSize];
        _WriteOutput(_format, dst, numComponents, value);
    }
}

void
HdGeminiRenderBuffer::Clear(size_t numComponents, int const* value)
{
    size_t formatSize = HdDataSizeOfFormat(_format);
    for (size_t i = 0; i < _width * _height; ++i) {
        uint8_t *dst = &_buffer[i * formatSize];
        _WriteOutput(_format, dst, numComponents, value);
    }
}
