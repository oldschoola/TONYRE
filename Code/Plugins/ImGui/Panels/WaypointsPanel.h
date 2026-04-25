// Waypoints debug panel — edit a plugin-owned std::vector<Waypoint> that
// the MissionPanel consumes when publishing race-type goals. Also draws
// the list as on-screen markers via ImGui's background draw list so the
// user can see where waypoints land in 3D space.
//
// Single active list for Phase 13 — mission-scoped ownership wires in
// Phase 15 where the MissionPanel swaps lists per mission.
#pragma once

#if defined(DEBUG_IMGUI)

#include <cstddef>
#include <vector>

#include "../IDebugPanel.h"

namespace Debug
{

class WaypointsPanel : public IDebugPanel
{
public:
	struct Waypoint
	{
		float	pos[3] = { 0.0f, 0.0f, 0.0f };
	};

	WaypointsPanel();

	const char *	GetName() const override	{ return "Waypoints"; }
	PanelGroup		GetGroup() const override	{ return PanelGroup::LevelEditor; }
	void			Draw() override;
	void			ResetToDefaults() override;

	// Singleton accessor — MissionPanel pulls the live waypoint list when
	// publishing a race goal. Pointer becomes valid after ImGuiLayer
	// registers the built-in panels.
	static WaypointsPanel *	Get()	{ return s_instance; }

	// Cross-panel accessors — MissionPanel reads the active list when
	// building race goal params.
	std::size_t		GetWaypointCount() const		{ return m_waypoints.size(); }
	const Waypoint *GetWaypoint(std::size_t i) const;
	void			ClearAll();
	void			ReplaceAll(const Waypoint *src, std::size_t count);
	void			AddWaypoint(float x, float y, float z);

private:
	void			DrawListSection();
	void			DrawMarkers();
	void			DrawActionBar();

	bool			TryReadSkaterPos(float out[3]);
	void			DeleteAt(int index);
	void			MoveUp(int index);
	void			MoveDown(int index);

	std::vector<Waypoint>	m_waypoints;
	int						m_selected_index = -1;
	bool					m_show_markers = true;
	bool					m_confirm_clear_open = false;

	static WaypointsPanel *	s_instance;
};

} // namespace Debug

#endif // DEBUG_IMGUI
