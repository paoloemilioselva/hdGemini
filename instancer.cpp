#include "instancer.h"
#include "pxr/imaging/hd/sceneDelegate.h"
#include "pxr/imaging/hd/renderIndex.h"
#include "pxr/imaging/hd/tokens.h"
#include "pxr/base/vt/array.h"
#include "pxr/base/gf/matrix4d.h"
#include "pxr/base/gf/vec3f.h"
#include "pxr/base/gf/vec4f.h"
#include "pxr/base/gf/quath.h"
#include "pxr/base/gf/quatf.h"
#include "pxr/base/gf/quatd.h"
#include "pxr/base/vt/value.h"
#include <iostream>

PXR_NAMESPACE_USING_DIRECTIVE

HdGeminiInstancer::HdGeminiInstancer(HdSceneDelegate* delegate, SdfPath const& id)
    : HdInstancer(delegate, id)
{
}

void
HdGeminiInstancer::Sync(HdSceneDelegate *sceneDelegate,
                        HdRenderParam   *renderParam,
                        HdDirtyBits     *dirtyBits)
{
    _UpdateInstancer(sceneDelegate, dirtyBits);
    
    if (HdChangeTracker::IsAnyPrimvarDirty(*dirtyBits, GetId()) ||
        HdChangeTracker::IsTransformDirty(*dirtyBits, GetId())) {
        std::lock_guard<std::mutex> lock(_cacheMutex);
        _cachedTransforms.clear();
    }
}

VtMatrix4dArray
HdGeminiInstancer::ComputeInstanceTransforms(SdfPath const &prototypeId)
{
    {
        std::lock_guard<std::mutex> lock(_cacheMutex);
        auto it = _cachedTransforms.find(prototypeId);
        if (it != _cachedTransforms.end()) {
            return it->second;
        }
    }

    HdSceneDelegate* delegate = GetDelegate();
    SdfPath const& instancerId = GetId();

    VtIntArray instanceIndices = delegate->GetInstanceIndices(instancerId, prototypeId);

    VtMatrix4dArray transforms;
    if (instanceIndices.empty()) {
        std::lock_guard<std::mutex> lock(_cacheMutex);
        _cachedTransforms[prototypeId] = transforms;
        return transforms;
    }
    
    transforms.resize(instanceIndices.size());

    VtMatrix4dArray instancerTransforms;
    VtValue transformsVal = delegate->Get(instancerId, HdInstancerTokens->instanceTransforms);
    if (transformsVal.IsHolding<VtMatrix4dArray>()) {
        instancerTransforms = transformsVal.UncheckedGet<VtMatrix4dArray>();
    }

    VtVec3fArray instancerTranslations;
    VtValue translationsVal = delegate->Get(instancerId, HdInstancerTokens->instanceTranslations);
    if (translationsVal.IsHolding<VtVec3fArray>()) {
        instancerTranslations = translationsVal.UncheckedGet<VtVec3fArray>();
    }

    VtVec4fArray instancerRotations;
    VtQuathArray instancerRotationsH;
    VtValue rotationsVal = delegate->Get(instancerId, HdInstancerTokens->instanceRotations);
    if (rotationsVal.IsHolding<VtVec4fArray>()) {
        instancerRotations = rotationsVal.UncheckedGet<VtVec4fArray>();
    } else if (rotationsVal.IsHolding<VtQuathArray>()) {
        instancerRotationsH = rotationsVal.UncheckedGet<VtQuathArray>();
    }

    VtVec3fArray instancerScales;
    VtValue scalesVal = delegate->Get(instancerId, HdInstancerTokens->instanceScales);
    if (scalesVal.IsHolding<VtVec3fArray>()) {
        instancerScales = scalesVal.UncheckedGet<VtVec3fArray>();
    }

    GfMatrix4d instancerTransform = delegate->GetInstancerTransform(instancerId);

    for (size_t i = 0; i < instanceIndices.size(); ++i) {
        int index = instanceIndices[i];
        GfMatrix4d transform(1.0);

        if (!instancerScales.empty() && index < instancerScales.size()) {
            GfVec3f s = instancerScales[index];
            GfMatrix4d scaleMat(1.0);
            scaleMat.SetScale(GfVec3d(s[0], s[1], s[2]));
            transform = scaleMat * transform;
        }

        if (!instancerRotations.empty() && index < instancerRotations.size()) {
            GfVec4f r = instancerRotations[index];
            GfQuatd quat(r[0], r[1], r[2], r[3]);
            GfMatrix4d rotMat(1.0);
            rotMat.SetRotate(quat);
            transform = rotMat * transform;
        } else if (!instancerRotationsH.empty() && index < instancerRotationsH.size()) {
            GfQuath r = instancerRotationsH[index];
            GfQuatd quat(r.GetReal(), r.GetImaginary()[0], r.GetImaginary()[1], r.GetImaginary()[2]);
            GfMatrix4d rotMat(1.0);
            rotMat.SetRotate(quat);
            transform = rotMat * transform;
        }

        if (!instancerTranslations.empty() && index < instancerTranslations.size()) {
            GfVec3f t = instancerTranslations[index];
            GfMatrix4d transMat(1.0);
            transMat.SetTranslate(GfVec3d(t[0], t[1], t[2]));
            transform = transMat * transform;
        }

        if (!instancerTransforms.empty() && index < instancerTransforms.size()) {
            transform = instancerTransforms[index] * transform;
        }

        transforms[i] = transform * instancerTransform;
    }
    
    SdfPath parentInstancerId = GetParentId();
    if (!parentInstancerId.IsEmpty()) {
        HdGeminiInstancer* parentInstancer = static_cast<HdGeminiInstancer*>(
            GetDelegate()->GetRenderIndex().GetInstancer(parentInstancerId));
        if (parentInstancer) {
            VtMatrix4dArray parentTransforms = parentInstancer->ComputeInstanceTransforms(GetId());
            VtMatrix4dArray newTransforms;
            newTransforms.reserve(transforms.size() * parentTransforms.size());
            for (const auto& pt : parentTransforms) {
                for (const auto& t : transforms) {
                    newTransforms.push_back(t * pt);
                }
            }
            transforms = newTransforms;
        }
    }
    
    {
        std::lock_guard<std::mutex> lock(_cacheMutex);
        _cachedTransforms[prototypeId] = transforms;
    }
    return transforms;
}
