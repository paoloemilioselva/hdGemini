#ifndef HD_GEMINI_IMPLICIT_SURFACE_SCENE_INDEX_PLUGIN_H
#define HD_GEMINI_IMPLICIT_SURFACE_SCENE_INDEX_PLUGIN_H

#include "pxr/pxr.h"
#include "pxr/imaging/hd/sceneIndexPlugin.h"

PXR_NAMESPACE_USING_DIRECTIVE

class HdGemini_ImplicitSurfaceSceneIndexPlugin : public HdSceneIndexPlugin
{
public:
    HdGemini_ImplicitSurfaceSceneIndexPlugin();

protected:
    HdSceneIndexBaseRefPtr _AppendSceneIndex(
        const HdSceneIndexBaseRefPtr &inputScene,
        const HdContainerDataSourceHandle &inputArgs) override;
};

#endif // HD_GEMINI_IMPLICIT_SURFACE_SCENE_INDEX_PLUGIN_H
