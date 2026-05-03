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

PXR_NAMESPACE_USING_DIRECTIVE

HdGeminiMesh::HdGeminiMesh(SdfPath const& id)
    : HdMesh(id)
    , _visible(true)
    , _bvhDirty(true)
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
            // Special case: if DirtyColors is set, any computed 'displayColor' is dirty
            if (pv.name == HdTokens->displayColor && (dirtyBits & HdChangeTracker::DirtyColors)) {
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
    
    HdExtComputationUtils::ValueStore valueStore = 
        HdExtComputationUtils::GetComputedPrimvarValues(compPrimvarsToUpdate, sceneDelegate);

    TfTokenVector compPrimvarNames;
    for (auto const& compPrimvar : compPrimvarsToUpdate) {
        auto const it = valueStore.find(compPrimvar.name);
        if (it == valueStore.end() || it->second.IsEmpty()) {
            continue;
        }
        
        compPrimvarNames.emplace_back(compPrimvar.name);
        const VtValue& val = it->second;

        if (compPrimvar.name == HdTokens->points) {
            if (val.IsHolding<VtVec3fArray>()) {
                _points = val.UncheckedGet<VtVec3fArray>();
                _bvhDirty = true;
            } else if (val.IsHolding<VtVec3dArray>()) {
                const VtVec3dArray& pointsd = val.UncheckedGet<VtVec3dArray>();
                _points.resize(pointsd.size());
                for (size_t j = 0; j < pointsd.size(); ++j) _points[j] = GfVec3f(pointsd[j]);
                _bvhDirty = true;
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
    _materialId = sceneDelegate->GetMaterialId(id);
    
    if (HdChangeTracker::IsVisibilityDirty(*dirtyBits, id)) {
        _visible = sceneDelegate->GetVisible(id);
    }
    
    if (HdChangeTracker::IsTransformDirty(*dirtyBits, id)) {
        _transform = GfMatrix4f(sceneDelegate->GetTransform(id));
    }

    // --- COMPUTED PRIMVARS ---
    // We check for all computed primvars first.
    {
        for (size_t i=0; i < HdInterpolationCount; ++i) {
            HdInterpolation interp = static_cast<HdInterpolation>(i);
            HdExtComputationPrimvarDescriptorVector compPrimvars = 
                sceneDelegate->GetExtComputationPrimvarDescriptors(id, interp);
            for (auto const& pv: compPrimvars) {
                std::cout << "[Gemini] Found computed PV descriptor: " << pv.name.GetText() 
                          << " interp: " << interp << " dirty: " 
                          << HdChangeTracker::IsPrimvarDirty(*dirtyBits, id, pv.name) << std::endl;
            }
        }
    }

    TfTokenVector computedNames = _UpdateComputedPrimvarSources(sceneDelegate, *dirtyBits);
    bool pointsUpdatedByComputation = false;
    bool colorsUpdatedByComputation = false;
    for (const auto& name : computedNames) {
        if (name == HdTokens->points) pointsUpdatedByComputation = true;
        if (name == HdTokens->displayColor) colorsUpdatedByComputation = true;
    }

    // --- POINTS UPDATING ---
    // We update points if:
    // 1. The DirtyPoints bit is set
    // 2. The 'points' primvar is marked dirty
    // 3. We don't have any points yet
    bool pointsDirty = (*dirtyBits & HdChangeTracker::DirtyPoints) || 
                       HdChangeTracker::IsPrimvarDirty(*dirtyBits, id, HdTokens->points) ||
                       _points.empty();

    if (pointsDirty) {
        bool pointsActuallyUpdated = pointsUpdatedByComputation;

        // 1. Try standard "points" attribute if not already found in computed sources
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

        // 2. Last resort: BRUTE FORCE SCOUTING for anything that looks like points
        // (Sometimes points are named 'P' or provided in custom primvars)
        if (!pointsActuallyUpdated) {
            for (int i = 0; i < HdInterpolationCount; ++i) {
                HdPrimvarDescriptorVector pvs = sceneDelegate->GetPrimvarDescriptors(id, (HdInterpolation)i);
                for (const auto& pv : pvs) {
                    // Skip if we already checked 'points'
                    if (pv.name == HdTokens->points) continue;

                    VtValue val = sceneDelegate->Get(id, pv.name);
                    if (!val.IsEmpty()) {
                        if (val.IsHolding<VtVec3fArray>()) {
                            const auto& arr = val.UncheckedGet<VtVec3fArray>();
                            if (!arr.empty()) {
                                std::cout << "[Gemini]   Found potential points in PV " << pv.name.GetText() << " with " << arr.size() << " elements." << std::endl;
                                _points = arr;
                                pointsActuallyUpdated = true;
                                break;
                            }
                        } else if (val.IsHolding<VtVec3dArray>()) {
                            const auto& arr = val.UncheckedGet<VtVec3dArray>();
                            if (!arr.empty()) {
                                std::cout << "[Gemini]   Found potential points (double) in PV " << pv.name.GetText() << " with " << arr.size() << " elements." << std::endl;
                                _points.resize(arr.size());
                                for (size_t j = 0; j < arr.size(); ++j) _points[j] = GfVec3f(arr[j]);
                                pointsActuallyUpdated = true;
                                break;
                            }
                        }
                    }
                }
                if (pointsActuallyUpdated) break;
            }
        }

        if (pointsActuallyUpdated) {
            _bvhDirty = true;
        }
    }

    // --- COLOR UPDATING ---
    if (HdChangeTracker::IsPrimvarDirty(*dirtyBits, id, HdTokens->displayColor)) {
        if (!colorsUpdatedByComputation) {
            VtValue val = sceneDelegate->Get(id, HdTokens->displayColor);
            if (val.IsHolding<VtVec3fArray>()) {
                _colors = val.UncheckedGet<VtVec3fArray>();
            }
        }
    }

    if (HdChangeTracker::IsTopologyDirty(*dirtyBits, id)) {
        HdMeshTopology topology = GetMeshTopology(sceneDelegate);
        HdMeshUtil meshUtil(&topology, id);
        VtIntArray trianglePrimitiveParams;
        meshUtil.ComputeTriangleIndices(&_triangulatedIndices, &trianglePrimitiveParams);
        _bvhDirty = true;
    }

    if (_bvhDirty) {
        _range.SetEmpty();
        if (!_points.empty()) {
            for (const auto& p : _points) {
                _range.ExtendBy(p);
            }
            _bvh.Build(_points, _triangulatedIndices);
        } else {
            _bvh.Build(VtVec3fArray(), VtVec3iArray());
        }
        _bvhDirty = false;
    }

    if (_points.size() > 0) {
        std::cout << "[Gemini] Mesh " << id.GetText() << " synced. Points: " << _points.size() << " Triangles: " << _triangulatedIndices.size() << std::endl;
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
