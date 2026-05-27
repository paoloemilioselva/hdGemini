#include "mesh.h"
#include "renderParam.h"
#include "renderDelegate.h"
#include "pxr/imaging/hd/meshUtil.h"
#include "pxr/imaging/hd/sceneDelegate.h"
#include "pxr/imaging/hd/tokens.h"
#include "pxr/imaging/hd/extComputationUtils.h"
#include "pxr/base/vt/value.h"
#include <iostream>
#include <set>
#include <algorithm>
#include <map>

#include <opensubdiv/far/topologyDescriptor.h>
#include <opensubdiv/far/topologyRefinerFactory.h>
#include <opensubdiv/far/primvarRefiner.h>

PXR_NAMESPACE_USING_DIRECTIVE

struct OsdGfVec3f {
    GfVec3f v;
    OsdGfVec3f() : v(0) {}
    OsdGfVec3f(const GfVec3f& vec) : v(vec) {}
    void Clear() { v = GfVec3f(0); }
    void AddWithWeight(const OsdGfVec3f& src, float weight) { v += src.v * weight; }
};

struct OsdGfVec2f {
    GfVec2f v;
    OsdGfVec2f() : v(0) {}
    OsdGfVec2f(const GfVec2f& vec) : v(vec) {}
    void Clear() { v = GfVec2f(0); }
    void AddWithWeight(const OsdGfVec2f& src, float weight) { v += src.v * weight; }
};

HdGeminiMesh::HdGeminiMesh(SdfPath const& id)
    : HdMesh(id)
    , _visible(true)
    , _subsetsDirty(true)
{
}

void
HdGeminiMesh::Finalize(HdRenderParam *renderParam)
{
    HdGeminiRenderParam *geminiRenderParam = static_cast<HdGeminiRenderParam*>(renderParam);
    geminiRenderParam->GetRenderDelegate()->RemoveMesh(GetId());
}

void HdGeminiMesh::UpdateOcean(const GfMatrix4f& viewProj, const GfVec3f& cameraPos, float time) {
    if (!_isOcean) return;

    bool rebuildTopology = false;
    if (!_oceanSimulator) {
        _oceanSimulator = std::make_unique<HdGeminiOcean>();
        _oceanSimulator->Init(_oceanParams);
        rebuildTopology = true;
    } else if (_oceanSimulator->GetParams() != _oceanParams) {
        _oceanSimulator->Init(_oceanParams);
        rebuildTopology = true;
    }
    
    _oceanSimulator->Update(time);
    
    if (_points.empty()) return;

    GfMatrix4f transform = GfMatrix4f(_transform);
    GfVec3f scale(transform.GetRow3(0).GetLength(), 
                  transform.GetRow3(1).GetLength(), 
                  transform.GetRow3(2).GetLength());
    if (scale[0] < 1e-4f) scale[0] = 1.0f;
    if (scale[1] < 1e-4f) scale[1] = 1.0f;
    if (scale[2] < 1e-4f) scale[2] = 1.0f;

    std::vector<GfVec3f> scaledPoints(_points.size());
    for (size_t i = 0; i < _points.size(); ++i) {
        scaledPoints[i] = GfCompMult(_points[i], scale);
    }

    std::vector<GfVec3f> displacedScaledPoints, displacedScaledNormals, displacedColors;
    _oceanSimulator->DisplaceGrid(scaledPoints, std::vector<GfVec3f>(), GfVec3f(0.0f), displacedScaledPoints, displacedScaledNormals, displacedColors);
    
    std::vector<GfVec3f> displacedPoints(_points.size());
    std::vector<GfVec3f> displacedNormals(_points.size());
    for (size_t i = 0; i < _points.size(); ++i) {
        displacedPoints[i] = GfVec3f(displacedScaledPoints[i][0] / scale[0],
                                     displacedScaledPoints[i][1] / scale[1],
                                     displacedScaledPoints[i][2] / scale[2]);
        displacedNormals[i] = GfVec3f(displacedScaledNormals[i][0] * scale[0],
                                      displacedScaledNormals[i][1] * scale[1],
                                      displacedScaledNormals[i][2] * scale[2]).GetNormalized();
    }
    
    VtVec3fArray vtPoints(displacedPoints.begin(), displacedPoints.end());
    VtVec3fArray vtNormals(displacedNormals.begin(), displacedNormals.end());
    VtVec3fArray vtColors(displacedColors.begin(), displacedColors.end());

    for (auto& subset : _subsets) {
        if (!subset.indices.empty()) {
            subset.bvh.Build(vtPoints, subset.indices, subset.uvs, vtNormals, vtColors, std::vector<int>());
            
            subset.range.SetEmpty();
            for (const auto& tri : subset.indices) {
                subset.range.ExtendBy(vtPoints[tri[0]]);
                subset.range.ExtendBy(vtPoints[tri[1]]);
                subset.range.ExtendBy(vtPoints[tri[2]]);
            }
        }
    }
}

HdDirtyBits
HdGeminiMesh::GetInitialDirtyBitsMask() const
{
    return HdChangeTracker::AllSceneDirtyBits;
}

