#include "ModelPreview.h"

#if defined(DEBUG_IMGUI)

#include "imgui.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <system_error>

#include <glad/glad.h>

#include <Plat/Gfx/nx/fbo.h>

namespace Debug
{

namespace
{
	uint32_t HashPath(const char *s)
	{
		uint32_t h = 2166136261u;	// FNV-1a
		while (s && *s)
		{
			h ^= static_cast<uint8_t>(*s++);
			h *= 16777619u;
		}
		return h;
	}

	const char *kVertexSrc =
		"#version 330 core\n"
		"layout(location=0) in vec3 aPos;\n"
		"uniform mat4 uMVP;\n"
		"void main(){ gl_Position = uMVP * vec4(aPos,1.0); }\n";

	const char *kFragmentSrc =
		"#version 330 core\n"
		"uniform vec3 uColor;\n"
		"out vec4 FragColor;\n"
		"void main(){ FragColor = vec4(uColor,1.0); }\n";

	// 12-edge unit cube centered at origin, side length 2.
	constexpr float kCubeVerts[] = {
		-1.0f,-1.0f,-1.0f,  1.0f,-1.0f,-1.0f,
		 1.0f, 1.0f,-1.0f, -1.0f, 1.0f,-1.0f,
		-1.0f,-1.0f, 1.0f,  1.0f,-1.0f, 1.0f,
		 1.0f, 1.0f, 1.0f, -1.0f, 1.0f, 1.0f,
	};
	constexpr unsigned short kCubeLines[] = {
		0,1, 1,2, 2,3, 3,0,	// back face
		4,5, 5,6, 6,7, 7,4,	// front face
		0,4, 1,5, 2,6, 3,7,	// connectors
	};

	unsigned int CompileShader(GLenum type, const char *src)
	{
		GLuint s = glCreateShader(type);
		glShaderSource(s, 1, &src, nullptr);
		glCompileShader(s);
		GLint ok = GL_FALSE;
		glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
		if (!ok) { glDeleteShader(s); return 0; }
		return s;
	}

	// Row-major 4x4 multiply, columns consumed as vec4s (GL default column-major
	// upload). We build columns in-place and hand to glUniformMatrix4fv with
	// transpose=false.
	struct Mat4 { float m[16]; };
	Mat4 Identity() { Mat4 r{}; r.m[0]=r.m[5]=r.m[10]=r.m[15]=1.0f; return r; }
	Mat4 Perspective(float fov_rad, float aspect, float n, float f)
	{
		Mat4 r{};
		const float t = 1.0f / std::tan(fov_rad * 0.5f);
		r.m[0]  = t / aspect;
		r.m[5]  = t;
		r.m[10] = (f + n) / (n - f);
		r.m[11] = -1.0f;
		r.m[14] = (2.0f * f * n) / (n - f);
		return r;
	}
	Mat4 Translate(float x, float y, float z)
	{
		Mat4 r = Identity();
		r.m[12] = x; r.m[13] = y; r.m[14] = z;
		return r;
	}
	Mat4 RotateY(float a)
	{
		Mat4 r = Identity();
		const float c = std::cos(a), s = std::sin(a);
		r.m[0] = c;  r.m[2] = s;
		r.m[8] = -s; r.m[10] = c;
		return r;
	}
	Mat4 RotateX(float a)
	{
		Mat4 r = Identity();
		const float c = std::cos(a), s = std::sin(a);
		r.m[5] = c;  r.m[6] = -s;
		r.m[9] = s;  r.m[10] = c;
		return r;
	}
	Mat4 Scale(float k)
	{
		Mat4 r = Identity();
		r.m[0] = k; r.m[5] = k; r.m[10] = k;
		return r;
	}
	Mat4 Mul(const Mat4 &a, const Mat4 &b)
	{
		Mat4 r{};
		for (int i = 0; i < 4; ++i)
			for (int j = 0; j < 4; ++j)
				r.m[j * 4 + i] =
					a.m[0 * 4 + i] * b.m[j * 4 + 0] +
					a.m[1 * 4 + i] * b.m[j * 4 + 1] +
					a.m[2 * 4 + i] * b.m[j * 4 + 2] +
					a.m[3 * 4 + i] * b.m[j * 4 + 3];
		return r;
	}
}

ModelPreview::ModelPreview() = default;

ModelPreview::~ModelPreview()
{
	if (m_vao)		glDeleteVertexArrays(1, &m_vao);
	if (m_vbo)		glDeleteBuffers(1, &m_vbo);
	if (m_ebo)		glDeleteBuffers(1, &m_ebo);
	if (m_program)	glDeleteProgram(m_program);
	delete m_fbo;
	m_fbo = nullptr;
}

bool ModelPreview::Ensure()
{
	if (m_fbo != nullptr) return true;
	m_fbo = new NxWn32::sFBO(kSize, kSize, true);
	if (m_fbo == nullptr) return false;
	return m_fbo->GetColorTexture() != 0;
}

bool ModelPreview::EnsureGeometry()
{
	if (m_program != 0 && m_vao != 0) return true;

	GLuint vs = CompileShader(GL_VERTEX_SHADER, kVertexSrc);
	GLuint fs = CompileShader(GL_FRAGMENT_SHADER, kFragmentSrc);
	if (vs == 0 || fs == 0)
	{
		if (vs) glDeleteShader(vs);
		if (fs) glDeleteShader(fs);
		return false;
	}

	m_program = glCreateProgram();
	glAttachShader(m_program, vs);
	glAttachShader(m_program, fs);
	glBindAttribLocation(m_program, 0, "aPos");
	glLinkProgram(m_program);
	glDeleteShader(vs);
	glDeleteShader(fs);

	GLint ok = GL_FALSE;
	glGetProgramiv(m_program, GL_LINK_STATUS, &ok);
	if (!ok)
	{
		glDeleteProgram(m_program);
		m_program = 0;
		return false;
	}

	m_u_mvp = glGetUniformLocation(m_program, "uMVP");
	m_u_color = glGetUniformLocation(m_program, "uColor");

	// Save existing bindings so we don't clobber whatever was bound before.
	GLint prev_vao = 0, prev_vbo = 0, prev_ebo = 0;
	glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prev_vao);
	glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &prev_vbo);
	glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &prev_ebo);

	glGenVertexArrays(1, &m_vao);
	glGenBuffers(1, &m_vbo);
	glGenBuffers(1, &m_ebo);

	glBindVertexArray(m_vao);
	glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
	glBufferData(GL_ARRAY_BUFFER, sizeof(kCubeVerts), kCubeVerts, GL_STATIC_DRAW);
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);
	glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(kCubeLines), kCubeLines, GL_STATIC_DRAW);

	glEnableVertexAttribArray(0);
	glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void *)0);

	glBindVertexArray(static_cast<GLuint>(prev_vao));
	glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(prev_vbo));
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLuint>(prev_ebo));
	return true;
}

