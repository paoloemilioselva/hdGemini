#include "mesh.h"
#include "renderParam.h"
#include "pxr/imaging/hd/meshUtil.h"
#include "pxr/imaging/hd/sceneDelegate.h"

PXR_NAMESPACE_OPEN_SCOPE

HdGeminiMesh::HdGeminiMesh(SdfPath const& id)
    : HdMesh(id)
{
}

void
HdGeminiMesh::Finalize(HdRenderParam *renderParam)
{
}

HdDirtyBits
HdGeminiMesh::GetInitialDirtyBitsMask() const
{
    return HdChangeTracker::AllSceneDirtyBits;
}

void
HdGeminiMesh::Sync(HdSceneDelegate *sceneDelegate,
                   HdRenderParam   *renderParam,
                   HdDirtyBits     *dirtyBits,
                   TfToken const   &reprToken)
{
    HdGeminiRenderParam *geminiRenderParam = static_cast<HdGeminiRenderParam*>(renderParam);
    geminiRenderParam->AcquireSceneForEdit();

    const SdfPath& id = GetId();
    if (HdChangeTracker::IsTopologyDirty(*dirtyBits, id)) {
        // Handle topology update
    }
    if (HdChangeTracker::IsPrimvarDirty(*dirtyBits, id, HdTokens->points)) {
        // Handle points update
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

PXR_NAMESPACE_CLOSE_SCOPE
