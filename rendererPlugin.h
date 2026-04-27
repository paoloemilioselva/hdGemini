#ifndef HD_GEMINI_RENDERER_PLUGIN_H
#define HD_GEMINI_RENDERER_PLUGIN_H

#include "pxr/pxr.h"
#include "pxr/imaging/hd/rendererPlugin.h"

PXR_NAMESPACE_USING_DIRECTIVE

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

    virtual bool IsSupported(HdRendererCreateArgs const& createArgs,
                             std::string *reasonWhyNot = nullptr) const override;

private:
    HdGeminiRendererPlugin(const HdGeminiRendererPlugin&) = delete;
    HdGeminiRendererPlugin &operator =(const HdGeminiRendererPlugin&) = delete;
};

#endif // HD_GEMINI_RENDERER_PLUGIN_H
