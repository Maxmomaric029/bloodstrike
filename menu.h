#pragma once
// ============================================================
// menu.h — ImGui menu with ESP toggles
// ============================================================

void ToggleMenu();
bool IsMenuOpen();

bool IsESPEnabled();
bool ShowBoxes();
bool ShowSkeleton();
bool ShowDistance();
bool ShowHealth();

void RenderMenu();
