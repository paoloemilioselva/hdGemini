#ifndef HD_GEMINI_MESH_H
#define HD_GEMINI_MESH_H

#include "pxr/pxr.h"
#include "pxr/imaging/hd/mesh.h"

PXR_NAMESPACE_OPEN_SCOPE

class HdGeminiMesh final : public HdMesh {
public:
    HdGeminiMesh(SdfPath const& id);
    virtual ~HdGeminiMesh() = default;

    virtual HdDirtyBits GetInitialDirtyBitsMask() const override;

    virtual void Sync(HdSceneDelegate* sceneDelegate,
                      HdRenderParam*   renderParam,
                      HdDirtyBits*     dirtyBits,
                      TfToken const    &reprToken) override;

    virtual void Finalize(HdRenderParam *renderParam) override;

protected:
    virtual void _InitRepr(TfToken const &reprToken,
                           HdDirtyBits *dirtyBits) override;

    virtual HdDirtyBits _PropagateDirtyBits(HdDirtyBits bits) const override;

private:
    HdGeminiMesh(const HdGeminiMesh&) = delete;
    HdGeminiMesh &operator =(const HdGeminiMesh&) = delete;
};

PXR_NAMESPACE_CLOSE_SCOPE

#endif // HD_GEMINI_MESH_H
