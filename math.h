#pragma once
#include <cmath>
#include <stdint.h>
#include <cstring>

// --- Basic math types ---

struct Vector2 {
    float x, y;
    Vector2() : x(0), y(0) {}
    Vector2(float x, float y) : x(x), y(y) {}
};

struct Vector3 {
    float x, y, z;
    Vector3() : x(0), y(0), z(0) {}
    Vector3(float x, float y, float z) : x(x), y(y), z(z) {}
    float Length() const { return sqrtf(x*x + y*y + z*z); }
    Vector3 operator-(const Vector3& o) const { return {x-o.x, y-o.y, z-o.z}; }
    Vector3 operator+(const Vector3& o) const { return {x+o.x, y+o.y, z+o.z}; }
    Vector3 operator*(float s) const { return {x*s, y*s, z*s}; }
    Vector3 operator/(float s) const { return {x/s, y/s, z/s}; }
    float Dot(const Vector3& o) const { return x*o.x + y*o.y + z*o.z; }
};

struct Matrix4x4 {
    float m[4][4];
};

// XMFLOAT3X4 — Messiah engine bone matrix layout (3 rows x 4 cols)
struct XMFLOAT3X4 {
    float _11, _12, _13, _14;
    float _21, _22, _23, _24;
    float _31, _32, _33, _34;
};

// ============================================================
// Messiah-specific bone transform (from SDK)
// Transforms a bone matrix through the pose to get world position
// ============================================================
inline void MessiahMatrixAdd(const XMFLOAT3X4& bonemat, const XMFLOAT3X4& pos, Vector3& out) {
    out.x = (pos._11 * bonemat._32) + (pos._14 * bonemat._33) + (pos._23 * bonemat._34) + pos._32;
    out.y = (pos._12 * bonemat._32) + (pos._21 * bonemat._33) + (pos._24 * bonemat._34) + pos._33;
    out.z = (pos._13 * bonemat._32) + (pos._22 * bonemat._33) + (pos._31 * bonemat._34) + pos._34;
}

// ============================================================
// WorldToScreen — standard perspective projection
// ============================================================
inline bool WorldToScreen(const Matrix4x4& viewMatrix, const Vector3& world,
                          Vector2& screen, int width, int height)
{
    float x = viewMatrix.m[0][0]*world.x + viewMatrix.m[1][0]*world.y + viewMatrix.m[2][0]*world.z + viewMatrix.m[3][0];
    float y = viewMatrix.m[0][1]*world.x + viewMatrix.m[1][1]*world.y + viewMatrix.m[2][1]*world.z + viewMatrix.m[3][1];
    float w = viewMatrix.m[0][3]*world.x + viewMatrix.m[1][3]*world.y + viewMatrix.m[2][3]*world.z + viewMatrix.m[3][3];

    if (w < 0.01f) return false;

    float invW = 1.0f / w;
    screen.x = (width  * 0.5f) * (1.0f + x * invW);
    screen.y = (height * 0.5f) * (1.0f - y * invW);
    return true;
}
