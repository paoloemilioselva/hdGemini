#ifndef HD_GEMINI_VOLUME_H
#define HD_GEMINI_VOLUME_H

#include "pxr/pxr.h"
#include "pxr/imaging/hd/volume.h"
#include "field.h"
#include <unordered_map>

PXR_NAMESPACE_USING_DIRECTIVE

class HdGeminiVolume final : public HdVolume {
public:
    HdGeminiVolume(SdfPath const& id);
    virtual ~HdGeminiVolume() = default;

    virtual void Sync(HdSceneDelegate *sceneDelegate,
                      HdRenderParam   *renderParam,
                      HdDirtyBits     *dirtyBits,
                      TfToken const   &reprToken) override;
                      
    virtual void Finalize(HdRenderParam *renderParam) override;

    virtual HdDirtyBits GetInitialDirtyBitsMask() const override;

    const GfRange3d& GetExtents() const { return _extents; }
    const GfMatrix4d& GetTransform() const { return _transform; }
    
    // Semantic mappings (e.g., "density" -> Field)
    HdGeminiField* GetField(const TfToken& semantic) const {
        auto it = _fields.find(semantic);
        if (it != _fields.end()) return it->second;
        return nullptr;
    }

protected:
    virtual HdDirtyBits _PropagateDirtyBits(HdDirtyBits bits) const override;
    virtual void _InitRepr(TfToken const &reprToken, HdDirtyBits *dirtyBits) override;

private:
    GfRange3d _extents;
    GfMatrix4d _transform;
    std::unordered_map<TfToken, HdGeminiField*, TfToken::HashFunctor> _fields;
};

#endif // HD_GEMINI_VOLUME_H
