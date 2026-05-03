#ifndef HD_GEMINI_MATERIAL_H
#define HD_GEMINI_MATERIAL_H

#include "pxr/pxr.h"
#include "pxr/imaging/hd/material.h"
#include "pxr/base/gf/vec3f.h"
#include "pxr/usd/sdf/assetPath.h"

PXR_NAMESPACE_USING_DIRECTIVE

class HdGeminiMaterial final : public HdMaterial {
public:
    HdGeminiMaterial(SdfPath const& id);
    ~HdGeminiMaterial() override;

    void Sync(HdSceneDelegate *sceneDelegate,
              HdRenderParam   *renderParam,
              HdDirtyBits     *dirtyBits) override;

    HdDirtyBits GetInitialDirtyBitsMask() const override;

    const GfVec3f& GetDiffuseColor() const { return _diffuseColor; }
    float GetMetallic() const { return _metallic; }
    float GetRoughness() const { return _roughness; }
    float GetOpacity() const { return _opacity; }
    const GfVec3f& GetEmissionColor() const { return _emissionColor; }
    float GetEmission() const { return _emission; }

    const SdfAssetPath& GetDiffuseTexture() const { return _diffuseTexture; }

private:
    GfVec3f _diffuseColor;
    float _metallic;
    float _roughness;
    float _opacity;
    GfVec3f _emissionColor;
    float _emission;

    SdfAssetPath _diffuseTexture;
};

#endif // HD_GEMINI_MATERIAL_H
