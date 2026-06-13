#pragma once

// ============================================================================
// SDK_BloodStrike.h
//
// Hardcoded offsets, constants, and SDK function ports for BloodStrike
// (Messiah Engine).  All values are fixed per the provided specification.
// ============================================================================

#include <cstdint>
#include <cstring>
#include <cmath>
#include <vector>
#include <algorithm>
#include <iostream>

// ---------------------------------------------------------------------------
// Forward declare the driver so safe_read / can_read can use it
// ---------------------------------------------------------------------------
class MemoryReader;

// ---------------------------------------------------------------------------
// 3D / 2D Math primitives (compatible with glm-style usage)
//
// NOTE: The specification requested glm::vec3 / glm::vec2 from GLM.
// We use custom lightweight structs here to keep the project free of
// external dependencies. These types match the glm API closely so that
// switching to actual glm later requires minimal changes:
//   using Vector3 = glm::vec3;  using Vector2 = glm::vec2;
// ---------------------------------------------------------------------------
struct Vector2
{
    float x, y;

    Vector2() : x(0.f), y(0.f) {}
    Vector2(float _x, float _y) : x(_x), y(_y) {}

    Vector2 operator+(const Vector2& o) const { return Vector2(x + o.x, y + o.y); }
    Vector2 operator-(const Vector2& o) const { return Vector2(x - o.x, y - o.y); }
};

struct Vector3
{
    float x, y, z;

    Vector3() : x(0.f), y(0.f), z(0.f) {}
    Vector3(float _x, float _y, float _z) : x(_x), y(_y), z(_z) {}

    Vector3 operator+(const Vector3& o) const { return Vector3(x + o.x, y + o.y, z + o.z); }
    Vector3 operator-(const Vector3& o) const { return Vector3(x - o.x, y - o.y, z - o.z); }
    Vector3 operator*(float s)          const { return Vector3(x * s, y * s, z * s); }
    Vector3 operator/(float s)          const { return Vector3(x / s, y / s, z / s); }

    float Length() const { return std::sqrt(x * x + y * y + z * z); }
    float Dot(const Vector3& o) const { return x * o.x + y * o.y + z * o.z; }
};

struct Vector4
{
    float x, y, z, w;

    Vector4() : x(0.f), y(0.f), z(0.f), w(0.f) {}
    Vector4(float _x, float _y, float _z, float _w) : x(_x), y(_y), z(_z), w(_w) {}
};

// Row-major 4x4 matrix (as used by Messiah engine)
struct Matrix4x4
{
    union
    {
        float m[4][4];  // m[row][col]
        float data[16];
    };

    Matrix4x4()
    {
        for (int i = 0; i < 16; i++) data[i] = 0.0f;
    }

    Matrix4x4(const float* arr)
    {
        memcpy(data, arr, sizeof(float) * 16);
    }

    // Access helpers
    float& operator()(int row, int col)       { return m[row][col]; }
    float  operator()(int row, int col) const { return m[row][col]; }

    static Matrix4x4 Identity()
    {
        Matrix4x4 mat;
        mat.m[0][0] = 1.f; mat.m[1][1] = 1.f; mat.m[2][2] = 1.f; mat.m[3][3] = 1.f;
        return mat;
    }

    // Multiply vector (treating as column vector: M * v)
    Vector4 Transform(const Vector4& v) const
    {
        Vector4 result;
        result.x = m[0][0] * v.x + m[0][1] * v.y + m[0][2] * v.z + m[0][3] * v.w;
        result.y = m[1][0] * v.x + m[1][1] * v.y + m[1][2] * v.z + m[1][3] * v.w;
        result.z = m[2][0] * v.x + m[2][1] * v.y + m[2][2] * v.z + m[2][3] * v.w;
        result.w = m[3][0] * v.x + m[3][1] * v.y + m[3][2] * v.z + m[3][3] * v.w;
        return result;
    }
};



// ---------------------------------------------------------------------------
// Color helper (constexpr for use with constexpr color constants)
// ---------------------------------------------------------------------------
struct Color
{
    uint8_t r, g, b, a;

