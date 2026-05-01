#include "instancer.h"
#include "pxr/imaging/hd/sceneDelegate.h"
#include "pxr/imaging/hd/renderIndex.h"
#include "pxr/base/vt/array.h"
#include "pxr/base/gf/matrix4d.h"

PXR_NAMESPACE_USING_DIRECTIVE

HdGeminiInstancer::HdGeminiInstancer(HdSceneDelegate* delegate, SdfPath const& id)
    : HdInstancer(delegate, id)
{
}

void
HdGeminiInstancer::Sync(HdSceneDelegate *sceneDelegate,
                        HdRenderParam   *renderParam,
                        HdDirtyBits     *dirtyBits)
{
    _UpdateInstancer(sceneDelegate, dirtyBits);
}

VtMatrix4dArray
HdGeminiInstancer::ComputeInstanceTransforms(SdfPath const &prototypeId)
{
    VtMatrix4dArray transforms = GetDelegate()->GetInstancerInstanceTransforms(GetId(), prototypeId);
    
    SdfPath parentInstancerId = GetParentId();
    if (!parentInstancerId.IsEmpty()) {
        HdGeminiInstancer* parentInstancer = static_cast<HdGeminiInstancer*>(
            GetDelegate()->GetRenderIndex().GetInstancer(parentInstancerId));
        if (parentInstancer) {
            VtMatrix4dArray parentTransforms = parentInstancer->ComputeInstanceTransforms(GetId());
            VtMatrix4dArray newTransforms;
            newTransforms.reserve(transforms.size() * parentTransforms.size());
            for (const auto& pt : parentTransforms) {
                for (const auto& t : transforms) {
                    newTransforms.push_back(t * pt);
                }
            }
            transforms = newTransforms;
        }
    }
    return transforms;
}
