// ============================================================
// esp.cpp — ESP logic: entity traversal, bone chain, rendering
// ============================================================

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstdio>
#include <cstring>
#include <cmath>

#include "esp.h"
#include "offsets.h"
#include "memory.h"
#include "math.h"
#include "hooks.h"

#include "imgui.h"

// ============================================================
// Module base (cached)
// ============================================================
static uintptr_t GetBase() {
    static uintptr_t base = (uintptr_t)GetModuleHandleA(NULL);
    return base;
}

// ============================================================
// Bone chain resolution
// ActorInstance + 0x278 -> +0x18 -> +0x40 -> +0x18 -> +0x90 -> +0x8
// ============================================================
static uintptr_t ResolveBoneChain(uintptr_t actorInstance) {
    uintptr_t p = ReadPtr(actorInstance + off::BoneStep1);
    if (!p) return 0;
    p = ReadPtr(p + off::BoneStep2);
    if (!p) return 0;
    p = ReadPtr(p + off::BoneStep3);
    if (!p) return 0;
    p = ReadPtr(p + off::BoneStep4);
    if (!p) return 0;
    p = ReadPtr(p + off::BoneStep5);
    if (!p) return 0;
    return p + off::BoneData;
}

// ============================================================
// Read a single bone's world position via MessiahMatrixAdd
// Each bone entry = 2 x XMFLOAT3X4 = 96 bytes (bonemat + pos)
// ============================================================
static Vector3 ReadBone(uintptr_t boneBase, int index) {
    if (!boneBase) return {};
    constexpr size_t BONE_ENTRY_SIZE = sizeof(XMFLOAT3X4) * 2;
    uintptr_t addr = boneBase + index * BONE_ENTRY_SIZE;

    XMFLOAT3X4 bonemat{};
    XMFLOAT3X4 pos{};
    if (!ReadRaw(addr, &bonemat, sizeof(bonemat))) return {};
    if (!ReadRaw(addr + sizeof(bonemat), &pos, sizeof(pos))) return {};

    Vector3 out;
    MessiahMatrixAdd(bonemat, pos, out);
    return out;
}

// ============================================================
// Read entity data
// ============================================================
bool ReadEntity(uintptr_t actor, EntityInfo& info) {
    info.address = actor;
    info.alive   = false;
    info.health  = 0;
    info.team    = 0;

    uint32_t mask = SafeRead<uint32_t>(actor + off::IEntity_to_entityMask);
    if (mask != off::ENTITY_MASK_PLAYER) return false;
    info.alive = true;

    info.health = SafeRead<int>(actor + off::Actor_to_health);
    info.team   = SafeRead<int>(actor + off::Actor_to_team);

    uintptr_t boneBase = ResolveBoneChain(actor);
    if (!boneBase) return false;

    memset(info.bones, 0, sizeof(info.bones));

    info.bones[6]  = ReadBone(boneBase, 6);   // head
    info.bones[5]  = ReadBone(boneBase, 5);   // neck
    info.bones[4]  = ReadBone(boneBase, 4);   // chest
    info.bones[3]  = ReadBone(boneBase, 3);   // spine
    info.bones[2]  = ReadBone(boneBase, 2);   // pelvis
    info.bones[9]  = ReadBone(boneBase, 9);   // L shoulder
    info.bones[10] = ReadBone(boneBase, 10);  // L elbow
    info.bones[11] = ReadBone(boneBase, 11);  // L hand
    info.bones[14] = ReadBone(boneBase, 14);  // R shoulder
    info.bones[15] = ReadBone(boneBase, 15);  // R elbow
    info.bones[16] = ReadBone(boneBase, 16);  // R hand
    info.bones[23] = ReadBone(boneBase, 23);  // L knee
    info.bones[24] = ReadBone(boneBase, 24);  // L foot
    info.bones[26] = ReadBone(boneBase, 26);  // R knee
    info.bones[27] = ReadBone(boneBase, 27);  // R foot

    info.headPos = info.bones[6];
    info.footPos = info.bones[24];

    return true;
}

// ============================================================
// Drawing helpers
// ============================================================
static void DrawCornerBox(ImDrawList* dl, Vector2 tl, Vector2 br,
                           ImU32 color, float thickness = 2.0f)
{
    float w = br.x - tl.x;
    float h = br.y - tl.y;
    float cw = w * 0.25f;
    float ch = h * 0.25f;

    dl->AddLine({tl.x, tl.y}, {tl.x + cw, tl.y}, color, thickness);
    dl->AddLine({tl.x, tl.y}, {tl.x, tl.y + ch}, color, thickness);
    dl->AddLine({br.x, tl.y}, {br.x - cw, tl.y}, color, thickness);
    dl->AddLine({br.x, tl.y}, {br.x, tl.y + ch}, color, thickness);
    dl->AddLine({tl.x, br.y}, {tl.x + cw, br.y}, color, thickness);
    dl->AddLine({tl.x, br.y}, {tl.x, br.y - ch}, color, thickness);
    dl->AddLine({br.x, br.y}, {br.x - cw, br.y}, color, thickness);
    dl->AddLine({br.x, br.y}, {br.x, br.y - ch}, color, thickness);
}

