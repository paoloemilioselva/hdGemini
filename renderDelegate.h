#ifndef HD_GEMINI_RENDER_DELEGATE_H
#define HD_GEMINI_RENDER_DELEGATE_H

#include "pxr/pxr.h"
#include "pxr/imaging/hd/renderDelegate.h"
#include "pxr/imaging/hd/renderThread.h"
#include "pxr/imaging/hd/resourceRegistry.h"
#include "renderer.h"

#include <mutex>
#include <map>
#include <vector>

PXR_NAMESPACE_USING_DIRECTIVE

class HdGeminiRenderParam;
class HdGeminiMesh;
class HdGeminiInstancer;
class HdGeminiLight;
class HdGeminiMaterial;

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
    virtual TfTokenVector GetShaderSourceTypes() const override;

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

    void AddInstancer(const SdfPath& id, HdGeminiInstancer* instancer);
    void RemoveInstancer(const SdfPath& id);
    HdGeminiInstancer* GetInstancer(const SdfPath& id) const;

    void AddMaterial(const SdfPath& id, HdGeminiMaterial* material);
    void RemoveMaterial(const SdfPath& id);
    HdGeminiMaterial* GetMaterial(const SdfPath& id) const;

    void AddLight(const SdfPath& id, HdGeminiLight* light);
    void RemoveLight(const SdfPath& id);
    const std::map<SdfPath, HdGeminiLight*>& GetLights() const { return _lights; }

    std::recursive_mutex& GetSceneLock() { return _sceneLock; }

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
    std::map<SdfPath, HdGeminiInstancer*> _instancers;
    std::map<SdfPath, HdGeminiLight*> _lights;
    std::map<SdfPath, HdGeminiMaterial*> _materials;
    std::recursive_mutex _sceneLock;

    HdGeminiRenderDelegate(const HdGeminiRenderDelegate &) = delete;
    HdGeminiRenderDelegate &operator =(const HdGeminiRenderDelegate &) = delete;
};

#endif // HD_GEMINI_RENDER_DELEGATE_H
