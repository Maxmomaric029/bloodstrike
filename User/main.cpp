// ============================================================================
// main.cpp — BloodStrike ESP
//
//   Initializes the kernel driver overlay, finds the game process,
//   iterates through the entity list, reads bone transforms via the
//   driver, converts to screen coordinates, and draws ESP (skeleton,
//   corner box, health info).
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
bool ReadEntity(const KM_Driver& driver, HANDLE pid, uint64_t gameBase,
                uint64_t entityAddress, EntityInfo& entity, bool isLocal)
{
    entity.actorAddress = entityAddress;
    entity.isLocal = isLocal;

    // Verify the entity has the Actor vtable
    uint64_t vtablePtr = 0;
    if (!driver.ReadMemory<uint64_t>(pid, entityAddress, vtablePtr))
        return false;

    // Optional: check against known Actor vtable
    // (We skip strict vtable check since it may vary; rely on chain reads instead)

    // Read actor properties pointer:
    // actorInstance + field::actorInstance_to_actorProps (0x278) -> actorProps
    uint64_t actorProps = 0;
    if (!driver.ReadMemory<uint64_t>(pid,
            entityAddress + bloodstrike::field::actorInstance_to_actorProps,
            actorProps))
        return false;

    if (actorProps == 0)
        return false;

    // Read entity mask (health / alive status from IEntity)
    // IEntity is at the base of the actor. mask at IEntity + 0x2E0
    uint32_t entityMask = 0;
    if (!driver.ReadMemory<uint32_t>(pid,
            entityAddress + bloodstrike::field::IEntity_to_entityMask,
            entityMask))
        return false;

    // Typical mask: bit 0 = alive, bits for team/health
    entity.isAlive = (entityMask & 1) != 0;
    entity.health  = (entityMask >> 8) & 0xFF;  // Example extraction
    entity.team    = (entityMask >> 16) & 0xFF;

    // Read the skeleton component from the actor
    // Typically the skeleton is found via the SkeletonComponent vtable
    // We need to scan actor's components for the skeleton
    // Common pattern: Actor + 0x98 -> TArray<Component*>
    uint64_t componentsArray = 0;
    uint32_t componentsCount = 0;
    if (!driver.ReadMemory<uint64_t>(pid, entityAddress + 0x98, componentsArray))
        return false;
    if (!driver.ReadMemory<uint32_t>(pid, entityAddress + 0xA0, componentsCount))
        return false;

    // For Messiah Engine, the skeleton is often at a fixed offset in the actor
    // due to the ActorComponent hierarchy.
    // Simplified: try to read bone transforms from known skeleton offset.
    // In practice, the skeleton component can be found by scanning components,
    // but for performance we use a known fixed offset pattern.
    //
    // Common Actor layout:
    //   +0x000: vtable (Messiah__Actor)
    //   +0x008..0x278: Actor fields
    //   +0x278: actorProps (ActorProperties*)
    //   +0x098: Components array (TArray<Component*>)
    //
    // The skeleton component is often at a component index (e.g. index 2 or 3).
    // We'll try to read the skeleton component pointer from the components array.

    uint64_t skeletonComp = 0;
    const int SKELETON_COMP_INDEX = 2; // Example index — adjust per game version

    if (componentsArray != 0 && componentsCount > SKELETON_COMP_INDEX)
    {
        driver.ReadMemory<uint64_t>(pid,
            componentsArray + SKELETON_COMP_INDEX * sizeof(uint64_t),
            skeletonComp);
    }

    // If we couldn't find skeleton via components, try the pose offset directly
    // from actor properties: actorProps + known offset -> Pose
    // pose_to_BipedPose = 0x90 relative to Pose object
    if (skeletonComp == 0)
    {
        // Try to find pose from actor props
        uint64_t posePtr = 0;
        if (driver.ReadMemory<uint64_t>(pid, actorProps + 0x50, posePtr) && posePtr != 0)
        {
            // posePtr + pose_to_BipedPose -> actual bone data
            uint64_t bipedPose = 0;
            if (driver.ReadMemory<uint64_t>(pid,
                    posePtr + bloodstrike::field::pose_to_BipedPose,
                    bipedPose) && bipedPose != 0)
            {
                skeletonComp = bipedPose;
            }
        }
    }

    if (skeletonComp == 0)
        return false;

    // Read bone positions
    // Use the bone indices defined in SDK_BloodStrike.h
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
            sdk::ReadBoneWorldPosition(driver, pid, skeletonComp, boneIdx,
                                       entity.bonePositions[boneIdx]);
        }
    }

    // Head and foot positions for box ESP
    entity.headPos = entity.bonePositions[bloodstrike::BONE_HEAD];
    entity.footPos = entity.bonePositions[bloodstrike::BONE_PELVIS];
    // Adjust foot to be below pelvis
    entity.footPos.y -= 40.0f; // Approximate height offset

    return true;
}

