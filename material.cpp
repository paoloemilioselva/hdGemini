#include "material.h"
#include "pxr/imaging/hd/sceneDelegate.h"
#include "pxr/imaging/hd/materialNetwork2Interface.h"
#include "pxr/usd/sdr/registry.h"
#include "pxr/usd/sdr/shaderNode.h"
#include "pxr/base/vt/value.h"
#include <map>
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

void
HdGeminiMaterial::Sync(HdSceneDelegate *sceneDelegate,
                       HdRenderParam   *renderParam,
                       HdDirtyBits     *dirtyBits)
{
    if (*dirtyBits & HdMaterial::DirtyResource) {
        VtValue materialResource = sceneDelegate->GetMaterialResource(GetId());
        if (materialResource.IsHolding<HdMaterialNetworkMap>()) {
            HdMaterialNetworkMap const& map = materialResource.UncheckedGet<HdMaterialNetworkMap>();
            
            // Convert to HdMaterialNetwork2 for easier terminal lookup
            HdMaterialNetwork2 network = HdConvertToHdMaterialNetwork2(map);
            
            // Find the surface terminal (prioritize mtlx:surface, then surface)
            auto itTerm = network.terminals.find(TfToken("mtlx:surface"));
            if (itTerm == network.terminals.end()) {
                itTerm = network.terminals.find(HdMaterialTerminalTokens->surface);
            }
            if (itTerm == network.terminals.end() && !network.terminals.empty()) {
                itTerm = network.terminals.begin();
            }

            if (itTerm != network.terminals.end()) {
                SdfPath terminalPath = itTerm->second.upstreamNode;
                auto itNode = network.nodes.find(terminalPath);
                
                if (itNode != network.nodes.end()) {
                    const HdMaterialNode2& surfaceNode = itNode->second;
                    
                    SdrRegistry &sdrRegistry = SdrRegistry::GetInstance();
                    
                    // Prioritize mtlx context for shader resolution
                    SdrShaderNodeConstPtr sdrEntry = sdrRegistry.GetShaderNodeByIdentifier(surfaceNode.nodeTypeId, {TfToken("mtlx")});
                    if (!sdrEntry) {
                        // Fallback to no-context
                        sdrEntry = sdrRegistry.GetShaderNodeByIdentifier(surfaceNode.nodeTypeId);
                    }
                    
                    TfToken shaderId = sdrEntry ? sdrEntry->GetIdentifier() : surfaceNode.nodeTypeId;

                    // 1. Read constant parameters
                    if (shaderId == TfToken("UsdPreviewSurface")) {
                        for (auto const& param : surfaceNode.parameters) {
                            if (param.first == TfToken("diffuseColor") && param.second.IsHolding<GfVec3f>()) {
                                _diffuseColor = param.second.UncheckedGet<GfVec3f>();
                            } else if (param.first == TfToken("metallic") && param.second.IsHolding<float>()) {
                                _metallic = param.second.UncheckedGet<float>();
                            } else if (param.first == TfToken("roughness") && param.second.IsHolding<float>()) {
                                _roughness = param.second.UncheckedGet<float>();
                            } else if (param.first == TfToken("opacity") && param.second.IsHolding<float>()) {
                                _opacity = param.second.UncheckedGet<float>();
                            } else if (param.first == TfToken("ior") && param.second.IsHolding<float>()) {
                                _ior = param.second.UncheckedGet<float>();
                            } else if (param.first == TfToken("emissiveColor") && param.second.IsHolding<GfVec3f>()) {
                                _emissionColor = param.second.UncheckedGet<GfVec3f>();
                                _emission = 1.0f;
                            }
                        }
                    } else if (shaderId == TfToken("ND_standard_surface_surfaceshader") ||
                               shaderId == TfToken("standard_surface") ||
                               shaderId == TfToken("StandardSurface")) {
                        float base = 1.0f;
                        GfVec3f baseColor(0.8f);
                        for (auto const& param : surfaceNode.parameters) {
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
                            } else if (param.first == TfToken("ior") && param.second.IsHolding<float>()) {
                                _ior = param.second.UncheckedGet<float>();
                            } else if ((param.first == TfToken("transmission") || param.first == TfToken("transmission_weight")) && param.second.IsHolding<float>()) {
                                _transmission = param.second.UncheckedGet<float>();
                            } else if ((param.first == TfToken("emission") || param.first == TfToken("emission_weight")) && param.second.IsHolding<float>()) {
                                _emission = param.second.UncheckedGet<float>();
                            } else if (param.first == TfToken("emission_color") && param.second.IsHolding<GfVec3f>()) {
                                _emissionColor = param.second.UncheckedGet<GfVec3f>();
                            }
                        }
                        _diffuseColor = baseColor * base;
                    }

                    // 2. Read connections (textures)
                    for (auto const& connPair : surfaceNode.inputConnections) {
                        TfToken inputName = connPair.first;
                        if (inputName == TfToken("diffuseColor") || inputName == TfToken("base_color")) {
                            for (auto const& conn : connPair.second) {
                                auto itInputNode = network.nodes.find(conn.upstreamNode);
                                if (itInputNode != network.nodes.end()) {
                                    const HdMaterialNode2& inputNode = itInputNode->second;
                                    
                                    SdrShaderNodeConstPtr inputSdrEntry = sdrRegistry.GetShaderNodeByIdentifier(inputNode.nodeTypeId, {TfToken("mtlx")});
                                    if (!inputSdrEntry) {
                                        inputSdrEntry = sdrRegistry.GetShaderNodeByIdentifier(inputNode.nodeTypeId);
                                    }
                                    TfToken inputShaderId = inputSdrEntry ? inputSdrEntry->GetIdentifier() : inputNode.nodeTypeId;

                                    if (inputShaderId == TfToken("UsdUVTexture") ||
                                        inputShaderId == TfToken("ND_image_color3") ||
                                        inputShaderId == TfToken("ND_image")) {
                                        for (auto const& param : inputNode.parameters) {
                                            if ((param.first == TfToken("file") || param.first == TfToken("texcoord")) && 
                                                param.second.IsHolding<SdfAssetPath>()) {
                                                _diffuseTexture = param.second.UncheckedGet<SdfAssetPath>();
                                                std::cout << "[Gemini]   Found texture for " << inputName.GetText() << ": " << _diffuseTexture.GetAssetPath() << std::endl;
                                            }
                                        }
                                    }
                                }
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
