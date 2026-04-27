#ifndef HD_GEMINI_RENDER_DELEGATE_H
#define HD_GEMINI_RENDER_DELEGATE_H

#include "pxr/pxr.h"
#include "pxr/imaging/hd/renderDelegate.h"
#include "pxr/imaging/hd/renderThread.h"
#include "pxr/imaging/hd/resourceRegistry.h"
#include "renderer.h"

#include <mutex>

PXR_NAMESPACE_OPEN_SCOPE

class HdGeminiRenderParam;

class HdGeminiRenderDelegate final : public HdRenderDelegate
{
public:
    HdGeminiRenderDelegate();
    HdGeminiRenderDelegate(HdRenderSettingsMap const& settingsMap);
    virtual ~HdGeminiRenderDelegate() override;

    virtual HdRenderParam *GetRenderParam() const override;

    virtual const TfTokenVector &GetSupportedRprimTypes() const override;
    virtual const TfTokenVector &GetSupportedSprimTypes() const override;
    virtual const TfTokenVector &GetSupportedBprimTypes() const override;

    virtual HdResourceRegistrySharedPtr GetResourceRegistry() const override;

    virtual HdRenderPassSharedPtr CreateRenderPass(HdRenderIndex *index,
                HdRprimCollection const& collection) override;

    virtual HdInstancer *CreateInstancer(HdSceneDelegate *delegate,
                                 SdfPath const& id) override;
    virtual void DestroyInstancer(HdInstancer *instancer) override;

    virtual HdRprim *CreateRprim(TfToken const& typeId,
                         SdfPath const& rprimId) override;
    virtual void DestroyRprim(HdRprim *rPrim) override;

    virtual HdSprim *CreateSprim(TfToken const& typeId,
                         SdfPath const& sprimId) override;
    virtual HdSprim *CreateFallbackSprim(TfToken const& typeId) override;
    virtual void DestroySprim(HdSprim *sPrim) override;

    virtual HdBprim *CreateBprim(TfToken const& typeId,
                         SdfPath const& bprimId) override;
    virtual HdBprim *CreateFallbackBprim(TfToken const& typeId) override;
    virtual void DestroyBprim(HdBprim *bPrim) override;

    virtual void CommitResources(HdChangeTracker *tracker) override;

    virtual HdAovDescriptor GetDefaultAovDescriptor(TfToken const& name) const override;

    void AddMesh(const SdfPath& id, HdGeminiMesh* mesh);
    void RemoveMesh(const SdfPath& id);
    const std::map<SdfPath, HdGeminiMesh*>& GetMeshes() const { return _meshes; }

private:
    static const TfTokenVector SUPPORTED_RPRIM_TYPES;
    static const TfTokenVector SUPPORTED_SPRIM_TYPES;
    static const TfTokenVector SUPPORTED_BPRIM_TYPES;

    static std::mutex _mutexResourceRegistry;
    static std::atomic_int _counterResourceRegistry;
    static HdResourceRegistrySharedPtr _resourceRegistry;

    void _Initialize();

    std::shared_ptr<HdGeminiRenderParam> _renderParam;
    HdRenderThread _renderThread;
    HdGeminiRenderer _renderer;
    
    std::atomic<int> _sceneVersion;

    std::map<SdfPath, HdGeminiMesh*> _meshes;

    HdGeminiRenderDelegate(const HdGeminiRenderDelegate &) = delete;
    HdGeminiRenderDelegate &operator =(const HdGeminiRenderDelegate &) = delete;
};

PXR_NAMESPACE_OPEN_SCOPE

#endif // HD_GEMINI_RENDER_DELEGATE_H
