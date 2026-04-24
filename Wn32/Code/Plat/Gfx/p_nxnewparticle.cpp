/*****************************************************************************
**																			**
**			              Neversoft Entertainment.			                **
**																		   	**
**				   Copyright (C) 2000 - All Rights Reserved				   	**
**																			**
******************************************************************************
**																			**
**	Project:		Skate5													**
**																			**
**	Module:			Gfx			 											**
**																			**
**	File name:		p_NxNewParticle.cpp										**
**																			**
**	Created by:		3/25/03	-	SPG											**
**																			**
**	Description:	Xbox new parametric particle system						**
*****************************************************************************/

#include <Core/Defines.h>

#include "p_nxnewparticle.h"

#include <Gfx/NxTexMan.h>
#include "p_nxtexture.h"
#include "nx/nx_init.h"
#include "nx/render.h"
#include "nx/shader.h"
#include <glm/gtc/type_ptr.hpp>
#include <vector>


/*****************************************************************************
**								DBG Information								**
*****************************************************************************/

namespace Nx
{


/*****************************************************************************
**								  Externals									**
*****************************************************************************/

/*****************************************************************************
**								   Defines									**
*****************************************************************************/

/*****************************************************************************
**								Private Types								**
*****************************************************************************/

/*****************************************************************************
**								 Private Data								**
*****************************************************************************/

static int rand_seed;
static int rand_a	= 314159265;
static int rand_b	= 178453311;


/*****************************************************************************
**								 Public Data								**
*****************************************************************************/

/*****************************************************************************
**							  Private Prototypes							**
*****************************************************************************/

/*****************************************************************************
**							  Private Functions								**
*****************************************************************************/



/******************************************************************/
/*                                                                */
/*                                                                */
/******************************************************************/
static void seed_particle_rnd( int s, int a, int b )
{
	rand_seed		= s;
	rand_a			= a;
	rand_b			= b;
}



/******************************************************************/
/*                                                                */
/*                                                                */
/******************************************************************/
static inline int particle_rnd( int n )
{
	rand_seed	= rand_seed * rand_a + rand_b;
	rand_a		= ( rand_a ^ rand_seed ) + ( rand_seed >> 4 );
	rand_b		+= ( rand_seed >> 3 ) - 0x10101010L;
	return (int)(( rand_seed & 0xffff ) * n ) >> 16;
}



/******************************************************************/
/*                                                                */
/*                                                                */
/******************************************************************/
void CParticleStream::AdvanceSeed( int num_places )
{
	// Seed the random number generator back to the current seed.
	seed_particle_rnd( m_rand_seed, m_rand_a, m_rand_b );

	// Each particle will call the random function four times.
	for( int i = 0; i < ( num_places * 4 ); i++ )
	{
		rand_seed	= rand_seed * rand_a + rand_b;
		rand_a		= ( rand_a ^ rand_seed ) + ( rand_seed >> 4 );
		rand_b		+= ( rand_seed >> 3 ) - 0x10101010L;
	}

	m_rand_seed = rand_seed;
	m_rand_a	= rand_a;
	m_rand_b	= rand_b;
}

/******************************************************************/
/*                                                                */
/*                                                                */
/******************************************************************/
void CXboxNewParticle::plat_render( void )
{
	#define PART_TRACE(tag) do { \
		FILE *_f = fopen("frame_trace.log", "a"); \
		if (_f) { fprintf(_f, "part:%s this=%p\n", tag, (void*)this); fclose(_f); } \
	} while(0)

	PART_TRACE("enter");

	// Stream bookkeeping (births/deaths/ages). Ported from Xbox path, no draw calls yet.
	CParticleStream* p_stream;
	int i;

