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

    virtual HdDirtyBits GetInitialDirtyBitsMask() const override;

    const GfVec3f& GetDiffuseColor() const { return _diffuseColor; }
    void SetDiffuseColor(const GfVec3f& v) { _diffuseColor = v; }

    float GetMetallic() const { return _metallic; }
    void SetMetallic(float v) { _metallic = v; }

    float GetRoughness() const { return _roughness; }
    void SetRoughness(float v) { _roughness = v; }

    float GetOpacity() const { return _opacity; }
    void SetOpacity(float v) { _opacity = v; }

    float GetIor() const { return _ior; }
    void SetIor(float v) { _ior = v; }

    float GetTransmission() const { return _transmission; }
    void SetTransmission(float v) { _transmission = v; }

    const GfVec3f& GetTransmissionColor() const { return _transmissionColor; }
    void SetTransmissionColor(const GfVec3f& v) { _transmissionColor = v; }

    const GfVec3f& GetEmissionColor() const { return _emissionColor; }
    void SetEmissionColor(const GfVec3f& v) { _emissionColor = v; }

    float GetEmission() const { return _emission; }
    void SetEmission(float v) { _emission = v; }

    const SdfAssetPath& GetDiffuseTexture() const { return _diffuseTexture; }
    void SetDiffuseTexture(const SdfAssetPath& v) { _diffuseTexture = v; }

    private:
    GfVec3f _diffuseColor;
    float _metallic;
    float _roughness;
    float _opacity;
    float _ior;
    float _transmission;
    GfVec3f _transmissionColor;
    GfVec3f _emissionColor;
    float _emission;

    SdfAssetPath _diffuseTexture;
};

#endif // HD_GEMINI_MATERIAL_H
