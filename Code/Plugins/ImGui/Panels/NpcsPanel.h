// NPCs debug panel — live enumeration of pedestrians in the active level.
// Lets the user pick a ped, teleport it, delete it, run a behavior script
// on it, or spawn a new ped cloned from an existing NodeArray entry.
// Lives in the LevelEditor group (F2), since every action mutates game state.
#pragma once

#if defined(DEBUG_IMGUI)

#include <cstdint>

#include "../IDebugPanel.h"

namespace Script { class CStruct; }

namespace Debug
{

class NpcsPanel : public IDebugPanel
{
public:
	const char *	GetName() const override	{ return "NPCs"; }
	PanelGroup		GetGroup() const override	{ return PanelGroup::LevelEditor; }
	void			Draw() override;
	void			ResetToDefaults() override;

private:
	struct PedEntry
	{
		uint32_t	id;
		float		pos[3];
		char		label[40];
	};

	void			RefreshList();
	void			DrawListSection();
	void			DrawSelectedSection();
	void			DrawSpawnSection();

	bool			EnsureTemplate();
	void			SpawnAt(float x, float y, float z);
	bool			DeleteSelected();
	void			TeleportSelectedTo(float x, float y, float z);
	void			RunScriptOnSelected(uint32_t script_crc);
	bool			TryReadSkaterPos(float out[3]);

public:
	void			ExportSnapshot();
	void			ImportSnapshot();

private:

	static constexpr int kMaxListRows = 64;

	PedEntry		m_entries[kMaxListRows];
	int				m_num_entries = 0;
	int				m_selected_row = -1;
	uint32_t		m_selected_id = 0;

	float			m_edit_pos[3] = { 0.0f, 0.0f, 0.0f };
	int				m_script_choice = 0;

	bool			m_template_captured = false;
	bool			m_template_tried = false;
	Script::CStruct *	m_template = nullptr;

	float			m_spawn_pos[3] = { 0.0f, 0.0f, 0.0f };
	int				m_spawn_counter = 0;
	bool			m_spawn_use_skater = true;

	bool			m_confirm_delete_open = false;
};

} // namespace Debug

#endif // DEBUG_IMGUI
