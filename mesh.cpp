#include "mesh.h"
#include "renderParam.h"
#include "renderDelegate.h"
#include "pxr/imaging/hd/meshUtil.h"
#include "pxr/imaging/hd/sceneDelegate.h"
#include "pxr/imaging/hd/tokens.h"
#include "pxr/imaging/hd/extComputationUtils.h"
#include "pxr/base/vt/value.h"
#include <iostream>
#include <set>
#include <algorithm>
#include <map>

PXR_NAMESPACE_USING_DIRECTIVE

HdGeminiMesh::HdGeminiMesh(SdfPath const& id)
    : HdMesh(id)
    , _visible(true)
    , _subsetsDirty(true)
{
}

void
HdGeminiMesh::Finalize(HdRenderParam *renderParam)
{
    HdGeminiRenderParam *geminiRenderParam = static_cast<HdGeminiRenderParam*>(renderParam);
    geminiRenderParam->GetRenderDelegate()->RemoveMesh(GetId());
}

HdDirtyBits
HdGeminiMesh::GetInitialDirtyBitsMask() const
{
    return HdChangeTracker::AllSceneDirtyBits;
}

TfTokenVector
HdGeminiMesh::_UpdateComputedPrimvarSources(HdSceneDelegate* sceneDelegate,
                                            HdDirtyBits dirtyBits)
{
    SdfPath const& id = GetId();
    HdExtComputationPrimvarDescriptorVector compPrimvarsToUpdate;
    for (size_t i=0; i < HdInterpolationCount; ++i) {
        HdInterpolation interp = static_cast<HdInterpolation>(i);
        HdExtComputationPrimvarDescriptorVector descriptors = 
            sceneDelegate->GetExtComputationPrimvarDescriptors(id, interp);

        for (auto const& pv: descriptors) {
            bool dirty = HdChangeTracker::IsPrimvarDirty(dirtyBits, id, pv.name);
            
            // Special case: if DirtyPoints is set, any computed 'points' is dirty
            if (pv.name == HdTokens->points && (dirtyBits & HdChangeTracker::DirtyPoints)) {
                dirty = true;
            }

            if (dirty || _points.empty()) {
                compPrimvarsToUpdate.emplace_back(pv);
            }
        }
    }

    if (compPrimvarsToUpdate.empty()) {
        return TfTokenVector();
    }
    
    HdExtComputationUtils::ValueStore valueStore;
    try {
        valueStore = HdExtComputationUtils::GetComputedPrimvarValues(compPrimvarsToUpdate, sceneDelegate);
    } catch (...) {
        std::cout << "[Gemini] ERROR: GetComputedPrimvarValues threw an exception for " << id.GetText() << std::endl;
    }

    TfTokenVector compPrimvarNames;
    for (auto const& compPrimvar : compPrimvarsToUpdate) {
        auto it = valueStore.find(compPrimvar.name);
        if (it == valueStore.end() || it->second.IsEmpty()) {
            it = valueStore.find(compPrimvar.sourceComputationOutputName);
        }

        VtValue val;
        if (it != valueStore.end() && !it->second.IsEmpty()) {
            val = it->second;
        } else {
            // Fallback: try direct Get()
            try {
                val = sceneDelegate->Get(id, compPrimvar.name);
                if (val.IsEmpty() && compPrimvar.name != compPrimvar.sourceComputationOutputName) {
                    val = sceneDelegate->Get(id, compPrimvar.sourceComputationOutputName);
                }
            } catch (...) {
                // Ignore fallback errors
            }
        }

        if (val.IsEmpty()) {
            std::cout << "[Gemini] Warning: Failed to find computed value for PV " << compPrimvar.name.GetText() 
                      << " (output: " << compPrimvar.sourceComputationOutputName.GetText() 
                      << " comp: " << compPrimvar.sourceComputationId.GetText() << ")" << std::endl;
            continue;
        }
        
        compPrimvarNames.emplace_back(compPrimvar.name);

        if (compPrimvar.name == HdTokens->points) {
            if (val.IsHolding<VtVec3fArray>()) {
                _points = val.UncheckedGet<VtVec3fArray>();
                _subsetsDirty = true;
            } else if (val.IsHolding<VtVec3dArray>()) {
                const VtVec3dArray& pointsd = val.UncheckedGet<VtVec3dArray>();
                _points.resize(pointsd.size());
                for (size_t j = 0; j < pointsd.size(); ++j) _points[j] = GfVec3f(pointsd[j]);
                _subsetsDirty = true;
            }
        } else if (compPrimvar.name == HdTokens->displayColor) {
            if (val.IsHolding<VtVec3fArray>()) {
                _colors = val.UncheckedGet<VtVec3fArray>();
            }
        }
    }

    return compPrimvarNames;
}

