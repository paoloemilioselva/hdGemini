#ifndef HD_GEMINI_MESH_H
#define HD_GEMINI_MESH_H

#include "pxr/pxr.h"
#include "pxr/imaging/hd/mesh.h"
#include "pxr/base/vt/array.h"
#include "pxr/base/gf/matrix4f.h"
#include "pxr/base/gf/vec3f.h"
#include "pxr/base/gf/vec3i.h"
#include "bvh.h"
#include "ocean.h"
#include <string>
#include <vector>
#include <memory>

PXR_NAMESPACE_USING_DIRECTIVE

class HdGeminiMesh final : public HdMesh {
public:
    struct Subset {
        SdfPath materialId;
        VtVec3iArray indices;
        BVH bvh;
        GfRange3f range;
        VtVec2fArray uvs;
        VtVec3fArray colors;
    };

    HdGeminiMesh(SdfPath const& id);
    virtual ~HdGeminiMesh() = default;

    virtual HdDirtyBits GetInitialDirtyBitsMask() const override;

    virtual void Sync(HdSceneDelegate* sceneDelegate,
                      HdRenderParam*   renderParam,
                      HdDirtyBits*     dirtyBits,
                      TfToken const    &reprToken) override;

    virtual void Finalize(HdRenderParam *renderParam) override;

    const VtVec3fArray& GetPoints() const { return _points; }
    const GfMatrix4f& GetTransform() const { return _transform; }
    bool IsVisible() const { return _visible; }
    const VtVec3fArray& GetColors() const { return _colors; }
    const VtVec2fArray& GetUVs() const { return _uvs; }
    const VtVec3fArray& GetNormals() const { return _normals; }
    
    const std::vector<Subset>& GetSubsets() const { return _subsets; }
    const SdfPath& GetInstancerId() const { return _instancerId; }

    bool IsOcean() const { return _isOcean; }
    const HdGeminiOceanParams& GetOceanParams() const { return _oceanParams; }
    
    class HdGeminiOcean* GetOceanSimulator() { return _oceanSimulator.get(); }
    void UpdateOcean(const GfMatrix4f& viewProj, const GfVec3f& cameraPos, float time);

protected:
    virtual void _InitRepr(TfToken const &reprToken,
                           HdDirtyBits *dirtyBits) override;

    virtual HdDirtyBits _PropagateDirtyBits(HdDirtyBits bits) const override;

private:
    TfTokenVector _UpdateComputedPrimvarSources(HdSceneDelegate* sceneDelegate,
                                                HdDirtyBits dirtyBits);

    VtVec3fArray _points;
    GfMatrix4f _transform;
    SdfPath _instancerId;
    std::vector<Subset> _subsets;
    bool _visible;
    bool _subsetsDirty;
    
    bool _isOcean = false;
    HdGeminiOceanParams _oceanParams;
    uint32_t _authoredOceanPrimvars = 0;
    std::unique_ptr<HdGeminiOcean> _oceanSimulator;
    
    VtVec3fArray _colors;
    VtVec2fArray _uvs;
    VtVec3fArray _normals;

    HdInterpolation _colorInterp = HdInterpolationConstant;
    HdInterpolation _uvInterp = HdInterpolationConstant;
    HdInterpolation _normalInterp = HdInterpolationConstant;

    HdGeminiMesh(const HdGeminiMesh&) = delete;
    HdGeminiMesh &operator =(const HdGeminiMesh&) = delete;
};

#endif // HD_GEMINI_MESH_H
