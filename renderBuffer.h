#ifndef HD_GEMINI_RENDER_BUFFER_H
#define HD_GEMINI_RENDER_BUFFER_H

#include "pxr/pxr.h"
#include "pxr/imaging/hd/renderBuffer.h"
#include "pxr/base/gf/vec3i.h"
#include "pxr/base/gf/vec4f.h"
#include <vector>
#include <atomic>

PXR_NAMESPACE_USING_DIRECTIVE

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

    virtual void Resolve() override;
    void ResolveBucket(unsigned int startX, unsigned int startY, unsigned int endX, unsigned int endY);
    virtual bool IsConverged() const override { return _converged.load(); }
    void SetConverged(bool converged) { _converged.store(converged); }

    void WriteSample(GfVec3i const& pixel, GfVec4f const& color);
    void Write(GfVec3i const& pixel, size_t numComponents, float const* value);
    void Write(GfVec3i const& pixel, size_t numComponents, int const* value);
    void Clear(size_t numComponents, float const* value);
    void Clear(size_t numComponents, int const* value);

protected:
    virtual void _Deallocate() override;

private:
    unsigned int _width, _height;
    HdFormat _format;
    bool _multiSampled;
    std::vector<uint8_t> _buffer;
    std::vector<uint8_t> _renderBuffer;
    std::vector<float> _accumBuffer;
    std::vector<int> _sampleCount;
    std::atomic<bool> _converged;
};

#endif // HD_GEMINI_RENDER_BUFFER_H