// ---------------------------------------------------------------------------
// Get camera matrix from local player
// ---------------------------------------------------------------------------
bool GetCameraMatrix(const KM_Driver& driver, HANDLE pid, uint64_t gameBase,
                     uint64_t localPlayerAddr, Matrix4x4& outMatrix)
{
    // Read camera pointer from local player:
    // ClientPlayer + field::ClientPlayer_to_camera (0x238) -> ICamera*
    uint64_t cameraPtr = 0;
    if (!driver.ReadMemory<uint64_t>(pid,
            localPlayerAddr + bloodstrike::field::ClientPlayer_to_camera,
            cameraPtr))
        return false;

    if (cameraPtr == 0)
        return false;

    // Verify camera vtable
    uint64_t cameraVtable = 0;
    if (!driver.ReadMemory<uint64_t>(pid, cameraPtr, cameraVtable))
        return false;

    // Optional: verify against Messiah__ICamera vtable
    // uint64_t expectedVtable = gameBase + bloodstrike::vftables::Messiah__ICamera;
    // if (cameraVtable != expectedVtable) return false;

    // Read the view-projection matrix from the camera object.
    // ICamera typically stores the view-projection matrix at a fixed offset.
    // Common offsets: 0x30, 0x40, or 0x50 for the 4x4 matrix.
    // We'll try offset 0x30 (common for Messiah Engine).
    if (!driver.ReadMemoryRaw(pid, cameraPtr + 0x30, outMatrix.data, sizeof(float) * 16))
        return false;

    // Validate matrix — reject NaN/Inf which would corrupt all W2S calculations
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
bool GetLocalPlayer(const KM_Driver& driver, HANDLE pid, uint64_t gameBase,
                    uint64_t& outClientEngine, uint64_t& outLocalPlayer,
                    uint64_t& outEntityList)
{
    // Read ClientEngine pointer
    uint64_t clientEnginePtr = 0;
    if (!driver.ReadMemory<uint64_t>(pid,
            gameBase + bloodstrike::offsets::Messiah__ClientEngine,
            clientEnginePtr))
        return false;

    if (clientEnginePtr == 0)
        return false;

    outClientEngine = clientEnginePtr;

    // Read IGameplay from ClientEngine:
    // ClientEngine + field::ClientEngine_to_IGameplay (0x58) -> IGameplay*
    uint64_t gameplayPtr = 0;
    if (!driver.ReadMemory<uint64_t>(pid,
            clientEnginePtr + bloodstrike::field::ClientEngine_to_IGameplay,
            gameplayPtr))
        return false;

    if (gameplayPtr == 0)
        return false;

    // The local player object (ClientPlayer) is typically at IGameplay + 0x10
    uint64_t localPlayerPtr = 0;
    if (!driver.ReadMemory<uint64_t>(pid, gameplayPtr + 0x10, localPlayerPtr))
        return false;

    outLocalPlayer = localPlayerPtr;

    // Read entity list base
    uint64_t entityListBase = 0;
    if (!driver.ReadMemory<uint64_t>(pid,
            gameBase + bloodstrike::offsets::Messiah__EntityList,
            entityListBase))
        return false;

    outEntityList = entityListBase;
    return true;
}

// ---------------------------------------------------------------------------
// Main ESP loop
// ---------------------------------------------------------------------------
void ESPLoop(const KM_Driver& driver, HANDLE pid, uint64_t gameBase,
             Overlay& overlay)
{
    uint64_t clientEngine  = 0;
    uint64_t localPlayer   = 0;
    uint64_t entityListPtr = 0;

    if (!GetLocalPlayer(driver, pid, gameBase, clientEngine, localPlayer, entityListPtr))
    {
        std::cerr << "[ESP] Failed to get local player / engine pointers.\n";
        return;
    }

    std::wcout << L"[ESP] Local player at: 0x" << std::hex << localPlayer << std::dec << L"\n";
    std::wcout << L"[ESP] Entity list at: 0x" << std::hex << entityListPtr << std::dec << L"\n";

    // Local player -> local actor
    uint64_t localActor = 0;
    if (!driver.ReadMemory<uint64_t>(pid,
            localPlayer + bloodstrike::field::ClientPlayer_to_localActor,
            localActor))
    {
        std::cerr << "[ESP] Failed to read local actor.\n";
        return;
    }

    std::wcout << L"[ESP] Local actor: 0x" << std::hex << localActor << std::dec << L"\n";

    // Screen dimensions
    int screenW = overlay.GetWidth();
    int screenH = overlay.GetHeight();

    // Initialize local player info
    EntityInfo localEntity;
    ReadEntity(driver, pid, gameBase, localActor, localEntity, true);

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
        // (The overlay window cannot receive keyboard focus, so we poll globally)
        {
            static bool s_insertWasDown = false;
            bool isDown = (GetAsyncKeyState(VK_INSERT) & 0x8000) != 0;
            if (isDown && !s_insertWasDown)
                overlay.Toggles().visible = !overlay.Toggles().visible;
            s_insertWasDown = isDown;
        }

        // ---- Re-read local actor address (may change on respawn) ----
        {
            uint64_t currentLocalActor = 0;
            driver.ReadMemory<uint64_t>(pid,
                localPlayer + bloodstrike::field::ClientPlayer_to_localActor,
                currentLocalActor);
            if (currentLocalActor != 0 && currentLocalActor != localActor)
            {
                localActor = currentLocalActor;
                ReadEntity(driver, pid, gameBase, localActor, localEntity, true);
            }
        }

        // ---- Re-read local player position each frame for accurate distances ----
        EntityInfo updatedLocal;
        if (ReadEntity(driver, pid, gameBase, localActor, updatedLocal, true))
            localEntity.headPos = updatedLocal.headPos;

        // ---- Read camera matrix ----
        Matrix4x4 camMatrix;
        if (!GetCameraMatrix(driver, pid, gameBase, localPlayer, camMatrix))
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }

        // ---- Re-read entity list pointer (may change) ----
        uint64_t entityListBase = 0;
        driver.ReadMemory<uint64_t>(pid,
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
            if (!driver.ReadMemory<uint64_t>(pid,
                    entityListBase + i * ENTITY_STRIDE,
                    entityAddr))
                continue;

            if (entityAddr == 0 || entityAddr == localActor)
                continue;

            // Read entity data
            EntityInfo entity;
            if (!ReadEntity(driver, pid, gameBase, entityAddr, entity, false))
                continue;

            if (!entity.isAlive)
                continue;

            // ---- World to Screen for bones ----
            // Head position -> screen
            Vector2 headScreen, pelvisScreen;
            bool headOnScreen = sdk::w2s(camMatrix, entity.headPos, headScreen, screenW, screenH);

            if (!headOnScreen)
                continue;

            // Pelvis (or foot) position -> screen
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

            // ---- Skeleton drawing (only if toggled on) ----
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
        // Try finding by class or other means
        gameWindow = FindWindowW(L"UnrealWindow", L"BloodStrike");
    }
    if (gameWindow == NULL)
    {
        // Fallback: find any visible window belonging to the target process
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

    // ---- Step 4: Initialize the driver ----
    KM_Driver driver;

    if (!driver.isConnected())
    {
        std::wcerr << L"[ERROR] Failed to connect to kernel driver "
                    L"(\\\\.\\BS_KernelDriver).\n";
        std::wcout << L"Ensure the driver is loaded before running this program.\n";
        std::wcout << L"Press Enter to exit...";
        std::cin.get();
        return 1;
    }

    std::wcout << L"[+] Kernel driver connected.\n";

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
    HANDLE hProcess = (HANDLE)(ULONG_PTR)gamePid;
    ESPLoop(driver, hProcess, gameBase, overlay);

    std::wcout << L"\n[ESP] Shutting down.\n";
    return 0;
}
