// ============================================================================
// main.cpp — BloodStrike ESP
//
//   Initializes the overlay, finds the game process,
//   iterates through the entity list, reads bone transforms via
//   direct NT API calls (NtReadVirtualMemory), converts to screen
//   coordinates, and draws ESP (skeleton, corner box, health info).
//
//   No kernel driver required — reads memory directly from usermode
//   using dynamically resolved NT functions (bypasses IAT hooks).
// ============================================================================

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <tlhelp32.h>
#include <vector>
#include <string>
#include <thread>
#include <chrono>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <algorithm>
#include <cmath>

#include "Memory.h"
#include "SDK_BloodStrike.h"
#include "Overlay.h"

// ---------------------------------------------------------------------------
// Process / Module helpers
// ---------------------------------------------------------------------------

DWORD FindProcessId(const wchar_t* processName)
{
    DWORD pid = 0;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE)
        return 0;

    PROCESSENTRY32W pe = { sizeof(PROCESSENTRY32W) };
    if (Process32FirstW(snapshot, &pe))
    {
        do
        {
            if (_wcsicmp(pe.szExeFile, processName) == 0)
            {
                pid = pe.th32ProcessID;
                break;
            }
        } while (Process32NextW(snapshot, &pe));
    }

    CloseHandle(snapshot);
    return pid;
}

uint64_t GetModuleBase(DWORD pid, const wchar_t* moduleName)
{
    uint64_t base = 0;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid);
    if (snapshot == INVALID_HANDLE_VALUE)
        return 0;

    MODULEENTRY32W me = { sizeof(MODULEENTRY32W) };
    if (Module32FirstW(snapshot, &me))
    {
        do
        {
            if (_wcsicmp(me.szModule, moduleName) == 0)
            {
                base = (uint64_t)me.modBaseAddr;
                break;
            }
        } while (Module32NextW(snapshot, &me));
    }

    CloseHandle(snapshot);
    return base;
}

// ---------------------------------------------------------------------------
// ESP Drawing helpers
// ---------------------------------------------------------------------------

// Color presets
namespace esp_colors
{
    constexpr Color ENEMY_VISIBLE  (255, 50, 50,   255);   // Red
    constexpr Color ENEMY_OCCLUDED (255, 150, 50,  255);   // Orange
    constexpr Color TEAMMATE       (50,  200, 50,  255);   // Green
    constexpr Color SKELETON       (200, 200, 50,  255);   // Yellow
    constexpr Color HEALTH_GREEN   (50,  255, 50,  255);
    constexpr Color HEALTH_YELLOW  (255, 255, 50,  255);
    constexpr Color HEALTH_RED     (255, 50,  50,  255);
    constexpr Color TEXT_WHITE     (255, 255, 255, 255);
    constexpr Color TEXT_SHADOW    (0,   0,   0,   200);
}

struct EntityInfo
{
    uint64_t actorAddress;
    Vector3  headPos;
    Vector3  footPos;
    Vector3  bonePositions[28]; // Store up to 28 bone world positions
    int      health;
    int      team;
    bool     isAlive;
    bool     isLocal;
};