void ModelPreview::RefreshMeta()
{
	m_basename[0] = '\0';
	m_file_size = 0;
	m_file_exists = false;
	m_cube_scale = 1.0f;

	if (m_model_path[0] == '\0')
	{
		m_color[0] = 0.20f;
		m_color[1] = 0.22f;
		m_color[2] = 0.28f;
		return;
	}

	const char *slash = std::strrchr(m_model_path, '/');
	const char *bslash = std::strrchr(m_model_path, '\\');
	const char *start = slash;
	if (bslash != nullptr && (start == nullptr || bslash > start)) start = bslash;
	const char *base = (start != nullptr) ? start + 1 : m_model_path;
	std::snprintf(m_basename, sizeof(m_basename), "%s", base);

	std::error_code ec;
	std::filesystem::path p = std::filesystem::path("Data/models") / m_model_path;
	if (std::filesystem::exists(p, ec))
	{
		m_file_exists = true;
		auto sz = std::filesystem::file_size(p, ec);
		if (!ec) m_file_size = static_cast<uint64_t>(sz);
	}

	const uint32_t h = HashPath(m_model_path);
	const float hue = static_cast<float>(h & 0xFFFFu) / 65535.0f;
	ImGui::ColorConvertHSVtoRGB(hue, 0.75f, 0.95f,
		m_color[0], m_color[1], m_color[2]);

	// Map file size (roughly kB..MB) to a cube scale factor 0.6..1.4.
	// Gives the render a visual indicator that distinguishes small props
	// from large hierarchies even though it's not the real geometry.
	double kb = static_cast<double>(m_file_size) / 1024.0;
	double norm = std::log10(kb + 1.0) / 4.0;	// 4.0 ≈ log10(10 MB)
	if (norm < 0.0) norm = 0.0;
	if (norm > 1.0) norm = 1.0;
	m_cube_scale = 0.6f + static_cast<float>(norm) * 0.8f;
}

void ModelPreview::SetModelPath(const char *path)
{
	if (path == nullptr) path = "";
	std::snprintf(m_model_path, sizeof(m_model_path), "%s", path);
	RefreshMeta();
}

unsigned int ModelPreview::GetColorTextureRaw() const
{
	if (m_fbo == nullptr) return 0;
	return m_fbo->GetColorTexture();
}

