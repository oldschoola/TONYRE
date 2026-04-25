// Objects debug panel — spawns arbitrary composite-objects (models) from
// a filesystem scan of Game/Data/models. Tracks placements per level so
// the user can export/import a level's objects independently.
//
// Lives in the ObjectEditor group (F3). Phase 14 covers scan + spawn +
// placements. Phase 16 wires a ModelPreview widget on top of this panel.
#pragma once

#if defined(DEBUG_IMGUI)

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "../IDebugPanel.h"
#include "../ModelPreview.h"

namespace Script { class CStruct; class CArray; }

namespace Debug
{

class ObjectsPanel : public IDebugPanel
{
public:
	struct Placement
	{
		char	model_path[128];	// relative to Game/Data/models, includes .skin/.mdl
		char	object_name[64];	// unique per placement
		float	pos[3];
	};

	ObjectsPanel();

	const char *	GetName() const override	{ return "Objects"; }
	PanelGroup		GetGroup() const override	{ return PanelGroup::ObjectEditor; }
	void			Draw() override;
	void			ResetToDefaults() override;

public:
	void			ExportSnapshot();
	void			ImportSnapshot();
	void			ScanModels();

	// Phase 17 — cross-panel / toolbar operations.
	int				SpawnAllPlacements();	// returns number successfully spawned
	int				KillAllPlacements();	// kills in-game objects, keeps list
	void			ClearPlacementList();	// forgets all placements (UI + persist)

	static ObjectsPanel *	Get()	{ return s_instance; }

private:
	void			DrawScanSection();
	void			DrawModelListSection();
	void			DrawPreviewSection();
	void			DrawSpawnSection();
	void			DrawPlacementsSection();

	bool			LoadCacheFromDisk();
	bool			SaveCacheToDisk();
	void			BuildLevelId(char *out, std::size_t cap) const;
	bool			TryReadSkaterPos(float out[3]) const;
	bool			SpawnPlacement(const Placement &p);	// live game spawn
	void			AddAndSpawn(const char *model_path, float x, float y, float z);
	void			DeletePlacement(int index);

	std::vector<std::string>	m_models;
	std::vector<Placement>		m_placements;
	int							m_model_filter_match = 0;
	int							m_selected_model_index = -1;
	int							m_selected_placement_index = -1;
	char						m_filter[64] = { 0 };
	// Cached filter result — rebuilt only when m_filter string changes or
	// m_models size/content changes. Keeps DrawModelListSection O(1) per
	// frame when filter and scan are stable.
	std::vector<int>			m_filtered_indices;
	char						m_last_filter[64] = { 0 };
	bool						m_filter_dirty = true;
	char						m_spawn_name_stem[48] = "object";
	int							m_spawn_counter = 0;
	float						m_spawn_pos[3] = { 0.0f, 0.0f, 0.0f };
	bool						m_spawn_use_skater = true;
	bool						m_cache_loaded = false;
	bool						m_confirm_delete_open = false;
	bool						m_confirm_clear_placements_open = false;

	ModelPreview				m_preview;
	int							m_preview_selected_index = -1;
	bool						m_preview_show = true;

	static ObjectsPanel *		s_instance;
};

} // namespace Debug

#endif // DEBUG_IMGUI