// ---------------------------------------------------------------------------
// Read entity data from the game
// ---------------------------------------------------------------------------
bool ReadEntity(MemoryReader& reader, uint64_t gameBase,
                uint64_t entityAddress, EntityInfo& entity, bool isLocal)
{
    entity.actorAddress = entityAddress;
    entity.isLocal = isLocal;

    // Verify the entity has the Actor vtable
    uint64_t vtablePtr = 0;
    if (!reader.ReadMemory<uint64_t>(entityAddress, vtablePtr))
        return false;

    // Read actor properties pointer:
    // actorInstance + field::actorInstance_to_actorProps (0x278) -> actorProps
    uint64_t actorProps = 0;
    if (!reader.ReadMemory<uint64_t>(
            entityAddress + bloodstrike::field::actorInstance_to_actorProps,
            actorProps))
        return false;

    if (actorProps == 0)
        return false;

    // Read entity mask (IEntity + 0x2E0): bit 0 = alive
    uint32_t entityMask = 0;
    if (!reader.ReadMemory<uint32_t>(
            entityAddress + bloodstrike::field::IEntity_to_entityMask,
            entityMask))
        return false;

    entity.isAlive = (entityMask & 1) != 0;

    // Read health and team from the Actor directly (right after entityMask):
    //   Actor + 0x2E0: entityMask (4 bytes)
    //   Actor + 0x2E4: health (int32)
    //   Actor + 0x2E8: team (int32)
    int32_t healthVal = 0;
    int32_t teamVal   = 0;
    reader.ReadMemory<int32_t>(entityAddress + bloodstrike::field::Actor_to_health, healthVal);
    reader.ReadMemory<int32_t>(entityAddress + bloodstrike::field::Actor_to_team, teamVal);

    // Validate health (clamp to 0-100, reject garbage)
    entity.health = std::clamp(healthVal, 0, 100);
    entity.team   = teamVal;

    // --- UNVERIFIED: Actor_to_componentsArray / Actor_to_componentsCount ---
    uint64_t componentsArray = 0;
    uint32_t componentsCount = 0;
    if (!reader.ReadMemory<uint64_t>(entityAddress + bloodstrike::field::Actor_to_componentsArray, componentsArray))
        return false;
    if (!reader.ReadMemory<uint32_t>(entityAddress + bloodstrike::field::Actor_to_componentsCount, componentsCount))
        return false;

    // Scan component array for the SkeletonComponent by comparing vtable
    // against the known skeleton vtable address (gameBase + vftable offset).
    uint64_t skeletonComp = 0;
    uint64_t expectedSkelVtable = gameBase + bloodstrike::vftables::Messiah__SkeletonComponent;

    if (componentsArray != 0 && componentsCount > 0 && componentsCount < 64)
    {
        for (uint32_t i = 0; i < componentsCount; i++)
        {
            uint64_t compPtr = 0;
            if (!reader.ReadMemory<uint64_t>(componentsArray + i * sizeof(uint64_t), compPtr))
                continue;
            if (compPtr == 0)
                continue;

            uint64_t compVtable = 0;
            if (!reader.ReadMemory<uint64_t>(compPtr, compVtable))
                continue;

            // Match against the skeleton component vtable
            if (compVtable == expectedSkelVtable)
            {
                skeletonComp = compPtr;
                break;
            }
        }
    }

    // --- UNVERIFIED: actorProps_to_pose ---
    // Fallback #1: try the hardcoded component index (UNVERIFIED)
    if (skeletonComp == 0 && componentsArray != 0 && componentsCount > bloodstrike::field::SKELETON_COMP_FALLBACK_INDEX)
    {
        reader.ReadMemory<uint64_t>(
            componentsArray + bloodstrike::field::SKELETON_COMP_FALLBACK_INDEX * sizeof(uint64_t),
            skeletonComp);

        if (skeletonComp != 0)
        {
            uint64_t compVtable = 0;
            if (reader.ReadMemory<uint64_t>(skeletonComp, compVtable) && compVtable != 0)
            {
                // Verify it's actually a skeleton component by checking vtable
                uint64_t expectedSkelVtable2 = gameBase + bloodstrike::vftables::Messiah__SkeletonComponent;
                if (compVtable != expectedSkelVtable2)
                    skeletonComp = 0; // Wrong component, discard
            }
        }
    }

    // Fallback #2: try to find BipedPose (world-space bone transforms) from actorProps
    if (skeletonComp == 0)
    {
        uint64_t posePtr = 0;
        if (reader.ReadMemory<uint64_t>(actorProps + bloodstrike::field::actorProps_to_pose, posePtr) && posePtr != 0)
        {
            uint64_t bipedPose = 0;
            if (reader.ReadMemory<uint64_t>(
                    posePtr + bloodstrike::field::pose_to_BipedPose,
                    bipedPose) && bipedPose != 0)
            {
                skeletonComp = bipedPose;
            }
        }
    }

    if (skeletonComp == 0)
        return false;

    // Zero-initialize bone positions
    memset(entity.bonePositions, 0, sizeof(entity.bonePositions));

    // Read bone positions
    const int BONES_TO_READ[] = {
        bloodstrike::BONE_HEAD,
        bloodstrike::BONE_NECK,
        bloodstrike::BONE_CHEST,
        bloodstrike::BONE_SPINE,
        bloodstrike::BONE_PELVIS,
        bloodstrike::BONE_L_SHOULDER,
        bloodstrike::BONE_L_ELBOW,
        bloodstrike::BONE_L_HAND,
        bloodstrike::BONE_R_SHOULDER,
        bloodstrike::BONE_R_ELBOW,
        bloodstrike::BONE_R_HAND,
        bloodstrike::BONE_L_KNEE,
        bloodstrike::BONE_L_FOOT,
        bloodstrike::BONE_R_KNEE,
        bloodstrike::BONE_R_FOOT
    };

    for (int boneIdx : BONES_TO_READ)
    {
        if (boneIdx >= 0 && boneIdx < 28)
        {
            sdk::ReadBoneWorldPosition(reader, skeletonComp, boneIdx,
                                       entity.bonePositions[boneIdx]);
        }
    }

    // Head position for box ESP
    entity.headPos = entity.bonePositions[bloodstrike::BONE_HEAD];

    // Foot position: use the average of both foot bones (actual ground position)
    // instead of guessing with a magic offset from pelvis.
    {
        Vector3 lFoot = entity.bonePositions[bloodstrike::BONE_L_FOOT];
        Vector3 rFoot = entity.bonePositions[bloodstrike::BONE_R_FOOT];

        bool hasLFoot = (lFoot.x != 0.0f || lFoot.y != 0.0f || lFoot.z != 0.0f);
        bool hasRFoot = (rFoot.x != 0.0f || rFoot.y != 0.0f || rFoot.z != 0.0f);

        if (hasLFoot && hasRFoot)
        {
            // Average both feet for a stable ground position
            entity.footPos.x = (lFoot.x + rFoot.x) * 0.5f;
            entity.footPos.y = (lFoot.y + rFoot.y) * 0.5f;
            entity.footPos.z = (lFoot.z + rFoot.z) * 0.5f;
        }
        else if (hasLFoot)
        {
            entity.footPos = lFoot;
        }
        else if (hasRFoot)
        {
            entity.footPos = rFoot;
        }
        else
        {
            // Fallback: pelvis position (better than a hardcoded offset)
            entity.footPos = entity.bonePositions[bloodstrike::BONE_PELVIS];
        }
    }

    return true;
}

