#include "instancer.h"

PXR_NAMESPACE_OPEN_SCOPE

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
    return VtMatrix4dArray();
}

PXR_NAMESPACE_CLOSE_SCOPE
