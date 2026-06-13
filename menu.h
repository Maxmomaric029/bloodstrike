#pragma once
// ============================================================
// menu.h — ImGui menu with ESP toggles
// ============================================================

// Toggle menu visibility (called from WndProc on Insert key)
void ToggleMenu();
bool IsMenuOpen();

// Toggle ESP on/off (called from menu checkbox)
bool IsESPEnabled();

// Render the ImGui menu (called from HookedPresent)
void RenderMenu();
