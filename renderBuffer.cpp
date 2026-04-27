#include "renderBuffer.h"
#include <pxr/base/gf/vec3i.h>

PXR_NAMESPACE_OPEN_SCOPE

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

PXR_NAMESPACE_CLOSE_SCOPE
