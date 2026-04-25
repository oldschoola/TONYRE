#pragma once

#include "nx_init.h"

namespace NxWn32
{
	class sShader
	{
		public:
			// Shader program and shaders
			GLuint program;

			// Cached uniform locations. Resolved once after link in the
			// constructor; -1 if the shader does not declare that uniform,
			// which glUniform* silently ignores. Moves per-frame
			// glGetUniformLocation calls (measurably hot — mesh.cpp::Submit
			// was resolving ~10 names per draw) into a one-time cost.
			GLint loc_u_m              = -1;
			GLint loc_u_v              = -1;
			GLint loc_u_p              = -1;
			GLint loc_u_col            = -1;
			GLint loc_u_tex_proj       = -1;
			GLint loc_u_shadow_enabled = -1;
			GLint loc_u_shadow_origin  = -1;
			GLint loc_u_shadow_fade_near = -1;
			GLint loc_u_shadow_fade_far  = -1;
			GLint loc_u_shadow_tex     = -1;

		public:
			sShader(const char *vertex, const char *fragment);
			~sShader();

		private:
			void ResolveCachedUniforms();
	};

	sShader *DirectShader();
	sShader *SpriteShader();
	sShader *BasicShader();
	sShader *BonedShader();
	sShader *ParticleShader();
	sShader *ShadowCasterShader();
} // namespace NxWn32
