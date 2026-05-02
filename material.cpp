#include "material.h"
#include "pxr/imaging/hd/sceneDelegate.h"
#include "pxr/base/vt/value.h"

PXR_NAMESPACE_USING_DIRECTIVE

HdGeminiMaterial::HdGeminiMaterial(SdfPath const& id)
    : HdMaterial(id)
    , _diffuseColor(0.8f)
    , _metallic(0.0f)
    , _roughness(0.5f)
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
                            }
                        }
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
