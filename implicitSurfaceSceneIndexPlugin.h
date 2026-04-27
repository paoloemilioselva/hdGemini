#ifndef HD_GEMINI_IMPLICIT_SURFACE_SCENE_INDEX_PLUGIN_H
#define HD_GEMINI_IMPLICIT_SURFACE_SCENE_INDEX_PLUGIN_H

#include "pxr/pxr.h"
#include "pxr/imaging/hd/sceneIndexPlugin.h"

PXR_NAMESPACE_OPEN_SCOPE

class HdGemini_ImplicitSurfaceSceneIndexPlugin : public HdSceneIndexPlugin
{
public:
    HdGemini_ImplicitSurfaceSceneIndexPlugin();

protected:
    HdSceneIndexBaseRefPtr _AppendSceneIndex(
        const HdSceneIndexBaseRefPtr &inputScene,
        const HdContainerDataSourceHandle &inputArgs) override;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // HD_GEMINI_IMPLICIT_SURFACE_SCENE_INDEX_PLUGIN_H
