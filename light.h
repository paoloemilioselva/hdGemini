#ifndef HD_GEMINI_LIGHT_H
#define HD_GEMINI_LIGHT_H

#include "pxr/pxr.h"
#include "pxr/imaging/hd/light.h"
#include "pxr/base/gf/vec3f.h"
#include "pxr/base/gf/matrix4d.h"
#include "pxr/usd/sdf/assetPath.h"

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
    float GetIntensity() const { return _intensity * std::exp2(_exposure); }
    const GfMatrix4d& GetTransform() const { return _transform; }
    const TfToken& GetLightType() const { return _lightType; }
    const SdfAssetPath& GetTextureFile() const { return _textureFile; }
    float GetWidth() const { return _width; }
    float GetHeight() const { return _height; }
    float GetShapingConeAngle() const { return _shapingConeAngle; }
    float GetShapingConeSoftness() const { return _shapingConeSoftness; }

private:
    GfVec3f _color;
    float _intensity;
    float _exposure;
    GfMatrix4d _transform;
    TfToken _lightType;
    SdfAssetPath _textureFile;
    float _width;
    float _height;
    float _shapingConeAngle = 180.0f;
    float _shapingConeSoftness = 0.0f;
};

#endif // HD_GEMINI_LIGHT_H
