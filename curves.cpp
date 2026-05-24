#include "curves.h"
#include "renderParam.h"
#include "renderDelegate.h"
#include "pxr/imaging/hd/sceneDelegate.h"
#include "pxr/imaging/hd/tokens.h"

PXR_NAMESPACE_USING_DIRECTIVE

HdGeminiBasisCurves::HdGeminiBasisCurves(SdfPath const& id)
    : HdBasisCurves(id)
    , _visible(true)
{
}

void
HdGeminiBasisCurves::Finalize(HdRenderParam *renderParam)
{
    HdGeminiRenderParam *geminiRenderParam = static_cast<HdGeminiRenderParam*>(renderParam);
    geminiRenderParam->GetRenderDelegate()->RemoveBasisCurves(GetId());
}

HdDirtyBits
HdGeminiBasisCurves::GetInitialDirtyBitsMask() const
{
    return HdChangeTracker::AllSceneDirtyBits;
}

void
HdGeminiBasisCurves::Sync(HdSceneDelegate* sceneDelegate,
                          HdRenderParam*   renderParam,
                          HdDirtyBits*     dirtyBits,
                          TfToken const   &reprToken)
{
    const SdfPath& id = GetId();
    HdGeminiRenderParam *geminiRenderParam = static_cast<HdGeminiRenderParam*>(renderParam);
    geminiRenderParam->GetRenderDelegate()->AddBasisCurves(id, this);

    if (*dirtyBits & HdChangeTracker::DirtyVisibility) {
        _visible = sceneDelegate->GetVisible(id);
    }
    if (*dirtyBits & HdChangeTracker::DirtyTransform) {
        _transform = GfMatrix4f(sceneDelegate->GetTransform(id));
    }
    if (*dirtyBits & HdChangeTracker::DirtyInstancer) {
        _instancerId = sceneDelegate->GetInstancerId(id);
    }

    bool topologyChanged = (*dirtyBits & HdChangeTracker::DirtyTopology);
    bool pointsChanged = (*dirtyBits & HdChangeTracker::DirtyPoints);
    bool widthsChanged = (*dirtyBits & HdChangeTracker::DirtyWidths);
    bool normalsChanged = (*dirtyBits & HdChangeTracker::DirtyNormals);

    if (topologyChanged) {
        HdBasisCurvesTopology topology = GetBasisCurvesTopology(sceneDelegate);
        
        Subset subset;
        subset.curveVertexCounts = topology.GetCurveVertexCounts();
        subset.indices = topology.GetCurveIndices();
        subset.materialId = sceneDelegate->GetMaterialId(id);
        
        _subsets.clear();
        _subsets.push_back(subset);
    }

    if (pointsChanged) {
        VtValue pointsVal = sceneDelegate->Get(id, HdTokens->points);
        if (pointsVal.IsHolding<VtVec3fArray>()) {
            _points = pointsVal.UncheckedGet<VtVec3fArray>();
        } else if (pointsVal.IsHolding<VtVec3dArray>()) {
            const VtVec3dArray& pointsd = pointsVal.UncheckedGet<VtVec3dArray>();
            _points.resize(pointsd.size());
            for (size_t i = 0; i < pointsd.size(); ++i) _points[i] = GfVec3f(pointsd[i]);
        }
    }

    if (widthsChanged) {
        VtValue widthsVal = sceneDelegate->Get(id, HdTokens->widths);
        if (widthsVal.IsHolding<VtFloatArray>()) {
            _widths = widthsVal.UncheckedGet<VtFloatArray>();
        } else if (widthsVal.IsHolding<VtDoubleArray>()) {
            const VtDoubleArray& widthsd = widthsVal.UncheckedGet<VtDoubleArray>();
            _widths.resize(widthsd.size());
            for(size_t i = 0; i < widthsd.size(); ++i) _widths[i] = (float)widthsd[i];
        } else if (widthsVal.IsHolding<float>()) {
            _widths.assign(1, widthsVal.UncheckedGet<float>());
        }
    }

    if (normalsChanged) {
        VtValue normalsVal = sceneDelegate->Get(id, HdTokens->normals);
        if (normalsVal.IsHolding<VtVec3fArray>()) {
            _normals = normalsVal.UncheckedGet<VtVec3fArray>();
        }
    }

    if (topologyChanged || pointsChanged || widthsChanged || normalsChanged) {
        geminiRenderParam->AcquireSceneForEdit();
        for (auto& subset : _subsets) {
            subset.bvh.BuildCurves(_points, _widths, _normals, subset.curveVertexCounts, subset.indices, -1);
            
            subset.range.SetEmpty();
            if (!subset.bvh.IsEmpty()) {
                for(const auto& p : _points) subset.range.ExtendBy(p);
            }
        }
    }

    *dirtyBits &= ~HdChangeTracker::AllSceneDirtyBits;
}

void
HdGeminiBasisCurves::_InitRepr(TfToken const &reprToken, HdDirtyBits *dirtyBits)
{
}

HdDirtyBits
HdGeminiBasisCurves::_PropagateDirtyBits(HdDirtyBits bits) const
{
    return bits;
}
