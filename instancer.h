#ifndef HD_GEMINI_INSTANCER_H
#define HD_GEMINI_INSTANCER_H

#include "pxr/pxr.h"
#include "pxr/imaging/hd/instancer.h"
#include "pxr/base/vt/array.h"
#include "pxr/base/gf/matrix4d.h"

PXR_NAMESPACE_USING_DIRECTIVE

class HdGeminiInstancer final : public HdInstancer {
public:
    HdGeminiInstancer(HdSceneDelegate* delegate, SdfPath const& id);
    virtual ~HdGeminiInstancer() = default;

    virtual void Sync(HdSceneDelegate *sceneDelegate,
                      HdRenderParam   *renderParam,
                      HdDirtyBits     *dirtyBits) override;

    VtMatrix4dArray ComputeInstanceTransforms(SdfPath const &prototypeId);
};

#endif // HD_GEMINI_INSTANCER_H
