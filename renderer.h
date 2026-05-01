#ifndef HD_GEMINI_RENDERER_H
#define HD_GEMINI_RENDERER_H

#include "pxr/pxr.h"
#include "pxr/imaging/hd/renderThread.h"
#include "pxr/imaging/hd/aov.h"
#include "pxr/base/gf/matrix4d.h"
#include "pxr/base/gf/matrix4f.h"
#include "pxr/base/gf/rect2i.h"
#include "pxr/base/gf/vec3f.h"
#include "pxr/base/gf/range3f.h"
#include <vector>
#include <atomic>

PXR_NAMESPACE_USING_DIRECTIVE

class HdGeminiRenderDelegate;
class HdGeminiMesh;
class HdGeminiLight;

class HdGeminiRenderer final
{
public:
    HdGeminiRenderer();
    ~HdGeminiRenderer();

    void SetCamera(const GfMatrix4d& viewMatrix, const GfMatrix4d& projMatrix);
    void SetDataWindow(const GfRect2i& dataWindow);
    void SetAovBindings(const HdRenderPassAovBindingVector& aovBindings);
    const HdRenderPassAovBindingVector& GetAovBindings() const { return _aovBindings; }

    void Render(HdRenderThread *renderThread, HdGeminiRenderDelegate* delegate);
    void Clear();
    void MarkAovBuffersUnconverged();
    bool IsConverged() const { return false; } // Keep accumulating samples

private:
    struct MeshInstance {
        HdGeminiMesh* mesh;
        GfMatrix4f transform;
        GfMatrix4f invTransform;
        GfRange3f bounds;
        GfVec3f centroid;
    };

    struct TLASNode {
        GfRange3f bounds;
        int leftChild;
        int instanceCount;
    };

    struct HitRecord {
        float t = 1e30f;
        GfVec3f normal;
        GfVec3f baseColor = GfVec3f(1.0f);
        bool hit = false;
    };

    static GfVec4f _GetClearColor(VtValue const& clearValue);
    void _RenderTiles(HdRenderThread *renderThread, HdGeminiRenderDelegate* delegate);
    void _PrepareScene(HdRenderThread *renderThread, HdGeminiRenderDelegate* delegate);
    void _BuildTLAS(HdRenderThread *renderThread);
    void _SubdivideTLAS(int nodeIdx, int start, int end, HdRenderThread *renderThread);
    bool _IntersectTLAS(int nodeIdx, const GfVec3f& rayOrigin, const GfVec3f& rayDir, HitRecord& hit, HdRenderThread* renderThread) const;
    GfVec3f _TraceRay(const GfVec3f& rayOrigin, const GfVec3f& rayDir, int depth, bool isInteractive, HdRenderThread* renderThread, const std::map<SdfPath, HdGeminiLight*>& lights, uint32_t& rng) const;
    GfVec3f _SampleEnvironment(const GfVec3f& rayDir, const std::map<SdfPath, HdGeminiLight*>& lights) const;

    HdRenderPassAovBindingVector _aovBindings;
    GfRect2i _dataWindow;
    GfMatrix4d _viewMatrix;
    GfMatrix4d _projMatrix;
    GfMatrix4d _inverseViewMatrix;
    GfMatrix4d _inverseProjMatrix;
    std::vector<MeshInstance> _instances;
    std::vector<TLASNode> _tlasNodes;
    std::vector<int> _tlasInstanceIndices;
    int _resolutionLevel;
    int _frameCount;

    std::vector<float> _envMapPixels;
    int _envMapWidth = 0;
    int _envMapHeight = 0;
    std::vector<float> _envMapRowCdf;
    std::vector<float> _envMapColCdf;
};

#endif // HD_GEMINI_RENDERER_H
