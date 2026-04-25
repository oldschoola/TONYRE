// Dear ImGui debug layer implementation.
// Phase 2: real backend init/shutdown so the ImGui stack is live, but no
// widgets drawn. Frame-loop hooks (BeginFrame/Render call sites) land in Phase 3.
#include "ImGuiLayer.h"

#if defined(DEBUG_IMGUI)

#include <SDL.h>

#include "imgui.h"
#include "backends/imgui_impl_sdl2.h"
#include "backends/imgui_impl_opengl3.h"

#include "IDebugPanel.h"
#include "Panels/LightingPanel.h"
#include "Panels/NpcsPanel.h"
#include "Panels/GoalsPanel.h"
#include "Panels/RailsPanel.h"
#include "Panels/WaypointsPanel.h"
#include "Panels/ObjectsPanel.h"
#include "Panels/MissionPanel.h"
#include "Panels/EditorToolbarPanel.h"
#include "Panels/PerformancePanel.h"

namespace Debug
{

bool			ImGuiLayer::s_initialised = false;
bool			ImGuiLayer::s_visible = true;	// visible on boot so smoke test is obvious; F1 hides.
bool			ImGuiLayer::s_level_editor_visible = false;	// hidden on boot; F2 reveals.
bool			ImGuiLayer::s_object_editor_visible = false;	// hidden on boot; F3 reveals.
IDebugPanel *	ImGuiLayer::s_panels[ImGuiLayer::kMaxPanels] = {};
int				ImGuiLayer::s_num_panels = 0;

bool ImGuiLayer::Init(SDL_Window *window, SDL_GLContext context)
{
	if (s_initialised)
		return true;

	IMGUI_CHECKVERSION();
	ImGui::CreateContext();

	ImGuiIO &io = ImGui::GetIO();
	io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
	io.IniFilename = nullptr;	// don't spam imgui.ini into the game's cwd

	ImGui::StyleColorsDark();

	if (!ImGui_ImplSDL2_InitForOpenGL(window, context))
	{
		ImGui::DestroyContext();
		return false;
	}

	// GLSL 330 core matches the game's OpenGL 3.3 core context.
	if (!ImGui_ImplOpenGL3_Init("#version 330 core"))
	{
		ImGui_ImplSDL2_Shutdown();
		ImGui::DestroyContext();
		return false;
	}

	s_initialised = true;

	RegisterBuiltinPanels();
	return true;
}

bool ImGuiLayer::RegisterPanel(IDebugPanel *panel)
{
	if (panel == nullptr || s_num_panels >= kMaxPanels)
		return false;

	s_panels[s_num_panels++] = panel;
	return true;
}

void ImGuiLayer::RegisterBuiltinPanels()
{
	// File-scope singletons — lifetime matches process. Registry stores a
	// pointer, not ownership, so this is safe.
	static LightingPanel s_lighting_panel;
	RegisterPanel(&s_lighting_panel);

	static NpcsPanel s_npcs_panel;
	RegisterPanel(&s_npcs_panel);

	static GoalsPanel s_goals_panel;
	RegisterPanel(&s_goals_panel);

	static RailsPanel s_rails_panel;
	RegisterPanel(&s_rails_panel);

	static WaypointsPanel s_waypoints_panel;
	RegisterPanel(&s_waypoints_panel);

	static ObjectsPanel s_objects_panel;
	RegisterPanel(&s_objects_panel);

	static MissionPanel s_mission_panel;
	RegisterPanel(&s_mission_panel);

	static EditorToolbarPanel s_editor_toolbar_panel;
	RegisterPanel(&s_editor_toolbar_panel);

	static PerformancePanel s_performance_panel;
	RegisterPanel(&s_performance_panel);
}

namespace
{
	void ReopenPanelsInGroup(IDebugPanel *const *panels, int count, PanelGroup group)
	{
		for (int i = 0; i < count; ++i)
		{
			if (panels[i] != nullptr && panels[i]->GetGroup() == group)
				panels[i]->SetOpen(true);
		}
	}
}

void ImGuiLayer::ToggleVisible()
{
	s_visible = !s_visible;
	if (s_visible)
		ReopenPanelsInGroup(s_panels, s_num_panels, PanelGroup::Overlay);
}

void ImGuiLayer::ToggleLevelEditor()
{
	s_level_editor_visible = !s_level_editor_visible;
	if (s_level_editor_visible)
		ReopenPanelsInGroup(s_panels, s_num_panels, PanelGroup::LevelEditor);
}

void ImGuiLayer::ToggleObjectEditor()
{
	s_object_editor_visible = !s_object_editor_visible;
	if (s_object_editor_visible)
		ReopenPanelsInGroup(s_panels, s_num_panels, PanelGroup::ObjectEditor);
}

void ImGuiLayer::Shutdown()
{
	if (!s_initialised)
		return;

	ImGui_ImplOpenGL3_Shutdown();
	ImGui_ImplSDL2_Shutdown();
	ImGui::DestroyContext();
	s_initialised = false;
}

void ImGuiLayer::BeginFrame()
{
	if (!s_initialised)
		return;

	ImGui_ImplOpenGL3_NewFrame();
	ImGui_ImplSDL2_NewFrame();
	ImGui::NewFrame();

	// Test window tracks the overlay group.
	if (s_visible)
		DrawTestWindow();

	// Each panel gates itself on its own group — some on F1, some on F2, some on F3.
	// Panels still have to be open (their own close-button flag).
	DrawPanels();
}

void ImGuiLayer::DrawPanels()
{
	for (int i = 0; i < s_num_panels; ++i)
	{
		IDebugPanel *panel = s_panels[i];
		if (panel == nullptr || !panel->IsOpen())
			continue;

		PanelGroup group = panel->GetGroup();
		if (group == PanelGroup::Overlay && !s_visible)
			continue;
		if (group == PanelGroup::LevelEditor && !s_level_editor_visible)
			continue;
		if (group == PanelGroup::ObjectEditor && !s_object_editor_visible)
			continue;

		panel->Draw();
	}
}

void ImGuiLayer::DrawTestWindow()
{
	const ImGuiIO &io = ImGui::GetIO();

	ImGui::SetNextWindowPos(ImVec2(20.0f, 20.0f), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(280.0f, 120.0f), ImGuiCond_FirstUseEver);

	if (ImGui::Begin("Hello TONYRE"))
	{
		ImGui::Text("ImGui %s", IMGUI_VERSION);
		ImGui::Separator();
		ImGui::Text("FPS: %.1f", io.Framerate);
		ImGui::Text("Frame: %.3f ms", 1000.0f / (io.Framerate > 0.0f ? io.Framerate : 1.0f));
		ImGui::Separator();
		ImGui::TextDisabled("F1 hides overlay");
	}
	ImGui::End();
}

void ImGuiLayer::Render()
{
	if (!s_initialised)
		return;

	ImGui::Render();
	ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

bool ImGuiLayer::ProcessEvent(const SDL_Event *event)
{
	if (!s_initialised || event == nullptr)
		return false;

	ImGui_ImplSDL2_ProcessEvent(event);

	const ImGuiIO &io = ImGui::GetIO();
	return io.WantCaptureKeyboard || io.WantCaptureMouse;
}

} // namespace Debug

#endif // DEBUG_IMGUI
