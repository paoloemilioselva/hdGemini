#ifndef HD_GEMINI_LIGHT_H
#define HD_GEMINI_LIGHT_H

#include "pxr/pxr.h"
#include "pxr/imaging/hd/light.h"

PXR_NAMESPACE_USING_DIRECTIVE

class HdGeminiLight final : public HdLight {
public:
    HdGeminiLight(SdfPath const& id, TfToken const& lightType);
    virtual ~HdGeminiLight() = default;

    virtual void Sync(HdSceneDelegate *sceneDelegate,
                      HdRenderParam   *renderParam,
                      HdDirtyBits     *dirtyBits) override;

    virtual HdDirtyBits GetInitialDirtyBitsMask() const override;
};

#endif // HD_GEMINI_LIGHT_H