    constexpr Color() : r(255), g(255), b(255), a(255) {}
    constexpr Color(uint8_t _r, uint8_t _g, uint8_t _b, uint8_t _a = 255)
        : r(_r), g(_g), b(_b), a(_a) {}
};

// ============================================================================
// bloodstrike namespace — all hardcoded offsets and constants
// ============================================================================
namespace bloodstrike
{
    // Base address of Game.exe module (reference; must be adjusted at runtime)
    constexpr uint64_t GameBaseRef = 0x7FF6F9A20000ULL;

    // ---------- Renderer ----------
    namespace renderer
    {
        constexpr uint64_t hwnd = 0x6DE9430;    // GameBase + hwnd => window handle
    }

    // ---------- Function addresses (relative to GameBase) ----------
    namespace funcs
    {
        constexpr uint64_t Messiah__IObject__Initalizer          = 0x2CF160;
        constexpr uint64_t Messiah__IObject__Deconstructor        = 0x2CF890;
        constexpr uint64_t Messiah_WorldToScreen                  = 0x940F60;
        constexpr uint64_t Messiah__GetBoneTransform              = 0xD2BEC0;
        constexpr uint64_t Messiah__IEntity__Constructor          = 0x780C90;
    }

    // ---------- VTable addresses (relative to GameBase) ----------
    namespace vftables
    {
        constexpr uint64_t Messiah__IEntity                       = 0x3D80048;
        constexpr uint64_t Messiah__ICamera                       = 0x3F9B1D0;
        constexpr uint64_t Messiah__AnimationCore__Pose           = 0x3FA2F18;
        constexpr uint64_t Messiah__SkeletonComponent             = 0x4103698;
        constexpr uint64_t Messiah__Actor                         = 0x5001C20;
        constexpr uint64_t Messiah__ActorComponent                = 0x4138AB0;
        constexpr uint64_t Messiah__IArea                         = 0x3FBAA68;
        constexpr uint64_t Messiah__TachComponent                 = 0x4101FC8;
        constexpr uint64_t Messiah__FontType                      = 0x3C189F0;
    }

    // ---------- Object offsets ----------
    namespace offsets
    {
        constexpr uint64_t Messiah__ClientEngine                  = 0x65F7AD0;
        constexpr uint64_t Messiah__EntityList                    = 0x6E4D0D8;
    }

    // ---------- Field offsets ----------
    namespace field
    {
        constexpr uint64_t ClientEngine_to_IGameplay              = 0x58;
        constexpr uint64_t ClientPlayer_to_localActor             = 0x288;
        constexpr uint64_t ClientPlayer_to_camera                 = 0x238;
        constexpr uint64_t actorInstance_to_actorProps            = 0x278;
        constexpr uint64_t IEntity_to_entityMask                  = 0x2E0;
        constexpr uint64_t IEntity_to_IArea                       = 0x88;
        constexpr uint64_t pose_to_BipedPose                      = 0x90;
    }

    // ---------- Entity / Actor constants ----------
    constexpr int    ENTITY_LIST_MAX     = 512;
    constexpr int    INVALID_ENTRY       = -1;

    // ---------- Bone indices for skeleton drawing (simplified) ----------
    enum BoneIndex : int
    {
        BONE_HEAD       = 6,
        BONE_NECK       = 5,
        BONE_CHEST      = 4,
        BONE_SPINE      = 3,
        BONE_PELVIS     = 2,
        BONE_L_SHOULDER = 9,
        BONE_L_ELBOW    = 10,
        BONE_L_HAND     = 11,
        BONE_R_SHOULDER = 14,
        BONE_R_ELBOW    = 15,
        BONE_R_HAND     = 16,
        BONE_L_KNEE     = 23,
        BONE_L_FOOT     = 24,
        BONE_R_KNEE     = 26,
        BONE_R_FOOT     = 27
    };

} // namespace bloodstrike