TfTokenVector
HdGeminiMesh::_UpdateComputedPrimvarSources(HdSceneDelegate* sceneDelegate,
                                            HdDirtyBits dirtyBits)
{
    SdfPath const& id = GetId();
    HdExtComputationPrimvarDescriptorVector compPrimvarsToUpdate;
    for (size_t i=0; i < HdInterpolationCount; ++i) {
        HdInterpolation interp = static_cast<HdInterpolation>(i);
        HdExtComputationPrimvarDescriptorVector descriptors = 
            sceneDelegate->GetExtComputationPrimvarDescriptors(id, interp);

        for (auto const& pv: descriptors) {
            bool dirty = HdChangeTracker::IsPrimvarDirty(dirtyBits, id, pv.name);
            
            // Special case: if DirtyPoints is set, any computed 'points' is dirty
            if (pv.name == HdTokens->points && (dirtyBits & HdChangeTracker::DirtyPoints)) {
                dirty = true;
            }

            if (dirty || _points.empty()) {
                compPrimvarsToUpdate.emplace_back(pv);
            }
        }
    }

    if (compPrimvarsToUpdate.empty()) {
        return TfTokenVector();
    }
    
    HdExtComputationUtils::ValueStore valueStore;
    try {
        valueStore = HdExtComputationUtils::GetComputedPrimvarValues(compPrimvarsToUpdate, sceneDelegate);
    } catch (...) {
        HDGEMINI_LOG << "[Gemini] ERROR: GetComputedPrimvarValues threw an exception for " << id.GetText() << std::endl;
    }

    TfTokenVector compPrimvarNames;
    for (auto const& compPrimvar : compPrimvarsToUpdate) {
        auto it = valueStore.find(compPrimvar.name);
        if (it == valueStore.end() || it->second.IsEmpty()) {
            it = valueStore.find(compPrimvar.sourceComputationOutputName);
        }

        VtValue val;
        if (it != valueStore.end() && !it->second.IsEmpty()) {
            val = it->second;
        } else {
            // Fallback: try direct Get()
            try {
                val = sceneDelegate->Get(id, compPrimvar.name);
                if (val.IsEmpty() && compPrimvar.name != compPrimvar.sourceComputationOutputName) {
                    val = sceneDelegate->Get(id, compPrimvar.sourceComputationOutputName);
                }
            } catch (...) {
                // Ignore fallback errors
            }
        }

        if (val.IsEmpty()) {
            HDGEMINI_LOG << "[Gemini] Warning: Failed to find computed value for PV " << compPrimvar.name.GetText() 
                      << " (output: " << compPrimvar.sourceComputationOutputName.GetText() 
                      << " comp: " << compPrimvar.sourceComputationId.GetText() << ")" << std::endl;
            continue;
        }
        
        compPrimvarNames.emplace_back(compPrimvar.name);

        if (compPrimvar.name == HdTokens->points) {
            if (val.IsHolding<VtVec3fArray>()) {
                _points = val.UncheckedGet<VtVec3fArray>();
                _subsetsDirty = true;
            } else if (val.IsHolding<VtVec3dArray>()) {
                const VtVec3dArray& pointsd = val.UncheckedGet<VtVec3dArray>();
                _points.resize(pointsd.size());
                for (size_t j = 0; j < pointsd.size(); ++j) _points[j] = GfVec3f(pointsd[j]);
                _subsetsDirty = true;
            }
        } else if (compPrimvar.name == HdTokens->displayColor) {
            if (val.IsHolding<VtVec3fArray>()) {
                _colors = val.UncheckedGet<VtVec3fArray>();
            }
        }
    }

    return compPrimvarNames;
}

