// ============================================================
// menu.cpp — ImGui menu with ESP toggles
// ============================================================

#include "menu.h"
#include "imgui.h"

static bool g_MenuOpen    = true;
static bool g_ESPEnabled  = true;
static bool g_ShowBoxes   = true;
static bool g_ShowSkel    = true;
static bool g_ShowDist    = true;
static bool g_ShowHealth  = true;

void ToggleMenu()       { g_MenuOpen = !g_MenuOpen; }
bool IsMenuOpen()       { return g_MenuOpen; }
bool IsESPEnabled()     { return g_ESPEnabled; }
bool ShowBoxes()        { return g_ShowBoxes; }
bool ShowSkeleton()     { return g_ShowSkel; }
bool ShowDistance()     { return g_ShowDist; }
bool ShowHealth()       { return g_ShowHealth; }

void RenderMenu() {
    if (!g_MenuOpen) return;

    ImGui::SetNextWindowSize({220, 0}, ImGuiCond_Once);
    ImGui::Begin("BloodStrike ESP", &g_MenuOpen,
                 ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize);

    ImGui::Checkbox("Enable ESP",     &g_ESPEnabled);
    ImGui::Checkbox("Corner Boxes",   &g_ShowBoxes);
    ImGui::Checkbox("Skeleton",       &g_ShowSkel);
    ImGui::Checkbox("Distance",       &g_ShowDist);
    ImGui::Checkbox("Health Bar",     &g_ShowHealth);
    ImGui::Separator();
    ImGui::Text("Insert = toggle menu");
    ImGui::End();
}
