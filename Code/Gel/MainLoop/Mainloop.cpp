/*****************************************************************************
**																			**
**			              Neversoft Entertainment			                **
**																		   	**
**				   Copyright (C) 1999 - All Rights Reserved				   	**
**																			**
******************************************************************************
**																			**
**	Project:		GEL (Game Engine Library)								**
**																			**
**	Module:			Main Loop (ML) 											**
**																			**
**	File name:		mainloop.cpp											**
**																			**
**	Created:		05/27/99	-	mjb										**
**																			**
**	Description:	Main loop and support code								**
**																			**
*****************************************************************************/


/*****************************************************************************
**							  	  Includes									**
*****************************************************************************/

#include <Gel/mainloop.h>

#include <Core/Defines.h>
#include <Core/singleton.h>
#include <Core/Task.h>

#include <Gfx/nx.h>

#include <Sys/timer.h>

#include <Sys/Profiler.h>

#if defined(DEBUG_IMGUI)
#include "Plugins/ImGui/Panels/PerformancePanel.h"
#endif

#include <Plat/Gfx/p_memview.h>

#include <Gel/Scripting/symboltable.h>

#ifdef	__PLAT_NGPS__
    #include <Gel/Collision/BatchTriColl.h>
    #include <Gel/SoundFX/ngps/p_sfx.h>

    namespace NxPs2
    {
        void	WaitForRendering();
		void 	StuffAfterGSFinished();
    }
#endif

/*****************************************************************************
**								DBG Information								**
*****************************************************************************/

Dbg_DefineProject( GEL, "GEL Library" )

namespace Mdl
{
	void	Rail_DebugRender();			// for debugging
}

namespace Mlp
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

DefineSingletonClass( Manager, "Main Loop Manager" );

/*****************************************************************************
**								 Public Data								**
*****************************************************************************/


/*****************************************************************************
**							  Private Prototypes							**
*****************************************************************************/


/*****************************************************************************
**							   Private Functions							**
*****************************************************************************/

Manager::Manager( void )
{
	
	
	start_render_hook = nullptr;
	end_render_hook = nullptr;
	done = false;
}

/******************************************************************/
/*                                                                */
/*                                                                */
/******************************************************************/

Manager::~Manager( void )
{
	
}

/******************************************************************/
/*                                                                */
/*                                                                */
/******************************************************************/

inline void		Manager::service_system( void )
{
	

#ifndef __PLAT_WN32__
#	ifdef __USE_PROFILER__
	Sys::CPUProfiler->PushContext( 255, 0, 0 );
#	endif // __USE_PROFILER__
#endif

	system_task_stack.Process (currently_profiling);

#ifndef __PLAT_WN32__
#	ifdef __USE_PROFILER__
	Sys::CPUProfiler->PopContext();
#	endif // __USE_PROFILER__
#endif
}

/******************************************************************/
/*                                                                */
/*                                                                */
/******************************************************************/

inline void		Manager::game_logic( void )
{
	
	
#ifndef __PLAT_WN32__
#	ifdef __USE_PROFILER__
	Sys::CPUProfiler->PushContext( 0, 255, 0 );
#	endif // __USE_PROFILER__
#endif

//	printf ("\nTiming Logic\n"); 
	logic_task_stack.Process(currently_profiling);

//	printf ("\nDumping Task List\n\n");	
//	logic_task_stack.Dump();

#ifndef __PLAT_WN32__
#	ifdef __USE_PROFILER__
	Sys::CPUProfiler->PopContext();
#	endif // __USE_PROFILER__
#endif
}

/******************************************************************/
/*                                                                */
/*                                                                */
/******************************************************************/

