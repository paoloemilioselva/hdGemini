#include "bvh.h"
#include <algorithm>
#include <cmath>

PXR_NAMESPACE_USING_DIRECTIVE

static bool 
IntersectTriangle(const GfVec3f& rayOrigin, const GfVec3f& rayDir,
                  const GfVec3f& v0, const GfVec3f& v1, const GfVec3f& v2,
                  float& t, float& outU, float& outV)
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
    outU = u;
    outV = v;
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

void BVH::Build(const VtVec3fArray& points, const VtVec3iArray& indices, const VtVec2fArray& uvs) {
    _triangles.clear();
    _nodes.clear();
    if (indices.empty() || points.empty()) return;

    _triangles.reserve(indices.size());
    for (size_t i = 0; i < indices.size(); ++i) {
        const auto& triIdx = indices[i];
        if (triIdx[0] < 0 || triIdx[0] >= (int)points.size() || 
            triIdx[1] < 0 || triIdx[1] >= (int)points.size() || 
            triIdx[2] < 0 || triIdx[2] >= (int)points.size()) continue;
        
        BVHTriangle tri;
        tri.v0 = points[triIdx[0]];
        tri.v1 = points[triIdx[1]];
        tri.v2 = points[triIdx[2]];

        if (!uvs.empty()) {
            if (uvs.size() == indices.size() * 3) {
                // Face-varying (triangulated) UVs
                tri.uv0 = uvs[i * 3 + 0];
                tri.uv1 = uvs[i * 3 + 1];
                tri.uv2 = uvs[i * 3 + 2];
            } else {
                // Vertex-indexed UVs
                tri.uv0 = (triIdx[0] < (int)uvs.size()) ? uvs[triIdx[0]] : GfVec2f(0.0f);
                tri.uv1 = (triIdx[1] < (int)uvs.size()) ? uvs[triIdx[1]] : GfVec2f(0.0f);
                tri.uv2 = (triIdx[2] < (int)uvs.size()) ? uvs[triIdx[2]] : GfVec2f(0.0f);
            }
        } else {
            tri.uv0 = tri.uv1 = tri.uv2 = GfVec2f(0.0f);
        }

        tri.centroid = (tri.v0 + tri.v1 + tri.v2) / 3.0f;
        _triangles.push_back(tri);
    }

    if (_triangles.empty()) return;

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
    _nodes[nodeIdx].leftChild = leftChildIdx;
    _nodes[nodeIdx].triangleCount = 0;

    _Subdivide(leftChildIdx, start, i);
    _Subdivide(leftChildIdx + 1, i, end);
}

bool BVH::Intersect(const GfVec3f& rayOrigin, const GfVec3f& rayDir, float& t, GfVec3f& normal, GfVec2f& uv) const {
    if (_nodes.empty()) return false;
    return _IntersectNode(0, rayOrigin, rayDir, t, normal, uv);
}

bool BVH::_IntersectNode(int nodeIdx, const GfVec3f& rayOrigin, const GfVec3f& rayDir, float& t, GfVec3f& normal, GfVec2f& uv) const {
    const BVHNode& node = _nodes[nodeIdx];
    float tAabb;
    if (!IntersectAABB(rayOrigin, rayDir, node.bounds, tAabb)) return false;
    if (tAabb > t) return false;

    if (node.leftChild < 0) {
        bool hit = false;
        int start = -node.leftChild - 1;
        for (int i = 0; i < node.triangleCount; ++i) {
            const auto& tri = _triangles[start + i];
            float triT, triU, triV;
            if (IntersectTriangle(rayOrigin, rayDir, tri.v0, tri.v1, tri.v2, triT, triU, triV)) {
                if (triT < t) {
                    t = triT;
                    normal = GfCross(tri.v1 - tri.v0, tri.v2 - tri.v0).GetNormalized();
                    uv = tri.uv0 * (1.0f - triU - triV) + tri.uv1 * triU + tri.uv2 * triV;
                    hit = true;
                }
            }
        }
        return hit;
    } else {
        bool hitLeft = _IntersectNode(node.leftChild, rayOrigin, rayDir, t, normal, uv);
        bool hitRight = _IntersectNode(node.leftChild + 1, rayOrigin, rayDir, t, normal, uv);
        return hitLeft || hitRight;
    }
}
