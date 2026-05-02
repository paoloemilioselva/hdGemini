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
    HdExtComputationPrimvarDescriptorVector dirtyCompPrimvars;
    for (size_t i=0; i < HdInterpolationCount; ++i) {
        HdInterpolation interp = static_cast<HdInterpolation>(i);
        HdExtComputationPrimvarDescriptorVector compPrimvars = 
            sceneDelegate->GetExtComputationPrimvarDescriptors(id, interp);

        for (auto const& pv: compPrimvars) {
            if (HdChangeTracker::IsPrimvarDirty(dirtyBits, id, pv.name)) {
                dirtyCompPrimvars.emplace_back(pv);
            }
        }
    }

    if (dirtyCompPrimvars.empty()) {
        return TfTokenVector();
    }
    
    HdExtComputationUtils::ValueStore valueStore = 
        HdExtComputationUtils::GetComputedPrimvarValues(dirtyCompPrimvars, sceneDelegate);

    TfTokenVector compPrimvarNames;
    for (auto const& compPrimvar : dirtyCompPrimvars) {
        auto const it = valueStore.find(compPrimvar.name);
        if (it == valueStore.end()) {
            continue;
        }
        
        compPrimvarNames.emplace_back(compPrimvar.name);
        if (compPrimvar.name == HdTokens->points) {
            const VtValue& val = it->second;
            if (val.IsHolding<VtVec3fArray>()) {
                _points = val.UncheckedGet<VtVec3fArray>();
                _bvhDirty = true;
            } else if (val.IsHolding<VtVec3dArray>()) {
                const VtVec3dArray& pointsd = val.UncheckedGet<VtVec3dArray>();
                _points.resize(pointsd.size());
                for (size_t j = 0; j < pointsd.size(); ++j) _points[j] = GfVec3f(pointsd[j]);
                _bvhDirty = true;
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

    // --- BRUTE FORCE SCOUTING ---
    _points.clear();
    bool pointsUpdated = false;

    // First try all computed sources
    _UpdateComputedPrimvarSources(sceneDelegate, *dirtyBits);
    if (!_points.empty()) pointsUpdated = true;

    // Then try every single primvar on the mesh
    if (!pointsUpdated) {
        for (int i = 0; i < HdInterpolationCount; ++i) {
            HdPrimvarDescriptorVector pvs = sceneDelegate->GetPrimvarDescriptors(id, (HdInterpolation)i);
            for (const auto& pv : pvs) {
                VtValue val = sceneDelegate->Get(id, pv.name);
                if (!val.IsEmpty()) {
                    if (val.IsHolding<VtVec3fArray>()) {
                        const auto& arr = val.UncheckedGet<VtVec3fArray>();
                        if (!arr.empty()) {
                            std::cout << "[Gemini]   Found PV " << pv.name.GetText() << " with " << arr.size() << " points!" << std::endl;
                            _points = arr;
                            pointsUpdated = true;
                            break;
                        }
                    } else if (val.IsHolding<VtVec3dArray>()) {
                        const auto& arr = val.UncheckedGet<VtVec3dArray>();
                        if (!arr.empty()) {
                            std::cout << "[Gemini]   Found PV " << pv.name.GetText() << " (double) with " << arr.size() << " points!" << std::endl;
                            _points.resize(arr.size());
                            for (size_t j = 0; j < arr.size(); ++j) _points[j] = GfVec3f(arr[j]);
                            pointsUpdated = true;
                            break;
                        }
                    }
                }
            }
            if (pointsUpdated) break;
        }
    }

    _bvhDirty = true;

    if (HdChangeTracker::IsPrimvarDirty(*dirtyBits, id, HdTokens->displayColor)) {
        VtValue val = sceneDelegate->Get(id, HdTokens->displayColor);
        if (val.IsHolding<VtVec3fArray>()) {
            _colors = val.UncheckedGet<VtVec3fArray>();
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
