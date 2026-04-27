#ifndef HD_GEMINI_RENDERER_PLUGIN_H
#define HD_GEMINI_RENDERER_PLUGIN_H

#include "pxr/pxr.h"
#include "pxr/imaging/hd/rendererPlugin.h"

PXR_NAMESPACE_OPEN_SCOPE

class HdGeminiRendererPlugin final : public HdRendererPlugin
{
public:
    HdGeminiRendererPlugin() = default;
    virtual ~HdGeminiRendererPlugin() = default;

    virtual HdRenderDelegate *CreateRenderDelegate() override;
    virtual HdRenderDelegate *CreateRenderDelegate(
        HdRenderSettingsMap const& settingsMap) override;

    virtual void DeleteRenderDelegate(
        HdRenderDelegate *renderDelegate) override;

    virtual bool IsSupported(bool glEnabled = true) const override;

private:
    HdGeminiRendererPlugin(const HdGeminiRendererPlugin&) = delete;
    HdGeminiRendererPlugin &operator =(const HdGeminiRendererPlugin&) = delete;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // HD_GEMINI_RENDERER_PLUGIN_H
