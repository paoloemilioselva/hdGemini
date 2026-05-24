#ifndef HD_GEMINI_CURVES_H
#define HD_GEMINI_CURVES_H

#include "pxr/pxr.h"
#include "pxr/imaging/hd/basisCurves.h"
#include "pxr/base/vt/array.h"
#include "pxr/base/gf/matrix4f.h"
#include "pxr/base/gf/vec3f.h"
#include "bvh.h"
#include <vector>

PXR_NAMESPACE_USING_DIRECTIVE

class HdGeminiBasisCurves final : public HdBasisCurves {
public:
    struct Subset {
        SdfPath materialId;
        VtIntArray curveVertexCounts;
        VtIntArray indices; // Can be empty if not indexed
        BVH bvh;
        GfRange3f range;
    };

    HdGeminiBasisCurves(SdfPath const& id);
    virtual ~HdGeminiBasisCurves() = default;

    virtual HdDirtyBits GetInitialDirtyBitsMask() const override;

    virtual void Sync(HdSceneDelegate* sceneDelegate,
                      HdRenderParam*   renderParam,
                      HdDirtyBits*     dirtyBits,
                      TfToken const    &reprToken) override;

    virtual void Finalize(HdRenderParam *renderParam) override;

    const VtVec3fArray& GetPoints() const { return _points; }
    const VtFloatArray& GetWidths() const { return _widths; }
    const VtVec3fArray& GetNormals() const { return _normals; }
    const GfMatrix4f& GetTransform() const { return _transform; }
    bool IsVisible() const { return _visible; }
    
    const std::vector<Subset>& GetSubsets() const { return _subsets; }
    const SdfPath& GetInstancerId() const { return _instancerId; }

protected:
    virtual void _InitRepr(TfToken const &reprToken,
                           HdDirtyBits *dirtyBits) override;

    virtual HdDirtyBits _PropagateDirtyBits(HdDirtyBits bits) const override;

private:
    VtVec3fArray _points;
    VtFloatArray _widths;
    VtVec3fArray _normals;
    GfMatrix4f _transform;
    SdfPath _instancerId;
    std::vector<Subset> _subsets;
    bool _visible;

    HdGeminiBasisCurves(const HdGeminiBasisCurves&) = delete;
    HdGeminiBasisCurves &operator =(const HdGeminiBasisCurves&) = delete;
};

#endif // HD_GEMINI_CURVES_H
