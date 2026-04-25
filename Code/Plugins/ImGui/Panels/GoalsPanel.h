// Goals debug panel — enumerates active CGoalManager entries, shows their
// params (name/type/score/time_limit/pos), and exposes Activate/Deactivate/
// Win/Lose/Remove plus a minimal Add Goal form. LevelEditor group (F2).
#pragma once

#if defined(DEBUG_IMGUI)

#include <cstdint>

#include "../IDebugPanel.h"

namespace Script { class CStruct; }

namespace Debug
{

class GoalsPanel : public IDebugPanel
{
public:
	const char *	GetName() const override	{ return "Goals"; }
	PanelGroup		GetGroup() const override	{ return PanelGroup::LevelEditor; }
	void			Draw() override;
	void			ResetToDefaults() override;

private:
	struct GoalEntry
	{
		uint32_t	id;
		bool		active;
		bool		won;
		char		label[48];
	};

	void			RefreshList();
	void			DrawListSection();
	void			DrawSelectedSection();
	void			DrawAddGoalSection();

	void			ActivateSelected();
	void			DeactivateSelected();
	void			WinSelected();
	void			LoseSelected();
	void			RemoveSelected();

	bool			TryAddGoal();

public:
	void			ExportSnapshot();
	void			ImportSnapshot();

private:

	static constexpr int kMaxListRows = 64;

	GoalEntry		m_entries[kMaxListRows];
	int				m_num_entries = 0;
	int				m_selected_row = -1;
	uint32_t		m_selected_id = 0;

	// Add-Goal form state.
	char			m_new_id_name[48] = { 0 };
	int				m_new_type_choice = 0;
	int				m_new_score = 1000;
	int				m_new_time_limit = 60;
	float			m_new_pos[3] = { 0.0f, 0.0f, 0.0f };

	// Destructive-op confirm modals.
	bool			m_confirm_remove_open = false;
	bool			m_confirm_win_open = false;
};

} // namespace Debug

#endif // DEBUG_IMGUI