inline	void		Manager::render_frame( void )
{
	
	
//	printf ("############################## render_frame #############################\n");	
	

#ifndef __PLAT_WN32__
#	ifdef __USE_PROFILER__
	Sys::CPUProfiler->PushContext( 0, 0, 255 );
#	endif // __USE_PROFILER__
#endif

#	ifdef __PLAT_NGC__
	if( start_render_hook )
	{
		start_render_hook->Call();
	}
	if( !display_tasks_paused )
	{
		display_task_stack.Process( currently_profiling );
	}
#	else
	if (!display_tasks_paused)
	{
		// If paused don't call the start_render_hook
		// as this clears the none-visible frame buffer
		// which will result in a flash when display is unpaused
		if ( start_render_hook )			// set up for rendering
		{
			start_render_hook->Call();
		}

//		printf ("\nTiming render\n"); 
	
		display_task_stack.Process(currently_profiling);		// service rendering routines
	}
#	endif // __PLAT_NGC__
	
#ifndef __PLAT_WN32__
#	ifdef __USE_PROFILER__
	Sys::CPUProfiler->PopContext();
	Sys::CPUProfiler->PushContext( 255, 255, 0 );
#	endif // __USE_PROFILER__
#endif

	if ( end_render_hook )				// end rendering
	{
		end_render_hook->Call();
	}
#ifndef __PLAT_WN32__
#	ifdef __USE_PROFILER__
	Sys::CPUProfiler->PopContext();
#	endif // __USE_PROFILER__
#endif
}

/*****************************************************************************
**							   Public Functions								**
*****************************************************************************/

