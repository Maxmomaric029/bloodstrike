// ============================================================
// esp.cpp — ESP with verbose debug logging at every chain link
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
#include "menu.h"

#include "imgui.h"

// ============================================================
// Bone chain resolution
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
// Read a single bone via MessiahMatrixAdd
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
    if (mask != off::ENTITY_MASK_PLAYER) {
        static bool s_MaskLogged = false;
        if (!s_MaskLogged) {
            printf("[ESP] EntityMask at actor+0x%llX = %u (expected %u)\n",
                   off::IEntity_to_entityMask, mask, off::ENTITY_MASK_PLAYER);
            s_MaskLogged = true;
        }
        return false;
    }
    info.alive = true;

    info.health = SafeRead<int>(actor + off::Actor_to_health);
    info.team   = SafeRead<int>(actor + off::Actor_to_team);

    uintptr_t boneBase = ResolveBoneChain(actor);
    if (!boneBase) return false;

    memset(info.bones, 0, sizeof(info.bones));
    info.bones[6]  = ReadBone(boneBase, 6);
    info.bones[5]  = ReadBone(boneBase, 5);
    info.bones[4]  = ReadBone(boneBase, 4);
    info.bones[3]  = ReadBone(boneBase, 3);
    info.bones[2]  = ReadBone(boneBase, 2);
    info.bones[9]  = ReadBone(boneBase, 9);
    info.bones[10] = ReadBone(boneBase, 10);
    info.bones[11] = ReadBone(boneBase, 11);
    info.bones[14] = ReadBone(boneBase, 14);
    info.bones[15] = ReadBone(boneBase, 15);
    info.bones[16] = ReadBone(boneBase, 16);
    info.bones[23] = ReadBone(boneBase, 23);
    info.bones[24] = ReadBone(boneBase, 24);
    info.bones[26] = ReadBone(boneBase, 26);
    info.bones[27] = ReadBone(boneBase, 27);

    info.headPos = info.bones[6];
    info.footPos = info.bones[24];
    return true;
}