// ============================================================================
// sdk namespace — ported SDK functions
// ============================================================================
namespace sdk
{
    // -----------------------------------------------------------------------
    // safe_read / can_read — delegate to kernel driver
    // -----------------------------------------------------------------------
    template<typename T>
    inline bool safe_read(const MemoryReader& reader, uint64_t address, T& out)
    {
        if (address == 0)
            return false;
        return reader.ReadMemory<T>(address, out);
    }

    inline bool can_read(const MemoryReader& reader, uint64_t address)
    {
        if (address == 0)
            return false;
        uint8_t dummy = 0;
        return reader.ReadMemory<uint8_t>(address, dummy);
    }

    // -----------------------------------------------------------------------
    // ReadChain convenience wrapper
    // -----------------------------------------------------------------------
    template<typename T>
    inline bool read_chain(const MemoryReader& reader, uint64_t baseAddress,
                           const std::vector<uint64_t>& offsets, T& out)
    {
        if (baseAddress == 0)
            return false;
        return reader.ReadChain<T>(baseAddress, offsets, out);
    }

    // -----------------------------------------------------------------------
    // MessiahMatrixAdd — add an offset to a matrix pointer (bone chain math)
    // -----------------------------------------------------------------------
    inline uint64_t MessiahMatrixAdd(uint64_t basePtr, int index, size_t stride)
    {
        return basePtr + static_cast<uint64_t>(index) * static_cast<uint64_t>(stride);
    }

    // -----------------------------------------------------------------------
    // Affine (matrix overload)
    //   Transform a local-space point through a bone matrix to world-space.
    //
    //   The |boneMatrix| is expected to be a 4x4 row-major matrix
    //   where rows 0-2 contain the basis vectors and row 3 contains translation.
    // -----------------------------------------------------------------------
    inline Vector3 Affine(const Matrix4x4& boneMatrix, const Vector3& localPos)
    {
        Vector3 world;
        world.x = boneMatrix.m[0][0] * localPos.x
                + boneMatrix.m[1][0] * localPos.y
                + boneMatrix.m[2][0] * localPos.z
                + boneMatrix.m[3][0];

        world.y = boneMatrix.m[0][1] * localPos.x
                + boneMatrix.m[1][1] * localPos.y
                + boneMatrix.m[2][1] * localPos.z
                + boneMatrix.m[3][1];

        world.z = boneMatrix.m[0][2] * localPos.x
                + boneMatrix.m[1][2] * localPos.y
                + boneMatrix.m[2][2] * localPos.z
                + boneMatrix.m[3][2];

        return world;
    }

    // -----------------------------------------------------------------------
    // Affine (uint64_t overload)
    //   Reads a 4x3 bone matrix from |matrixAddress| via the driver,
    //   then transforms |localPos| to world-space.
    //
    //   The Messiah engine stores 4x3 matrices in the following layout:
    //     [r00, r01, r02, tx,  r10, r11, r12, ty,  r20, r21, r22, tz]
    //   We reconstruct a 4x4 matrix from this and apply the affine transform.
    // -----------------------------------------------------------------------
    inline Vector3 Affine(const MemoryReader& reader,
                          uint64_t matrixAddress, const Vector3& localPos)
    {
        float raw[12];
        if (!reader.ReadMemoryRaw(matrixAddress, raw, sizeof(raw)))
            return Vector3(0, 0, 0);

        // Reconstruct row-major 4x4 from Messiah 4x3 layout:
        // raw layout: [r00, r01, r02, tx,  r10, r11, r12, ty,  r20, r21, r22, tz]
        Matrix4x4 boneMatrix;
        boneMatrix.m[0][0] = raw[0];  boneMatrix.m[0][1] = raw[1];  boneMatrix.m[0][2] = raw[2];  boneMatrix.m[0][3] = 0.f;
        boneMatrix.m[1][0] = raw[4];  boneMatrix.m[1][1] = raw[5];  boneMatrix.m[1][2] = raw[6];  boneMatrix.m[1][3] = 0.f;
        boneMatrix.m[2][0] = raw[8];  boneMatrix.m[2][1] = raw[9];  boneMatrix.m[2][2] = raw[10]; boneMatrix.m[2][3] = 0.f;
        boneMatrix.m[3][0] = raw[3];  boneMatrix.m[3][1] = raw[7];  boneMatrix.m[3][2] = raw[11]; boneMatrix.m[3][3] = 1.f;

        return Affine(boneMatrix, localPos);
    }

