#include "rendererPlugin.h"
#include "renderDelegate.h"
#include "pxr/imaging/hd/rendererPluginRegistry.h"

PXR_NAMESPACE_OPEN_SCOPE

TF_REGISTRY_FUNCTION(TfType)
{
    HdRendererPluginRegistry::Define<HdGeminiRendererPlugin>();
}

HdRenderDelegate*
HdGeminiRendererPlugin::CreateRenderDelegate()
{
    return new HdGeminiRenderDelegate();
}

HdRenderDelegate*
HdGeminiRendererPlugin::CreateRenderDelegate(
    HdRenderSettingsMap const& settingsMap)
{
    return new HdGeminiRenderDelegate(settingsMap);
}

void
HdGeminiRendererPlugin::DeleteRenderDelegate(HdRenderDelegate *renderDelegate)
{
    delete renderDelegate;
}

bool 
HdGeminiRendererPlugin::IsSupported(bool /* glEnabled */) const
{
    return true;
}

PXR_NAMESPACE_CLOSE_SCOPE