	if( m_params.m_EmitRate && ( !m_emitting || ( m_params.m_EmitRate != mp_newest_stream->m_rate )))
	{
		if( m_num_streams < m_max_streams )
		{
			m_num_streams++;
			mp_newest_stream++;
			if( mp_newest_stream == mp_stream + m_max_streams )
				mp_newest_stream = mp_stream;

			mp_newest_stream->m_rate			= m_params.m_EmitRate;
			mp_newest_stream->m_interval		= 1.0f / m_params.m_EmitRate;
			mp_newest_stream->m_oldest_age		= 0.0f;
			mp_newest_stream->m_num_particles	= 0;
			mp_newest_stream->m_rand_seed		= rand();
			mp_newest_stream->m_rand_a			= 314159265;
			mp_newest_stream->m_rand_b			= 178453311;
			m_emitting = true;
		}
		else
		{
			m_emitting = false;
		}
	}
	else
	{
		m_emitting = (m_params.m_EmitRate != 0.0f);
	}

	if( !m_num_streams )
		return;

	// Age streams.
	for( i = 0, p_stream = mp_oldest_stream; i < m_num_streams; ++i )
	{
		p_stream->m_oldest_age += 1.0f / 60.0f;
		p_stream++;
		if( p_stream == mp_stream + m_max_streams )
			p_stream = mp_stream;
	}

	if( m_emitting )
	{
		mp_newest_stream->m_num_particles = (int)( mp_newest_stream->m_oldest_age * mp_newest_stream->m_rate + 1.0f );
	}

	if( mp_oldest_stream->m_oldest_age > m_params.m_Lifetime )
	{
		int particles_dead = (int)(( mp_oldest_stream->m_oldest_age - m_params.m_Lifetime ) * mp_oldest_stream->m_rate + 1.0f );
		mp_oldest_stream->m_num_particles -= particles_dead;

		if( mp_oldest_stream->m_num_particles > 0 || ( m_num_streams == 1 && m_emitting ))
		{
			mp_oldest_stream->m_oldest_age -= (float)particles_dead * mp_oldest_stream->m_interval;
			mp_oldest_stream->AdvanceSeed( particles_dead );
		}
		else
		{
			m_num_streams--;
			mp_oldest_stream++;
			if( mp_oldest_stream == mp_stream + m_max_streams )
				mp_oldest_stream = mp_stream;
			if( !m_num_streams )
				return;
		}
	}

	// Build CPU-side billboard quads per alive particle and submit.
	if( m_vao == 0 || m_vbo == 0 )
		return;

	// Camera-space right/up for billboarding.
	glm::vec3 cam_right = NxWn32::EngineGlobals.cam_right;
	glm::vec3 cam_up    = NxWn32::EngineGlobals.cam_up;

	struct Vert { float px, py, pz; float u, v; float r, g, b, a; };
	std::vector< Vert > verts;

	// Emitter origin — local-coord systems use m_RotMatrix origin; world-coord use m_BoxPos[0].
	Mth::Vector emitter_pos = m_params.m_BoxPos[vBOX_START];
	if( m_params.m_LocalCoord )
	{
		emitter_pos = m_params.m_RotMatrix.GetPos();
	}

