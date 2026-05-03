#include "material.h"
#include "pxr/imaging/hd/sceneDelegate.h"
#include "pxr/imaging/hd/materialNetwork2Interface.h"
#include "pxr/usd/sdr/registry.h"
#include "pxr/usd/sdr/shaderNode.h"
#include "pxr/base/vt/value.h"
#include <map>
#include <set>
#include <iostream>

PXR_NAMESPACE_USING_DIRECTIVE

HdGeminiMaterial::HdGeminiMaterial(SdfPath const& id)
    : HdMaterial(id)
    , _diffuseColor(0.8f)
    , _metallic(0.0f)
    , _roughness(0.5f)
    , _opacity(1.0f)
    , _ior(1.5f)
    , _transmission(0.0f)
    , _emissionColor(1.0f)
    , _emission(0.0f)
{
}

HdGeminiMaterial::~HdGeminiMaterial() = default;

// Helper to walk upstream and process nodes
static void _ProcessNodeUpstream(
    const HdMaterialNetwork2& network,
    const SdfPath& nodePath,
    std::set<SdfPath>& visited,
    HdGeminiMaterial* material)
{
    if (visited.count(nodePath)) return;
    visited.insert(nodePath);

    auto itNode = network.nodes.find(nodePath);
    if (itNode == network.nodes.end()) return;

    const HdMaterialNode2& node = itNode->second;
    SdrRegistry &sdrRegistry = SdrRegistry::GetInstance();
    
    // Resolve shader with MaterialX context preference
    SdrShaderNodeConstPtr sdrEntry = sdrRegistry.GetShaderNodeByIdentifier(node.nodeTypeId, {TfToken("mtlx")});
    if (!sdrEntry) sdrEntry = sdrRegistry.GetShaderNodeByIdentifier(node.nodeTypeId);
    
    TfToken shaderId = sdrEntry ? sdrEntry->GetIdentifier() : node.nodeTypeId;
    
    std::cout << "[Gemini]     Node: " << nodePath.GetText() << " | Resolved ID: " << shaderId.GetText() << " | Type: " << node.nodeTypeId.GetText() << std::endl;

    // Parse parameters based on shader type
    if (shaderId == TfToken("UsdPreviewSurface")) {
        for (auto const& param : node.parameters) {
            if (param.first == TfToken("diffuseColor") && param.second.IsHolding<GfVec3f>()) {
                material->SetDiffuseColor(param.second.UncheckedGet<GfVec3f>());
            } else if (param.first == TfToken("metallic") && param.second.IsHolding<float>()) {
                material->SetMetallic(param.second.UncheckedGet<float>());
            } else if (param.first == TfToken("roughness") && param.second.IsHolding<float>()) {
                material->SetRoughness(param.second.UncheckedGet<float>());
            } else if (param.first == TfToken("opacity") && param.second.IsHolding<float>()) {
                material->SetOpacity(param.second.UncheckedGet<float>());
            } else if (param.first == TfToken("ior") && param.second.IsHolding<float>()) {
                material->SetIor(param.second.UncheckedGet<float>());
            } else if (param.first == TfToken("emissiveColor") && param.second.IsHolding<GfVec3f>()) {
                material->SetEmissionColor(param.second.UncheckedGet<GfVec3f>());
                material->SetEmission(1.0f);
            }
        }
    } else if (shaderId == TfToken("ND_standard_surface_surfaceshader") ||
               shaderId == TfToken("standard_surface") ||
               shaderId == TfToken("StandardSurface")) {
        float base = 1.0f;
        GfVec3f baseColor(0.8f);
        for (auto const& param : node.parameters) {
            if ((param.first == TfToken("base") || param.first == TfToken("base_weight")) && param.second.IsHolding<float>()) {
                base = param.second.UncheckedGet<float>();
            } else if (param.first == TfToken("base_color") && param.second.IsHolding<GfVec3f>()) {
                baseColor = param.second.UncheckedGet<GfVec3f>();
            } else if (param.first == TfToken("metalness") && param.second.IsHolding<float>()) {
                material->SetMetallic(param.second.UncheckedGet<float>());
            } else if ((param.first == TfToken("specular_roughness") || param.first == TfToken("roughness")) && param.second.IsHolding<float>()) {
                material->SetRoughness(param.second.UncheckedGet<float>());
            } else if (param.first == TfToken("opacity") && param.second.IsHolding<float>()) {
                material->SetOpacity(param.second.UncheckedGet<float>());
            } else if (param.first == TfToken("ior") && param.second.IsHolding<float>()) {
                material->SetIor(param.second.UncheckedGet<float>());
            } else if ((param.first == TfToken("transmission") || param.first == TfToken("transmission_weight")) && param.second.IsHolding<float>()) {
                material->SetTransmission(param.second.UncheckedGet<float>());
            } else if ((param.first == TfToken("emission") || param.first == TfToken("emission_weight")) && param.second.IsHolding<float>()) {
                material->SetEmission(param.second.UncheckedGet<float>());
            } else if (param.first == TfToken("emission_color") && param.second.IsHolding<GfVec3f>()) {
                material->SetEmissionColor(param.second.UncheckedGet<GfVec3f>());
            }
        }
        material->SetDiffuseColor(baseColor * base);
    } else if (shaderId == TfToken("UsdUVTexture") ||
               shaderId == TfToken("ND_image_color3") ||
               shaderId == TfToken("ND_image") ||
               shaderId == TfToken("ND_image_float") ||
               shaderId == TfToken("ND_image_vector2") ||
               shaderId == TfToken("ND_image_vector3") ||
               shaderId == TfToken("ND_image_vector4") ||
               shaderId == TfToken("ND_image_color4")) {
        for (auto const& param : node.parameters) {
            if ((param.first == TfToken("file") || param.first == TfToken("texcoord")) && 
                param.second.IsHolding<SdfAssetPath>()) {
                material->SetDiffuseTexture(param.second.UncheckedGet<SdfAssetPath>());
                std::cout << "[Gemini]       Mapped texture: " << material->GetDiffuseTexture().GetAssetPath() << std::endl;
            }
        }
    }

    // Walk upstream recursively
    for (auto const& connPair : node.inputConnections) {
        for (auto const& conn : connPair.second) {
            _ProcessNodeUpstream(network, conn.upstreamNode, visited, material);
        }
    }
}

