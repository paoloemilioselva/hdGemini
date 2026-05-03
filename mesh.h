#ifndef HD_GEMINI_MESH_H
#define HD_GEMINI_MESH_H

#include "pxr/pxr.h"
#include "pxr/imaging/hd/mesh.h"
#include "pxr/base/vt/array.h"
#include "pxr/base/gf/matrix4f.h"
#include "pxr/base/gf/vec3f.h"
#include "pxr/base/gf/vec3i.h"
#include "bvh.h"
#include <string>
#include <vector>
#include <memory>

PXR_NAMESPACE_USING_DIRECTIVE

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

    const VtVec3fArray& GetPoints() const { return _points; }
    const VtVec3iArray& GetIndices() const { return _triangulatedIndices; }
    const GfMatrix4f& GetTransform() const { return _transform; }
    const GfRange3f& GetRange() const { return _range; }
    const BVH& GetBVH() const { return _bvh; }
    const SdfPath& GetInstancerId() const { return _instancerId; }
    const SdfPath& GetMaterialId() const { return _materialId; }
    bool IsVisible() const { return _visible; }
    const VtVec3fArray& GetColors() const { return _colors; }
    const VtVec2fArray& GetUVs() const { return _uvs; }

protected:
    virtual void _InitRepr(TfToken const &reprToken,
                           HdDirtyBits *dirtyBits) override;

    virtual HdDirtyBits _PropagateDirtyBits(HdDirtyBits bits) const override;

private:
    TfTokenVector _UpdateComputedPrimvarSources(HdSceneDelegate* sceneDelegate,
                                                HdDirtyBits dirtyBits);

    VtVec3fArray _points;
    VtVec3iArray _triangulatedIndices;
    GfMatrix4f _transform;
    GfRange3f _range;
    BVH _bvh;
    SdfPath _instancerId;
    SdfPath _materialId;
    bool _visible;
    bool _bvhDirty;
    VtVec3fArray _colors;
    VtVec2fArray _uvs;

    HdGeminiMesh(const HdGeminiMesh&) = delete;
    HdGeminiMesh &operator =(const HdGeminiMesh&) = delete;
};

#endif // HD_GEMINI_MESH_H
