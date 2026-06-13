// ============================================================
// menu.cpp — ImGui menu with ESP toggles
// ============================================================

#include "menu.h"
#include "imgui.h"

// ============================================================
// Settings
// ============================================================
static bool g_MenuOpen   = true;
static bool g_ESPEnabled = true;

void ToggleMenu()      { g_MenuOpen = !g_MenuOpen; }
bool IsMenuOpen()      { return g_MenuOpen; }
bool IsESPEnabled()    { return g_ESPEnabled; }

// ============================================================
// Render menu window
// ============================================================
void RenderMenu() {
    if (!g_MenuOpen) return;

    ImGui::SetNextWindowSize({220, 0}, ImGuiCond_Once);
    ImGui::Begin("BloodStrike ESP", &g_MenuOpen,
                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize);

    ImGui::Checkbox("Enable ESP",     &g_ESPEnabled);
    ImGui::Separator();
    ImGui::Text("Insert = toggle menu");
    ImGui::End();
}
