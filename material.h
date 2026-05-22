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

    const GfVec3f& GetSpecularColor() const { return _specularColor; }
    void SetSpecularColor(const GfVec3f& v) { _specularColor = v; }

    float GetSpecular() const { return _specular; }
    void SetSpecular(float v) { _specular = v; }

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

    float GetCoat() const { return _coat; }
    void SetCoat(float v) { _coat = v; }
    
    const GfVec3f& GetCoatColor() const { return _coatColor; }
    void SetCoatColor(const GfVec3f& v) { _coatColor = v; }
    
    float GetCoatRoughness() const { return _coatRoughness; }
    void SetCoatRoughness(float v) { _coatRoughness = v; }
    
    float GetCoatIor() const { return _coatIor; }
    void SetCoatIor(float v) { _coatIor = v; }

    float GetTransmissionDepth() const { return _transmissionDepth; }
    void SetTransmissionDepth(float v) { _transmissionDepth = v; }
    
    const GfVec3f& GetTransmissionScatter() const { return _transmissionScatter; }
    void SetTransmissionScatter(const GfVec3f& v) { _transmissionScatter = v; }

    float GetSheen() const { return _sheen; }
    void SetSheen(float v) { _sheen = v; }
    
    const GfVec3f& GetSheenColor() const { return _sheenColor; }
    void SetSheenColor(const GfVec3f& v) { _sheenColor = v; }
    
    float GetSheenRoughness() const { return _sheenRoughness; }
    void SetSheenRoughness(float v) { _sheenRoughness = v; }

    float GetSubsurface() const { return _subsurface; }
    void SetSubsurface(float v) { _subsurface = v; }

    const GfVec3f& GetSubsurfaceColor() const { return _subsurfaceColor; }
    void SetSubsurfaceColor(const GfVec3f& v) { _subsurfaceColor = v; }

    const GfVec3f& GetSubsurfaceRadius() const { return _subsurfaceRadius; }
    void SetSubsurfaceRadius(const GfVec3f& v) { _subsurfaceRadius = v; }

    float GetSubsurfaceScale() const { return _subsurfaceScale; }
    void SetSubsurfaceScale(float v) { _subsurfaceScale = v; }

    float GetSubsurfaceAnisotropy() const { return _subsurfaceAnisotropy; }
    void SetSubsurfaceAnisotropy(float v) { _subsurfaceAnisotropy = v; }

    bool GetThinWalled() const { return _thinWalled; }
    void SetThinWalled(bool v) { _thinWalled = v; }
    
    float GetDiffuseRoughness() const { return _diffuseRoughness; }
    void SetDiffuseRoughness(float v) { _diffuseRoughness = v; }

    const SdfAssetPath& GetDiffuseTexture() const { return _diffuseTexture; }
    void SetDiffuseTexture(const SdfAssetPath& v) { _diffuseTexture = v; }

    const SdfAssetPath& GetNormalTexture() const { return _normalTexture; }
    void SetNormalTexture(const SdfAssetPath& v) { _normalTexture = v; }

    const SdfAssetPath& GetMetallicTexture() const { return _metallicTexture; }
    void SetMetallicTexture(const SdfAssetPath& v) { _metallicTexture = v; }

    const SdfAssetPath& GetRoughnessTexture() const { return _roughnessTexture; }
    void SetRoughnessTexture(const SdfAssetPath& v) { _roughnessTexture = v; }

    int GetMetallicTextureChannel() const { return _metallicTextureChannel; }
    void SetMetallicTextureChannel(int v) { _metallicTextureChannel = v; }

    int GetRoughnessTextureChannel() const { return _roughnessTextureChannel; }
    void SetRoughnessTextureChannel(int v) { _roughnessTextureChannel = v; }

    private:
    GfVec3f _diffuseColor;    float _metallic;
    float _roughness;
    GfVec3f _specularColor;
    float _specular;
    float _opacity;
    float _ior;
    float _transmission;
    GfVec3f _transmissionColor;
    float _transmissionDepth;
    GfVec3f _transmissionScatter;
    GfVec3f _emissionColor;
    float _emission;
    float _coat;
    GfVec3f _coatColor;
    float _coatRoughness;
    float _coatIor;
    float _sheen;
    GfVec3f _sheenColor;
    float _sheenRoughness;
    float _subsurface;
    GfVec3f _subsurfaceColor;
    GfVec3f _subsurfaceRadius;
    float _subsurfaceScale;
    float _subsurfaceAnisotropy;
    bool _thinWalled;
    float _diffuseRoughness;

    SdfAssetPath _diffuseTexture;
    SdfAssetPath _normalTexture;
    SdfAssetPath _metallicTexture;
    SdfAssetPath _roughnessTexture;
    int _metallicTextureChannel = 0;
    int _roughnessTextureChannel = 0;
};

#endif // HD_GEMINI_MATERIAL_H
