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
    // Use a much smaller epsilon to prevent rejecting valid intersections on scaled-down rays
    if (std::abs(det) < 1e-12f) return false;
    float invDet = 1.0f / det;
    GfVec3f tvec = rayOrigin - v0;
    float u = GfDot(tvec, pvec) * invDet;
    if (u < -1e-5f || u > 1.00001f) return false;
    GfVec3f qvec = GfCross(tvec, edge1);
    float v = GfDot(rayDir, qvec) * invDet;
    if (v < -1e-5f || u + v > 1.00001f) return false;
    t = GfDot(edge2, qvec) * invDet;
    outU = u;
    outV = v;
    return (t > 1e-6f);
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
        if (std::abs(rayDir[i]) < 1e-12f) {
            if (rayOrigin[i] < min[i] || rayOrigin[i] > max[i]) return false;
        } else {
            float invDir = 1.0f / rayDir[i];
            float t1 = (min[i] - rayOrigin[i]) * invDir;
            float t2 = (max[i] - rayOrigin[i]) * invDir;
            if (t1 > t2) std::swap(t1, t2);
            tmin = std::max(tmin, t1);
            tmax = std::min(tmax, t2);
            if (tmin > tmax + 1e-5f) return false;
        }
    }
    tMinHit = tmin;
    return tmax > 0 && tmax > 1e-6f;
}

void BVH::Build(const VtVec3fArray& points, const VtVec3iArray& indices, const VtVec2fArray& uvs, const VtVec3fArray& normals, const VtVec3fArray& colors, const std::vector<int>& materialIndices) {
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
            if (uvs.size() == 1) {
                tri.uv0 = tri.uv1 = tri.uv2 = uvs[0];
            } else if (uvs.size() == indices.size()) {
                tri.uv0 = tri.uv1 = tri.uv2 = uvs[i];
            } else if (uvs.size() == indices.size() * 3) {
                tri.uv0 = uvs[i * 3 + 0];
                tri.uv1 = uvs[i * 3 + 1];
                tri.uv2 = uvs[i * 3 + 2];
            } else {
                tri.uv0 = (triIdx[0] < (int)uvs.size()) ? uvs[triIdx[0]] : GfVec2f(0.0f);
                tri.uv1 = (triIdx[1] < (int)uvs.size()) ? uvs[triIdx[1]] : GfVec2f(0.0f);
                tri.uv2 = (triIdx[2] < (int)uvs.size()) ? uvs[triIdx[2]] : GfVec2f(0.0f);
            }
        } else {
            tri.uv0 = tri.uv1 = tri.uv2 = GfVec2f(0.0f);
        }

        if (!normals.empty()) {
            if (normals.size() == 1) {
                tri.n0 = tri.n1 = tri.n2 = normals[0];
            } else if (normals.size() == indices.size()) {
                tri.n0 = tri.n1 = tri.n2 = normals[i];
            } else if (normals.size() == indices.size() * 3) {
                tri.n0 = normals[i * 3 + 0];
                tri.n1 = normals[i * 3 + 1];
                tri.n2 = normals[i * 3 + 2];
            } else {
                tri.n0 = (triIdx[0] < (int)normals.size()) ? normals[triIdx[0]] : GfVec3f(0.0f, 1.0f, 0.0f);
                tri.n1 = (triIdx[1] < (int)normals.size()) ? normals[triIdx[1]] : GfVec3f(0.0f, 1.0f, 0.0f);
                tri.n2 = (triIdx[2] < (int)normals.size()) ? normals[triIdx[2]] : GfVec3f(0.0f, 1.0f, 0.0f);
            }
        } else {
            tri.n0 = tri.n1 = tri.n2 = GfVec3f(0.0f, 0.0f, 0.0f);
        }

        if (!colors.empty()) {
            if (colors.size() == 1) {
                tri.c0 = tri.c1 = tri.c2 = colors[0];
            } else if (colors.size() == indices.size()) {
                tri.c0 = tri.c1 = tri.c2 = colors[i];
            } else if (colors.size() == indices.size() * 3) {
                tri.c0 = colors[i * 3 + 0];
                tri.c1 = colors[i * 3 + 1];
                tri.c2 = colors[i * 3 + 2];
            } else {
                tri.c0 = (triIdx[0] < (int)colors.size()) ? colors[triIdx[0]] : GfVec3f(0.5f);
                tri.c1 = (triIdx[1] < (int)colors.size()) ? colors[triIdx[1]] : GfVec3f(0.5f);
                tri.c2 = (triIdx[2] < (int)colors.size()) ? colors[triIdx[2]] : GfVec3f(0.5f);
            }
        } else {
            tri.c0 = tri.c1 = tri.c2 = GfVec3f(0.5f);
        }

        // Compute tangent and bitangent (dpdu, dpdv)
        GfVec3f edge1 = tri.v1 - tri.v0;
        GfVec3f edge2 = tri.v2 - tri.v0;
        GfVec2f deltaUV1 = tri.uv1 - tri.uv0;
        GfVec2f deltaUV2 = tri.uv2 - tri.uv0;

        float f = deltaUV1[0] * deltaUV2[1] - deltaUV2[0] * deltaUV1[1];
        if (std::abs(f) > 1e-8f) {
            f = 1.0f / f;
            tri.dpdu = (edge1 * deltaUV2[1] - edge2 * deltaUV1[1]) * f;
            tri.dpdv = (edge2 * deltaUV1[0] - edge1 * deltaUV2[0]) * f;
            tri.dpdu.Normalize();
            tri.dpdv.Normalize();
        } else {
            // Fallback tangent frame
            GfVec3f n = GfCross(edge1, edge2).GetNormalized();
            GfVec3f up = std::abs(n[1]) < 0.999f ? GfVec3f(0, 1, 0) : GfVec3f(1, 0, 0);
            tri.dpdu = GfCross(up, n).GetNormalized();
            tri.dpdv = GfCross(n, tri.dpdu).GetNormalized();
        }

        tri.materialIndex = (i < materialIndices.size()) ? materialIndices[i] : -1;
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

