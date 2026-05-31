#ifndef HD_GEMINI_RENDER_SETTINGS_H
#define HD_GEMINI_RENDER_SETTINGS_H

#include "pxr/pxr.h"
#include "pxr/imaging/hd/bprim.h"

class HdGeminiRenderDelegate;

PXR_NAMESPACE_OPEN_SCOPE

class HdGeminiRenderSettings : public HdBprim {
public:
    HdGeminiRenderSettings(SdfPath const& id, HdGeminiRenderDelegate* delegate);
    virtual ~HdGeminiRenderSettings();

    virtual void Sync(HdSceneDelegate* sceneDelegate,
                      HdRenderParam* renderParam,
                      HdDirtyBits* dirtyBits) override;

    virtual HdDirtyBits GetInitialDirtyBitsMask() const override;
    
    virtual void Finalize(HdRenderParam* renderParam) override;

private:
    HdGeminiRenderDelegate* _delegate;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // HD_GEMINI_RENDER_SETTINGS_H
