// Mission debug panel — authors a full A→Z mission: id, giver NPC,
// description text shown in the engine's native speech box, goal type,
// optional waypoints sourced from WaypointsPanel, and rewards. Publishing
// calls Game::CGoalManager::AddGoal + ActivateGoal so the engine handles
// its own start-of-mission dialog (no ImGui chat bubble).
//
// LevelEditor group (F2). Persists one file per mission under
// `userdata/editor/missions/<id>.json` via EditorPersistence.
#pragma once

#if defined(DEBUG_IMGUI)

#include <cstddef>
#include <cstdint>
#include <vector>

#include "../IDebugPanel.h"

namespace Script { class CStruct; }

namespace Debug
{

class MissionPanel : public IDebugPanel
{
public:
	struct Mission
	{
		char		id[48];				// lowercase identifier, also used as filename
		char		display_name[64];	// shown in speech-box header
		char		giver_npc[48];		// object_name of a Npc spawned earlier (optional)
		char		description[512];	// multi-line body of the speech box
		int			type_choice;		// index into kTypeChoices
		int			reward_cash;
		int			reward_skill_points;
		bool		has_waypoints;		// snapshot taken from WaypointsPanel at publish
		std::vector<float>	waypoints;	// flat x,y,z triples
	};

	MissionPanel();

	const char *	GetName() const override	{ return "Mission"; }
	PanelGroup		GetGroup() const override	{ return PanelGroup::LevelEditor; }
	void			Draw() override;
	void			ResetToDefaults() override;

	static MissionPanel *	Get()	{ return s_instance; }

public:
	bool			PublishMission();
	bool			RevokeMission();
	bool			ExportMission();
	bool			ImportMission(const char *id);
	bool			DiscardMission(const char *id);
	void			RefreshMissionList();

	// Phase 17 helper — replays all persisted missions from disk.
	int				GetMissionListCount() const		{ return m_mission_count; }
	const char *	GetMissionListId(int idx) const;
	bool			LoadMissionByIndex(int idx);

private:
	void			DrawFormSection();
	void			DrawActionsSection();
	void			DrawPersistenceSection();
	void			DrawListSection();

	void			BuildGoalParams(Script::CStruct &out_params);
	void			SnapshotWaypointsFromPanel();
	uint32_t		GoalId() const;

	static constexpr int kMaxMissionIds = 64;
	static constexpr std::size_t kMaxMissionIdLen = 48;

	Mission			m_form = {};
	char			m_mission_ids[kMaxMissionIds][kMaxMissionIdLen] = {};
	int				m_mission_count = 0;
	int				m_selected_list_index = -1;
	bool			m_confirm_revoke_open = false;
	bool			m_confirm_discard_open = false;
	char			m_status_message[128] = { 0 };
	bool			m_first_draw_refresh = false;

	static MissionPanel *	s_instance;
};

} // namespace Debug

#endif // DEBUG_IMGUI
