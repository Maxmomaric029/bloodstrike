#pragma once
#include <cstdint>

// ============================================================
// BloodStrike (Messiah Engine) — Verified Offsets
// Source: BloodStrike Offset Finder v2.0 + UC guide
// ============================================================
//
// CRITICAL: ClientPlayer offset
// The SDK dump says 0x30 but that is WRONG.
// UC guide confirms the correct offset is 0x58.
//
// EntityMask == 2 means the entity is a player.
// Entity list is a CIRCULAR linked list, not a flat array.
// ============================================================

namespace off
{
    // --- Global pointers (GameBase + offset -> pointer) ---
    constexpr uintptr_t ClientEngine   = 0x65F7AD0;
    constexpr uintptr_t EntityList     = 0x6E4D0D8;

    // --- Engine hierarchy ---
    constexpr uintptr_t ClientEngine_to_IGameplay   = 0x58;
    constexpr uintptr_t IGameplay_to_ClientPlayer    = 0x58;  // SDK says 0x30, WRONG. Use 0x58 from UC guide.
    constexpr uintptr_t ClientPlayer_to_Camera       = 0x238;
    constexpr uintptr_t ClientPlayer_to_LocalActor   = 0x288;

    // --- Entity / Actor ---
    constexpr uintptr_t IEntity_to_entityMask        = 0x2E0;
    constexpr uintptr_t IEntity_to_IArea             = 0x88;
    constexpr uintptr_t IEntity_to_actorTransform    = 0x58;  // 3x4 matrix
    constexpr uintptr_t ActorInstance_to_actorProps  = 0x278;
    constexpr uintptr_t actorProps_to_pose           = 0x50;
    constexpr uintptr_t pose_to_BipedPose            = 0x90;

    // --- Entity list (circular linked list) ---
    constexpr uintptr_t EntityList_to_head           = 0x8;   // head = ReadPtr(base+EntityList) + 0x8 (ADD, not deref)
    constexpr uintptr_t EntityNode_to_actor          = 0x18;  // actor = *(node + 0x18)
    constexpr uintptr_t EntityNode_to_next           = 0x0;   // next = *(node)

    // --- Camera ---
    constexpr uintptr_t Camera_to_viewMatrix         = 0x40;  // 4x4 view-projection matrix

    // --- Bone chain (from UC guide) ---
    // ActorInstance + 0x278 -> +0x18 -> +0x40 -> +0x18 -> +0x90 -> +0x8
    constexpr uintptr_t BoneStep1                    = 0x278;
    constexpr uintptr_t BoneStep2                    = 0x18;
    constexpr uintptr_t BoneStep3                    = 0x40;
    constexpr uintptr_t BoneStep4                    = 0x18;
    constexpr uintptr_t BoneStep5                    = 0x90;
    constexpr uintptr_t BoneData                     = 0x8;

    // --- Actor properties ---
    constexpr uintptr_t Actor_to_componentsArray     = 0x98;
    constexpr uintptr_t Actor_to_componentsCount     = 0xA0;
    constexpr uintptr_t Actor_to_health              = 0x2E4;
    constexpr uintptr_t Actor_to_team                = 0x2E8;

    // --- EntityMask values ---
    constexpr uint32_t ENTITY_MASK_PLAYER            = 2;

    // --- TODO: Add any additional offsets here ---
    // If an offset is unknown, set it to 0 and skip that feature gracefully.
}