    // -----------------------------------------------------------------------
    // w2s — World to Screen conversion
    //
    //   Projects a 3D world point onto 2D screen coordinates using the
    //   camera's view-projection matrix (row-major convention).
    //
    //   |camMatrix| — the 4x4 view-projection matrix from the game camera.
    //   |world|     — 3D position in world space.
    //   |screen|    — [out] 2D screen coordinates.
    //   |width|, |height| — screen resolution.
    //
    //   Returns true if the point is on screen (in front of the camera).
    // -----------------------------------------------------------------------

    // -----------------------------------------------------------------------
    // w2s — World to Screen conversion
    //
    //   Projects a 3D world point onto 2D screen coordinates using the
    //   camera's view-projection matrix.
    //
    //   |camMatrix| — the 4x4 view-projection matrix from the game camera.
    //   |world|     — 3D position in world space.
    //   |screen|    — [out] 2D screen coordinates.
    //   |width|, |height| — screen resolution.
    //
    //   Returns true if the point is on screen (in front of the camera).
    // -----------------------------------------------------------------------
    inline bool w2s(const Matrix4x4& camMatrix, const Vector3& world,
                    Vector2& screen, int width, int height)
    {
        // Transform world position by the view-projection matrix
        Vector4 clip;
        clip.x = world.x * camMatrix.m[0][0]
               + world.y * camMatrix.m[1][0]
               + world.z * camMatrix.m[2][0]
               + camMatrix.m[3][0];

        clip.y = world.x * camMatrix.m[0][1]
               + world.y * camMatrix.m[1][1]
               + world.z * camMatrix.m[2][1]
               + camMatrix.m[3][1];

        clip.z = world.x * camMatrix.m[0][2]
               + world.y * camMatrix.m[1][2]
               + world.z * camMatrix.m[2][2]
               + camMatrix.m[3][2];

        clip.w = world.x * camMatrix.m[0][3]
               + world.y * camMatrix.m[1][3]
               + world.z * camMatrix.m[2][3]
               + camMatrix.m[3][3];

        // Behind the camera
        if (clip.w < 0.01f)
            return false;

        // Perspective divide -> Normalized Device Coordinates
        float ndcX = clip.x / clip.w;
        float ndcY = clip.y / clip.w;

        // Check if within view frustum (allow slight overflow for edge cases)
        if (ndcX < -1.5f || ndcX > 1.5f || ndcY < -1.5f || ndcY > 1.5f)
            return false;

        // NDC to screen coordinates
        screen.x = (width  / 2.0f) * ndcX + (width  / 2.0f);
        screen.y = -(height / 2.0f) * ndcY + (height / 2.0f);

        return true;
    }

    // -----------------------------------------------------------------------
    // ReadBoneWorldPosition — convenience: read a single bone's world position
    //
    //   In the Messiah Engine the skeleton component stores an array of bone
    //   transforms. Each entry is 0x20 bytes, with the translation stored as
    //   the first three floats (Vector3) at offset 0.
    // -----------------------------------------------------------------------
    inline bool ReadBoneWorldPosition(const MemoryReader& reader,
                                       uint64_t skeletonComponentAddr,
                                       int boneIndex,
                                       Vector3& outWorldPos)
    {
        constexpr size_t BONE_STRIDE = 0x20;           // 32 bytes per bone entry
        uint64_t boneAddr = MessiahMatrixAdd(skeletonComponentAddr, boneIndex, BONE_STRIDE);

        // Translation is the first 3 floats at the bone address
        if (!reader.ReadMemoryRaw(boneAddr, &outWorldPos, sizeof(Vector3)))
            return false;

        return true;
    }

} // namespace sdk
