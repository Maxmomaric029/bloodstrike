#pragma once
// ============================================================
// esp.h — ESP logic: entity traversal, bone chain, rendering
// ============================================================

#include "math.h"

struct EntityInfo {
    uintptr_t address;
    Vector3   headPos;
    Vector3   footPos;
    Vector3   bones[32];
    int       health;
    int       team;
    bool      alive;
};

// Render ESP for all players in the entity list
void RenderESP();

// Read a single entity (used for local actor too)
bool ReadEntity(uintptr_t actor, EntityInfo& info);