// ---------------------------------------------------------------------------
// Get camera matrix from local player
// ---------------------------------------------------------------------------
bool GetCameraMatrix(MemoryReader& reader, uint64_t gameBase, Matrix4x4& outMatrix)
{
    // --- Read camera pointer from renderer namespace global (UNVERIFIED) ---
    // The renderer stores camera as a static global, just like hwnd.
    uint64_t cameraPtr = 0;
    if (!reader.ReadMemory<uint64_t>(
            gameBase + bloodstrike::renderer::camera,
            cameraPtr))
    {
        std::cerr << "[Chain] FAILED to read camera at GameBase+renderer::camera (0x"
                  << std::hex << bloodstrike::renderer::camera << ")" << std::dec << "\n";
        return false;
    }

    if (cameraPtr == 0)
    {
        std::cerr << "[Chain] camera pointer is NULL. renderer::camera offset needs scanning.\n";
        return false;
    }

    std::cout << "[Chain] Camera            = 0x" << std::hex << cameraPtr << std::dec
              << "  (GameBase + 0x" << std::hex << bloodstrike::renderer::camera
              << " [renderer::camera UNVERIFIED])" << std::dec << "\n";

    // Verify camera vtable
    uint64_t cameraVtable = 0;
    if (!reader.ReadMemory<uint64_t>(cameraPtr, cameraVtable))
        return false;

    // --- UNVERIFIED: ICamera_to_viewMatrix ---
    // Read the view-projection matrix from the camera object
    if (!reader.ReadMemoryRaw(cameraPtr + bloodstrike::field::ICamera_to_viewMatrix, outMatrix.data, sizeof(float) * 16))
        return false;

    // Validate matrix — reject NaN/Inf
    for (int i = 0; i < 16; i++)
    {
        if (!std::isfinite(outMatrix.data[i]))
            return false;
    }

    return true;
}