void
HdGeminiMaterial::Sync(HdSceneDelegate *sceneDelegate,
                       HdRenderParam   *renderParam,
                       HdDirtyBits     *dirtyBits)
{
    if (*dirtyBits & HdMaterial::DirtyResource) {
        VtValue materialResource = sceneDelegate->GetMaterialResource(GetId());
        if (materialResource.IsHolding<HdMaterialNetworkMap>()) {
            HdMaterialNetworkMap const& map = materialResource.UncheckedGet<HdMaterialNetworkMap>();
            
            // Convert to HdMaterialNetwork2
            HdMaterialNetwork2 network = HdConvertToHdMaterialNetwork2(map);

            std::cout << "[Gemini] Syncing material " << GetId().GetText() << ":" << std::endl;
            
            std::set<SdfPath> visited;
            
            std::cout << "[Gemini]   Terminals found:";
            for (auto const& t : network.terminals) std::cout << " " << t.first.GetText();
            std::cout << std::endl;

            // Find the best terminal
            TfToken selectedTerminal;
            TfToken terminalPriorities[] = { 
                TfToken("mtlx:surface"), 
                HdMaterialTerminalTokens->surface,
                TfToken("outputs:surface") 
            };

            for (const auto& t : terminalPriorities) {
                if (network.terminals.count(t)) {
                    selectedTerminal = t;
                    break;
                }
            }

            // Fallback to first if none of the above found
            if (selectedTerminal.IsEmpty() && !network.terminals.empty()) {
                selectedTerminal = network.terminals.begin()->first;
            }

            if (!selectedTerminal.IsEmpty()) {
                const SdfPath& terminalPath = network.terminals.at(selectedTerminal).upstreamNode;
                std::cout << "[Gemini]   Selected terminal: " << selectedTerminal.GetText() << " -> " << terminalPath.GetText() << std::endl;
                _ProcessNodeUpstream(network, terminalPath, visited, this);
            } else {
                std::cout << "[Gemini]   No terminals found in network!" << std::endl;
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
