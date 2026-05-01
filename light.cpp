#include "light.h"
#include "renderDelegate.h"
#include "renderParam.h"
#include "pxr/imaging/hd/sceneDelegate.h"
#include "pxr/imaging/hd/tokens.h"

PXR_NAMESPACE_USING_DIRECTIVE

HdGeminiLight::HdGeminiLight(SdfPath const& id, TfToken const& lightType)
    : HdLight(id)
    , _color(1.0f)
    , _intensity(1.0f)
    , _transform(1.0)
    , _lightType(lightType)
{
}

void
HdGeminiLight::Sync(HdSceneDelegate *sceneDelegate,
                    HdRenderParam   *renderParam,
                    HdDirtyBits     *dirtyBits)
{
    SdfPath const& id = GetId();

    if (*dirtyBits & HdLight::DirtyTransform) {
        _transform = sceneDelegate->GetTransform(id);
    }
    
    if (*dirtyBits & HdLight::DirtyParams) {
        VtValue colorVal = sceneDelegate->GetLightParamValue(id, HdLightTokens->color);
        if (colorVal.IsHolding<GfVec3f>()) {
            _color = colorVal.UncheckedGet<GfVec3f>();
        }

        VtValue intensityVal = sceneDelegate->GetLightParamValue(id, HdLightTokens->intensity);
        if (intensityVal.IsHolding<float>()) {
            _intensity = intensityVal.UncheckedGet<float>();
        }

        if (_lightType == HdPrimTypeTokens->domeLight) {
            VtValue textureVal = sceneDelegate->GetLightParamValue(id, HdLightTokens->textureFile);
            if (textureVal.IsHolding<SdfAssetPath>()) {
                _textureFile = textureVal.UncheckedGet<SdfAssetPath>();
            }
        }
    }

    HdGeminiRenderParam *geminiRenderParam = static_cast<HdGeminiRenderParam*>(renderParam);
    geminiRenderParam->AcquireSceneForEdit();
    geminiRenderParam->GetRenderDelegate()->AddLight(id, this);

    *dirtyBits &= ~HdLight::AllDirty;
}

HdDirtyBits
HdGeminiLight::GetInitialDirtyBitsMask() const
{
    return HdLight::AllDirty;
}