// ============================================================
// Drawing helpers
// ============================================================
static void DrawCornerBox(ImDrawList* dl, Vector2 tl, Vector2 br,
                           ImU32 color, float thickness = 2.0f) {
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
                          const Matrix4x4& cam, int sw, int sh, ImU32 color) {
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
// Main ESP render — with debug logging on first call
// ============================================================
static bool s_ChainLogged = false;

void RenderESP() {
    uintptr_t base = GetBase();

    if (!s_ChainLogged) {
        printf("[ESP] === Engine chain debug ===\n");
        printf("[ESP] base = 0x%llX\n", base);
    }

    // ClientEngine
    uintptr_t clientEngine = ReadPtr(base + off::ClientEngine);
    if (!s_ChainLogged) {
        printf("[ESP] ClientEngine @ base+0x%llX = 0x%llX\n",
               off::ClientEngine, clientEngine);
    }
    if (!clientEngine) { s_ChainLogged = true; return; }

    // IGameplay
    uintptr_t gameplay = ReadPtr(clientEngine + off::ClientEngine_to_IGameplay);
    if (!s_ChainLogged) {
        printf("[ESP] IGameplay @ CE+0x%llX = 0x%llX\n",
               off::ClientEngine_to_IGameplay, gameplay);
    }
    if (!gameplay) { s_ChainLogged = true; return; }

    // ClientPlayer
    uintptr_t clientPlayer = ReadPtr(gameplay + off::IGameplay_to_ClientPlayer);
    if (!s_ChainLogged) {
        printf("[ESP] ClientPlayer @ IG+0x%llX = 0x%llX\n",
               off::IGameplay_to_ClientPlayer, clientPlayer);
    }
    if (!clientPlayer) { s_ChainLogged = true; return; }

    // Camera
    uintptr_t camera = ReadPtr(clientPlayer + off::ClientPlayer_to_Camera);
    if (!s_ChainLogged) {
        printf("[ESP] Camera @ CP+0x%llX = 0x%llX\n",
               off::ClientPlayer_to_Camera, camera);
    }
    if (!camera) { s_ChainLogged = true; return; }

    // View matrix
    Matrix4x4 viewMatrix{};
    if (!ReadRaw(camera + off::Camera_to_viewMatrix, &viewMatrix, sizeof(Matrix4x4))) {
        if (!s_ChainLogged) printf("[ESP] FAILED to read view matrix @ Cam+0x%llX\n", off::Camera_to_viewMatrix);
        s_ChainLogged = true;
        return;
    }
    if (!s_ChainLogged) printf("[ESP] View matrix OK\n");

    // LocalActor
    uintptr_t localActor = ReadPtr(clientPlayer + off::ClientPlayer_to_LocalActor);
    if (!s_ChainLogged) {
        printf("[ESP] LocalActor @ CP+0x%llX = 0x%llX\n",
               off::ClientPlayer_to_LocalActor, localActor);
    }
    if (!localActor) { s_ChainLogged = true; return; }

    // Read local entity — try ReadEntity first, if mask fails, read manually
    EntityInfo local{};
    local.address = localActor;
    if (!ReadEntity(localActor, local)) {
        // Local actor might not have entityMask at expected offset — read it anyway
        local.alive = true;
        local.health = SafeRead<int>(localActor + off::Actor_to_health);
        local.team = SafeRead<int>(localActor + off::Actor_to_team);
        memset(local.bones, 0, sizeof(local.bones));
        uintptr_t boneBase = 0;
        uintptr_t p1 = ReadPtr(localActor + off::BoneStep1);
        if (p1) {
            uintptr_t p2 = ReadPtr(p1 + off::BoneStep2);
            if (p2) {
                uintptr_t p3 = ReadPtr(p2 + off::BoneStep3);
                if (p3) {
                    uintptr_t p4 = ReadPtr(p3 + off::BoneStep4);
                    if (p4) {
                        uintptr_t p5 = ReadPtr(p4 + off::BoneStep5);
                        if (p5) boneBase = p5 + off::BoneData;
                    }
                }
            }
        }
        if (boneBase) {
            local.bones[6] = ReadBone(boneBase, 6);
            local.footPos = ReadBone(boneBase, 24);
            local.headPos = local.bones[6];
        }
        if (!s_ChainLogged) printf("[ESP] Local entity read manually (mask check failed but reading anyway)\n");
    }
    if (!s_ChainLogged) {
        printf("[ESP] Local entity: head=(%.1f, %.1f, %.1f)\n",
               local.headPos.x, local.headPos.y, local.headPos.z);
        printf("[ESP] === Chain OK — ESP should render ===\n");
    }
    s_ChainLogged = true;

    // Entity list — UC guide: head = *(base + EntityList) + 0x8
    // That is ONE deref + add 0x8, NOT two derefs.
    uintptr_t entityListPtr = ReadPtr(base + off::EntityList);
    if (!entityListPtr) return;
    uintptr_t head = entityListPtr + off::EntityList_to_head;  // add 0x8, no second deref
    if (!s_ChainLogged) printf("[ESP] EntityList: read 0x%llX, head = 0x%llX (+0x%llX)\n", entityListPtr, head, off::EntityList_to_head);

    int sw = (int)ImGui::GetIO().DisplaySize.x;
    int sh = (int)ImGui::GetIO().DisplaySize.y;
    ImDrawList* dl = ImGui::GetBackgroundDrawList();
    Vector3 localHead = local.headPos;

    uintptr_t current = ReadPtr(head);  // first node = *(head)
    if (!s_ChainLogged) printf("[ESP] First node = 0x%llX\n", current);
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
                    continue;

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

                if (ShowBoxes()) {
                    Vector2 tl(headScreen.x - boxW/2, headScreen.y);
                    Vector2 br(headScreen.x + boxW/2, footScreen.y);
                    DrawCornerBox(dl, tl, br, color);
                    if (ShowHealth()) {
                        float hp = (float)info.health / 100.0f;
                        if (hp < 0.0f) hp = 0.0f;
                        if (hp > 1.0f) hp = 1.0f;
                        float barH = boxH * hp;
                        ImU32 hpColor = hp > 0.6f ? IM_COL32(50,255,50,255)
                                       : hp > 0.3f ? IM_COL32(255,255,50,255)
                                       : IM_COL32(255,50,50,255);
                        dl->AddRectFilled({tl.x-5, tl.y}, {tl.x-1, tl.y+boxH}, IM_COL32(30,30,30,200));
                        dl->AddRectFilled({tl.x-5, tl.y+boxH-barH}, {tl.x-1, tl.y+boxH}, hpColor);
                    }
                }
                if (ShowSkeleton())
                    DrawSkeleton(dl, info, viewMatrix, sw, sh, IM_COL32(200, 200, 50, 255));
                if (ShowDistance()) {
                    char buf[32];
                    snprintf(buf, sizeof(buf), "%.0fm", dist);
                    dl->AddText({headScreen.x-10, headScreen.y-18}, IM_COL32(255,255,255,255), buf);
                }
            }
        }
        current = ReadPtr(current + off::EntityNode_to_next);
    } while (current != head);
}
