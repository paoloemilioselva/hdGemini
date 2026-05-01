#include "mesh.h"
#include "renderParam.h"
#include "renderDelegate.h"
#include "pxr/imaging/hd/meshUtil.h"
#include "pxr/imaging/hd/sceneDelegate.h"
#include "pxr/imaging/hd/tokens.h"
#include "pxr/imaging/hd/extComputationUtils.h"
#include "pxr/base/vt/value.h"

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

void
HdGeminiMesh::_UpdateComputedPrimvarSources(HdSceneDelegate* sceneDelegate,
                                            HdDirtyBits dirtyBits)
{
    const SdfPath& id = GetId();
    HdExtComputationPrimvarDescriptorVector compPrimvars =
        sceneDelegate->GetExtComputationPrimvarDescriptors(id, HdInterpolationVertex);

    if (compPrimvars.empty()) return;

    HdExtComputationUtils::ValueStore valueStore =
        HdExtComputationUtils::GetComputedPrimvarValues(compPrimvars, sceneDelegate);

    auto it = valueStore.find(HdTokens->points);
    if (it != valueStore.end()) {
        const VtValue& value = it->second;
        if (value.IsHolding<VtVec3fArray>()) {
            _points = value.UncheckedGet<VtVec3fArray>();
            _bvhDirty = true;
        } else if (value.IsHolding<VtVec3dArray>()) {
            const VtVec3dArray& pointsd = value.UncheckedGet<VtVec3dArray>();
            _points.resize(pointsd.size());
            for (size_t i = 0; i < pointsd.size(); ++i) {
                _points[i] = GfVec3f(pointsd[i]);
            }
            _bvhDirty = true;
        }
    }
}

void
HdGeminiMesh::Sync(HdSceneDelegate* sceneDelegate,
                   HdRenderParam*   renderParam,
                   HdDirtyBits*     dirtyBits,
                   TfToken const   &reprToken)
{
    HdGeminiRenderParam *geminiRenderParam = static_cast<HdGeminiRenderParam*>(renderParam);
    geminiRenderParam->AcquireSceneForEdit();
    geminiRenderParam->GetRenderDelegate()->AddMesh(GetId(), this);

    const SdfPath& id = GetId();
    _instancerId = sceneDelegate->GetInstancerId(id);
    
    if (HdChangeTracker::IsVisibilityDirty(*dirtyBits, id)) {
        _visible = sceneDelegate->GetVisible(id);
    }
    
    if (HdChangeTracker::IsTransformDirty(*dirtyBits, id)) {
        _transform = GfMatrix4f(sceneDelegate->GetTransform(id));
    }

    // Process computed primvars first (e.g., CPU skinning)
    _UpdateComputedPrimvarSources(sceneDelegate, *dirtyBits);

    if (HdChangeTracker::IsPrimvarDirty(*dirtyBits, id, HdTokens->points)) {
        VtValue value = sceneDelegate->Get(id, HdTokens->points);
        if (value.IsHolding<VtVec3fArray>()) {
            _points = value.UncheckedGet<VtVec3fArray>();
            _bvhDirty = true;
        } else if (value.IsHolding<VtVec3dArray>()) {
            const VtVec3dArray& pointsd = value.UncheckedGet<VtVec3dArray>();
            _points.resize(pointsd.size());
            for (size_t i = 0; i < pointsd.size(); ++i) {
                _points[i] = GfVec3f(pointsd[i]);
            }
            _bvhDirty = true;
        }
    }

    if (_bvhDirty) {
        _range.SetEmpty();
        for (const auto& p : _points) {
            _range.ExtendBy(p);
        }
    }

    if (HdChangeTracker::IsPrimvarDirty(*dirtyBits, id, HdTokens->displayColor)) {
        VtValue value = sceneDelegate->Get(id, HdTokens->displayColor);
        if (value.IsHolding<VtVec3fArray>()) {
            _colors = value.UncheckedGet<VtVec3fArray>();
        }
    }

    if (HdChangeTracker::IsTopologyDirty(*dirtyBits, id)) {
        HdMeshTopology topology = GetMeshTopology(sceneDelegate);
        HdMeshUtil meshUtil(&topology, id);
        VtIntArray trianglePrimitiveParams;
        meshUtil.ComputeTriangleIndices(&_triangulatedIndices, &trianglePrimitiveParams);
        _bvhDirty = true;
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
