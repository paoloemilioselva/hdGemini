#include "light.h"

PXR_NAMESPACE_USING_DIRECTIVE

HdGeminiLight::HdGeminiLight(SdfPath const& id, TfToken const& lightType)
    : HdLight(id)
{
}

void
HdGeminiLight::Sync(HdSceneDelegate *sceneDelegate,
                    HdRenderParam   *renderParam,
                    HdDirtyBits     *dirtyBits)
{
    *dirtyBits &= ~HdLight::AllDirty;
}

HdDirtyBits
HdGeminiLight::GetInitialDirtyBitsMask() const
{
    return HdLight::AllDirty;
}