bool BVH::Intersect(const GfVec3f& rayOrigin, const GfVec3f& rayDir, float& t, GfVec3f& normal, GfVec2f& uv, GfVec3f& smoothNormal, GfVec3f& dpdu, GfVec3f& dpdv, GfVec3f& smoothColor, int& materialIndex) const {
    if (_nodes.empty()) return false;
    if (_isCurveBVH) {
        return _IntersectCurveNode(0, rayOrigin, rayDir, t, normal, uv, smoothNormal, dpdu, dpdv, smoothColor, materialIndex);
    } else {
        return _IntersectNode(0, rayOrigin, rayDir, t, normal, uv, smoothNormal, dpdu, dpdv, smoothColor, materialIndex);
    }
}

bool BVH::_IntersectNode(int nodeIdx, const GfVec3f& rayOrigin, const GfVec3f& rayDir, float& t, GfVec3f& normal, GfVec2f& uv, GfVec3f& smoothNormal, GfVec3f& dpdu, GfVec3f& dpdv, GfVec3f& smoothColor, int& materialIndex) const {
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
                    float w = 1.0f - triU - triV;
                    uv = tri.uv0 * w + tri.uv1 * triU + tri.uv2 * triV;
                    
                    if (tri.n0.GetLengthSq() > 1e-6f) {
                        smoothNormal = (tri.n0 * w + tri.n1 * triU + tri.n2 * triV).GetNormalized();
                    } else {
                        smoothNormal = normal;
                    }
                    dpdu = tri.dpdu;
                    dpdv = tri.dpdv;
                    smoothColor = (tri.c0 * w + tri.c1 * triU + tri.c2 * triV);
                    materialIndex = tri.materialIndex;
                    hit = true;
                }
            }
        }
        return hit;
    } else {
        bool hitLeft = _IntersectNode(node.leftChild, rayOrigin, rayDir, t, normal, uv, smoothNormal, dpdu, dpdv, smoothColor, materialIndex);
        bool hitRight = _IntersectNode(node.leftChild + 1, rayOrigin, rayDir, t, normal, uv, smoothNormal, dpdu, dpdv, smoothColor, materialIndex);
        return hitLeft || hitRight;
    }
}

