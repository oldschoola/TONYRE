#include "WaypointsPanel.h"

#if defined(DEBUG_IMGUI)

#include <cstdio>
#include <cstring>

#include "imgui.h"

#include <Core/math.h>

#include <Sk/Modules/Skate/skate.h>
#include <Sk/Objects/skater.h>

#include "../WorldProject.h"

namespace Debug
{

namespace
{

// Marker visual tuning — screen-space sizes so distant waypoints still
// remain clickable/readable. Halo + core + selected ring.
constexpr float kMarkerCoreRadius	= 6.0f;
constexpr float kMarkerHaloRadius	= 10.0f;
constexpr float kSelectedRing		= 14.0f;

ImU32 MarkerColour(int idx)
{
	// Cycle hue by index so consecutive waypoints are visually distinct.
	// Saturation/value fixed for legibility against most backgrounds.
	float h = (static_cast<float>(idx) * 0.17f);
	h -= static_cast<int>(h);
	float r = 0.0f, g = 0.0f, b = 0.0f;
	ImGui::ColorConvertHSVtoRGB(h, 0.85f, 1.0f, r, g, b);
	return ImGui::ColorConvertFloat4ToU32(ImVec4(r, g, b, 1.0f));
}

} // anon namespace

WaypointsPanel *WaypointsPanel::s_instance = nullptr;

WaypointsPanel::WaypointsPanel()
{
	// Latest constructed panel wins — only one file-scope singleton exists
	// at a time via RegisterBuiltinPanels, so this is stable for the
	// process lifetime.
	s_instance = this;
}

const WaypointsPanel::Waypoint *WaypointsPanel::GetWaypoint(std::size_t i) const
{
	if (i >= m_waypoints.size())
		return nullptr;
	return &m_waypoints[i];
}

void WaypointsPanel::ClearAll()
{
	m_waypoints.clear();
	m_selected_index = -1;
}

void WaypointsPanel::ReplaceAll(const Waypoint *src, std::size_t count)
{
	m_waypoints.assign(src, src + count);
	m_selected_index = (count > 0) ? 0 : -1;
}

void WaypointsPanel::AddWaypoint(float x, float y, float z)
{
	Waypoint w;
	w.pos[0] = x;
	w.pos[1] = y;
	w.pos[2] = z;
	m_waypoints.push_back(w);
	m_selected_index = static_cast<int>(m_waypoints.size()) - 1;
}

void WaypointsPanel::ResetToDefaults()
{
	ClearAll();
	m_show_markers = true;
	m_confirm_clear_open = false;
}

bool WaypointsPanel::TryReadSkaterPos(float out[3])
{
	Mdl::Skate *skate = Mdl::Skate::Instance();
	if (skate == nullptr)
		return false;
	Obj::CSkater *local = skate->GetLocalSkater();
	if (local == nullptr)
		return false;
	const Mth::Vector &p = local->GetPos();
	out[0] = p.GetX();
	out[1] = p.GetY();
	out[2] = p.GetZ();
	return true;
}

void WaypointsPanel::DeleteAt(int index)
{
	if (index < 0 || index >= static_cast<int>(m_waypoints.size()))
		return;
	m_waypoints.erase(m_waypoints.begin() + index);
	if (m_waypoints.empty())
		m_selected_index = -1;
	else if (m_selected_index >= static_cast<int>(m_waypoints.size()))
		m_selected_index = static_cast<int>(m_waypoints.size()) - 1;
}

void WaypointsPanel::MoveUp(int index)
{
	if (index <= 0 || index >= static_cast<int>(m_waypoints.size()))
		return;
	std::swap(m_waypoints[index - 1], m_waypoints[index]);
	if (m_selected_index == index) m_selected_index = index - 1;
	else if (m_selected_index == index - 1) m_selected_index = index;
}

void WaypointsPanel::MoveDown(int index)
{
	if (index < 0 || index + 1 >= static_cast<int>(m_waypoints.size()))
		return;
	std::swap(m_waypoints[index], m_waypoints[index + 1]);
	if (m_selected_index == index) m_selected_index = index + 1;
	else if (m_selected_index == index + 1) m_selected_index = index;
}

void WaypointsPanel::DrawActionBar()
{
	if (ImGui::Button("Add at Skater"))
	{
		float p[3];
		if (TryReadSkaterPos(p))
			AddWaypoint(p[0], p[1], p[2]);
	}
	ImGui::SameLine();
	if (ImGui::Button("Add at Origin"))
		AddWaypoint(0.0f, 0.0f, 0.0f);
	ImGui::SameLine();
	if (ImGui::Button("Clear All") && !m_waypoints.empty())
		m_confirm_clear_open = true;

	ImGui::SameLine();
	ImGui::Checkbox("Show 3D Markers", &m_show_markers);
}

void WaypointsPanel::DrawListSection()
{
	ImGui::Text("Waypoints (%d)", static_cast<int>(m_waypoints.size()));
	ImGui::Separator();

	if (m_waypoints.empty())
	{
		ImGui::TextDisabled("No waypoints yet.");
		return;
	}

	if (ImGui::BeginTable("waypoints", 6,
			ImGuiTableFlags_SizingStretchProp |
			ImGuiTableFlags_RowBg |
			ImGuiTableFlags_BordersInnerH))
	{
		ImGui::TableSetupColumn("#",	ImGuiTableColumnFlags_WidthFixed, 28.0f);
		ImGui::TableSetupColumn("Pos",	ImGuiTableColumnFlags_WidthStretch);
		ImGui::TableSetupColumn("Up",	ImGuiTableColumnFlags_WidthFixed, 22.0f);
		ImGui::TableSetupColumn("Dn",	ImGuiTableColumnFlags_WidthFixed, 22.0f);
		ImGui::TableSetupColumn("Del",	ImGuiTableColumnFlags_WidthFixed, 28.0f);
		ImGui::TableSetupColumn("Hit",	ImGuiTableColumnFlags_WidthFixed, 44.0f);
		ImGui::TableHeadersRow();

		int pending_delete = -1;
		int pending_up = -1;
		int pending_down = -1;

		for (int i = 0; i < static_cast<int>(m_waypoints.size()); ++i)
		{
			ImGui::PushID(i);
			ImGui::TableNextRow();

			ImGui::TableSetColumnIndex(0);
			const bool selected = (m_selected_index == i);
			char label[16];
			std::snprintf(label, sizeof(label), "%d", i);
			if (ImGui::Selectable(label, selected, ImGuiSelectableFlags_SpanAllColumns))
				m_selected_index = i;

			ImGui::TableSetColumnIndex(1);
			ImGui::SetNextItemWidth(-FLT_MIN);
			ImGui::DragFloat3("##pos", m_waypoints[i].pos, 1.0f);

			ImGui::TableSetColumnIndex(2);
			if (ImGui::SmallButton("^"))	pending_up = i;

			ImGui::TableSetColumnIndex(3);
			if (ImGui::SmallButton("v"))	pending_down = i;

			ImGui::TableSetColumnIndex(4);
			if (ImGui::SmallButton("x"))	pending_delete = i;

			ImGui::TableSetColumnIndex(5);
			if (ImGui::SmallButton("Skater"))
			{
				float p[3];
				if (TryReadSkaterPos(p))
				{
					m_waypoints[i].pos[0] = p[0];
					m_waypoints[i].pos[1] = p[1];
					m_waypoints[i].pos[2] = p[2];
				}
			}

			ImGui::PopID();
		}

		ImGui::EndTable();

		if (pending_up >= 0)	MoveUp(pending_up);
		if (pending_down >= 0)	MoveDown(pending_down);
		if (pending_delete >= 0) DeleteAt(pending_delete);
	}
}

void WaypointsPanel::DrawMarkers()
{
	if (!m_show_markers || m_waypoints.empty())
		return;

	ImDrawList *dl = ImGui::GetBackgroundDrawList();
	const ImU32 halo_col	= ImGui::ColorConvertFloat4ToU32(ImVec4(0.0f, 0.0f, 0.0f, 0.55f));
	const ImU32 ring_col	= ImGui::ColorConvertFloat4ToU32(ImVec4(1.0f, 1.0f, 0.0f, 0.95f));
	const ImU32 line_col	= ImGui::ColorConvertFloat4ToU32(ImVec4(1.0f, 1.0f, 1.0f, 0.35f));

	ImVec2 prev_screen(0.0f, 0.0f);
	bool prev_valid = false;

	for (int i = 0; i < static_cast<int>(m_waypoints.size()); ++i)
	{
		const Waypoint &wp = m_waypoints[i];
		Mth::Vector world(wp.pos[0], wp.pos[1], wp.pos[2]);
		ImVec2 screen;
		float depth = 0.0f;
		const bool ok = WorldProject::WorldToScreen(world, screen, depth);
		if (!ok)
		{
			prev_valid = false;
			continue;
		}

		if (prev_valid)
			dl->AddLine(prev_screen, screen, line_col, 2.0f);

		dl->AddCircleFilled(screen, kMarkerHaloRadius, halo_col);
		dl->AddCircleFilled(screen, kMarkerCoreRadius, MarkerColour(i));
		if (i == m_selected_index)
			dl->AddCircle(screen, kSelectedRing, ring_col, 0, 2.5f);

		char tag[12];
		std::snprintf(tag, sizeof(tag), "%d", i);
		dl->AddText(ImVec2(screen.x + 8.0f, screen.y - 8.0f), ring_col, tag);

		prev_screen = screen;
		prev_valid = true;
	}
}

void WaypointsPanel::Draw()
{
	ImGui::SetNextWindowPos(ImVec2(20.0f, 200.0f), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(460.0f, 360.0f), ImGuiCond_FirstUseEver);

	if (!ImGui::Begin(GetName(), GetOpenFlag()))
	{
		ImGui::End();
		return;
	}

	DrawActionBar();
	ImGui::Separator();
	DrawListSection();

	ImGui::End();

	// Markers draw outside the window so they're always overlaid on the
	// scene even when the panel is small or off-screen.
	DrawMarkers();

	if (m_confirm_clear_open)
	{
		ImGui::OpenPopup("Clear all waypoints?");
		m_confirm_clear_open = false;
	}
	if (ImGui::BeginPopupModal("Clear all waypoints?", nullptr,
			ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::TextUnformatted("Drop every waypoint from the list?");
		ImGui::Separator();
		if (ImGui::Button("Clear", ImVec2(120.0f, 0.0f)))
		{
			ClearAll();
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f)))
			ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
	}
}

} // namespace Debug

#endif // DEBUG_IMGUI
