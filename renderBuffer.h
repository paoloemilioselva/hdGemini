#ifndef HD_GEMINI_RENDER_BUFFER_H
#define HD_GEMINI_RENDER_BUFFER_H

#include "pxr/pxr.h"
#include "pxr/imaging/hd/renderBuffer.h"

PXR_NAMESPACE_OPEN_SCOPE

class HdGeminiRenderBuffer final : public HdRenderBuffer {
public:
    HdGeminiRenderBuffer(SdfPath const& id);
    virtual ~HdGeminiRenderBuffer() = default;

    virtual bool Allocate(GfVec3i const& dimensions,
                          HdFormat format,
                          bool multiSampled) override;

    virtual unsigned int GetWidth() const override { return _width; }
    virtual unsigned int GetHeight() const override { return _height; }
    virtual unsigned int GetDepth() const override { return 1; }
    virtual HdFormat GetFormat() const override { return _format; }
    virtual bool IsMultiSampled() const override { return _multiSampled; }

    virtual void* Map() override { return _buffer.data(); }
    virtual void Unmap() override {}
    virtual bool IsMapped() const override { return false; }

    virtual void Resolve() override {}
    virtual bool IsConverged() const override { return true; }

private:
    unsigned int _width, _height;
    HdFormat _format;
    bool _multiSampled;
    std::vector<uint8_t> _buffer;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // HD_GEMINI_RENDER_BUFFER_H
