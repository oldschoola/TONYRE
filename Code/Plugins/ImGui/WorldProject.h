// World-to-screen projection helper used by editor panels that draw
// markers on top of the 3D scene (waypoints, spawn points, etc.).
// Kept tiny and header-light so every panel can include it without
// dragging in the full Gfx/Nx stack.
#pragma once

#if defined(DEBUG_IMGUI)

#include "imgui.h"

namespace Mth { class Vector; }

namespace Debug::WorldProject
{

// Project a world-space point to ImGui screen coordinates. Returns true on
// success (point is in front of the camera and inside the viewport after
// projection). On failure out_screen is left untouched.
bool WorldToScreen(const Mth::Vector &world_pos, ImVec2 &out_screen);

// Same as WorldToScreen, but also reports the view-space depth (distance
// along the camera's forward axis). Useful when callers want to scale or
// fade markers by distance.
bool WorldToScreen(const Mth::Vector &world_pos, ImVec2 &out_screen, float &out_depth);

} // namespace Debug::WorldProject

#endif // DEBUG_IMGUI
