// Minimal panel interface — each debug panel owns its own ImGui window and
// its own open/closed state. Registered with Debug::ImGuiLayer during Init.
#pragma once

#if defined(DEBUG_IMGUI)

namespace Debug
{

// Panels belong to one of three groups. Overlay = passive debug (F1).
// LevelEditor = mutating editor (F2 — peds/goals/rails/waypoints/missions).
// ObjectEditor = object placement / model preview tools (F3).
// The layer filters rendering by visibility of each group.
enum class PanelGroup
{
	Overlay,
	LevelEditor,
	ObjectEditor,
};

class IDebugPanel
{
public:
	virtual			~IDebugPanel() = default;

	virtual const char *	GetName() const = 0;
	virtual void			Draw() = 0;

	// Optional: revert any live engine state the panel has mutated back to the
	// baseline the panel captured on first Draw. Default no-op for panels that
	// don't mutate state.
	virtual void			ResetToDefaults()	{}

	// Panel group classification. Default Overlay so existing panels don't move.
	virtual PanelGroup		GetGroup() const	{ return PanelGroup::Overlay; }

	bool			IsOpen() const		{ return m_open; }
	void			SetOpen(bool open)	{ m_open = open; }
	bool *			GetOpenFlag()		{ return &m_open; }

protected:
	bool			m_open = true;
};

} // namespace Debug

#endif // DEBUG_IMGUI