void		Manager::MainLoop( void )
{
	

	bool								old_flag = done;	// push the current done flag

	done = false;

	while ( !done )
	{
	
		if (trigger_profiling)
		{
			printf ("\nProfiling.....Start of main loop\n\n");
			trigger_profiling--;
			currently_profiling = 1;
		}
		

#ifdef	__PLAT_NGPS__		
// for profiling on the PS2, we do a bit of the code from sPreRender here
// so we can time the final waiting for GS		
#	ifdef __USE_PROFILER__
		Sys::CPUProfiler->PushContext( 0,0,255 );		// blue = wait for VU/DMA/GPU to finish
#	endif		
		NxPs2::WaitForRendering();		// PS2 Specific, wait for prior frame's DMA to finish	
		NxPs2::StuffAfterGSFinished();		// PS2 Specific, DMA altering stuff run after previous frame's DMA finished
#	ifdef __USE_PROFILER__
		Sys::CPUProfiler->PopContext(  );
		Sys::CPUProfiler->PushContext( 128,0,0 );		// red = wait for vblank
#	endif		
		int framerate = Script::GetInteger(0x3214c818/*"lock_framerate"*/);
		if (framerate)	// normal vsync
		{
			static uint64 next_vblanks= 0;
			// Still call VSync, as it updates some functions
			Tmr::VSync();
			while (Tmr::GetVblanks()<next_vblanks)
			{
				// just hanging until vblanks gets big enough
			}
			next_vblanks = Tmr::GetVblanks() + framerate;
		}
		else
		{
			Tmr::VSync1();
		}
#	ifdef __USE_PROFILER__
		Sys::CPUProfiler->PopContext(  );
#	endif		
#endif				
#ifndef	__PLAT_NGC__		
#	ifdef __USE_PROFILER__
		Sys::Profiler::sStartFrame();		  		
#	endif		
#endif		
		
		Nx::CEngine::sPreRender();			 			// start rendering previous frame's DMA list

#ifdef	__PLAT_NGPS__		
		Sfx::CSpuManager::sUpdateStatus();				// Garrett: This should go into some system task, but I'll put it here for now
#endif

//		Sys::Profiler::sStartFrame();		  		

#ifdef	__PLAT_NGC__		
#	ifdef __USE_PROFILER__
		Sys::Profiler::sStartFrame();		  		
#	endif		
#	endif		
					 
#if defined(DEBUG_IMGUI)
		const bool uncapped = Debug::PerformancePanel::IsFramerateUncapped();
#else
		const bool uncapped = false;
#endif

		// Fixed-step game logic under VFR. When the render rate is uncapped,
		// ticking service_system() + game_logic() once per render frame makes
		// everything that counts logic ticks (wait N gameframes, per-frame
		// particle spawns, frame-counted state machines) run at render rate
		// instead of the engine's 60 Hz contract. Drive logic from a wall-clock
		// accumulator and pin FrameLength() to 1/60 during drain so every
		// FrameLength-scaled component keeps its original per-tick delta.
		static bool   s_was_uncapped = false;
		static double s_logic_accum  = 0.0;
		static double s_last_wall    = 0.0;
		constexpr double kFixedStep  = 1.0 / 60.0;
		constexpr double kMaxAccum   = 5.0 / 60.0;  // anti-spiral: drop surplus after long stall

		if (uncapped)
		{
			const double now = Tmr::GetWallTimeSeconds();
			if (!s_was_uncapped)
			{
				// Cap → uncap transition: reset bookkeeping so we don't try to
				// "catch up" frames the capped path already serviced.
				s_logic_accum = 0.0;
				s_last_wall   = now;
			}
			s_logic_accum += (now - s_last_wall);
			s_last_wall    = now;
			if (s_logic_accum > kMaxAccum)
				s_logic_accum = kMaxAccum;

#if defined(DEBUG_IMGUI)
			// Stamp "service" before the drain so the "logic" segment captures wall
			// time spent on the entire drain loop (the user-meaningful number under
			// uncap). One pair of checkpoints per render frame regardless of tick
			// count — inner-loop checkpoints would double-count into kMaxSegments.
			Debug::PerformancePanel::RecordCheckpoint("service");
#endif

			Tmr::SetFixedFrameDelta(kFixedStep);
			while (s_logic_accum >= kFixedStep)
			{
				service_system();
				game_logic();
				s_logic_accum -= kFixedStep;
			}
			Tmr::ClearFixedFrameDelta();

#if defined(DEBUG_IMGUI)
			Debug::PerformancePanel::RecordCheckpoint("logic");
#endif
		}
		else
		{
			// Capped: original behaviour, bit-for-bit identical.
			service_system();

#if defined(DEBUG_IMGUI)
			Debug::PerformancePanel::RecordCheckpoint("service");
#endif

#if	defined(__PLAT_NGPS__) && defined(BATCH_TRI_COLLISION)
			// Enable VU0 collision
			bool got_vu0 = Nx::CBatchTriCollMan::sUseVU0Micro();
			Dbg_Assert(got_vu0);
#endif

 #ifdef	__PLAT_NGPS__
//		snProfSetRange( -1, (void*)0, (void*)-1);
//		snProfSetFlagValue(0x01);
 #endif

			game_logic();

#if defined(DEBUG_IMGUI)
			Debug::PerformancePanel::RecordCheckpoint("logic");
#endif

 #ifdef	__PLAT_NGPS__		
//		snProfSetRange( 4, (void*)nullptr, (void*)-1);
 #endif		


#if	defined(__PLAT_NGPS__) && defined(BATCH_TRI_COLLISION)
			// Disable VU0 collision
			if (got_vu0)
			{
				Nx::CBatchTriCollMan::sDisableVU0Micro();
			}
#endif
		}
		s_was_uncapped = uncapped;

#	ifdef __USE_PROFILER__
		Sys::CPUProfiler->PushContext( 255, 255, 0 );  // yellow = render world
#	endif		

 #ifdef	__PLAT_NGPS__		
//		snProfSetRange( -1, (void*)0, (void*)-1);
//		snProfSetFlagValue(0x01);
 #endif
		
		Nx::CEngine::sRenderWorld();		
#ifdef	__PLAT_NGC__		
#ifdef		__USE_PROFILER__
		Sys::Render_Profiler();		
#endif
#endif
		// Mick: bit of a patch here, we need some better debug hooks
		Mdl::Rail_DebugRender();
#	ifdef __USE_PROFILER__
			Sys::CPUProfiler->PushContext( 0, 0, 0 );	 	// Black (Under Yellow) = sPostRender
#	endif // __USE_PROFILER__
		Nx::CEngine::sPostRender();		  // Previous frames profiler is rendered here
#	ifdef __USE_PROFILER__
		Sys::CPUProfiler->PopContext(  );
#	endif		


	

#	ifdef __USE_PROFILER__
		Sys::CPUProfiler->PopContext(  );
#	endif		
		Tmr::OncePerRender();
 #ifdef	__PLAT_NGPS__		
//		snProfSetRange( 4, (void*)nullptr, (void*)-1);
 #endif		
		
		currently_profiling = false;

	}

	done = old_flag;
}

void Manager::ProfileTasks(int n)
{
	trigger_profiling = n;
	currently_profiling = 0;
}


/******************************************************************/
/*                                                                */
/*                                                                */
/******************************************************************/

void		Manager::QuitLoop( void )
{
	

	Dbg_Notify( "Exiting..." );

	done = true;
}

/******************************************************************/
/*                                                                */
/*                                                                */
/******************************************************************/

void		Manager::DoGameLogic( void )
{
	game_logic();
}

/******************************************************************/
/*                                                                */
/*                                                                */
/******************************************************************/

} // namespace Mlp

