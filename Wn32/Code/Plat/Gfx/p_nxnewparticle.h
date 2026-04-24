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
**	File name:		p_NxNewParticle.h										**
**																			**
**	Created by:		3/24/03	-	SPG											**
**																			**
**	Description:	Xbox implementation of new parametric particle system	**
**																			**
*****************************************************************************/

#ifndef __GFX_XBOX_P_NXNEWPARTICLE_H__
#define __GFX_XBOX_P_NXNEWPARTICLE_H__

/*****************************************************************************
**							  	  Includes									**
*****************************************************************************/

#include <Gfx/NxNewParticle.h>
#include "nx/material.h"
#include "nx/nx_init.h"
#include "nx/texture.h"

/*****************************************************************************
**								   Defines									**
*****************************************************************************/


namespace Nx
{

                        
/*****************************************************************************
**							Class Definitions								**
*****************************************************************************/

/******************************************************************/
/*                                                                */
/*                                                                */
/******************************************************************/
class CParticleStream
{
public:
	int						m_num_particles;
	float					m_rate;
	float					m_interval;
	float					m_oldest_age;
	uint32					m_rand_seed;
	uint32					m_rand_a;
	uint32					m_rand_b;
	void					AdvanceSeed( int num_places );
};



/******************************************************************/
/*                                                                */
/*                                                                */
/******************************************************************/
class CXboxNewParticle : public CNewParticle
{
	bool				m_emitting;
	int					m_max_streams;
	int					m_num_streams;
	CParticleStream*	mp_stream = nullptr;
	CParticleStream*	mp_newest_stream = nullptr;
	CParticleStream*	mp_oldest_stream = nullptr;
	Mth::Matrix 		m_rotation;
	Mth::Matrix			m_new_matrix;
	// NxXbox::sMaterial*	mp_material;

	Mth::Vector			m_s0;
	Mth::Vector			m_s1;
	Mth::Vector			m_s2;
	Mth::Vector			m_p0;
	Mth::Vector			m_p1;
	Mth::Vector			m_p2;

	// OpenGL resources for rendering billboard quads.
	GLuint				m_vao = 0;
	GLuint				m_vbo = 0;
	NxWn32::sTexture*	mp_texture = nullptr;

protected:
	void	plat_build( void );
	void	plat_destroy( void );
	void	plat_render( void );
	void	plat_update( void );
	void	update_position( void );
};



/*****************************************************************************
**							 Private Declarations							**
*****************************************************************************/

/*****************************************************************************
**							  Private Prototypes							**
*****************************************************************************/

/*****************************************************************************
**							  Public Declarations							**
*****************************************************************************/

/*****************************************************************************
**							   Public Prototypes							**
*****************************************************************************/

/*****************************************************************************
**								Inline Functions							**
*****************************************************************************/

/******************************************************************/
/*                                                                */
/*                                                                */
/******************************************************************/

} // namespace Nx

#endif	// __GFX_XBOX_P_NXNEWPARTICLE_H__


