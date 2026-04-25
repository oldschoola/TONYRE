#include "EditorToolbarPanel.h"

#if defined(DEBUG_IMGUI)

#include <cstdio>

#include "imgui.h"

#include "ObjectsPanel.h"
#include "WaypointsPanel.h"
#include "MissionPanel.h"

namespace Debug
{

void EditorToolbarPanel::ResetAllEditors()
{
	int killed_objects = 0;
	int cleared_waypoints = 0;
	bool revoked_mission = false;

	if (ObjectsPanel *op = ObjectsPanel::Get())
	{
		killed_objects = op->KillAllPlacements();
		op->ClearPlacementList();
	}
	if (WaypointsPanel *wp = WaypointsPanel::Get())
	{
		cleared_waypoints = static_cast<int>(wp->GetWaypointCount());
		wp->ClearAll();
	}
	if (MissionPanel *mp = MissionPanel::Get())
	{
		// RevokeMission returns false when there's no live goal — that's
		// fine, we still consider the reset successful.
		revoked_mission = mp->RevokeMission();
	}

	std::snprintf(m_status, sizeof(m_status),
		"reset: %d objects killed, %d waypoints cleared, mission goal %s",
		killed_objects, cleared_waypoints,
		revoked_mission ? "revoked" : "not live");
}

void EditorToolbarPanel::Draw()
{
	if (!IsOpen()) return;

	// Dock just under the FPS test window so both overlay widgets sit
	// together at the top-left.
	ImGui::SetNextWindowPos(ImVec2(20.0f, 160.0f), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(280.0f, 110.0f), ImGuiCond_FirstUseEver);

	bool open = true;
	if (!ImGui::Begin("Editor Toolbar", &open))
	{
		ImGui::End();
		if (!open) SetOpen(false);
		return;
	}

	ImGui::TextDisabled("Cross-panel actions");
	ImGui::Separator();

	if (ImGui::Button("Reset All Editors", ImVec2(-1.0f, 0.0f)))
		m_confirm_reset_open = true;

	if (m_status[0] != '\0')
	{
		ImGui::Separator();
		ImGui::TextWrapped("%s", m_status);
	}

	if (m_confirm_reset_open)
	{
		ImGui::OpenPopup("Reset all editor state?");
		m_confirm_reset_open = false;
	}
	if (ImGui::BeginPopupModal("Reset all editor state?", nullptr,
			ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::TextWrapped("This will:");
		ImGui::BulletText("Kill all placed objects in the scene");
		ImGui::BulletText("Clear the placement list");
		ImGui::BulletText("Clear every authored waypoint");
		ImGui::BulletText("Revoke the current mission's live goal");
		ImGui::Separator();
		if (ImGui::Button("Reset", ImVec2(120.0f, 0.0f)))
		{
			ResetAllEditors();
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f)))
			ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
	}

	ImGui::End();
	if (!open) SetOpen(false);
}

} // namespace Debug

#endif // DEBUG_IMGUI
