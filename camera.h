#ifndef HD_GEMINI_CAMERA_H
#define HD_GEMINI_CAMERA_H

#include "pxr/pxr.h"
#include "pxr/imaging/hd/camera.h"

PXR_NAMESPACE_OPEN_SCOPE

class HdGeminiCamera final : public HdCamera {
public:
    HdGeminiCamera(SdfPath const& id) : HdCamera(id) {}
    virtual ~HdGeminiCamera() = default;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // HD_GEMINI_CAMERA_H
