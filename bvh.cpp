#include "bvh.h"
#include <algorithm>
#include <cmath>

PXR_NAMESPACE_USING_DIRECTIVE

static bool 
IntersectTriangle(const GfVec3f& rayOrigin, const GfVec3f& rayDir,
                  const GfVec3f& v0, const GfVec3f& v1, const GfVec3f& v2,
                  float& t)
{
    GfVec3f edge1 = v1 - v0;
    GfVec3f edge2 = v2 - v0;
    GfVec3f pvec = GfCross(rayDir, edge2);
    float det = GfDot(edge1, pvec);
    if (std::abs(det) < 1e-8) return false;
    float invDet = 1.0f / det;
    GfVec3f tvec = rayOrigin - v0;
    float u = GfDot(tvec, pvec) * invDet;
    if (u < 0.0f || u > 1.0f) return false;
    GfVec3f qvec = GfCross(tvec, edge1);
    float v = GfDot(rayDir, qvec) * invDet;
    if (v < 0.0f || u + v > 1.0f) return false;
    t = GfDot(edge2, qvec) * invDet;
    return (t > 1e-4);
}

static bool
IntersectAABB(const GfVec3f& rayOrigin, const GfVec3f& rayDir, const GfRange3f& range, float& tMinHit)
{
    if (range.IsEmpty()) return false;
    const GfVec3f& min = range.GetMin();
    const GfVec3f& max = range.GetMax();

    float tmin = -1e30f;
    float tmax = 1e30f;

    for (int i = 0; i < 3; ++i) {
        if (std::abs(rayDir[i]) < 1e-8) {
            if (rayOrigin[i] < min[i] || rayOrigin[i] > max[i]) return false;
        } else {
            float invDir = 1.0f / rayDir[i];
            float t1 = (min[i] - rayOrigin[i]) * invDir;
            float t2 = (max[i] - rayOrigin[i]) * invDir;
            if (t1 > t2) std::swap(t1, t2);
            tmin = std::max(tmin, t1);
            tmax = std::min(tmax, t2);
            if (tmin > tmax) return false;
        }
    }
    tMinHit = tmin;
    return tmax > 0 && tmax > 1e-4;
}

void BVH::Build(const VtVec3fArray& points, const VtVec3iArray& indices) {
    _triangles.clear();
    _nodes.clear();
    if (indices.empty()) return;

    _triangles.reserve(indices.size());
    for (const auto& triIdx : indices) {
        BVHTriangle tri;
        tri.v0 = points[triIdx[0]];
        tri.v1 = points[triIdx[1]];
        tri.v2 = points[triIdx[2]];
        tri.centroid = (tri.v0 + tri.v1 + tri.v2) / 3.0f;
        _triangles.push_back(tri);
    }

    _nodes.reserve(indices.size() * 2);
    _nodes.push_back(BVHNode()); // Root
    _Subdivide(0, 0, (int)_triangles.size());
}

void BVH::_Subdivide(int nodeIdx, int start, int end) {
    BVHNode& node = _nodes[nodeIdx];
    node.bounds.SetEmpty();
    for (int i = start; i < end; ++i) {
        node.bounds.ExtendBy(_triangles[i].v0);
        node.bounds.ExtendBy(_triangles[i].v1);
        node.bounds.ExtendBy(_triangles[i].v2);
    }

    int count = end - start;
    if (count <= 4) {
        node.leftChild = -start - 1;
        node.triangleCount = count;
        return;
    }

    // Split along largest axis
    GfVec3f size = node.bounds.GetSize();
    int axis = 0;
    if (size[1] > size[0]) axis = 1;
    if (size[2] > size[axis]) axis = 2;

    float splitPos = node.bounds.GetMin()[axis] + size[axis] * 0.5f;

    int i = start;
    int j = end - 1;
    while (i <= j) {
        if (_triangles[i].centroid[axis] < splitPos) {
            i++;
        } else {
            std::swap(_triangles[i], _triangles[j]);
            j--;
        }
    }

    int leftCount = i - start;
    if (leftCount == 0 || leftCount == count) {
        // Failed split, just split in middle
        i = start + count / 2;
    }

    int leftChildIdx = (int)_nodes.size();
    _nodes.push_back(BVHNode());
    _nodes.push_back(BVHNode());
    node.leftChild = leftChildIdx;
    node.triangleCount = 0;

    _Subdivide(leftChildIdx, start, i);
    _Subdivide(leftChildIdx + 1, i, end);
}

bool BVH::Intersect(const GfVec3f& rayOrigin, const GfVec3f& rayDir, float& t, GfVec3f& normal) const {
    if (_nodes.empty()) return false;
    return _IntersectNode(0, rayOrigin, rayDir, t, normal);
}

bool BVH::_IntersectNode(int nodeIdx, const GfVec3f& rayOrigin, const GfVec3f& rayDir, float& t, GfVec3f& normal) const {
    const BVHNode& node = _nodes[nodeIdx];
    float tAabb;
    if (!IntersectAABB(rayOrigin, rayDir, node.bounds, tAabb)) return false;
    if (tAabb > t) return false;

    if (node.leftChild < 0) {
        bool hit = false;
        int start = -node.leftChild - 1;
        for (int i = 0; i < node.triangleCount; ++i) {
            const auto& tri = _triangles[start + i];
            float triT;
            if (IntersectTriangle(rayOrigin, rayDir, tri.v0, tri.v1, tri.v2, triT)) {
                if (triT < t) {
                    t = triT;
                    normal = GfCross(tri.v1 - tri.v0, tri.v2 - tri.v0).GetNormalized();
                    hit = true;
                }
            }
        }
        return hit;
    } else {
        bool hitLeft = _IntersectNode(node.leftChild, rayOrigin, rayDir, t, normal);
        bool hitRight = _IntersectNode(node.leftChild + 1, rayOrigin, rayDir, t, normal);
        return hitLeft || hitRight;
    }
}
