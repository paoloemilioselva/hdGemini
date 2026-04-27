#ifndef HD_GEMINI_LIGHT_H
#define HD_GEMINI_LIGHT_H

#include "pxr/pxr.h"
#include "pxr/imaging/hd/light.h"

PXR_NAMESPACE_OPEN_SCOPE

class HdGeminiLight final : public HdLight {
public:
    HdGeminiLight(SdfPath const& id, TfToken const& lightType);
    virtual ~HdGeminiLight() = default;

    virtual void Sync(HdSceneDelegate *sceneDelegate,
                      HdRenderParam   *renderParam,
                      HdDirtyBits     *dirtyBits) override;

    virtual HdDirtyBits GetInitialDirtyBitsMask() const override;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // HD_GEMINI_LIGHT_H
