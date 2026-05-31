#ifndef HD_GEMINI_RENDER_SETTINGS_H
#define HD_GEMINI_RENDER_SETTINGS_H

#include "pxr/pxr.h"
#include "pxr/imaging/hd/renderSettings.h"

class HdGeminiRenderDelegate;

PXR_NAMESPACE_OPEN_SCOPE

class HdGeminiRenderSettings : public HdRenderSettings {
public:
    HdGeminiRenderSettings(SdfPath const& id, HdGeminiRenderDelegate* delegate);
    virtual ~HdGeminiRenderSettings();

protected:
    virtual void _Sync(HdSceneDelegate* sceneDelegate,
                       HdRenderParam* renderParam,
                       const HdDirtyBits* dirtyBits) override;

private:
    HdGeminiRenderDelegate* _delegate;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // HD_GEMINI_RENDER_SETTINGS_H
