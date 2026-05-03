#include "light.h"
#include "renderDelegate.h"
#include "renderParam.h"
#include "pxr/imaging/hd/sceneDelegate.h"
#include "pxr/imaging/hd/tokens.h"

#include <cmath>

PXR_NAMESPACE_USING_DIRECTIVE

HdGeminiLight::HdGeminiLight(SdfPath const& id, TfToken const& lightType)
    : HdLight(id)
    , _color(1.0f)
    , _intensity(1.0f)
    , _exposure(0.0f)
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

        VtValue exposureVal = sceneDelegate->GetLightParamValue(id, HdLightTokens->exposure);
        if (exposureVal.IsHolding<float>()) {
            _exposure = exposureVal.UncheckedGet<float>();
        }

        VtValue coneAngleVal = sceneDelegate->GetLightParamValue(id, TfToken("shaping:cone:angle"));
        if (coneAngleVal.IsHolding<float>()) {
            _shapingConeAngle = coneAngleVal.UncheckedGet<float>();
        }

        VtValue coneSoftnessVal = sceneDelegate->GetLightParamValue(id, TfToken("shaping:cone:softness"));
        if (coneSoftnessVal.IsHolding<float>()) {
            _shapingConeSoftness = coneSoftnessVal.UncheckedGet<float>();
        }

        if (_lightType == HdPrimTypeTokens->domeLight) {
            VtValue textureVal = sceneDelegate->GetLightParamValue(id, HdLightTokens->textureFile);
            if (textureVal.IsHolding<SdfAssetPath>()) {
                _textureFile = textureVal.UncheckedGet<SdfAssetPath>();
            }
        } else if (_lightType == HdPrimTypeTokens->rectLight) {
            VtValue widthVal = sceneDelegate->GetLightParamValue(id, HdLightTokens->width);
            if (widthVal.IsHolding<float>()) {
                _width = widthVal.UncheckedGet<float>();
            }
            VtValue heightVal = sceneDelegate->GetLightParamValue(id, HdLightTokens->height);
            if (heightVal.IsHolding<float>()) {
                _height = heightVal.UncheckedGet<float>();
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
