// Dear ImGui debug layer for TONYRE (Wn32 / OpenGL 3.3 + SDL2).
// Phase 2 skeleton: Init/Shutdown bring the ImGui context up; BeginFrame/Render
// are defined but intentionally empty of UI — panels get wired in Phase 3.
// Whole subsystem is gated by DEBUG_IMGUI so shipping builds compile it out.
#pragma once

#if defined(DEBUG_IMGUI)

struct SDL_Window;
typedef void *SDL_GLContext;
union SDL_Event;

namespace Debug
{

class IDebugPanel;

class ImGuiLayer
{
public:
	static bool		Init(SDL_Window *window, SDL_GLContext context);
	static void		Shutdown();

	static void		BeginFrame();
	static void		Render();

	static bool		ProcessEvent(const SDL_Event *event);

	static bool		IsInitialised()				{ return s_initialised; }
	static bool		IsVisible()					{ return s_visible; }
	static void		SetVisible(bool vis)		{ s_visible = vis; }
	static void		ToggleVisible();

	// Secondary group gated by F2 — mutating editor panels (NPCs, goals, rails).
	// Kept independent from s_visible so the user can watch FPS without the
	// editor floating on top, and enter the editor without triggering the
	// overlay if they had previously hidden it.
	static bool		IsLevelEditorVisible()		{ return s_level_editor_visible; }
	static void		SetLevelEditorVisible(bool v){ s_level_editor_visible = v; }
	static void		ToggleLevelEditor();

	// Tertiary group gated by F3 — object placement + model preview tools.
	// Independent of F1/F2 for the same reasons as above.
	static bool		IsObjectEditorVisible()		{ return s_object_editor_visible; }
	static void		SetObjectEditorVisible(bool v){ s_object_editor_visible = v; }
	static void		ToggleObjectEditor();

	// Panels register their singleton instances here. Layer does not own
	// their lifetime — panels are static storage in their own translation units.
	static bool		RegisterPanel(IDebugPanel *panel);

private:
	static void		DrawTestWindow();
	static void		DrawPanels();
	static void		RegisterBuiltinPanels();

	static constexpr int kMaxPanels = 16;

	static bool				s_initialised;
	static bool				s_visible;
	static bool				s_level_editor_visible;
	static bool				s_object_editor_visible;
	static IDebugPanel *	s_panels[kMaxPanels];
	static int				s_num_panels;
};

} // namespace Debug

#endif // DEBUG_IMGUI