// ---------------------------------------------------------------------------
// Get local player and engine info
// ---------------------------------------------------------------------------
bool GetLocalPlayer(MemoryReader& reader, uint64_t gameBase,
                    uint64_t& outClientEngine, uint64_t& outLocalActor,
                    uint64_t& outEntityList)
{
    std::cout << "[Chain] ===== Pointer chain (renderer globals pattern) =====\n";
    std::cout << "[Chain] GameBase          = 0x" << std::hex << gameBase << std::dec << "\n";

    // --- Step 1: ClientEngine (verified global) ---
    uint64_t clientEnginePtr = 0;
    if (!reader.ReadMemory<uint64_t>(
            gameBase + bloodstrike::offsets::Messiah__ClientEngine,
            clientEnginePtr))
    {
        std::cerr << "[Chain] FAILED to read ClientEngine at GameBase+0x"
                  << std::hex << bloodstrike::offsets::Messiah__ClientEngine << std::dec << "\n";
        return false;
    }

    if (clientEnginePtr == 0)
    {
        std::cerr << "[Chain] ClientEngine pointer is NULL.\n";
        return false;
    }

    outClientEngine = clientEnginePtr;
    std::cout << "[Chain] ClientEngine      = 0x" << std::hex << clientEnginePtr << std::dec
              << "  (verified global)" << std::dec << "\n";

    // --- Step 2: IGameplay (verified offset, debug only) ---
    uint64_t gameplayPtr = 0;
    if (reader.ReadMemory<uint64_t>(
            clientEnginePtr + bloodstrike::field::ClientEngine_to_IGameplay,
            gameplayPtr))
    {
        std::cout << "[Chain] IGameplay         = 0x" << std::hex << gameplayPtr << std::dec
                  << "  (ClientEngine + 0x58, verified)" << std::dec << "\n";
    }
    else
    {
        std::cerr << "[Chain] WARNING: Could not read IGameplay (non-fatal)\n";
    }

    // NOTE: The dumper does NOT provide an offset from IGameplay to ClientPlayer.
    // The chain IGameplay -> ClientPlayer -> localActor was INVENTED by a previous
    // AI and produced garbage values. It has been REMOVED.
    //
    // Instead, localActor and camera are stored as static globals in the renderer
    // namespace (same pattern as hwnd). Their offsets must be found with a memory
    // scanner and set in bloodstrike::renderer::localActor and renderer::camera.

    // --- Step 3: LocalActor (renderer namespace global, UNVERIFIED) ---
    if (bloodstrike::renderer::localActor == 0)
    {
        std::cerr << "[Chain] renderer::localActor offset is 0 (NOT SET).\n"
                  << "         You must scan for the localActor global pointer and set\n"
                  << "         bloodstrike::renderer::localActor in SDK_BloodStrike.h.\n"
                  << "         The dumper never provided this offset — it was invented.\n";
        return false;
    }

    uint64_t localActorPtr = 0;
    if (!reader.ReadMemory<uint64_t>(
            gameBase + bloodstrike::renderer::localActor,
            localActorPtr))
    {
        std::cerr << "[Chain] FAILED to read LocalActor at GameBase+renderer::localActor (0x"
                  << std::hex << bloodstrike::renderer::localActor << ")" << std::dec << "\n";
        return false;
    }

    if (localActorPtr == 0)
    {
        std::cerr << "[Chain] LocalActor pointer is NULL. renderer::localActor offset may be wrong.\n";
        return false;
    }

    outLocalActor = localActorPtr;
    std::cout << "[Chain] LocalActor        = 0x" << std::hex << localActorPtr << std::dec
              << "  (renderer global, UNVERIFIED offset 0x" << std::hex
              << bloodstrike::renderer::localActor << ")" << std::dec << "\n";

    // Warn if LocalActor looks like garbage
    if (localActorPtr != 0 && ((localActorPtr >> 32) != 0) && ((localActorPtr & 0xFFFFFFFF) >> 28) != 0)
    {
        std::cerr << "[Chain] WARNING: LocalActor 0x" << std::hex << localActorPtr
                  << " looks like garbage! The renderer::localActor offset may be wrong."
                  << std::dec << "\n";
    }

    // --- Step 4: Entity list (verified global) ---
    uint64_t entityListBase = 0;
    if (!reader.ReadMemory<uint64_t>(
            gameBase + bloodstrike::offsets::Messiah__EntityList,
            entityListBase))
        return false;

    outEntityList = entityListBase;
    std::cout << "[Chain] EntityList        = 0x" << std::hex << entityListBase << std::dec
              << "  (verified global)" << std::dec << "\n";
    std::cout << "[Chain] ===== End of pointer chain =====\n";

    return true;
}

