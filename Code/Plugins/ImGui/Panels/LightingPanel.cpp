#include "LightingPanel.h"

#if defined(DEBUG_IMGUI)

#include "imgui.h"

#include <Core/math.h>
#include <Gfx/Image/ImageBasic.h>
#include <Gfx/NxLightMan.h>

#include <math.h>

namespace Debug
{

namespace
{

void RgbaToFloat4(const Image::RGBA &in, float out[4])
{
	out[0] = static_cast<float>(in.r) / 255.0f;
	out[1] = static_cast<float>(in.g) / 255.0f;
	out[2] = static_cast<float>(in.b) / 255.0f;
	out[3] = static_cast<float>(in.a) / 255.0f;
}

Image::RGBA Float4ToRgba(const float in[4])
{
	auto clamp_byte = [](float f) -> uint8 {
		int v = static_cast<int>(f * 255.0f + 0.5f);
		if (v < 0)   v = 0;
		if (v > 255) v = 255;
		return static_cast<uint8>(v);
	};
	return Image::RGBA(clamp_byte(in[0]), clamp_byte(in[1]), clamp_byte(in[2]), clamp_byte(in[3]));
}

void NormaliseInPlace(float v[3])
{
	float len_sq = v[0] * v[0] + v[1] * v[1] + v[2] * v[2];
	if (len_sq < 1e-6f)
	{
		// Avoid zero vector — fall back to -Y (typical sun direction).
		v[0] = 0.0f; v[1] = -1.0f; v[2] = 0.0f;
		return;
	}
	float inv_len = 1.0f / sqrtf(len_sq);
	v[0] *= inv_len; v[1] *= inv_len; v[2] *= inv_len;
}

} // anon namespace

void LightingPanel::CaptureBaseline()
{
	m_baseline.ambient_color = Nx::CLightManager::sGetLightAmbientColor();
	m_baseline.ambient_mod = Nx::CLightManager::sGetAmbientLightModulationFactor();
	for (int i = 0; i < Nx::CLightManager::MAX_LIGHTS; ++i)
	{
		m_baseline.direction[i] = Nx::CLightManager::sGetLightDirection(i);
		m_baseline.diffuse_color[i] = Nx::CLightManager::sGetLightDiffuseColor(i);
		m_baseline.diffuse_mod[i] = Nx::CLightManager::sGetDiffuseLightModulationFactor(i);
	}
	m_baseline_valid = true;
}

void LightingPanel::ResetAmbient()
{
	if (!m_baseline_valid)
		return;
	Nx::CLightManager::sSetLightAmbientColor(m_baseline.ambient_color);
	Nx::CLightManager::sSetAmbientLightModulationFactor(m_baseline.ambient_mod);
}

void LightingPanel::ResetDirectionalLight(int light_index)
{
	if (!m_baseline_valid || light_index < 0 || light_index >= Nx::CLightManager::MAX_LIGHTS)
		return;
	Nx::CLightManager::sSetLightDirection(light_index, m_baseline.direction[light_index]);
	Nx::CLightManager::sSetLightDiffuseColor(light_index, m_baseline.diffuse_color[light_index]);
	Nx::CLightManager::sSetDiffuseLightModulationFactor(light_index, m_baseline.diffuse_mod[light_index]);
}

void LightingPanel::ResetToDefaults()
{
	if (!m_baseline_valid)
		return;
	ResetAmbient();
	for (int i = 0; i < Nx::CLightManager::MAX_LIGHTS; ++i)
		ResetDirectionalLight(i);
}

void LightingPanel::Draw()
{
	if (!ImGui::Begin(GetName(), GetOpenFlag()))
	{
		ImGui::End();
		return;
	}

	// First frame the panel draws, snapshot what the engine currently reports
	// as the light setup. That becomes the revert target. User can overwrite
	// it after a level change with [Re-snapshot baseline].
	if (!m_baseline_valid)
		CaptureBaseline();

	if (ImGui::Button("Reset All"))
		ResetToDefaults();
	ImGui::SameLine();
	if (ImGui::Button("Re-snapshot baseline"))
		CaptureBaseline();
	ImGui::SameLine();
	ImGui::TextDisabled("baseline %s", m_baseline_valid ? "captured" : "empty");

	ImGui::Separator();

	if (ImGui::CollapsingHeader("Ambient", ImGuiTreeNodeFlags_DefaultOpen))
		DrawAmbientSection();

	for (int i = 0; i < Nx::CLightManager::MAX_LIGHTS; ++i)
	{
		char header[32];
		snprintf(header, sizeof(header), "Directional Light %d", i);
		ImGuiTreeNodeFlags flags = (i == 0) ? ImGuiTreeNodeFlags_DefaultOpen : 0;
		if (ImGui::CollapsingHeader(header, flags))
			DrawDirectionalLightSection(i);
	}

	if (ImGui::CollapsingHeader("Fog"))
		DrawFogSection();

	ImGui::End();
}

void LightingPanel::DrawAmbientSection()
{
	if (ImGui::SmallButton("Reset##ambient"))
		ResetAmbient();
	ImGui::SameLine();
	ImGui::TextDisabled("revert to baseline");

	// Ambient color — read-edit-writeback through engine setter.
	float col[4];
	RgbaToFloat4(Nx::CLightManager::sGetLightAmbientColor(), col);
	if (ImGui::ColorEdit4("Ambient Color", col, ImGuiColorEditFlags_AlphaBar))
		Nx::CLightManager::sSetLightAmbientColor(Float4ToRgba(col));

	// Ambient modulation — 0..2 covers doubling/halving common cases.
	float ambient_mod = Nx::CLightManager::sGetAmbientLightModulationFactor();
	if (ImGui::SliderFloat("Ambient Modulation", &ambient_mod, 0.0f, 2.0f, "%.3f"))
		Nx::CLightManager::sSetAmbientLightModulationFactor(ambient_mod);

	ImGui::TextDisabled("Live brightness: %.3f", Nx::CLightManager::sGetAmbientBrightness());
}

void LightingPanel::DrawDirectionalLightSection(int light_index)
{
	ImGui::PushID(light_index);

	if (ImGui::SmallButton("Reset"))
		ResetDirectionalLight(light_index);
	ImGui::SameLine();
	ImGui::TextDisabled("revert to baseline");

	// Direction — sliders in [-1,1], renormalised on any edit.
	const Mth::Vector &cur_dir = Nx::CLightManager::sGetLightDirection(light_index);
	float dir[3] = { cur_dir.GetX(), cur_dir.GetY(), cur_dir.GetZ() };
	if (ImGui::SliderFloat3("Direction", dir, -1.0f, 1.0f, "%.3f"))
	{
		NormaliseInPlace(dir);
		Mth::Vector new_dir(dir[0], dir[1], dir[2]);
		Nx::CLightManager::sSetLightDirection(light_index, new_dir);
	}

	// Diffuse color.
	float col[4];
	RgbaToFloat4(Nx::CLightManager::sGetLightDiffuseColor(light_index), col);
	if (ImGui::ColorEdit4("Diffuse Color", col, ImGuiColorEditFlags_AlphaBar))
		Nx::CLightManager::sSetLightDiffuseColor(light_index, Float4ToRgba(col));

	// Per-light modulation.
	float mod = Nx::CLightManager::sGetDiffuseLightModulationFactor(light_index);
	if (ImGui::SliderFloat("Modulation", &mod, 0.0f, 2.0f, "%.3f"))
		Nx::CLightManager::sSetDiffuseLightModulationFactor(light_index, mod);

	ImGui::TextDisabled("Live brightness: %.3f", Nx::CLightManager::sGetDiffuseBrightness(light_index));

	ImGui::PopID();
}

void LightingPanel::DrawFogSection()
{
	// Fog is intentionally inert here. The Wn32 OpenGL platform layer
	// (p_nxmiscfx.cpp) still holds the original D3D stubs, and the GL shaders
	// carry no fog uniforms, so CFog setters write to dead state. Wiring real
	// GL fog is engine work outside the ImGui plugin's scope.
	ImGui::TextDisabled("Fog deferred — not wired on GL path");
}

} // namespace Debug

#endif // DEBUG_IMGUI
