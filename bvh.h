#ifndef HD_GEMINI_BVH_H
#define HD_GEMINI_BVH_H

#include <pxr/pxr.h>
#include <pxr/base/gf/vec3f.h>
#include <pxr/base/gf/vec3i.h>
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
    GfVec3f centroid;
};

class BVH {
public:
    BVH() = default;
    void Build(const VtVec3fArray& points, const VtVec3iArray& indices);
    bool Intersect(const GfVec3f& rayOrigin, const GfVec3f& rayDir, float& t, GfVec3f& normal) const;

private:
    struct BuildItem {
        int nodeIdx;
        int start, end;
    };

    void _Subdivide(int nodeIdx, int start, int end);
    bool _IntersectNode(int nodeIdx, const GfVec3f& rayOrigin, const GfVec3f& rayDir, float& t, GfVec3f& normal) const;

    std::vector<BVHNode> _nodes;
    std::vector<BVHTriangle> _triangles;
};

#endif // HD_GEMINI_BVH_H
