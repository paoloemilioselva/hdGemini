#include "material.h"
#include "pxr/imaging/hd/sceneDelegate.h"
#include "pxr/base/vt/value.h"

PXR_NAMESPACE_USING_DIRECTIVE

HdGeminiMaterial::HdGeminiMaterial(SdfPath const& id)
    : HdMaterial(id)
    , _diffuseColor(0.8f)
    , _metallic(0.0f)
    , _roughness(0.5f)
    , _opacity(1.0f)
    , _emissionColor(1.0f)
    , _emission(0.0f)
{
}

HdGeminiMaterial::~HdGeminiMaterial() = default;

void
HdGeminiMaterial::Sync(HdSceneDelegate *sceneDelegate,
                       HdRenderParam   *renderParam,
                       HdDirtyBits     *dirtyBits)
{
    if (*dirtyBits & HdMaterial::DirtyResource) {
        VtValue materialResource = sceneDelegate->GetMaterialResource(GetId());
        if (materialResource.IsHolding<HdMaterialNetworkMap>()) {
            HdMaterialNetworkMap const& map = materialResource.UncheckedGet<HdMaterialNetworkMap>();
            for (auto const& pair : map.map) {
                HdMaterialNetwork const& network = pair.second;
                for (auto const& node : network.nodes) {
                    if (node.identifier == TfToken("UsdPreviewSurface")) {
                        for (auto const& param : node.parameters) {
                            if (param.first == TfToken("diffuseColor") && param.second.IsHolding<GfVec3f>()) {
                                _diffuseColor = param.second.UncheckedGet<GfVec3f>();
                            } else if (param.first == TfToken("metallic") && param.second.IsHolding<float>()) {
                                _metallic = param.second.UncheckedGet<float>();
                            } else if (param.first == TfToken("roughness") && param.second.IsHolding<float>()) {
                                _roughness = param.second.UncheckedGet<float>();
                            } else if (param.first == TfToken("opacity") && param.second.IsHolding<float>()) {
                                _opacity = param.second.UncheckedGet<float>();
                            } else if (param.first == TfToken("emissiveColor") && param.second.IsHolding<GfVec3f>()) {
                                _emissionColor = param.second.UncheckedGet<GfVec3f>();
                                _emission = 1.0f; // UsdPreviewSurface uses emissiveColor directly
                            }
                        }
                    } else if (node.identifier == TfToken("ND_standard_surface_surfaceshader") ||
                               node.identifier == TfToken("standard_surface") ||
                               node.identifier == TfToken("StandardSurface")) {
                        float base = 1.0f;
                        GfVec3f baseColor(0.8f);
                        for (auto const& param : node.parameters) {
                            if ((param.first == TfToken("base") || param.first == TfToken("base_weight")) && param.second.IsHolding<float>()) {
                                base = param.second.UncheckedGet<float>();
                            } else if (param.first == TfToken("base_color") && param.second.IsHolding<GfVec3f>()) {
                                baseColor = param.second.UncheckedGet<GfVec3f>();
                            } else if (param.first == TfToken("metalness") && param.second.IsHolding<float>()) {
                                _metallic = param.second.UncheckedGet<float>();
                            } else if ((param.first == TfToken("specular_roughness") || param.first == TfToken("roughness")) && param.second.IsHolding<float>()) {
                                _roughness = param.second.UncheckedGet<float>();
                            } else if (param.first == TfToken("opacity") && param.second.IsHolding<float>()) {
                                _opacity = param.second.UncheckedGet<float>();
                            } else if ((param.first == TfToken("emission") || param.first == TfToken("emission_weight")) && param.second.IsHolding<float>()) {
                                _emission = param.second.UncheckedGet<float>();
                            } else if (param.first == TfToken("emission_color") && param.second.IsHolding<GfVec3f>()) {
                                _emissionColor = param.second.UncheckedGet<GfVec3f>();
                            }
                        }
                        _diffuseColor = baseColor * base;
                    }
                }
            }
        }
    }
    *dirtyBits = HdMaterial::Clean;
}

HdDirtyBits
HdGeminiMaterial::GetInitialDirtyBitsMask() const
{
    return HdMaterial::AllDirty;
}
