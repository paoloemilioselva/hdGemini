#include "renderSettings.h"
#include "renderDelegate.h"
#include "pxr/imaging/hd/sceneDelegate.h"
#include "pxr/imaging/hd/tokens.h"
#include "pxr/base/vt/dictionary.h"

PXR_NAMESPACE_OPEN_SCOPE

HdGeminiRenderSettings::HdGeminiRenderSettings(SdfPath const& id, HdGeminiRenderDelegate* delegate)
    : HdRenderSettings(id), _delegate(delegate)
{
}

HdGeminiRenderSettings::~HdGeminiRenderSettings() = default;

void
HdGeminiRenderSettings::_Sync(HdSceneDelegate* sceneDelegate,
                              HdRenderParam* renderParam,
                              const HdDirtyBits* dirtyBits)
{
    SdfPath const& id = GetId();

    std::cout << "[Gemini] Syncing RenderSettings bprim: " << id.GetText() << std::endl;

    if (*dirtyBits & HdRenderSettings::DirtyNamespacedSettings) {
        VtDictionary nsSettings = GetNamespacedSettings();
        for (const auto& kv : nsSettings) {
            std::string keyStr = kv.first;
            size_t colonIdx = keyStr.rfind(':');
            if (colonIdx != std::string::npos) {
                keyStr = keyStr.substr(colonIdx + 1);
            }
            _delegate->SetRenderSetting(TfToken(keyStr), kv.second);
        }
    }
}

PXR_NAMESPACE_CLOSE_SCOPE