void
HdGeminiMesh::Sync(HdSceneDelegate* sceneDelegate,
                   HdRenderParam*   renderParam,
                   HdDirtyBits*     dirtyBits,
                   TfToken const   &reprToken)
{
    const SdfPath& id = GetId();
    HdGeminiRenderParam *geminiRenderParam = static_cast<HdGeminiRenderParam*>(renderParam);
    
    {
        std::lock_guard<std::recursive_mutex> lock(geminiRenderParam->GetRenderDelegate()->GetSceneLock());
        geminiRenderParam->AcquireSceneForEdit();
        geminiRenderParam->GetRenderDelegate()->AddMesh(id, this);
    }

    _instancerId = sceneDelegate->GetInstancerId(id);
    
    if (HdChangeTracker::IsVisibilityDirty(*dirtyBits, id)) {
        _visible = sceneDelegate->GetVisible(id);
    }
    
    VtValue oceanEnable = sceneDelegate->Get(id, TfToken("primvars:gemini:oceanEnable"));
    if (oceanEnable.IsHolding<bool>()) _isOcean = oceanEnable.Get<bool>();
    else {
        oceanEnable = sceneDelegate->Get(id, TfToken("gemini:oceanEnable"));
        if (oceanEnable.IsHolding<bool>()) _isOcean = oceanEnable.Get<bool>();
    }
    
    if (_isOcean) {
        _oceanParams = HdGeminiOceanParams(); // Default initialization

        VtValue height = sceneDelegate->Get(id, TfToken("primvars:gemini:oceanWaterHeight"));
        if (height.IsHolding<float>()) _oceanParams.waterHeight = height.Get<float>();
        else if (height.IsHolding<double>()) _oceanParams.waterHeight = (float)height.Get<double>();
        else {
            height = sceneDelegate->Get(id, TfToken("gemini:oceanWaterHeight"));
            if (height.IsHolding<float>()) _oceanParams.waterHeight = height.Get<float>();
            else if (height.IsHolding<double>()) _oceanParams.waterHeight = (float)height.Get<double>();
        }

        VtValue mpu = sceneDelegate->Get(id, TfToken("primvars:gemini:metersPerUnit"));
        if (mpu.IsHolding<float>()) _oceanParams.metersPerUnit = mpu.Get<float>();
        else if (mpu.IsHolding<double>()) _oceanParams.metersPerUnit = (float)mpu.Get<double>();
        else {
            mpu = sceneDelegate->Get(id, TfToken("gemini:metersPerUnit"));
            if (mpu.IsHolding<float>()) _oceanParams.metersPerUnit = mpu.Get<float>();
            else if (mpu.IsHolding<double>()) _oceanParams.metersPerUnit = (float)mpu.Get<double>();
        }

        VtValue res = sceneDelegate->Get(id, TfToken("primvars:gemini:oceanGridSize"));
        if (res.IsHolding<int>()) _oceanParams.gridSize = res.Get<int>();
        else {
            res = sceneDelegate->Get(id, TfToken("gemini:oceanGridSize"));
            if (res.IsHolding<int>()) _oceanParams.gridSize = res.Get<int>();
        }


        VtValue sizeVal = sceneDelegate->Get(id, TfToken("primvars:gemini:oceanSize"));
        if (sizeVal.IsHolding<float>()) _oceanParams.size = sizeVal.Get<float>();
        else if (sizeVal.IsHolding<double>()) _oceanParams.size = (float)sizeVal.Get<double>();
        else {
            sizeVal = sceneDelegate->Get(id, TfToken("gemini:oceanSize"));
            if (sizeVal.IsHolding<float>()) _oceanParams.size = sizeVal.Get<float>();
            else if (sizeVal.IsHolding<double>()) _oceanParams.size = (float)sizeVal.Get<double>();
        }

        const char* cascadeTokens[3] = {"1", "2", "3"};
        for (int c = 0; c < 3; ++c) {

            std::string primvarAmp = std::string("primvars:gemini:oceanAmplitude") + cascadeTokens[c];
            std::string geomAmp = std::string("gemini:oceanAmplitude") + cascadeTokens[c];
            VtValue amp = sceneDelegate->Get(id, TfToken(primvarAmp));
            if (amp.IsHolding<float>()) _oceanParams.amplitude[c] = amp.Get<float>();
            else if (amp.IsHolding<double>()) _oceanParams.amplitude[c] = (float)amp.Get<double>();
            else {
                amp = sceneDelegate->Get(id, TfToken(geomAmp));
                if (amp.IsHolding<float>()) _oceanParams.amplitude[c] = amp.Get<float>();
                else if (amp.IsHolding<double>()) _oceanParams.amplitude[c] = (float)amp.Get<double>();
            }

            std::string primvarChop = std::string("primvars:gemini:oceanChoppiness") + cascadeTokens[c];
            std::string geomChop = std::string("gemini:oceanChoppiness") + cascadeTokens[c];
            VtValue chop = sceneDelegate->Get(id, TfToken(primvarChop));
            if (chop.IsHolding<float>()) _oceanParams.choppiness[c] = chop.Get<float>();
            else if (chop.IsHolding<double>()) _oceanParams.choppiness[c] = (float)chop.Get<double>();
            else {
                chop = sceneDelegate->Get(id, TfToken(geomChop));
                if (chop.IsHolding<float>()) _oceanParams.choppiness[c] = chop.Get<float>();
                else if (chop.IsHolding<double>()) _oceanParams.choppiness[c] = (float)chop.Get<double>();
            }

            std::string primvarWindSpeed = std::string("primvars:gemini:oceanWindSpeed") + cascadeTokens[c];
            std::string geomWindSpeed = std::string("gemini:oceanWindSpeed") + cascadeTokens[c];
            VtValue speed = sceneDelegate->Get(id, TfToken(primvarWindSpeed));
            if (speed.IsHolding<float>()) _oceanParams.windSpeed[c] = speed.Get<float>();
            else if (speed.IsHolding<double>()) _oceanParams.windSpeed[c] = (float)speed.Get<double>();
            else {
                speed = sceneDelegate->Get(id, TfToken(geomWindSpeed));
                if (speed.IsHolding<float>()) _oceanParams.windSpeed[c] = speed.Get<float>();
                else if (speed.IsHolding<double>()) _oceanParams.windSpeed[c] = (float)speed.Get<double>();
            }

            std::string primvarWindDirX = std::string("primvars:gemini:oceanWindDirectionX") + cascadeTokens[c];
            std::string geomWindDirX = std::string("gemini:oceanWindDirectionX") + cascadeTokens[c];
            VtValue wdx = sceneDelegate->Get(id, TfToken(primvarWindDirX));
            if (wdx.IsHolding<float>()) _oceanParams.windDirection[c][0] = wdx.Get<float>();
            else if (wdx.IsHolding<double>()) _oceanParams.windDirection[c][0] = (float)wdx.Get<double>();
            else {
                wdx = sceneDelegate->Get(id, TfToken(geomWindDirX));
                if (wdx.IsHolding<float>()) _oceanParams.windDirection[c][0] = wdx.Get<float>();
                else if (wdx.IsHolding<double>()) _oceanParams.windDirection[c][0] = (float)wdx.Get<double>();
            }

            std::string primvarWindDirY = std::string("primvars:gemini:oceanWindDirectionY") + cascadeTokens[c];
            std::string geomWindDirY = std::string("gemini:oceanWindDirectionY") + cascadeTokens[c];
            VtValue wdy = sceneDelegate->Get(id, TfToken(primvarWindDirY));
            if (wdy.IsHolding<float>()) _oceanParams.windDirection[c][1] = wdy.Get<float>();
            else if (wdy.IsHolding<double>()) _oceanParams.windDirection[c][1] = (float)wdy.Get<double>();
            else {
                wdy = sceneDelegate->Get(id, TfToken(geomWindDirY));
                if (wdy.IsHolding<float>()) _oceanParams.windDirection[c][1] = wdy.Get<float>();
                else if (wdy.IsHolding<double>()) _oceanParams.windDirection[c][1] = (float)wdy.Get<double>();
            }

            std::string primvarMinK = std::string("primvars:gemini:oceanMinK") + cascadeTokens[c];
            std::string geomMinK = std::string("gemini:oceanMinK") + cascadeTokens[c];
            VtValue mink = sceneDelegate->Get(id, TfToken(primvarMinK));
            if (mink.IsHolding<float>()) _oceanParams.minK[c] = mink.Get<float>();
            else if (mink.IsHolding<double>()) _oceanParams.minK[c] = (float)mink.Get<double>();
            else {
                mink = sceneDelegate->Get(id, TfToken(geomMinK));
                if (mink.IsHolding<float>()) _oceanParams.minK[c] = mink.Get<float>();
                else if (mink.IsHolding<double>()) _oceanParams.minK[c] = (float)mink.Get<double>();
            }

            std::string primvarMaxK = std::string("primvars:gemini:oceanMaxK") + cascadeTokens[c];
            std::string geomMaxK = std::string("gemini:oceanMaxK") + cascadeTokens[c];
            VtValue maxk = sceneDelegate->Get(id, TfToken(primvarMaxK));
            if (maxk.IsHolding<float>()) _oceanParams.maxK[c] = maxk.Get<float>();
            else if (maxk.IsHolding<double>()) _oceanParams.maxK[c] = (float)maxk.Get<double>();
            else {
                maxk = sceneDelegate->Get(id, TfToken(geomMaxK));
                if (maxk.IsHolding<float>()) _oceanParams.maxK[c] = maxk.Get<float>();
                else if (maxk.IsHolding<double>()) _oceanParams.maxK[c] = (float)maxk.Get<double>();
            }
        }
        
        VtValue foamVis = sceneDelegate->Get(id, TfToken("primvars:gemini:oceanFoamVisibility"));
        if (foamVis.IsHolding<float>()) _oceanParams.foamVisibility = foamVis.Get<float>();
        else if (foamVis.IsHolding<double>()) _oceanParams.foamVisibility = (float)foamVis.Get<double>();
        else {
            foamVis = sceneDelegate->Get(id, TfToken("gemini:oceanFoamVisibility"));
            if (foamVis.IsHolding<float>()) _oceanParams.foamVisibility = foamVis.Get<float>();
            else if (foamVis.IsHolding<double>()) _oceanParams.foamVisibility = (float)foamVis.Get<double>();
        }
        
        VtValue disableShader = sceneDelegate->Get(id, TfToken("primvars:gemini:oceanDisableShader"));
        if (disableShader.IsHolding<bool>()) _oceanParams.disableShader = disableShader.Get<bool>();
        else {
            disableShader = sceneDelegate->Get(id, TfToken("gemini:oceanDisableShader"));
            if (disableShader.IsHolding<bool>()) _oceanParams.disableShader = disableShader.Get<bool>();
        }

        VtValue repeat = sceneDelegate->Get(id, TfToken("primvars:gemini:oceanRepeat"));
        if (repeat.IsHolding<bool>()) _oceanParams.repeat = repeat.Get<bool>();
        else {
            repeat = sceneDelegate->Get(id, TfToken("gemini:oceanRepeat"));
            if (repeat.IsHolding<bool>()) _oceanParams.repeat = repeat.Get<bool>();
        }
        
        VtValue scC = sceneDelegate->Get(id, TfToken("primvars:gemini:oceanScatteringColor"));
        if (scC.IsHolding<GfVec3f>()) _oceanParams.scatteringColor = scC.Get<GfVec3f>();
        else {
            scC = sceneDelegate->Get(id, TfToken("gemini:oceanScatteringColor"));
            if (scC.IsHolding<GfVec3f>()) _oceanParams.scatteringColor = scC.Get<GfVec3f>();
        }
        
        VtValue scD = sceneDelegate->Get(id, TfToken("primvars:gemini:oceanScatteringDepth"));
        if (scD.IsHolding<float>()) _oceanParams.scatteringDepth = scD.Get<float>();
        else if (scD.IsHolding<double>()) _oceanParams.scatteringDepth = (float)scD.Get<double>();
        else {
            scD = sceneDelegate->Get(id, TfToken("gemini:oceanScatteringDepth"));
            if (scD.IsHolding<float>()) _oceanParams.scatteringDepth = scD.Get<float>();
            else if (scD.IsHolding<double>()) _oceanParams.scatteringDepth = (float)scD.Get<double>();
        }
    }

    HdMeshTopology topology = GetMeshTopology(sceneDelegate);    
    if (HdChangeTracker::IsTransformDirty(*dirtyBits, id)) {
        _transform = GfMatrix4f(sceneDelegate->GetTransform(id));
    }

    // --- COMPUTED PRIMVARS ---
    TfTokenVector computedNames = _UpdateComputedPrimvarSources(sceneDelegate, *dirtyBits);
    bool pointsUpdatedByComputation = false;
    bool colorsUpdatedByComputation = false;
    for (const auto& name : computedNames) {
        if (name == HdTokens->points) pointsUpdatedByComputation = true;
        if (name == HdTokens->displayColor) colorsUpdatedByComputation = true;
    }

    // --- POINTS UPDATING ---
    bool pointsDirty = (*dirtyBits & HdChangeTracker::DirtyPoints) || 
                       HdChangeTracker::IsPrimvarDirty(*dirtyBits, id, HdTokens->points) ||
                       _points.empty();

    if (pointsDirty) {
        bool pointsActuallyUpdated = pointsUpdatedByComputation;

        if (!pointsActuallyUpdated) {
            VtValue val = sceneDelegate->Get(id, HdTokens->points);
            if (!val.IsEmpty()) {
                if (val.IsHolding<VtVec3fArray>()) {
                    _points = val.UncheckedGet<VtVec3fArray>();
                    pointsActuallyUpdated = true;
                } else if (val.IsHolding<VtVec3dArray>()) {
                    const auto& arr = val.UncheckedGet<VtVec3dArray>();
                    _points.resize(arr.size());
                    for (size_t j = 0; j < arr.size(); ++j) _points[j] = GfVec3f(arr[j]);
                    pointsActuallyUpdated = true;
                }
            }
        }

        if (pointsActuallyUpdated) {
            _subsetsDirty = true;
        }
    }

    // --- COLOR UPDATING ---
    TfToken colorToken = HdTokens->displayColor;
    if (HdChangeTracker::IsPrimvarDirty(*dirtyBits, id, colorToken)) {
        if (!colorsUpdatedByComputation) {
            HdInterpolation colorInterp = HdInterpolationVertex;
            for (int i = 0; i < HdInterpolationCount; ++i) {
                HdPrimvarDescriptorVector pvs = sceneDelegate->GetPrimvarDescriptors(id, (HdInterpolation)i);
                for (const auto& pv : pvs) {
                    if (pv.name == colorToken) {
                        colorInterp = pv.interpolation;
                        break;
                    }
                }
            }

            VtIntArray colorIndices;
            VtValue val = sceneDelegate->GetIndexedPrimvar(id, colorToken, &colorIndices);
            if (val.IsEmpty()) val = sceneDelegate->Get(id, colorToken);

            if (!val.IsEmpty() && (val.IsHolding<VtVec3fArray>() || val.IsHolding<VtVec4fArray>())) {
                VtVec3fArray colors;
                if (val.IsHolding<VtVec3fArray>()) {
                    colors = val.UncheckedGet<VtVec3fArray>();
                } else {
                    const auto& c4 = val.UncheckedGet<VtVec4fArray>();
                    colors.resize(c4.size());
                    for (size_t j = 0; j < c4.size(); ++j) colors[j] = GfVec3f(c4[j][0], c4[j][1], c4[j][2]);
                }

                if (!colorIndices.empty()) {
                    VtVec3fArray flattened(colorIndices.size());
                    for (size_t i = 0; i < colorIndices.size(); ++i) {
                        flattened[i] = colors[colorIndices[i]];
                    }
                    colors = flattened;
                }

                if (colorInterp == HdInterpolationFaceVarying) {
                    HdMeshTopology topology = GetMeshTopology(sceneDelegate);
                    HdMeshUtil meshUtil(&topology, id);
                    VtValue triangulated;
                    meshUtil.ComputeTriangulatedFaceVaryingPrimvar(colors.data(), (int)colors.size(), HdTypeFloatVec3, &triangulated);
                    if (!triangulated.IsEmpty() && triangulated.IsHolding<VtVec3fArray>()) {
                        _colors = triangulated.Get<VtVec3fArray>();
                    } else {
                        _colors = colors;
                    }
                } else {
                    _colors = colors;
                }
                _colorInterp = colorInterp;
                _subsetsDirty = true;
            }
        }
    }

    // --- UV UPDATING ---
    TfToken stToken("st");
    TfToken uvToken("uv");
    TfToken activeStToken = stToken;
    bool stDirty = HdChangeTracker::IsPrimvarDirty(*dirtyBits, id, stToken);
    bool uvDirty = HdChangeTracker::IsPrimvarDirty(*dirtyBits, id, uvToken);

    if (stDirty || uvDirty || _uvs.empty()) {
        HdInterpolation stInterp = HdInterpolationVertex;
        bool found = false;
        for (int i = 0; i < HdInterpolationCount; ++i) {
            HdPrimvarDescriptorVector pvs = sceneDelegate->GetPrimvarDescriptors(id, (HdInterpolation)i);
            for (const auto& pv : pvs) {
                if (pv.name == stToken || pv.name == uvToken) {
                    activeStToken = pv.name;
                    stInterp = pv.interpolation;
                    found = true;
                    break;
                }
            }
            if (found) break;
        }

        VtIntArray stIndices;
        VtValue val = sceneDelegate->GetIndexedPrimvar(id, activeStToken, &stIndices);
        if (val.IsEmpty()) val = sceneDelegate->Get(id, activeStToken);

        if (!val.IsEmpty() && val.IsHolding<VtVec2fArray>()) {
            VtVec2fArray uvs = val.UncheckedGet<VtVec2fArray>();
            if (!stIndices.empty()) {
                VtVec2fArray flattened(stIndices.size());
                for (size_t i = 0; i < stIndices.size(); ++i) {
                    flattened[i] = uvs[stIndices[i]];
                }
                uvs = flattened;
            }

            _uvs = uvs;
            _uvInterp = stInterp;
            _subsetsDirty = true;
        }
    }

    // --- NORMAL UPDATING ---
    TfToken normalToken = HdTokens->normals;
    bool normalsDirty = HdChangeTracker::IsPrimvarDirty(*dirtyBits, id, normalToken) || _normals.empty();
    if (normalsDirty) {
        HdInterpolation normalInterp = HdInterpolationVertex;
        bool found = false;
        for (int i = 0; i < HdInterpolationCount; ++i) {
            HdPrimvarDescriptorVector pvs = sceneDelegate->GetPrimvarDescriptors(id, (HdInterpolation)i);
            for (const auto& pv : pvs) {
                if (pv.name == normalToken) {
                    normalInterp = pv.interpolation;
                    found = true;
                    break;
                }
            }
            if (found) break;
        }

        VtIntArray normalIndices;
        VtValue val = sceneDelegate->GetIndexedPrimvar(id, normalToken, &normalIndices);
        if (val.IsEmpty()) val = sceneDelegate->Get(id, normalToken);

        if (!val.IsEmpty() && val.IsHolding<VtVec3fArray>()) {
            VtVec3fArray normals = val.UncheckedGet<VtVec3fArray>();
            if (!normalIndices.empty()) {
                VtVec3fArray flattened(normalIndices.size());
                for (size_t i = 0; i < normalIndices.size(); ++i) {
                    flattened[i] = normals[normalIndices[i]];
                }
                normals = flattened;
            }

            if (normalInterp == HdInterpolationFaceVarying) {
                HdMeshTopology topology = GetMeshTopology(sceneDelegate);
                HdMeshUtil meshUtil(&topology, id);
                VtValue triangulated;
                meshUtil.ComputeTriangulatedFaceVaryingPrimvar(normals.data(), (int)normals.size(), HdTypeFloatVec3, &triangulated);
                if (!triangulated.IsEmpty() && triangulated.IsHolding<VtVec3fArray>()) {
                    _normals = triangulated.Get<VtVec3fArray>();
                } else {
                    _normals = normals;
                }
            } else {
                _normals = normals;
            }
            _normalInterp = normalInterp;
            _subsetsDirty = true;
        }
    }

    if (HdChangeTracker::IsTopologyDirty(*dirtyBits, id) || 
        (*dirtyBits & HdChangeTracker::DirtyMaterialId) ||
        _subsetsDirty) {
                SdfPath defaultMaterialId = sceneDelegate->GetMaterialId(id);

        HdMeshTopology topology = GetMeshTopology(sceneDelegate);
        
        bool globalSubdivision = true;
        VtValue subdivVal = geminiRenderParam->GetRenderDelegate()->GetRenderSetting(HdGeminiRenderSettingsTokens->enableSubdivision);
        if (subdivVal.IsHolding<bool>()) {
            globalSubdivision = subdivVal.Get<bool>();
        }

        int refineLevel = sceneDelegate->GetDisplayStyle(id).refineLevel;
        if (refineLevel <= 0) {
            refineLevel = 2; // Default to level 2 if not explicitly requested
        }
        
        bool doSubdivide = (globalSubdivision && topology.GetScheme() == TfToken("catmullClark"));

        if (doSubdivide) {
            _normals.clear();
        }

        VtVec3iArray allTriangulatedIndices;
        VtIntArray trianglePrimitiveParams;

        if (doSubdivide && !_points.empty()) {
            typedef OpenSubdiv::Far::TopologyDescriptor Descriptor;
            Descriptor desc;
            desc.numVertices = _points.size();
            desc.numFaces = topology.GetFaceVertexCounts().size();
            desc.numVertsPerFace = topology.GetFaceVertexCounts().cdata();
            desc.vertIndicesPerFace = topology.GetFaceVertexIndices().cdata();

            int uvChannelIdx = -1, colorChannelIdx = -1, normalChannelIdx = -1;
            int numChannels = 0;
            Descriptor::FVarChannel channels[3];

            std::vector<int> fvIndices, fvColorsIndices, fvNormalsIndices;
            if (_uvInterp == HdInterpolationFaceVarying && !_uvs.empty()) {
                fvIndices.resize(_uvs.size());
                for(size_t i=0; i<fvIndices.size(); ++i) fvIndices[i] = (int)i;
                channels[numChannels].numValues = _uvs.size();
                channels[numChannels].valueIndices = fvIndices.data();
                uvChannelIdx = numChannels++;
            }
            if (_colorInterp == HdInterpolationFaceVarying && !_colors.empty()) {
                fvColorsIndices.resize(_colors.size());
                for(size_t i=0; i<fvColorsIndices.size(); ++i) fvColorsIndices[i] = (int)i;
                channels[numChannels].numValues = _colors.size();
                channels[numChannels].valueIndices = fvColorsIndices.data();
                colorChannelIdx = numChannels++;
            }
            if (_normalInterp == HdInterpolationFaceVarying && !_normals.empty()) {
                fvNormalsIndices.resize(_normals.size());
                for(size_t i=0; i<fvNormalsIndices.size(); ++i) fvNormalsIndices[i] = (int)i;
                channels[numChannels].numValues = _normals.size();
                channels[numChannels].valueIndices = fvNormalsIndices.data();
                normalChannelIdx = numChannels++;
            }

            desc.numFVarChannels = numChannels;
            desc.fvarChannels = channels;

            std::vector<int> creaseVertexIndexPairs;
            std::vector<float> creaseWeights;
            const PxOsdSubdivTags& subdivTags = topology.GetSubdivTags();
            
            const VtIntArray& cIndices = subdivTags.GetCreaseIndices();
            const VtIntArray& cLengths = subdivTags.GetCreaseLengths();
            const VtFloatArray& cWeights = subdivTags.GetCreaseWeights();
            
            int creaseIndexOffset = 0;
            int weightIdx = 0;
            bool perCreaseWeights = (cWeights.size() == cLengths.size());
            for (size_t i = 0; i < cLengths.size(); ++i) {
                int length = cLengths[i];
                if (perCreaseWeights) {
                    float weight = cWeights[weightIdx++];
                    for (int j = 0; j < length - 1; ++j) {
                        creaseVertexIndexPairs.push_back(cIndices[creaseIndexOffset + j]);
                        creaseVertexIndexPairs.push_back(cIndices[creaseIndexOffset + j + 1]);
                        creaseWeights.push_back(weight);
                    }
                } else {
                    for (int j = 0; j < length - 1; ++j) {
                        creaseVertexIndexPairs.push_back(cIndices[creaseIndexOffset + j]);
                        creaseVertexIndexPairs.push_back(cIndices[creaseIndexOffset + j + 1]);
                        creaseWeights.push_back(cWeights[weightIdx + j]);
                    }
                    weightIdx += length - 1;
                }
                creaseIndexOffset += length;
            }
            
            desc.numCreases = creaseWeights.size();
            desc.creaseVertexIndexPairs = creaseVertexIndexPairs.empty() ? nullptr : creaseVertexIndexPairs.data();
            desc.creaseWeights = creaseWeights.empty() ? nullptr : creaseWeights.data();
            
            const VtIntArray& cornerIndices = subdivTags.GetCornerIndices();
            const VtFloatArray& cornerWeights = subdivTags.GetCornerWeights();
            desc.numCorners = cornerIndices.size();
            desc.cornerVertexIndices = cornerIndices.empty() ? nullptr : cornerIndices.cdata();
            desc.cornerWeights = cornerWeights.empty() ? nullptr : cornerWeights.cdata();
            
            const VtIntArray& holeIndices = topology.GetHoleIndices();
            desc.numHoles = holeIndices.size();
            desc.holeIndices = holeIndices.empty() ? nullptr : holeIndices.cdata();

            OpenSubdiv::Sdc::SchemeType type = OpenSubdiv::Sdc::SCHEME_CATMARK;
            if (topology.GetScheme() == TfToken("loop")) type = OpenSubdiv::Sdc::SCHEME_LOOP;
            else if (topology.GetScheme() == TfToken("bilinear")) type = OpenSubdiv::Sdc::SCHEME_BILINEAR;
            
            OpenSubdiv::Sdc::Options options;
            
            TfToken vtxRule = subdivTags.GetVertexInterpolationRule();
            if (vtxRule == TfToken("edgeAndCorner")) options.SetVtxBoundaryInterpolation(OpenSubdiv::Sdc::Options::VTX_BOUNDARY_EDGE_AND_CORNER);
            else if (vtxRule == TfToken("edgeOnly")) options.SetVtxBoundaryInterpolation(OpenSubdiv::Sdc::Options::VTX_BOUNDARY_EDGE_ONLY);
            else if (vtxRule == TfToken("none")) options.SetVtxBoundaryInterpolation(OpenSubdiv::Sdc::Options::VTX_BOUNDARY_NONE);
            else options.SetVtxBoundaryInterpolation(OpenSubdiv::Sdc::Options::VTX_BOUNDARY_EDGE_ONLY);
            
            TfToken fvarRule = subdivTags.GetFaceVaryingInterpolationRule();
            if (fvarRule == TfToken("all")) options.SetFVarLinearInterpolation(OpenSubdiv::Sdc::Options::FVAR_LINEAR_ALL);
            else if (fvarRule == TfToken("cornersOnly")) options.SetFVarLinearInterpolation(OpenSubdiv::Sdc::Options::FVAR_LINEAR_CORNERS_ONLY);
            else if (fvarRule == TfToken("cornersPlus1")) options.SetFVarLinearInterpolation(OpenSubdiv::Sdc::Options::FVAR_LINEAR_CORNERS_PLUS1);
            else if (fvarRule == TfToken("none")) options.SetFVarLinearInterpolation(OpenSubdiv::Sdc::Options::FVAR_LINEAR_NONE);
            else if (fvarRule == TfToken("boundaries")) options.SetFVarLinearInterpolation(OpenSubdiv::Sdc::Options::FVAR_LINEAR_BOUNDARIES);
            else options.SetFVarLinearInterpolation(OpenSubdiv::Sdc::Options::FVAR_LINEAR_ALL);
            
            TfToken creaseMethod = subdivTags.GetCreaseMethod();
            if (creaseMethod == TfToken("chaikin")) options.SetCreasingMethod(OpenSubdiv::Sdc::Options::CREASE_CHAIKIN);
            else options.SetCreasingMethod(OpenSubdiv::Sdc::Options::CREASE_UNIFORM);
            
            TfToken triangleSubdiv = subdivTags.GetTriangleSubdivision();
            if (triangleSubdiv == TfToken("smooth")) options.SetTriangleSubdivision(OpenSubdiv::Sdc::Options::TRI_SUB_SMOOTH);
            else options.SetTriangleSubdivision(OpenSubdiv::Sdc::Options::TRI_SUB_CATMARK);

            OpenSubdiv::Far::TopologyRefiner* refiner = 
                OpenSubdiv::Far::TopologyRefinerFactory<Descriptor>::Create(
                    desc, OpenSubdiv::Far::TopologyRefinerFactory<Descriptor>::Options(type, options));

            if (refiner) {
                refiner->RefineUniform(OpenSubdiv::Far::TopologyRefiner::UniformOptions(refineLevel));
                OpenSubdiv::Far::PrimvarRefiner primvarRefiner(*refiner);

                // Interpolate Points
                std::vector<OsdGfVec3f> srcPoints(_points.size());
                for(size_t i=0; i<_points.size(); ++i) srcPoints[i] = OsdGfVec3f(_points[i]);
                for (int level = 1; level <= refineLevel; ++level) {
                    std::vector<OsdGfVec3f> dstPoints(refiner->GetLevel(level).GetNumVertices());
                    primvarRefiner.Interpolate(level, srcPoints, dstPoints);
                    srcPoints = dstPoints;
                }
                _points.resize(srcPoints.size());
                for(size_t i=0; i<srcPoints.size(); ++i) _points[i] = srcPoints[i].v;

                // Interpolate FVar
                std::vector<OsdGfVec2f> srcUvs;
                if (uvChannelIdx >= 0) {
                    srcUvs.resize(_uvs.size());
                    for(size_t i=0; i<_uvs.size(); ++i) srcUvs[i] = OsdGfVec2f(_uvs[i]);
                    for (int level = 1; level <= refineLevel; ++level) {
                        std::vector<OsdGfVec2f> dstUvs(refiner->GetLevel(level).GetNumFVarValues(uvChannelIdx));
                        primvarRefiner.InterpolateFaceVarying(level, srcUvs, dstUvs, uvChannelIdx);
                        srcUvs = dstUvs;
                    }
                }
                std::vector<OsdGfVec3f> srcColors;
                if (colorChannelIdx >= 0) {
                    srcColors.resize(_colors.size());
                    for(size_t i=0; i<_colors.size(); ++i) srcColors[i] = OsdGfVec3f(_colors[i]);
                    for (int level = 1; level <= refineLevel; ++level) {
                        std::vector<OsdGfVec3f> dstColors(refiner->GetLevel(level).GetNumFVarValues(colorChannelIdx));
                        primvarRefiner.InterpolateFaceVarying(level, srcColors, dstColors, colorChannelIdx);
                        srcColors = dstColors;
                    }
                }
                std::vector<OsdGfVec3f> srcNormals;
                if (normalChannelIdx >= 0) {
                    srcNormals.resize(_normals.size());
                    for(size_t i=0; i<_normals.size(); ++i) srcNormals[i] = OsdGfVec3f(_normals[i]);
                    for (int level = 1; level <= refineLevel; ++level) {
                        std::vector<OsdGfVec3f> dstNormals(refiner->GetLevel(level).GetNumFVarValues(normalChannelIdx));
                        primvarRefiner.InterpolateFaceVarying(level, srcNormals, dstNormals, normalChannelIdx);
                        srcNormals = dstNormals;
                    }
                }

                // Triangulate Refined Quads
                OpenSubdiv::Far::TopologyLevel const& refLevel = refiner->GetLevel(refineLevel);
                int numFaces = refLevel.GetNumFaces();
                
                VtVec2fArray triangulatedUvs;
                VtVec3fArray triangulatedColors;
                VtVec3fArray triangulatedNormals;

                for (int f = 0; f < numFaces; ++f) {
                    OpenSubdiv::Far::ConstIndexArray verts = refLevel.GetFaceVertices(f);
                    if (verts.size() == 4) {
                        allTriangulatedIndices.push_back(GfVec3i(verts[0], verts[1], verts[2]));
                        allTriangulatedIndices.push_back(GfVec3i(verts[0], verts[2], verts[3]));

                        int baseFace = f;
                        for(int l = refineLevel; l > 0; --l) {
                            baseFace = refiner->GetLevel(l).GetFaceParentFace(baseFace);
                        }
                        
                        int param = (baseFace << 2) | 0;
                        trianglePrimitiveParams.push_back(param);
                        trianglePrimitiveParams.push_back(param);

                        if (uvChannelIdx >= 0) {
                            OpenSubdiv::Far::ConstIndexArray fv = refLevel.GetFaceFVarValues(f, uvChannelIdx);
                            triangulatedUvs.push_back(srcUvs[fv[0]].v);
                            triangulatedUvs.push_back(srcUvs[fv[1]].v);
                            triangulatedUvs.push_back(srcUvs[fv[2]].v);
                            triangulatedUvs.push_back(srcUvs[fv[0]].v);
                            triangulatedUvs.push_back(srcUvs[fv[2]].v);
                            triangulatedUvs.push_back(srcUvs[fv[3]].v);
                        }
                        if (colorChannelIdx >= 0) {
                            OpenSubdiv::Far::ConstIndexArray fv = refLevel.GetFaceFVarValues(f, colorChannelIdx);
                            triangulatedColors.push_back(srcColors[fv[0]].v);
                            triangulatedColors.push_back(srcColors[fv[1]].v);
                            triangulatedColors.push_back(srcColors[fv[2]].v);
                            triangulatedColors.push_back(srcColors[fv[0]].v);
                            triangulatedColors.push_back(srcColors[fv[2]].v);
                            triangulatedColors.push_back(srcColors[fv[3]].v);
                        }
                        if (normalChannelIdx >= 0) {
                            OpenSubdiv::Far::ConstIndexArray fv = refLevel.GetFaceFVarValues(f, normalChannelIdx);
                            triangulatedNormals.push_back(srcNormals[fv[0]].v);
                            triangulatedNormals.push_back(srcNormals[fv[1]].v);
                            triangulatedNormals.push_back(srcNormals[fv[2]].v);
                            triangulatedNormals.push_back(srcNormals[fv[0]].v);
                            triangulatedNormals.push_back(srcNormals[fv[2]].v);
                            triangulatedNormals.push_back(srcNormals[fv[3]].v);
                        }
                    }
                }
                
                if (uvChannelIdx >= 0) _uvs = triangulatedUvs;
                if (colorChannelIdx >= 0) _colors = triangulatedColors;
                if (normalChannelIdx >= 0) _normals = triangulatedNormals;
                
                delete refiner;
            }
        } else {
            HdMeshUtil meshUtil(&topology, id);
            meshUtil.ComputeTriangleIndices(&allTriangulatedIndices, &trianglePrimitiveParams);

            if (_uvInterp == HdInterpolationFaceVarying && !_uvs.empty()) {
                VtValue triangulated;
                meshUtil.ComputeTriangulatedFaceVaryingPrimvar(_uvs.data(), (int)_uvs.size(), HdTypeFloatVec2, &triangulated);
                if (!triangulated.IsEmpty() && triangulated.IsHolding<VtVec2fArray>()) _uvs = triangulated.Get<VtVec2fArray>();
            }
            if (_colorInterp == HdInterpolationFaceVarying && !_colors.empty()) {
                VtValue triangulated;
                meshUtil.ComputeTriangulatedFaceVaryingPrimvar(_colors.data(), (int)_colors.size(), HdTypeFloatVec3, &triangulated);
                if (!triangulated.IsEmpty() && triangulated.IsHolding<VtVec3fArray>()) _colors = triangulated.Get<VtVec3fArray>();
            }
            if (_normalInterp == HdInterpolationFaceVarying && !_normals.empty()) {
                VtValue triangulated;
                meshUtil.ComputeTriangulatedFaceVaryingPrimvar(_normals.data(), (int)_normals.size(), HdTypeFloatVec3, &triangulated);
                if (!triangulated.IsEmpty() && triangulated.IsHolding<VtVec3fArray>()) _normals = triangulated.Get<VtVec3fArray>();
            }
        }

        // Handle left-handed winding order
        if (topology.GetOrientation() == HdTokens->leftHanded) {
            for (size_t i = 0; i < allTriangulatedIndices.size(); ++i) {
                std::swap(allTriangulatedIndices[i][1], allTriangulatedIndices[i][2]);
            }
            if (_uvInterp == HdInterpolationFaceVarying && _uvs.size() == allTriangulatedIndices.size() * 3) {
                for (size_t i = 0; i < allTriangulatedIndices.size(); ++i) {
                    std::swap(_uvs[i * 3 + 1], _uvs[i * 3 + 2]);
                }
            }
            if (_colorInterp == HdInterpolationFaceVarying && _colors.size() == allTriangulatedIndices.size() * 3) {
                for (size_t i = 0; i < allTriangulatedIndices.size(); ++i) {
                    std::swap(_colors[i * 3 + 1], _colors[i * 3 + 2]);
                }
            }
            if (_normalInterp == HdInterpolationFaceVarying && _normals.size() == allTriangulatedIndices.size() * 3) {
                for (size_t i = 0; i < allTriangulatedIndices.size(); ++i) {
                    std::swap(_normals[i * 3 + 1], _normals[i * 3 + 2]);
                }
            }
        }

        if (_normals.empty() && !_points.empty() && !allTriangulatedIndices.empty()) {
            VtVec3fArray smoothNormals(_points.size(), GfVec3f(0.0f));
            for (const auto& tri : allTriangulatedIndices) {
                GfVec3f p0 = _points[tri[0]];
                GfVec3f p1 = _points[tri[1]];
                GfVec3f p2 = _points[tri[2]];
                GfVec3f n = GfCross(p1 - p0, p2 - p0);
                smoothNormals[tri[0]] += n;
                smoothNormals[tri[1]] += n;
                smoothNormals[tri[2]] += n;
            }
            for (auto& n : smoothNormals) {
                n.Normalize();
            }
            _normals = smoothNormals;
            _normalInterp = HdInterpolationVertex;
        }
        
        // Map original faces to material IDs (GeomSubsets)
        HdGeomSubsets geomSubsets = topology.GetGeomSubsets();

        std::vector<SdfPath> faceMaterialPaths(topology.GetNumFaces(), defaultMaterialId);

        
        HDGEMINI_LOG << "[Gemini] Mesh " << id.GetText() << " splitting into subsets (Faces: " << topology.GetNumFaces() << "):" << std::endl;
        for (const auto& subset : geomSubsets) {
            HDGEMINI_LOG << "[Gemini]   Subset " << subset.id.GetText() << " | Material: " << subset.materialId.GetText() << " | Face count: " << subset.indices.size() << std::endl;
            for (int faceIdx : subset.indices) {
                if (faceIdx >= 0 && (size_t)faceIdx < faceMaterialPaths.size()) {
                    faceMaterialPaths[faceIdx] = subset.materialId;
                }
            }
        }
        
        // Group everything
        struct GroupedData {
            VtVec3iArray indices;
            VtVec3fArray colors;
            VtVec2fArray uvs;
            VtVec3fArray normals;
        };
        std::map<SdfPath, GroupedData> grouped;

        for (size_t i = 0; i < allTriangulatedIndices.size(); ++i) {
            // Correct face index decoding: 
            // - Shift right by 2 is typical for subdivided meshes where faceIndex is in high bits
            // - Masking with 0x0FFFFFFF is used for non-shifted face indices
            int faceIdx = trianglePrimitiveParams[i] >> 2;
            if (faceIdx < 0 || (size_t)faceIdx >= faceMaterialPaths.size()) {
                faceIdx = trianglePrimitiveParams[i] & 0x0FFFFFFF;
            }

            SdfPath matPath = defaultMaterialId;
            if (faceIdx >= 0 && (size_t)faceIdx < faceMaterialPaths.size()) {
                matPath = faceMaterialPaths[faceIdx];
            }
            
            GroupedData& g = grouped[matPath];
            g.indices.push_back(allTriangulatedIndices[i]);
            
            // Slice face-varying primvars
            if (_colors.size() == allTriangulatedIndices.size() * 3) {
                g.colors.push_back(_colors[i * 3 + 0]);
                g.colors.push_back(_colors[i * 3 + 1]);
                g.colors.push_back(_colors[i * 3 + 2]);
            }
            if (_uvs.size() == allTriangulatedIndices.size() * 3) {
                g.uvs.push_back(_uvs[i * 3 + 0]);
                g.uvs.push_back(_uvs[i * 3 + 1]);
                g.uvs.push_back(_uvs[i * 3 + 2]);
            }
            if (_normals.size() == allTriangulatedIndices.size() * 3) {
                g.normals.push_back(_normals[i * 3 + 0]);
                g.normals.push_back(_normals[i * 3 + 1]);
                g.normals.push_back(_normals[i * 3 + 2]);
            } else if (_normals.size() == _points.size()) {
                g.normals.push_back(_normals[allTriangulatedIndices[i][0]]);
                g.normals.push_back(_normals[allTriangulatedIndices[i][1]]);
                g.normals.push_back(_normals[allTriangulatedIndices[i][2]]);
            }
        }

        // Rebuild subsets
        _subsets.clear();
        for (auto& pair : grouped) {
            Subset subset;
            subset.materialId = pair.first;
            subset.indices = std::move(pair.second.indices);
            
            HDGEMINI_LOG << "[Gemini]   Created sub-mesh from " << id.GetText() << " for material " << subset.materialId.GetText() << " with " << subset.indices.size() << " triangles." << std::endl;

            subset.colors = pair.second.colors.empty() ? _colors : pair.second.colors;
            subset.uvs = pair.second.uvs.empty() ? _uvs : pair.second.uvs;
            VtVec3fArray subsetNormals = pair.second.normals.empty() ? _normals : pair.second.normals;

            if (!subset.indices.empty() && !_points.empty()) {
                subset.bvh.Build(_points, subset.indices, subset.uvs, subsetNormals, subset.colors, std::vector<int>());
                
                subset.range.SetEmpty();
                for (const auto& tri : subset.indices) {
                    subset.range.ExtendBy(_points[tri[0]]);
                    subset.range.ExtendBy(_points[tri[1]]);
                    subset.range.ExtendBy(_points[tri[2]]);
                }
            }
            _subsets.push_back(std::move(subset));
        }
        _subsetsDirty = false;
    }

    *dirtyBits &= ~HdChangeTracker::AllSceneDirtyBits;
}

void
HdGeminiMesh::_InitRepr(TfToken const &reprToken, HdDirtyBits *dirtyBits)
{
}

HdDirtyBits
HdGeminiMesh::_PropagateDirtyBits(HdDirtyBits bits) const
{
    return bits;
}
