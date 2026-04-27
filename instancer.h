#ifndef HD_GEMINI_INSTANCER_H
#define HD_GEMINI_INSTANCER_H

#include "pxr/pxr.h"
#include "pxr/imaging/hd/instancer.h"

PXR_NAMESPACE_OPEN_SCOPE

class HdGeminiInstancer final : public HdInstancer {
public:
    HdGeminiInstancer(HdSceneDelegate* delegate, SdfPath const& id);
    virtual ~HdGeminiInstancer() = default;

    virtual void Sync(HdSceneDelegate *sceneDelegate,
                      HdRenderParam   *renderParam,
                      HdDirtyBits     *dirtyBits) override;

    VtMatrix4dArray ComputeInstanceTransforms(SdfPath const &prototypeId);
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // HD_GEMINI_INSTANCER_H