static bool IntersectCurveSegment(const GfVec3f& ro, const GfVec3f& rd, const BVHCurveSegment& seg, float& tHit, GfVec3f& normalHit, GfVec2f& uvHit)
{
    if (seg.hasNormals) {
        GfVec3f edge = seg.p1 - seg.p0;
        GfVec3f n_avg = (seg.n0 + seg.n1).GetNormalized();
        GfVec3f bitangent = GfCross(edge, n_avg).GetNormalized();
        
        GfVec3f v0 = seg.p0 - bitangent * (seg.w0 * 0.5f);
        GfVec3f v1 = seg.p0 + bitangent * (seg.w0 * 0.5f);
        GfVec3f v2 = seg.p1 + bitangent * (seg.w1 * 0.5f);
        GfVec3f v3 = seg.p1 - bitangent * (seg.w1 * 0.5f);
        
        float t, u, v;
        if (IntersectTriangle(ro, rd, v0, v1, v2, t, u, v)) {
            tHit = t; normalHit = GfCross(v1-v0, v2-v0).GetNormalized(); uvHit = GfVec2f(u, v); return true;
        }
        if (IntersectTriangle(ro, rd, v0, v2, v3, t, u, v)) {
            tHit = t; normalHit = GfCross(v2-v0, v3-v0).GetNormalized(); uvHit = GfVec2f(u, v); return true;
        }
        return false;
    } else {
        GfVec3f pa = seg.p0; GfVec3f pb = seg.p1;
        float r = (seg.w0 + seg.w1) * 0.25f; // avg radius
        GfVec3f ba = pb - pa;
        float baba = GfDot(ba, ba);
        if (baba < 1e-8f) return false;
        
        GfVec3f oc = ro - pa;
        float bard = GfDot(ba, rd);
        float baoc = GfDot(ba, oc);
        float k2 = baba - bard * bard;
        float k1 = baba * GfDot(oc, rd) - baoc * bard;
        float k0 = baba * GfDot(oc, oc) - baoc * baoc - r * r * baba;
        float h = k1 * k1 - k2 * k0;
        if (h < 0.0f) return false;
        
        h = std::sqrt(h);
        float t = (-k1 - h) / k2;
        float y = baoc + t * bard;
        if (y > 0.0f && y < baba && t > 1e-6f) {
            tHit = t;
            GfVec3f p = ro + rd * tHit;
            normalHit = (p - pa - ba * (y / baba)).GetNormalized();
            uvHit = GfVec2f(0.5f, y / baba);
            return true;
        }
        return false;
    }
}

void BVH::BuildCurves(const VtVec3fArray& points, const VtFloatArray& widths, const VtVec3fArray& normals, const VtIntArray& curveVertexCounts, const VtIntArray& indices, int materialId) {
    _isCurveBVH = true;
    _curveSegments.clear();
    _nodes.clear();
    if (points.empty() || curveVertexCounts.empty()) return;
    
    int pointOffset = 0;
    for (size_t i = 0; i < curveVertexCounts.size(); ++i) {
        int count = curveVertexCounts[i];
        for (int j = 0; j < count - 1; ++j) {
            int i0 = pointOffset + j;
            int i1 = pointOffset + j + 1;
            if (!indices.empty()) { i0 = indices[i0]; i1 = indices[i1]; }
            
            BVHCurveSegment seg;
            seg.p0 = points[i0]; seg.p1 = points[i1];
            seg.w0 = (i0 < widths.size()) ? widths[i0] : 0.1f;
            seg.w1 = (i1 < widths.size()) ? widths[i1] : 0.1f;
            if (!normals.empty() && i0 < normals.size() && i1 < normals.size()) {
                seg.n0 = normals[i0]; seg.n1 = normals[i1]; seg.hasNormals = true;
            } else { seg.hasNormals = false; }
            seg.centroid = (seg.p0 + seg.p1) * 0.5f;
            seg.materialIndex = materialId;
            _curveSegments.push_back(seg);
        }
        pointOffset += count;
    }
    
    if (_curveSegments.empty()) return;
    _nodes.reserve(_curveSegments.size() * 2);
    _nodes.push_back(BVHNode());
    _SubdivideCurves(0, 0, (int)_curveSegments.size());
}

