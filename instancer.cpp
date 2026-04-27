#include "instancer.h"
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
    return VtMatrix4dArray();
}
