#include "WorldProject.h"

#if defined(DEBUG_IMGUI)

#include <cmath>

#include <Core/math.h>
#include <Gfx/camera.h>
#include <Gfx/NxViewMan.h>

namespace Debug::WorldProject
{

namespace
{

constexpr float kPi			= 3.14159265358979323846f;
constexpr float kDegToRad	= kPi / 180.0f;

} // anon namespace

bool WorldToScreen(const Mth::Vector &world_pos, ImVec2 &out_screen, float &out_depth)
{
	Gfx::Camera *p_camera = Nx::CViewportManager::sGetActiveCamera();
	if (p_camera == nullptr)
		return false;

	// ImGui DisplaySize is set by the SDL2 backend every BeginFrame and
	// matches the backbuffer the engine draws into — safe to use as the
	// viewport size for projection.
	const ImGuiIO &io = ImGui::GetIO();
	const float vp_w = io.DisplaySize.x;
	const float vp_h = io.DisplaySize.y;
	if (vp_w <= 1.0f || vp_h <= 1.0f)
		return false;

	Mth::Matrix cam_matrix = p_camera->GetMatrix();
	const Mth::Vector cam_pos = p_camera->GetPos();

	// Camera basis: matrix[X] = right, matrix[Y] = up, matrix[Z] = forward (at).
	const Mth::Vector right		= cam_matrix[X];
	const Mth::Vector up		= cam_matrix[Y];
	const Mth::Vector forward	= cam_matrix[Z];

	// Vector from camera to target, transformed into camera space.
	Mth::Vector rel = world_pos - cam_pos;
	const float z_cam = Mth::DotProduct(rel, forward);
	if (z_cam <= p_camera->GetNearClipPlane())
		return false;	// behind camera / too close to project safely.

	const float x_cam = Mth::DotProduct(rel, right);
	const float y_cam = Mth::DotProduct(rel, up);

	// Build NDC from horizontal FOV + aspect ratio. Camera::GetAdjustedHFOV
	// already accounts for the display's pixel-aspect correction.
	const float h_fov_deg = p_camera->GetAdjustedHFOV();
	const float tan_half_h = std::tan(h_fov_deg * 0.5f * kDegToRad);
	if (tan_half_h <= 0.0f)
		return false;
	const float aspect = vp_w / vp_h;
	const float tan_half_v = tan_half_h / aspect;

	const float ndc_x = x_cam / (z_cam * tan_half_h);
	const float ndc_y = y_cam / (z_cam * tan_half_v);

	out_screen.x = (ndc_x * 0.5f + 0.5f) * vp_w;
	// ImGui y grows downward; world up must flip to screen down.
	out_screen.y = (1.0f - (ndc_y * 0.5f + 0.5f)) * vp_h;
	out_depth = z_cam;
	return true;
}

bool WorldToScreen(const Mth::Vector &world_pos, ImVec2 &out_screen)
{
	float depth = 0.0f;
	return WorldToScreen(world_pos, out_screen, depth);
}

} // namespace Debug::WorldProject

#endif // DEBUG_IMGUI
