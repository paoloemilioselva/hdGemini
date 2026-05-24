#ifndef HD_GEMINI_BVH_H
#define HD_GEMINI_BVH_H

#include <pxr/pxr.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/gf/vec3i.h>
#include <pxr/base/gf/vec2f.h>
#include <pxr/base/gf/range3f.h>
#include <pxr/base/vt/array.h>
#include <vector>

PXR_NAMESPACE_USING_DIRECTIVE

struct BVHNode {
    GfRange3f bounds;
    int leftChild; // if < 0, it's a leaf and -leftChild-1 is the start index in the triangle list
    int triangleCount;
};

struct BVHTriangle {
    GfVec3f v0, v1, v2;
    GfVec2f uv0, uv1, uv2;
    GfVec3f n0, n1, n2;
    GfVec3f c0, c1, c2;
    GfVec3f dpdu, dpdv;
    GfVec3f centroid;
    int materialIndex = -1;
};

struct BVHCurveSegment {
    GfVec3f p0, p1;
    float w0, w1;
    GfVec3f n0, n1;
    bool hasNormals;
    GfVec3f centroid;
    int materialIndex = -1;
};

class BVH {
public:
    BVH() : _isCurveBVH(false) {}
    void Build(const VtVec3fArray& points, const VtVec3iArray& indices, const VtVec2fArray& uvs, const VtVec3fArray& normals, const VtVec3fArray& colors, const std::vector<int>& materialIndices);
    void BuildCurves(const VtVec3fArray& points, const VtFloatArray& widths, const VtVec3fArray& normals, const VtIntArray& curveVertexCounts, const VtIntArray& indices, int materialId);
    bool Intersect(const GfVec3f& rayOrigin, const GfVec3f& rayDir, float& t, GfVec3f& normal, GfVec2f& uv, GfVec3f& smoothNormal, GfVec3f& dpdu, GfVec3f& dpdv, GfVec3f& smoothColor, int& materialIndex) const;
    bool IsEmpty() const { return _nodes.empty(); }

private:
    struct BuildItem {
        int nodeIdx;
        int start, end;
    };

    void _Subdivide(int nodeIdx, int start, int end);
    void _SubdivideCurves(int nodeIdx, int start, int end);
    bool _IntersectNode(int nodeIdx, const GfVec3f& rayOrigin, const GfVec3f& rayDir, float& t, GfVec3f& normal, GfVec2f& uv, GfVec3f& smoothNormal, GfVec3f& dpdu, GfVec3f& dpdv, GfVec3f& smoothColor, int& materialIndex) const;
    bool _IntersectCurveNode(int nodeIdx, const GfVec3f& rayOrigin, const GfVec3f& rayDir, float& t, GfVec3f& normal, GfVec2f& uv, GfVec3f& smoothNormal, GfVec3f& dpdu, GfVec3f& dpdv, GfVec3f& smoothColor, int& materialIndex) const;

    std::vector<BVHNode> _nodes;
    std::vector<BVHTriangle> _triangles;
    std::vector<BVHCurveSegment> _curveSegments;
    bool _isCurveBVH;
};

#endif // HD_GEMINI_BVH_H
