// Rails debug panel — read-only inspection of CRailEditorComponent's active
// rails plus the few mutations its public API exposes (Clear, NewRail,
// compressed-buffer snapshot/restore). Per-rail/point editing is deferred
// because the engine keeps mp_edited_rails private with no accessor — see
// the "deferred" label in the selected-rail section.
#pragma once

#if defined(DEBUG_IMGUI)

#include "../IDebugPanel.h"

namespace Obj { class CRailEditorComponent; }

namespace Debug
{

class RailsPanel : public IDebugPanel
{
public:
	const char *	GetName() const override	{ return "Rails"; }
	PanelGroup		GetGroup() const override	{ return PanelGroup::LevelEditor; }
	void			Draw() override;
	void			ResetToDefaults() override;

private:
	struct PointRow
	{
		float		pos[3];
		bool		has_post;
	};

	struct RailRow
	{
		int			num_points;
		int			first_point_index;	// into m_points
	};

	Obj::CRailEditorComponent *	GetEditor();
	void			RefreshList();
	void			DrawListSection();
	void			DrawSelectedSection();
	void			DrawActionsSection();

	void			CaptureBaseline();
	void			RestoreBaseline();

	static constexpr int kMaxRails = 200;
	static constexpr int kMaxPoints = 400;

	RailRow			m_rails[kMaxRails];
	int				m_num_rails = 0;
	PointRow		m_points[kMaxPoints];
	int				m_num_points = 0;

	int				m_selected_rail = -1;

	// Baseline is the CompressedRailsBuffer captured on first open, restored
	// on ResetToDefaults. Owned by the panel.
	unsigned char *	m_baseline = nullptr;
	int				m_baseline_size = 0;
	bool			m_baseline_valid = false;
	bool			m_baseline_tried = false;

	bool			m_confirm_clear_open = false;

public:
	void			ExportSnapshot();
	void			ImportSnapshot();
};

} // namespace Debug

#endif // DEBUG_IMGUI