	p_stream = mp_oldest_stream;
	for( int s = 0; s < m_num_streams; ++s )
	{
		int num = p_stream->m_num_particles;
		if( num > 0 )
		{
			float stream_age = p_stream->m_oldest_age;
			for( int pi = 0; pi < num; ++pi )
			{
				float age = stream_age - pi * p_stream->m_interval;
				if( age < 0.0f || age > m_params.m_Lifetime )
					continue;

				float t = age / m_params.m_Lifetime;

				// Position = emitter + p0 + p1*t + p2*t²  (average path, ignoring per-particle rand jitter).
				Mth::Vector pos = emitter_pos
					+ m_p0
					+ m_p1 * age
					+ m_p2 * (age * age * 0.5f);

				// Radius = s0 + s1*t + s2*t² (approx).
				float radius = m_s0[3] + m_s1[3] * age + m_s2[3] * (age * age * 0.5f);
				if( radius < 0.5f ) radius = 0.5f;

				// Color interpolation between box colors over lifetime.
				Image::RGBA c;
				if( m_params.m_UseMidcolor )
				{
					float mid = m_params.m_ColorMidpointPct * 0.01f;
					if( t < mid )
					{
						float k = (mid > 0.0f) ? (t / mid) : 0.0f;
						c.r = (uint8)(m_params.m_Color[0].r * (1.0f - k) + m_params.m_Color[1].r * k);
						c.g = (uint8)(m_params.m_Color[0].g * (1.0f - k) + m_params.m_Color[1].g * k);
						c.b = (uint8)(m_params.m_Color[0].b * (1.0f - k) + m_params.m_Color[1].b * k);
						c.a = (uint8)(m_params.m_Color[0].a * (1.0f - k) + m_params.m_Color[1].a * k);
					}
					else
					{
						float k = (1.0f - mid > 0.0f) ? ((t - mid) / (1.0f - mid)) : 0.0f;
						c.r = (uint8)(m_params.m_Color[1].r * (1.0f - k) + m_params.m_Color[2].r * k);
						c.g = (uint8)(m_params.m_Color[1].g * (1.0f - k) + m_params.m_Color[2].g * k);
						c.b = (uint8)(m_params.m_Color[1].b * (1.0f - k) + m_params.m_Color[2].b * k);
						c.a = (uint8)(m_params.m_Color[1].a * (1.0f - k) + m_params.m_Color[2].a * k);
					}
				}
				else
				{
					float k = t;
					c.r = (uint8)(m_params.m_Color[0].r * (1.0f - k) + m_params.m_Color[2].r * k);
					c.g = (uint8)(m_params.m_Color[0].g * (1.0f - k) + m_params.m_Color[2].g * k);
					c.b = (uint8)(m_params.m_Color[0].b * (1.0f - k) + m_params.m_Color[2].b * k);
					c.a = (uint8)(m_params.m_Color[0].a * (1.0f - k) + m_params.m_Color[2].a * k);
				}

				float fr = c.r / 255.0f;
				float fg = c.g / 255.0f;
				float fb = c.b / 255.0f;
				float fa = c.a / 255.0f;

				glm::vec3 p( pos[X], pos[Y], pos[Z] );
				glm::vec3 r = cam_right * radius;
				glm::vec3 u = cam_up * radius;

				glm::vec3 v0 = p - r - u;
				glm::vec3 v1 = p + r - u;
				glm::vec3 v2 = p + r + u;
				glm::vec3 v3 = p - r + u;

				verts.push_back({ v0.x, v0.y, v0.z, 0.0f, 1.0f, fr, fg, fb, fa });
				verts.push_back({ v1.x, v1.y, v1.z, 1.0f, 1.0f, fr, fg, fb, fa });
				verts.push_back({ v2.x, v2.y, v2.z, 1.0f, 0.0f, fr, fg, fb, fa });
				verts.push_back({ v0.x, v0.y, v0.z, 0.0f, 1.0f, fr, fg, fb, fa });
				verts.push_back({ v2.x, v2.y, v2.z, 1.0f, 0.0f, fr, fg, fb, fa });
				verts.push_back({ v3.x, v3.y, v3.z, 0.0f, 0.0f, fr, fg, fb, fa });
			}
		}
		p_stream++;
		if( p_stream == mp_stream + m_max_streams )
			p_stream = mp_stream;
	}

	if( verts.empty() )
	{
		PART_TRACE("exit-empty");
		return;
	}

	PART_TRACE("pre-draw");

	// Upload, bind state, draw.
	NxWn32::sShader *shader = NxWn32::ParticleShader();
	glUseProgram( shader->program );

	glUniformMatrix4fv( glGetUniformLocation( shader->program, "u_view" ), 1, GL_FALSE, glm::value_ptr( NxWn32::EngineGlobals.view_matrix ));
	glUniformMatrix4fv( glGetUniformLocation( shader->program, "u_proj" ), 1, GL_FALSE, glm::value_ptr( NxWn32::EngineGlobals.projection_matrix ));