void
HdGeminiMesh::Sync(HdSceneDelegate* sceneDelegate,
                   HdRenderParam*   renderParam,
                   HdDirtyBits*     dirtyBits,
                   TfToken const   &reprToken)
{
    const SdfPath& id = GetId();
    HdGeminiRenderParam *geminiRenderParam = static_cast<HdGeminiRenderParam*>(renderParam);
    
    {
        std::lock_guard<std::recursive_mutex> lock(geminiRenderParam->GetRenderDelegate()->GetSceneLock());
        geminiRenderParam->AcquireSceneForEdit();
        geminiRenderParam->GetRenderDelegate()->AddMesh(id, this);
    }

    _instancerId = sceneDelegate->GetInstancerId(id);
    
    if (HdChangeTracker::IsVisibilityDirty(*dirtyBits, id)) {
        _visible = sceneDelegate->GetVisible(id);
    }
    
    if (HdChangeTracker::IsTransformDirty(*dirtyBits, id)) {
        _transform = GfMatrix4f(sceneDelegate->GetTransform(id));
    }

    // --- COMPUTED PRIMVARS ---
    TfTokenVector computedNames = _UpdateComputedPrimvarSources(sceneDelegate, *dirtyBits);
    bool pointsUpdatedByComputation = false;
    bool colorsUpdatedByComputation = false;
    for (const auto& name : computedNames) {
        if (name == HdTokens->points) pointsUpdatedByComputation = true;
        if (name == HdTokens->displayColor) colorsUpdatedByComputation = true;
    }

    // --- POINTS UPDATING ---
    bool pointsDirty = (*dirtyBits & HdChangeTracker::DirtyPoints) || 
                       HdChangeTracker::IsPrimvarDirty(*dirtyBits, id, HdTokens->points) ||
                       _points.empty();

    if (pointsDirty) {
        bool pointsActuallyUpdated = pointsUpdatedByComputation;

        if (!pointsActuallyUpdated) {
            VtValue val = sceneDelegate->Get(id, HdTokens->points);
            if (!val.IsEmpty()) {
                if (val.IsHolding<VtVec3fArray>()) {
                    _points = val.UncheckedGet<VtVec3fArray>();
                    pointsActuallyUpdated = true;
                } else if (val.IsHolding<VtVec3dArray>()) {
                    const auto& arr = val.UncheckedGet<VtVec3dArray>();
                    _points.resize(arr.size());
                    for (size_t j = 0; j < arr.size(); ++j) _points[j] = GfVec3f(arr[j]);
                    pointsActuallyUpdated = true;
                }
            }
        }

        if (pointsActuallyUpdated) {
            _subsetsDirty = true;
        }
    }

    // --- COLOR UPDATING ---
    TfToken colorToken = HdTokens->displayColor;
    if (HdChangeTracker::IsPrimvarDirty(*dirtyBits, id, colorToken)) {
        if (!colorsUpdatedByComputation) {
            HdInterpolation colorInterp = HdInterpolationVertex;
            for (int i = 0; i < HdInterpolationCount; ++i) {
                HdPrimvarDescriptorVector pvs = sceneDelegate->GetPrimvarDescriptors(id, (HdInterpolation)i);
                for (const auto& pv : pvs) {
                    if (pv.name == colorToken) {
                        colorInterp = pv.interpolation;
                        break;
                    }
                }
            }

            VtIntArray colorIndices;
            VtValue val = sceneDelegate->GetIndexedPrimvar(id, colorToken, &colorIndices);
            if (val.IsEmpty()) val = sceneDelegate->Get(id, colorToken);

            if (!val.IsEmpty() && val.IsHolding<VtVec3fArray>()) {
                VtVec3fArray colors = val.UncheckedGet<VtVec3fArray>();
                if (!colorIndices.empty()) {
                    VtVec3fArray flattened(colorIndices.size());
                    for (size_t i = 0; i < colorIndices.size(); ++i) {
                        flattened[i] = colors[colorIndices[i]];
                    }
                    colors = flattened;
                }

                if (colorInterp == HdInterpolationFaceVarying) {
                    HdMeshTopology topology = GetMeshTopology(sceneDelegate);
                    HdMeshUtil meshUtil(&topology, id);
                    VtValue triangulated;
                    meshUtil.ComputeTriangulatedFaceVaryingPrimvar(colors.data(), (int)colors.size(), HdTypeFloatVec3, &triangulated);
                    if (!triangulated.IsEmpty() && triangulated.IsHolding<VtVec3fArray>()) {
                        _colors = triangulated.Get<VtVec3fArray>();
                    } else {
                        _colors = colors;
                    }
                } else {
                    _colors = colors;
                }
            }
        }
    }

    // --- UV UPDATING ---
    TfToken stToken("st");
    TfToken uvToken("uv");
    TfToken activeStToken = stToken;
    bool stDirty = HdChangeTracker::IsPrimvarDirty(*dirtyBits, id, stToken);
    bool uvDirty = HdChangeTracker::IsPrimvarDirty(*dirtyBits, id, uvToken);

    if (stDirty || uvDirty || _uvs.empty()) {
        HdInterpolation stInterp = HdInterpolationVertex;
        bool found = false;
        for (int i = 0; i < HdInterpolationCount; ++i) {
            HdPrimvarDescriptorVector pvs = sceneDelegate->GetPrimvarDescriptors(id, (HdInterpolation)i);
            for (const auto& pv : pvs) {
                if (pv.name == stToken || pv.name == uvToken) {
                    activeStToken = pv.name;
                    stInterp = pv.interpolation;
                    found = true;
                    break;
                }
            }
            if (found) break;
        }

        VtIntArray stIndices;
        VtValue val = sceneDelegate->GetIndexedPrimvar(id, activeStToken, &stIndices);
        if (val.IsEmpty()) val = sceneDelegate->Get(id, activeStToken);

        if (!val.IsEmpty() && val.IsHolding<VtVec2fArray>()) {
            VtVec2fArray uvs = val.UncheckedGet<VtVec2fArray>();
            if (!stIndices.empty()) {
                VtVec2fArray flattened(stIndices.size());
                for (size_t i = 0; i < stIndices.size(); ++i) {
                    flattened[i] = uvs[stIndices[i]];
                }
                uvs = flattened;
            }

            if (stInterp == HdInterpolationFaceVarying) {
                HdMeshTopology topology = GetMeshTopology(sceneDelegate);
                HdMeshUtil meshUtil(&topology, id);
                VtValue triangulated;
                meshUtil.ComputeTriangulatedFaceVaryingPrimvar(uvs.data(), (int)uvs.size(), HdTypeFloatVec2, &triangulated);
                if (!triangulated.IsEmpty() && triangulated.IsHolding<VtVec2fArray>()) {
                    _uvs = triangulated.Get<VtVec2fArray>();
                } else {
                    _uvs = uvs;
                }
            } else {
                _uvs = uvs;
            }
        }
    }

    // --- NORMAL UPDATING ---
    TfToken normalToken = HdTokens->normals;
    bool normalsDirty = HdChangeTracker::IsPrimvarDirty(*dirtyBits, id, normalToken) || _normals.empty();
    if (normalsDirty) {
        HdInterpolation normalInterp = HdInterpolationVertex;
        bool found = false;
        for (int i = 0; i < HdInterpolationCount; ++i) {
            HdPrimvarDescriptorVector pvs = sceneDelegate->GetPrimvarDescriptors(id, (HdInterpolation)i);
            for (const auto& pv : pvs) {
                if (pv.name == normalToken) {
                    normalInterp = pv.interpolation;
                    found = true;
                    break;
                }
            }
            if (found) break;
        }

        VtIntArray normalIndices;
        VtValue val = sceneDelegate->GetIndexedPrimvar(id, normalToken, &normalIndices);
        if (val.IsEmpty()) val = sceneDelegate->Get(id, normalToken);

        if (!val.IsEmpty() && val.IsHolding<VtVec3fArray>()) {
            VtVec3fArray normals = val.UncheckedGet<VtVec3fArray>();
            if (!normalIndices.empty()) {
                VtVec3fArray flattened(normalIndices.size());
                for (size_t i = 0; i < normalIndices.size(); ++i) {
                    flattened[i] = normals[normalIndices[i]];
                }
                normals = flattened;
            }

            if (normalInterp == HdInterpolationFaceVarying) {
                HdMeshTopology topology = GetMeshTopology(sceneDelegate);
                HdMeshUtil meshUtil(&topology, id);
                VtValue triangulated;
                meshUtil.ComputeTriangulatedFaceVaryingPrimvar(normals.data(), (int)normals.size(), HdTypeFloatVec3, &triangulated);
                if (!triangulated.IsEmpty() && triangulated.IsHolding<VtVec3fArray>()) {
                    _normals = triangulated.Get<VtVec3fArray>();
                } else {
                    _normals = normals;
                }
            } else {
                _normals = normals;
            }
        }
    }

    if (HdChangeTracker::IsTopologyDirty(*dirtyBits, id) || 
        (*dirtyBits & HdChangeTracker::DirtyMaterialId) ||
        _subsetsDirty) {
        
        SdfPath defaultMaterialId = sceneDelegate->GetMaterialId(id);
        HdMeshTopology topology = GetMeshTopology(sceneDelegate);
        HdMeshUtil meshUtil(&topology, id);
        
        VtVec3iArray allTriangulatedIndices;
        VtIntArray trianglePrimitiveParams;
        meshUtil.ComputeTriangleIndices(&allTriangulatedIndices, &trianglePrimitiveParams);
        
        // Map original faces to material IDs (GeomSubsets)
        HdGeomSubsets geomSubsets = topology.GetGeomSubsets();
        std::vector<SdfPath> faceMaterialPaths(topology.GetNumFaces(), defaultMaterialId);
        
        std::cout << "[Gemini] Mesh " << id.GetText() << " has " << geomSubsets.size() << " geomsubsets." << std::endl;
        for (const auto& subset : geomSubsets) {
            std::cout << "[Gemini]   Subset " << subset.id.GetText() << " | Material: " << subset.materialId.GetText() << " | Face count: " << subset.indices.size() << std::endl;
            for (int faceIdx : subset.indices) {
                if (faceIdx >= 0 && (size_t)faceIdx < faceMaterialPaths.size()) {
                    faceMaterialPaths[faceIdx] = subset.materialId;
                }
            }
        }
        
        // Group triangulated indices by material ID
        std::map<SdfPath, VtVec3iArray> groupedIndices;
        std::cout << "[Gemini] Mesh " << id.GetText() << " splitting into subsets:" << std::endl;
        for (size_t i = 0; i < allTriangulatedIndices.size(); ++i) {
            int primIdx = trianglePrimitiveParams[i];
            SdfPath matPath = defaultMaterialId;
            if (primIdx >= 0 && (size_t)primIdx < faceMaterialPaths.size()) {
                matPath = faceMaterialPaths[primIdx];
            }
            groupedIndices[matPath].push_back(allTriangulatedIndices[i]);
        }

        // Rebuild subsets
        _subsets.clear();
        for (auto& pair : groupedIndices) {
            Subset subset;
            subset.materialId = pair.first;
            subset.indices = std::move(pair.second);
            std::cout << "[Gemini]   Sub-mesh for " << pair.first.GetText() << " has " << subset.indices.size() << " triangles." << std::endl;
            if (!subset.indices.empty()) {
                std::cout << "[Gemini]     First triangle: " << subset.indices[0] << std::endl;
            }
            
            // Build subset BVH
            if (!subset.indices.empty() && !_points.empty()) {
                subset.bvh.Build(_points, subset.indices, _uvs, _normals, std::vector<int>());
                
                // Compute subset bounds
                subset.range.SetEmpty();
                for (const auto& tri : subset.indices) {
                    subset.range.ExtendBy(_points[tri[0]]);
                    subset.range.ExtendBy(_points[tri[1]]);
                    subset.range.ExtendBy(_points[tri[2]]);
                }
            }
            _subsets.push_back(std::move(subset));
        }
        _subsetsDirty = false;
    }

    *dirtyBits &= ~HdChangeTracker::AllSceneDirtyBits;
}

void
HdGeminiMesh::_InitRepr(TfToken const &reprToken, HdDirtyBits *dirtyBits)
{
}

HdDirtyBits
HdGeminiMesh::_PropagateDirtyBits(HdDirtyBits bits) const
{
    return bits;
}
