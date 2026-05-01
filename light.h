#ifndef HD_GEMINI_LIGHT_H
#define HD_GEMINI_LIGHT_H

#include "pxr/pxr.h"
#include "pxr/imaging/hd/light.h"
#include "pxr/base/gf/vec3f.h"
#include "pxr/base/gf/matrix4d.h"

PXR_NAMESPACE_USING_DIRECTIVE

class HdGeminiLight final : public HdLight {
public:
    HdGeminiLight(SdfPath const& id, TfToken const& lightType);
    virtual ~HdGeminiLight() = default;

    virtual void Sync(HdSceneDelegate *sceneDelegate,
                      HdRenderParam   *renderParam,
                      HdDirtyBits     *dirtyBits) override;

    virtual HdDirtyBits GetInitialDirtyBitsMask() const override;

    const GfVec3f& GetColor() const { return _color; }
    float GetIntensity() const { return _intensity; }
    const GfMatrix4d& GetTransform() const { return _transform; }
    const TfToken& GetLightType() const { return _lightType; }

private:
    GfVec3f _color;
    float _intensity;
    GfMatrix4d _transform;
    TfToken _lightType;
};

#endif // HD_GEMINI_LIGHT_H