static void DrawSkeleton(ImDrawList* dl, const EntityInfo& e,
                          const Matrix4x4& cam, int sw, int sh, ImU32 color)
{
    struct BonePair { int a, b; };
    static const BonePair pairs[] = {
        {6,5}, {5,4}, {4,3}, {3,2},
        {5,9}, {9,10}, {10,11},
        {5,14}, {14,15}, {15,16},
        {2,23}, {23,24}, {2,26}, {26,27}
    };

    for (auto& p : pairs) {
        Vector2 s1, s2;
        bool on1 = WorldToScreen(cam, e.bones[p.a], s1, sw, sh);
        bool on2 = WorldToScreen(cam, e.bones[p.b], s2, sw, sh);
        if (on1 && on2)
            dl->AddLine({s1.x, s1.y}, {s2.x, s2.y}, color, 1.5f);
    }
}

// ============================================================
// Main ESP render (called from HookedPresent)
// ============================================================
void RenderESP() {
    uintptr_t base = GetBase();

    // Read engine chain: ClientEngine -> IGameplay -> ClientPlayer -> Camera, LocalActor
    uintptr_t clientEngine = ReadPtr(base + off::ClientEngine);
    if (!clientEngine) return;

    uintptr_t gameplay = ReadPtr(clientEngine + off::ClientEngine_to_IGameplay);
    if (!gameplay) return;

    uintptr_t clientPlayer = ReadPtr(gameplay + off::IGameplay_to_ClientPlayer);
    if (!clientPlayer) return;

    uintptr_t camera = ReadPtr(clientPlayer + off::ClientPlayer_to_Camera);
    if (!camera) return;

    Matrix4x4 viewMatrix{};
    if (!ReadRaw(camera + off::Camera_to_viewMatrix, &viewMatrix, sizeof(Matrix4x4)))
        return;

    uintptr_t localActor = ReadPtr(clientPlayer + off::ClientPlayer_to_LocalActor);
    if (!localActor) return;

    EntityInfo local{};
    if (!ReadEntity(localActor, local)) return;

    int sw = (int)ImGui::GetIO().DisplaySize.x;
    int sh = (int)ImGui::GetIO().DisplaySize.y;

    // Traverse entity list (circular linked list)
    uintptr_t listBase = ReadPtr(base + off::EntityList);
    if (!listBase) return;

    uintptr_t head = ReadPtr(listBase + off::EntityList_to_head);
    if (!head) return;

    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    Vector3 localHead = local.headPos;

    uintptr_t current = ReadPtr(head);
    int iterations = 0;
    constexpr int MAX_ITERATIONS = 512;

    do {
        if (!current || iterations++ > MAX_ITERATIONS) break;

        uintptr_t actor = ReadPtr(current + off::EntityNode_to_actor);
        if (actor && actor != localActor) {
            EntityInfo info{};
            if (ReadEntity(actor, info) && info.alive) {
                Vector2 headScreen;
                if (!WorldToScreen(viewMatrix, info.headPos, headScreen, sw, sh))
                    goto next;

                Vector2 footScreen;
                WorldToScreen(viewMatrix, info.footPos, footScreen, sw, sh);

                float boxH = fabsf(footScreen.y - headScreen.y);
                float boxW = boxH * 0.5f;
                float dist = (info.headPos - localHead).Length();

                ImU32 color;
                if (info.team == local.team)
                    color = IM_COL32(50, 200, 50, 255);
                else if (dist < 30.0f)
                    color = IM_COL32(255, 50, 50, 255);
                else
                    color = IM_COL32(255, 150, 50, 255);

                // Corner box
                {
                    Vector2 tl(headScreen.x - boxW/2, headScreen.y);
                    Vector2 br(headScreen.x + boxW/2, footScreen.y);
                    DrawCornerBox(dl, tl, br, color);

                    // Health bar
                    float hp = (float)info.health / 100.0f;
                    if (hp < 0.0f) hp = 0.0f;
                    if (hp > 1.0f) hp = 1.0f;
                    float barH = boxH * hp;
                    ImU32 hpColor = hp > 0.6f ? IM_COL32(50,255,50,255)
                                   : hp > 0.3f ? IM_COL32(255,255,50,255)
                                   : IM_COL32(255,50,50,255);
                    dl->AddRectFilled(
                        {tl.x - 5.0f, tl.y},
                        {tl.x - 1.0f, tl.y + boxH},
                        IM_COL32(30, 30, 30, 200));
                    dl->AddRectFilled(
                        {tl.x - 5.0f, tl.y + boxH - barH},
                        {tl.x - 1.0f, tl.y + boxH},
                        hpColor);
                }

                // Skeleton
                DrawSkeleton(dl, info, viewMatrix, sw, sh, IM_COL32(200, 200, 50, 255));

                // Distance
                char buf[32];
                snprintf(buf, sizeof(buf), "%.0fm", dist);
                dl->AddText({headScreen.x - 10.0f, headScreen.y - 18.0f},
                            IM_COL32(255, 255, 255, 255), buf);
            }
        }
    next:
        current = ReadPtr(current + off::EntityNode_to_next);
    } while (current != head);
}
