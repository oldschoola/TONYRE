// Small always-visible toolbar in the Overlay (F1) group. Carries the
// single "Reset All Editors" button that clears the three pieces of
// authored editor state at once: placed objects, authored waypoints, and
// the currently-authored mission's live goal. Confirm-modal gates the
// destructive action so accidental clicks can't wipe a session.
#pragma once

#if defined(DEBUG_IMGUI)

#include "../IDebugPanel.h"

namespace Debug
{

class EditorToolbarPanel : public IDebugPanel
{
public:
	const char *	GetName() const override	{ return "Editor Toolbar"; }
	PanelGroup		GetGroup() const override	{ return PanelGroup::Overlay; }
	void			Draw() override;

private:
	void			ResetAllEditors();

	bool			m_confirm_reset_open = false;
	char			m_status[128] = { 0 };
};

} // namespace Debug

#endif // DEBUG_IMGUI
