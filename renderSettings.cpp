#include "renderSettings.h"
#include "renderDelegate.h"
#include "pxr/imaging/hd/sceneDelegate.h"
#include "pxr/imaging/hd/tokens.h"
#include "pxr/base/vt/dictionary.h"

PXR_NAMESPACE_OPEN_SCOPE

HdGeminiRenderSettings::HdGeminiRenderSettings(SdfPath const& id, HdGeminiRenderDelegate* delegate)
    : HdBprim(id), _delegate(delegate)
{
}

HdGeminiRenderSettings::~HdGeminiRenderSettings() = default;

void
HdGeminiRenderSettings::Sync(HdSceneDelegate* sceneDelegate,
                             HdRenderParam* renderParam,
                             HdDirtyBits* dirtyBits)
{
    SdfPath const& id = GetId();

    VtValue nsSettingsValue = sceneDelegate->Get(id, HdRenderSettingsPrimTokens->namespacedSettings);
    if (nsSettingsValue.IsHolding<VtDictionary>()) {
        VtDictionary nsSettings = nsSettingsValue.UncheckedGet<VtDictionary>();
        for (const auto& kv : nsSettings) {
            _delegate->SetRenderSetting(TfToken(kv.first), kv.second);
        }
    }
    
    *dirtyBits = HdChangeTracker::Clean;
}

HdDirtyBits
HdGeminiRenderSettings::GetInitialDirtyBitsMask() const
{
    // Need a bit that will trigger sync, use AllDirty or similar
    return HdChangeTracker::AllDirty;
}

void
HdGeminiRenderSettings::Finalize(HdRenderParam* renderParam)
{
}

PXR_NAMESPACE_CLOSE_SCOPE
