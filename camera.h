#ifndef HD_GEMINI_CAMERA_H
#define HD_GEMINI_CAMERA_H

#include "pxr/pxr.h"
#include "pxr/imaging/hd/camera.h"

PXR_NAMESPACE_USING_DIRECTIVE

class HdGeminiCamera final : public HdCamera {
public:
    HdGeminiCamera(SdfPath const& id) : HdCamera(id) {}
    virtual ~HdGeminiCamera() = default;
};

#endif // HD_GEMINI_CAMERA_H
