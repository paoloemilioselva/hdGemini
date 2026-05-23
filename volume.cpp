#include "volume.h"
#include "pxr/imaging/hd/sceneDelegate.h"
#include "renderDelegate.h"
#include "renderParam.h"

PXR_NAMESPACE_USING_DIRECTIVE

HdGeminiVolume::HdGeminiVolume(SdfPath const& id)
    : HdVolume(id)
{
}

HdDirtyBits HdGeminiVolume::GetInitialDirtyBitsMask() const {
    return HdChangeTracker::DirtyTopology |
           HdChangeTracker::DirtyTransform |
           HdChangeTracker::DirtyExtent |
           HdChangeTracker::DirtyPrimvar |
           HdChangeTracker::DirtyMaterialId;
}

void HdGeminiVolume::Sync(HdSceneDelegate *sceneDelegate,
                          HdRenderParam   *renderParam,
                          HdDirtyBits     *dirtyBits,
                          TfToken const   &reprToken)
{
    SdfPath const& id = GetId();

    if (*dirtyBits & HdChangeTracker::DirtyExtent) {
        _extents = sceneDelegate->GetExtent(id);
    }

    if (*dirtyBits & HdChangeTracker::DirtyTransform) {
        _transform = sceneDelegate->GetTransform(id);
    }

    if (*dirtyBits & HdChangeTracker::DirtyTopology) {
        HdVolumeFieldDescriptorVector fields = sceneDelegate->GetVolumeFieldDescriptors(id);
        _fields.clear();
        for (const auto& fieldDesc : fields) {
            // Retrieve the Bprim (HdGeminiField) from the render index
            HdGeminiField* field = static_cast<HdGeminiField*>(
                sceneDelegate->GetRenderIndex().GetBprim(fieldDesc.fieldPrimType, fieldDesc.fieldId)
            );
            if (field) {
                _fields[fieldDesc.fieldName] = field;
            }
        }
    }
    
    HdGeminiRenderParam *geminiRenderParam = static_cast<HdGeminiRenderParam*>(renderParam);
    if (*dirtyBits & HdChangeTracker::DirtyTransform || *dirtyBits & HdChangeTracker::DirtyTopology) {
        geminiRenderParam->GetRenderDelegate()->AddVolume(id, this);
    }
    
    *dirtyBits = HdChangeTracker::Clean;
}

void
HdGeminiVolume::Finalize(HdRenderParam *renderParam)
{
    HdGeminiRenderParam *geminiRenderParam = static_cast<HdGeminiRenderParam*>(renderParam);
    geminiRenderParam->GetRenderDelegate()->RemoveVolume(GetId());
}

HdDirtyBits
HdGeminiVolume::_PropagateDirtyBits(HdDirtyBits bits) const
{
    return bits;
}

void
HdGeminiVolume::_InitRepr(TfToken const &reprToken, HdDirtyBits *dirtyBits)
{
    // Volumes don't need repr initialization
}