// ---------------------------------------------------------------------------
// Main ESP loop
// ---------------------------------------------------------------------------
void ESPLoop(MemoryReader& reader, uint64_t gameBase,
             Overlay& overlay)
{
    uint64_t clientEngine  = 0;
    uint64_t localActor    = 0;
    uint64_t entityListPtr = 0;

    if (!GetLocalPlayer(reader, gameBase, clientEngine, localActor, entityListPtr))
    {
        std::cerr << "[ESP] Failed to get local player / engine pointers.\n";
        return;
    }

    std::wcout << L"[ESP] Client engine: 0x" << std::hex << clientEngine << std::dec << L"\n";
    std::wcout << L"[ESP] Local actor:  0x" << std::hex << localActor << std::dec << L"\n";
    std::wcout << L"[ESP] Entity list:  0x" << std::hex << entityListPtr << std::dec << L"\n";

    // Screen dimensions
    int screenW = overlay.GetWidth();
    int screenH = overlay.GetHeight();

    // Initialize local player info
    EntityInfo localEntity;
    ReadEntity(reader, gameBase, localActor, localEntity, true);

    // Track FPS
    auto lastFrameTime = std::chrono::steady_clock::now();
    int frameCount = 0;
    float fps = 0.0f;

    constexpr int MAX_ENTITIES = 512;
    constexpr size_t ENTITY_STRIDE = sizeof(uint64_t); // Array of pointers

    while (overlay.IsRunning())
    {
        overlay.ProcessMessages();

        // ---- FPS calculation ----
        frameCount++;
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - lastFrameTime).count();
        if (elapsed >= 1000)
        {
            fps = frameCount * 1000.0f / elapsed;
            frameCount = 0;
            lastFrameTime = now;
        }

        // ---- Poll Insert key to toggle global visibility ----
        {
            static bool s_insertWasDown = false;
            bool isDown = (GetAsyncKeyState(VK_INSERT) & 0x8000) != 0;
            if (isDown && !s_insertWasDown)
                overlay.Toggles().visible = !overlay.Toggles().visible;
            s_insertWasDown = isDown;
        }

        // ---- Re-read local actor address from renderer global (may change on respawn) ----
        if (bloodstrike::renderer::localActor != 0)
        {
            uint64_t currentLocalActor = 0;
            reader.ReadMemory<uint64_t>(
                gameBase + bloodstrike::renderer::localActor,
                currentLocalActor);
            if (currentLocalActor != 0 && currentLocalActor != localActor)
            {
                localActor = currentLocalActor;
                ReadEntity(reader, gameBase, localActor, localEntity, true);
            }
        }

        // ---- Re-read local player position each frame ----
        EntityInfo updatedLocal;
        if (ReadEntity(reader, gameBase, localActor, updatedLocal, true))
            localEntity.headPos = updatedLocal.headPos;

        // ---- Read camera matrix from renderer global ----
        Matrix4x4 camMatrix;
        if (!GetCameraMatrix(reader, gameBase, camMatrix))
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        // ---- Re-read entity list pointer (may change) ----
        uint64_t entityListBase = 0;
        reader.ReadMemory<uint64_t>(
            gameBase + bloodstrike::offsets::Messiah__EntityList,
            entityListBase);

        if (entityListBase == 0)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        // ---- Begin drawing ----
        overlay.BeginFrame();

        // ---- Check global visibility toggle (Insert key) ----
        const ESPToggles& toggles = overlay.Toggles();

        // Draw status indicator when toggled off
        if (!toggles.visible)
        {
            overlay.AddText(Vector2(10.0f, 10.0f),
                L"ESP DISABLED (Insert to toggle)",
                Color(150, 150, 150, 180), 14.0f);
            overlay.EndFrame();
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        // ---- Iterate entities ----
        for (int i = 0; i < MAX_ENTITIES; i++)
        {
            uint64_t entityAddr = 0;
            if (!reader.ReadMemory<uint64_t>(
                    entityListBase + i * ENTITY_STRIDE,
                    entityAddr))
                continue;

            if (entityAddr == 0 || entityAddr == localActor)
                continue;

            // Read entity data
            EntityInfo entity;
            if (!ReadEntity(reader, gameBase, entityAddr, entity, false))
                continue;

            if (!entity.isAlive)
                continue;

            // ---- World to Screen for bones ----
            Vector2 headScreen, pelvisScreen;
            bool headOnScreen = sdk::w2s(camMatrix, entity.headPos, headScreen, screenW, screenH);

            if (!headOnScreen)
                continue;

            sdk::w2s(camMatrix, entity.footPos, pelvisScreen, screenW, screenH);

            // ---- Distance calculation ----
            Vector3 diff = entity.headPos - localEntity.headPos;
            float distance = diff.Length();

            // ---- Color based on distance / team ----
            Color espColor;
            if (entity.team == localEntity.team)
                espColor = esp_colors::TEAMMATE;
            else if (distance < 30.0f)
                espColor = esp_colors::ENEMY_VISIBLE;
            else
                espColor = esp_colors::ENEMY_OCCLUDED;

            // ---- 2D Bounding Box ----
            if (toggles.box)
            {
                float boxHeight = std::abs(pelvisScreen.y - headScreen.y);
                float boxWidth  = boxHeight * 0.6f;
                float centerX   = headScreen.x;

                Vector2 boxTopLeft(centerX - boxWidth / 2.0f, headScreen.y);
                Vector2 boxBottomRight(centerX + boxWidth / 2.0f, pelvisScreen.y);

                overlay.AddCornerBox(boxTopLeft, boxBottomRight, espColor, 2.0f);

                // ---- Health bar (drawn relative to box) ----
                if (toggles.healthBar)
                {
                    float healthPercent = std::clamp((float)entity.health / 100.0f, 0.0f, 1.0f);
                    Color healthColor;
                    if (healthPercent > 0.6f)
                        healthColor = esp_colors::HEALTH_GREEN;
                    else if (healthPercent > 0.3f)
                        healthColor = esp_colors::HEALTH_YELLOW;
                    else
                        healthColor = esp_colors::HEALTH_RED;

                    float barWidth  = 4.0f;
                    float barHeight = boxHeight;
                    float barX      = boxTopLeft.x - barWidth - 3.0f;
                    float barY      = boxTopLeft.y;

                    overlay.AddBox(
                        Vector2(barX, barY),
                        Vector2(barX + barWidth, barY + barHeight),
                        Color(30, 30, 30, 200), 1.0f, true, Color(30, 30, 30, 200)
                    );

                    float fillHeight = barHeight * healthPercent;
                    overlay.AddBox(
                        Vector2(barX + 1, barY + barHeight - fillHeight),
                        Vector2(barX + barWidth - 1, barY + barHeight),
                        healthColor, 1.0f, true, healthColor
                    );
                }

                // ---- Player info text ----
                if (toggles.infoText)
                {
                    std::wstringstream infoText;
                    infoText << L"HP: " << entity.health
                             << L" | " << std::fixed << std::setprecision(0) << distance << L"m";

                    Vector2 textPos(boxTopLeft.x, boxTopLeft.y - 18.0f);
                    overlay.AddText(Vector2(textPos.x + 1, textPos.y + 1),
                                   infoText.str(), esp_colors::TEXT_SHADOW, 13.0f);
                    overlay.AddText(textPos, infoText.str(), esp_colors::TEXT_WHITE, 13.0f);
                }
            }

            // ---- Skeleton drawing ----
            if (toggles.skeleton)
            {
                const int SKELETON_PAIRS[][2] = {
                    { bloodstrike::BONE_HEAD,       bloodstrike::BONE_NECK },
                    { bloodstrike::BONE_NECK,       bloodstrike::BONE_CHEST },
                    { bloodstrike::BONE_CHEST,      bloodstrike::BONE_SPINE },
                    { bloodstrike::BONE_SPINE,      bloodstrike::BONE_PELVIS },
                    { bloodstrike::BONE_NECK,       bloodstrike::BONE_L_SHOULDER },
                    { bloodstrike::BONE_L_SHOULDER, bloodstrike::BONE_L_ELBOW },
                    { bloodstrike::BONE_L_ELBOW,    bloodstrike::BONE_L_HAND },
                    { bloodstrike::BONE_NECK,       bloodstrike::BONE_R_SHOULDER },
                    { bloodstrike::BONE_R_SHOULDER, bloodstrike::BONE_R_ELBOW },
                    { bloodstrike::BONE_R_ELBOW,    bloodstrike::BONE_R_HAND },
                    { bloodstrike::BONE_PELVIS,     bloodstrike::BONE_L_KNEE },
                    { bloodstrike::BONE_L_KNEE,     bloodstrike::BONE_L_FOOT },
                    { bloodstrike::BONE_PELVIS,     bloodstrike::BONE_R_KNEE },
                    { bloodstrike::BONE_R_KNEE,     bloodstrike::BONE_R_FOOT },
                };

                constexpr int NUM_BONE_PAIRS = sizeof(SKELETON_PAIRS) / sizeof(SKELETON_PAIRS[0]);

                for (int j = 0; j < NUM_BONE_PAIRS; j++)
                {
                    int idx1 = SKELETON_PAIRS[j][0];
                    int idx2 = SKELETON_PAIRS[j][1];

                    Vector2 screen1, screen2;
                    bool onScreen1 = sdk::w2s(camMatrix, entity.bonePositions[idx1], screen1, screenW, screenH);
                    bool onScreen2 = sdk::w2s(camMatrix, entity.bonePositions[idx2], screen2, screenW, screenH);

                    if (onScreen1 && onScreen2)
                    {
                        overlay.AddSkeletonLine(screen1, screen2, esp_colors::SKELETON, 1.5f);
                    }
                }
            }
        }

        // ---- FPS counter ----
        std::wstringstream fpsText;
        fpsText << L"BloodStrike ESP | FPS: " << std::fixed << std::setprecision(0) << fps;
        overlay.SetTextSize(16.0f);
        overlay.AddText(Vector2(10.0f, 10.0f), fpsText.str(), Color(0, 255, 0, 255), 16.0f);

        overlay.SetTextSize(14.0f);

        // ---- End drawing ----
        overlay.EndFrame();

        // ---- Sleep to reduce CPU usage ----
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------
int main()
{
    std::wcout << L"=== BloodStrike ESP ===\n";
    std::wcout << L"Initializing...\n\n";

    // ---- Step 1: Find the game process ----
    const wchar_t* GAME_PROCESS = L"Game.exe";
    DWORD gamePid = FindProcessId(GAME_PROCESS);

    // Try alternate process names
    if (gamePid == 0)
    {
        GAME_PROCESS = L"BloodStrike.exe";
        gamePid = FindProcessId(GAME_PROCESS);
    }
    if (gamePid == 0)
    {
        GAME_PROCESS = L"BloodStrike-Win64-Shipping.exe";
        gamePid = FindProcessId(GAME_PROCESS);
    }

    if (gamePid == 0)
    {
        std::wcerr << L"[ERROR] BloodStrike process not found. "
                    L"Make sure the game is running.\n";
        std::wcout << L"Press Enter to exit...";
        std::cin.get();
        return 1;
    }

    std::wcout << L"[+] Found game process. PID: " << gamePid << L"\n";

    // ---- Step 2: Get module base address ----
    uint64_t gameBase = GetModuleBase(gamePid, GAME_PROCESS);
    if (gameBase == 0)
    {
        std::wcerr << L"[ERROR] Failed to get module base address.\n";
        std::wcout << L"Press Enter to exit...";
        std::cin.get();
        return 1;
    }

    std::wcout << L"[+] Game module base: 0x" << std::hex << gameBase << std::dec << L"\n";
    std::wcout << L"[+] Reference base:   0x" << std::hex << bloodstrike::GameBaseRef << std::dec << L"\n";

    // ---- Step 3: Find the game window ----
    HWND gameWindow = FindWindowW(NULL, L"BloodStrike");
    if (gameWindow == NULL)
    {
        gameWindow = FindWindowW(L"UnrealWindow", L"BloodStrike");
    }
    if (gameWindow == NULL)
    {
        struct EnumCtx { DWORD pid; HWND out; };
        EnumCtx ctx = { gamePid, NULL };
        EnumWindows([](HWND hwnd, LPARAM lParam) -> BOOL {
            auto& ctx = *reinterpret_cast<EnumCtx*>(lParam);
            DWORD windowPid = 0;
            GetWindowThreadProcessId(hwnd, &windowPid);
            if (windowPid == ctx.pid && IsWindowVisible(hwnd))
            {
                ctx.out = hwnd;
                return FALSE;
            }
            return TRUE;
        }, reinterpret_cast<LPARAM>(&ctx));
        gameWindow = ctx.out;
    }

    if (gameWindow == NULL)
    {
        std::wcerr << L"[ERROR] Could not find game window.\n";
        std::wcout << L"Press Enter to exit...";
        std::cin.get();
        return 1;
    }

    std::wcout << L"[+] Found game window: 0x" << std::hex << (uint64_t)gameWindow << std::dec << L"\n";

    // ---- Step 4: Open target process for memory reading ----
    // Uses NtOpenProcess from ntdll (bypasses kernel32 hooks)
    MemoryReader reader;
    if (!reader.Open(gamePid))
    {
        std::wcerr << L"[ERROR] Failed to open game process for memory reading.\n";
        std::wcout << L"Make sure the game is running and you are running as Administrator.\n";
        std::wcout << L"Press Enter to exit...";
        std::cin.get();
        return 1;
    }

    std::wcout << L"[+] Process opened for memory reading (NtReadVirtualMemory).\n";

    // ---- Step 5: Initialize overlay ----
    Overlay overlay;
    if (!overlay.Initialize(gameWindow))
    {
        std::wcerr << L"[ERROR] Failed to initialize overlay.\n";
        std::wcout << L"Press Enter to exit...";
        std::cin.get();
        return 1;
    }

    std::wcout << L"[+] Overlay initialized.\n";
    std::wcout << L"[+] ESP running. Press Alt+F4 or close the game to exit.\n\n";

    // ---- Step 6: Run ESP loop ----
    ESPLoop(reader, gameBase, overlay);

    std::wcout << L"\n[ESP] Shutting down.\n";
    return 0;
}