	glActiveTexture( GL_TEXTURE0 );
	glBindTexture( GL_TEXTURE_2D, mp_texture ? mp_texture->GLTexture : 0 );
	glUniform1i( glGetUniformLocation( shader->program, "u_texture" ), 0 );

	glBindVertexArray( m_vao );
	glBindBuffer( GL_ARRAY_BUFFER, m_vbo );
	glBufferData( GL_ARRAY_BUFFER, verts.size() * sizeof( Vert ), verts.data(), GL_DYNAMIC_DRAW );

	glEnable( GL_BLEND );
	glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
	glDepthMask( GL_FALSE );
	glDisable( GL_CULL_FACE );

	glDrawArrays( GL_TRIANGLES, 0, (GLsizei)verts.size() );

	PART_TRACE("post-draw");

	// Restore GL state fully so downstream HUD/sprite rendering isn't corrupted.
	// NOTE: engine default is GL_CULL_FACE DISABLED (nothing else in the codebase toggles it).
	// Enabling it here was culling HUD sprites. Leave it disabled.
	glDepthMask( GL_TRUE );
	glDisable( GL_CULL_FACE );
	glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
	glBindTexture( GL_TEXTURE_2D, 0 );
	glBindVertexArray( 0 );
	glBindBuffer( GL_ARRAY_BUFFER, 0 );
	glUseProgram( 0 );

	PART_TRACE("exit");
	#undef PART_TRACE
}




/******************************************************************/
/*                                                                */
/*                                                                */
/******************************************************************/
void CXboxNewParticle::update_position( void )
{
	float t1 = m_params.m_Lifetime * m_params.m_MidpointPct * 0.01f;
	float t2 = m_params.m_Lifetime;
	Mth::Vector u, a_;

	Mth::Vector x0	= m_params.m_BoxPos[0];
	x0[3]			= m_params.m_Radius[0];
	Mth::Vector x1	= m_params.m_BoxPos[1];
	x1[3]			= m_params.m_Radius[1];
	Mth::Vector x2	= m_params.m_BoxPos[2];
	x2[3]			= m_params.m_Radius[2];

	if( m_params.m_UseMidpoint && t1 > 0.0f && (t2 - t1) > 0.0f )
	{
		u  = ( t2 * t2 * ( x1 - x0 ) - t1 * t1 * ( x2 - x0 )) / ( t1 * t2 * ( t2 - t1 ));
		a_ = ( t1 * ( x2 - x0 ) - t2 * ( x1 - x0 )) / ( t1 * t2 * ( t2 - t1 ));
	}
	else if( t2 > 0.0f )
	{
		u  = ( x2 - x0 ) / t2;
		a_.Set( 0, 0, 0, 0 );
	}
	else
	{
		u.Set( 0, 0, 0, 0 );
		a_.Set( 0, 0, 0, 0 );
	}

	m_p0 = x0 - 1.5f * m_s0;
	m_p1 = u  - 1.5f * m_s1;
	m_p2 = a_ - 1.5f * m_s2;
	m_p0[3] = x0[3] - 1.5f * m_s0[3];
	m_p1[3] = u[3]  - 1.5f * m_s1[3];
	m_p2[3] = a_[3] - 1.5f * m_s2[3];
}



/******************************************************************/
/*                                                                */
/*                                                                */
/******************************************************************/
void CXboxNewParticle::plat_update( void )
{
	if( m_params.m_LocalCoord )
	{
		update_position();
	}
}



