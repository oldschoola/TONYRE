// FBO-backed model preview widget used by ObjectsPanel. Owns one sFBO
// plus a private GL program + VAO/VBO for rendering a rotating wireframe
// box scaled to the selected model's file size. Not the full mesh yet —
// loading the actual CMesh requires a valid texture dictionary and an
// engine-globals save/restore wrapper that hasn't landed — but the
// widget renders live 3D geometry per frame so the preview clearly
// responds to selection and proves the FBO→ImGui round-trip.
#pragma once

#if defined(DEBUG_IMGUI)

#include <cstdint>

namespace NxWn32 { class sFBO; }

namespace Debug
{

class ModelPreview
{
public:
	ModelPreview();
	~ModelPreview();

	ModelPreview(const ModelPreview &) = delete;
	ModelPreview &operator=(const ModelPreview &) = delete;

	bool			Ensure();

	void			SetModelPath(const char *path);

	// Caller marks the widget visible-this-frame so Render() can early-out
	// when the panel is hidden. Avoids 9× glGetIntegerv (CPU↔GPU sync) and
	// the FBO draw when the preview isn't on screen.
	void			SetVisible(bool v) { m_visible = v; }

	void			Render();

	void			ImGuiDraw(float width, float height);

	unsigned int	GetColorTextureRaw() const;

	static constexpr int kSize = 256;

private:
	void			RefreshMeta();
	bool			EnsureGeometry();

	NxWn32::sFBO *	m_fbo = nullptr;
	char			m_model_path[256] = { 0 };
	char			m_basename[128] = { 0 };
	uint64_t		m_file_size = 0;
	bool			m_file_exists = false;
	float			m_color[3] = { 0.20f, 0.22f, 0.28f };
	float			m_spin = 0.0f;
	float			m_cube_scale = 1.0f;
	bool			m_visible = false;

	// Owned GL objects for the wireframe render. Created lazily on first
	// Render() call so we can assume the GL context is current by then.
	unsigned int	m_program = 0;
	unsigned int	m_vao = 0;
	unsigned int	m_vbo = 0;
	unsigned int	m_ebo = 0;
	int				m_u_mvp = -1;
	int				m_u_color = -1;
};

} // namespace Debug

#endif // DEBUG_IMGUI
