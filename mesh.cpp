#include "mesh.h"
#include "renderParam.h"
#include "renderDelegate.h"
#include "pxr/imaging/hd/meshUtil.h"
#include "pxr/imaging/hd/sceneDelegate.h"
#include "pxr/imaging/hd/tokens.h"
#include "pxr/base/vt/value.h"

PXR_NAMESPACE_USING_DIRECTIVE

HdGeminiMesh::HdGeminiMesh(SdfPath const& id)
    : HdMesh(id)
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
    
    if (HdChangeTracker::IsTransformDirty(*dirtyBits, id)) {
        _transform = GfMatrix4f(sceneDelegate->GetTransform(id));
    }

    if (HdChangeTracker::IsPrimvarDirty(*dirtyBits, id, HdTokens->points)) {
        VtValue value = sceneDelegate->Get(id, HdTokens->points);
        _points = value.Get<VtVec3fArray>();
        _range.SetEmpty();
        for (const auto& p : _points) {
            _range.ExtendBy(p);
        }
    }

    if (HdChangeTracker::IsTopologyDirty(*dirtyBits, id)) {
        HdMeshTopology topology = GetMeshTopology(sceneDelegate);
        HdMeshUtil meshUtil(&topology, id);
        VtIntArray trianglePrimitiveParams;
        meshUtil.ComputeTriangleIndices(&_triangulatedIndices, &trianglePrimitiveParams);
        _bvh.Build(_points, _triangulatedIndices);
    } else if (HdChangeTracker::IsPrimvarDirty(*dirtyBits, id, HdTokens->points)) {
        // If points changed but topology didn't, we still need to rebuild BVH
        _bvh.Build(_points, _triangulatedIndices);
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