void ModelPreview::Render()
{
	if (!m_visible) return;
	if (!Ensure()) return;
	if (!EnsureGeometry()) return;

	// Capture everything we touch so the main render pass is unaffected.
	GLint prev_viewport[4];
	glGetIntegerv(GL_VIEWPORT, prev_viewport);

	GLint prev_fbo = 0, prev_program = 0, prev_vao = 0;
	GLint prev_vbo = 0, prev_ebo = 0;
	glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prev_fbo);
	glGetIntegerv(GL_CURRENT_PROGRAM, &prev_program);
	glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prev_vao);
	glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &prev_vbo);
	glGetIntegerv(GL_ELEMENT_ARRAY_BUFFER_BINDING, &prev_ebo);

	const GLboolean prev_depth = glIsEnabled(GL_DEPTH_TEST);
	const GLboolean prev_cull  = glIsEnabled(GL_CULL_FACE);
	const GLboolean prev_blend = glIsEnabled(GL_BLEND);
	GLboolean prev_depth_mask = GL_TRUE;
	glGetBooleanv(GL_DEPTH_WRITEMASK, &prev_depth_mask);
	GLfloat prev_clear[4];
	glGetFloatv(GL_COLOR_CLEAR_VALUE, prev_clear);
	GLfloat prev_line_width = 1.0f;
	glGetFloatv(GL_LINE_WIDTH, &prev_line_width);

	m_fbo->BindFBO();
	glViewport(0, 0, kSize, kSize);

	// Dark background so the wireframe reads clearly.
	glClearColor(0.06f, 0.07f, 0.10f, 1.0f);
	glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

	glEnable(GL_DEPTH_TEST);
	glDepthMask(GL_TRUE);
	glDisable(GL_CULL_FACE);
	glDisable(GL_BLEND);
	glLineWidth(1.5f);

	// Slow animated rotation so preview is live — 1 turn per ~4 seconds.
	m_spin += 0.016f;
	if (m_spin > 6.2831853f) m_spin -= 6.2831853f;

	const Mat4 proj = Perspective(0.9f, 1.0f, 0.1f, 20.0f);
	const Mat4 view = Translate(0.0f, 0.0f, -5.0f);
	const Mat4 model =
		Mul(Mul(RotateX(m_spin * 0.35f), RotateY(m_spin)),
			Scale(m_cube_scale));
	const Mat4 mvp = Mul(proj, Mul(view, model));

	glUseProgram(m_program);
	if (m_u_mvp >= 0)
		glUniformMatrix4fv(m_u_mvp, 1, GL_FALSE, mvp.m);
	if (m_u_color >= 0)
		glUniform3fv(m_u_color, 1, m_color);

	glBindVertexArray(m_vao);
	glDrawElements(GL_LINES,
		static_cast<GLsizei>(sizeof(kCubeLines) / sizeof(kCubeLines[0])),
		GL_UNSIGNED_SHORT, (void *)0);

	// Full restore.
	glBindVertexArray(static_cast<GLuint>(prev_vao));
	glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(prev_vbo));
	glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, static_cast<GLuint>(prev_ebo));
	glUseProgram(static_cast<GLuint>(prev_program));
	glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(prev_fbo));
	glViewport(prev_viewport[0], prev_viewport[1],
		prev_viewport[2], prev_viewport[3]);
	glLineWidth(prev_line_width);
	glClearColor(prev_clear[0], prev_clear[1], prev_clear[2], prev_clear[3]);
	if (prev_depth) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
	if (prev_cull)  glEnable(GL_CULL_FACE);  else glDisable(GL_CULL_FACE);
	if (prev_blend) glEnable(GL_BLEND);      else glDisable(GL_BLEND);
	glDepthMask(prev_depth_mask);
}

void ModelPreview::ImGuiDraw(float width, float height)
{
	if (!Ensure())
	{
		ImGui::TextDisabled("preview FBO unavailable");
		return;
	}

	const GLuint tex = m_fbo->GetColorTexture();
	if (tex == 0)
	{
		ImGui::TextDisabled("preview texture not ready");
		return;
	}

	ImTextureID id = static_cast<ImTextureID>(static_cast<intptr_t>(tex));
	const ImVec2 cursor = ImGui::GetCursorScreenPos();
	ImGui::Image(id, ImVec2(width, height), ImVec2(0.0f, 1.0f), ImVec2(1.0f, 0.0f));

	if (m_basename[0] != '\0')
	{
		ImDrawList *dl = ImGui::GetWindowDrawList();
		const ImVec2 text_pos(cursor.x + 8.0f, cursor.y + 8.0f);
		dl->AddText(ImVec2(text_pos.x + 1.0f, text_pos.y + 1.0f),
			IM_COL32(0, 0, 0, 220), m_basename);
		dl->AddText(text_pos, IM_COL32(255, 255, 255, 235), m_basename);
	}

	if (m_model_path[0] == '\0')
	{
		ImGui::TextDisabled("no model selected");
	}
	else
	{
		ImGui::TextWrapped("%s", m_model_path);
		if (m_file_exists)
		{
			if (m_file_size >= (1u << 20))
				ImGui::Text("size: %.2f MB", m_file_size / (1024.0 * 1024.0));
			else if (m_file_size >= 1024)
				ImGui::Text("size: %.1f KB", m_file_size / 1024.0);
			else
				ImGui::Text("size: %llu bytes",
					static_cast<unsigned long long>(m_file_size));
		}
		else
		{
			ImGui::TextDisabled("file not found under Data/models");
		}
	}
	ImGui::TextDisabled("proxy wireframe — real mesh load pending tex-dict integration");
}

} // namespace Debug

#endif // DEBUG_IMGUI