void BVH::_SubdivideCurves(int nodeIdx, int start, int end) {
    BVHNode& node = _nodes[nodeIdx];
    node.bounds.SetEmpty();
    for (int i = start; i < end; ++i) {
        float r = std::max(_curveSegments[i].w0, _curveSegments[i].w1) * 0.5f;
        GfVec3f rvec(r);
        node.bounds.ExtendBy(_curveSegments[i].p0 - rvec);
        node.bounds.ExtendBy(_curveSegments[i].p0 + rvec);
        node.bounds.ExtendBy(_curveSegments[i].p1 - rvec);
        node.bounds.ExtendBy(_curveSegments[i].p1 + rvec);
    }

    int count = end - start;
    if (count <= 4) { node.leftChild = -start - 1; node.triangleCount = count; return; }

    GfVec3f size = node.bounds.GetSize();
    int axis = 0;
    if (size[1] > size[0]) axis = 1;
    if (size[2] > size[axis]) axis = 2;
    float splitPos = node.bounds.GetMin()[axis] + size[axis] * 0.5f;

    int i = start; int j = end - 1;
    while (i <= j) {
        if (_curveSegments[i].centroid[axis] < splitPos) { i++; } else { std::swap(_curveSegments[i], _curveSegments[j]); j--; }
    }

    int leftCount = i - start;
    if (leftCount == 0 || leftCount == count) i = start + count / 2;

    int leftChildIdx = (int)_nodes.size();
    _nodes.push_back(BVHNode()); _nodes.push_back(BVHNode());
    _nodes[nodeIdx].leftChild = leftChildIdx; _nodes[nodeIdx].triangleCount = 0;

    _SubdivideCurves(leftChildIdx, start, i);
    _SubdivideCurves(leftChildIdx + 1, i, end);
}

bool BVH::_IntersectCurveNode(int nodeIdx, const GfVec3f& rayOrigin, const GfVec3f& rayDir, float& t, GfVec3f& normal, GfVec2f& uv, GfVec3f& smoothNormal, GfVec3f& dpdu, GfVec3f& dpdv, GfVec3f& smoothColor, int& materialIndex) const {
    const BVHNode& node = _nodes[nodeIdx];
    float tAabb;
    if (!IntersectAABB(rayOrigin, rayDir, node.bounds, tAabb)) return false;
    if (tAabb > t) return false;

    if (node.leftChild < 0) {
        bool hit = false;
        int start = -node.leftChild - 1;
        for (int i = 0; i < node.triangleCount; ++i) {
            const auto& seg = _curveSegments[start + i];
            float segT; GfVec3f segN; GfVec2f segUV;
            if (IntersectCurveSegment(rayOrigin, rayDir, seg, segT, segN, segUV)) {
                if (segT < t) {
                    t = segT; normal = segN; smoothNormal = segN; uv = segUV;
                    dpdu = GfVec3f(1,0,0); dpdv = GfVec3f(0,1,0); smoothColor = GfVec3f(0.5f);
                    materialIndex = seg.materialIndex;
                    hit = true;
                }
            }
        }
        return hit;
    } else {
        bool hitLeft = _IntersectCurveNode(node.leftChild, rayOrigin, rayDir, t, normal, uv, smoothNormal, dpdu, dpdv, smoothColor, materialIndex);
        bool hitRight = _IntersectCurveNode(node.leftChild + 1, rayOrigin, rayDir, t, normal, uv, smoothNormal, dpdu, dpdv, smoothColor, materialIndex);
        return hitLeft || hitRight;
    }
}