/******************************************************************/
/*                                                                */
/*                                                                */
/******************************************************************/
void CXboxNewParticle::plat_build( void )
{
	// Reduce emit rate selectively to improve performance.
	m_params.m_EmitRate	= m_params.m_EmitRate * 0.5f;

	// Initialise streams.
	m_max_streams		= 5;
	m_num_streams		= 0;

	mp_stream			= new CParticleStream[m_max_streams];
	mp_newest_stream	= mp_stream + m_max_streams - 1;
	mp_oldest_stream	= mp_stream;
	m_emitting			= false;

	// Resolve texture from the particle texture dictionary.
	mp_texture = nullptr;
	if( Nx::CTexDictManager::sp_particle_tex_dict )
	{
		Nx::CTexture* p_tex = Nx::CTexDictManager::sp_particle_tex_dict->GetTexture( m_params.m_Texture );
		Nx::CXboxTexture* p_xbox_tex = static_cast< Nx::CXboxTexture* >( p_tex );
		if( p_xbox_tex )
		{
			mp_texture = p_xbox_tex->GetEngineTexture();
		}
	}

	// Convert 3-point -> PVA format (size track uses BoxDims/RadiusSpread).
	float t1 = m_params.m_Lifetime * m_params.m_MidpointPct * 0.01f;
	float t2 = m_params.m_Lifetime;
	Mth::Vector x0, x1, x2, u, a_;

	x0    = m_params.m_BoxDims[0];
	x0[3] = m_params.m_RadiusSpread[0];
	x1    = m_params.m_BoxDims[1];
	x1[3] = m_params.m_RadiusSpread[1];
	x2    = m_params.m_BoxDims[2];
	x2[3] = m_params.m_RadiusSpread[2];

	if( m_params.m_UseMidpoint && t1 > 0.0f && ( t2 - t1 ) > 0.0f )
	{
		u  = ( t2 * t2 * ( x1 - x0 ) - t1 * t1 * ( x2 - x0 )) / ( t1 * t2 * ( t2 - t1 ));
		a_ = ( t1 * ( x2 - x0 ) - t2 * ( x1 - x0 )) / ( t1 * t2 * ( t2 - t1 ));
	}
	else if( t2 > 0.0f )
	{
		u  = ( x2 - x0 ) / t2;
		a_.Set( 0.0f, 0.0f, 0.0f, 0.0f );
	}
	else
	{
		u.Set( 0.0f, 0.0f, 0.0f, 0.0f );
		a_.Set( 0.0f, 0.0f, 0.0f, 0.0f );
	}

	m_s0 = x0;
	m_s1 = u;
	m_s2 = a_;

	// Position track uses BoxPos/Radius.
	update_position();

	// Rotation matrix.
	m_rotation.Identity();

	// GL resources for billboard quads.
	if( m_vao == 0 )
	{
		glGenVertexArrays( 1, &m_vao );
		glGenBuffers( 1, &m_vbo );

		glBindVertexArray( m_vao );
		glBindBuffer( GL_ARRAY_BUFFER, m_vbo );

		const GLsizei stride = sizeof( float ) * 9;

		// pos (vec3)
		glEnableVertexAttribArray( 0 );
		glVertexAttribPointer( 0, 3, GL_FLOAT, GL_FALSE, stride, (void*)0 );
		// uv (vec2)
		glEnableVertexAttribArray( 1 );
		glVertexAttribPointer( 1, 2, GL_FLOAT, GL_FALSE, stride, (void*)( sizeof( float ) * 3 ));
		// color (vec4)
		glEnableVertexAttribArray( 2 );
		glVertexAttribPointer( 2, 4, GL_FLOAT, GL_FALSE, stride, (void*)( sizeof( float ) * 5 ));

		glBindVertexArray( 0 );
		glBindBuffer( GL_ARRAY_BUFFER, 0 );
	}
}



/******************************************************************/
/*                                                                */
/*                                                                */
/******************************************************************/
void CXboxNewParticle::plat_destroy( void )
{
	if( mp_stream )
	{
		delete [] mp_stream;
		mp_stream = nullptr;
	}

	if( m_vbo )
	{
		glDeleteBuffers( 1, &m_vbo );
		m_vbo = 0;
	}
	if( m_vao )
	{
		glDeleteVertexArrays( 1, &m_vao );
		m_vao = 0;
	}

	// mp_texture is owned by the particle tex dict — do not free.
	mp_texture = nullptr;
}



/*****************************************************************************
**							  Public Functions								**
*****************************************************************************/

/******************************************************************/
/*                                                                */
/*                                                                */
/******************************************************************/

} // namespace Nx




