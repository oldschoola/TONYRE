// Lighting debug panel — live bind to Nx::CLightManager.
// Reads current state every frame and writes back through the same static
// setters the engine uses, so mutations persist into the normal update path.
// Captures a baseline on first Draw so the user can revert after tweaking.
// Fog section is deliberately inert: the GL path has no fog implementation
// today (platform setters are D3D stubs, shaders have no fog uniforms), so
// shipping those controls misleads the user.
#pragma once

#if defined(DEBUG_IMGUI)

#include "../IDebugPanel.h"

#include <Gfx/Image/ImageBasic.h>
#include <Core/math.h>
#include <Gfx/NxLightMan.h>

namespace Debug
{

class LightingPanel : public IDebugPanel
{
public:
	const char *	GetName() const override	{ return "Lighting"; }
	void			Draw() override;
	void			ResetToDefaults() override;

private:
	struct Baseline
	{
		Image::RGBA		ambient_color;
		float			ambient_mod;
		Mth::Vector		direction[Nx::CLightManager::MAX_LIGHTS];
		Image::RGBA		diffuse_color[Nx::CLightManager::MAX_LIGHTS];
		float			diffuse_mod[Nx::CLightManager::MAX_LIGHTS];
	};

	void			DrawAmbientSection();
	void			DrawDirectionalLightSection(int light_index);
	void			DrawFogSection();

	void			CaptureBaseline();
	void			ResetAmbient();
	void			ResetDirectionalLight(int light_index);

	bool			m_baseline_valid = false;
	Baseline		m_baseline = {};
};

} // namespace Debug

#endif // DEBUG_IMGUI
