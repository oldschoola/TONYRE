/*****************************************************************************
**																			**
**			              Neversoft Entertainment							**
**																		   	**
**				   Copyright (C) 1999 - All Rights Reserved				   	**
**																			**
******************************************************************************
**																			**
**	Project:		Core Library											**
**																			**
**	Module:			Task Manager (TSK)										**
**																			**
**	File name:		tlist.cpp												**
**																			**
**	Created by:		05/27/99	-	mjb										**
**																			**
**	Description:	Task List Support										**
**																			**
*****************************************************************************/

/*****************************************************************************
**							  	  Includes									**
*****************************************************************************/

#include <Core/Defines.h>
#include <Core/Task.h>

#include <Sys/Profiler.h>			// Including for debugging
#include <Sys/timer.h>				// Including for debugging

#if defined(DEBUG_IMGUI)
#include <SDL.h>
#include "Plugins/ImGui/Panels/PerformancePanel.h"
#endif

/*****************************************************************************
**							  DBG Information								**
*****************************************************************************/

namespace Tsk
{



/*****************************************************************************
**								   Externals								**
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


/*****************************************************************************
**								 Public Data								**
*****************************************************************************/

List*	TSK_BkGndTasks = nullptr;

/*****************************************************************************
**							  Private Prototypes							**
*****************************************************************************/


/*****************************************************************************
**							   Private Functions							**
*****************************************************************************/



/*****************************************************************************
**							   Public Functions								**
*****************************************************************************/

List::List( void )
: stamp( 0 ), list_changed( false )
{
	
	
}

/******************************************************************/
/*                                                                */
/*                                                                */
/******************************************************************/

List::~List ( void )
{
	Dbg_MsgAssert( list.IsEmpty(),( "Task List Not Empty" ));
}

/******************************************************************/
/*                                                                */
/*                                                                */
/******************************************************************/

void		List::Process( bool time, uint mask )
{
	(void)time;

	if ( !HaveAccess ())
	{
		return;
	}

	stamp++;
	
	do
	{
		Lst::Search<BaseTask>	iterator;
		BaseTask*				next = iterator.FirstItem( list );

		list_changed = false; 
	
		while ( next )
		{
			BaseTask*	task = next;

			Dbg_AssertType(task, BaseTask);
			Dbg_AssertPtr(task);
			Dbg_AssertPtr(task->node);
			Dbg_MsgAssert(task->node->InList(),( "Task was removed from list by previous task" ));
			
			next = iterator.NextItem();		// get next task before we execute the current 	
											// one as the task might remove itself
			
			Dbg_MsgAssert(!list_changed,( "list changed, you fucker"));

			if ( task->HaveAccess() )
			{
				if ( task->stamp != stamp )
				{
					task->stamp = stamp;
					
#ifdef		__USE_PROFILER__			
					Tmr::MicroSeconds start_time=0;
#ifdef __PLAT_NGPS__
					Tmr::MicroSeconds end_time=0;
#endif
					if (time)
					{
						start_time =  Tmr::GetTimeInUSeconds();
					}
#endif
					// only call the task if it is not masked off
					if (! (task->GetMask() & mask))
					{
#if defined(DEBUG_IMGUI)
						const bool perf_enabled = Debug::PerformancePanel::IsEnabled();
						uint64_t task_t0 = perf_enabled ? SDL_GetPerformanceCounter() : 0;
						task->vCall();
						if (perf_enabled)
						{
							uint64_t task_t1 = SDL_GetPerformanceCounter();
							const double freq = static_cast<double>(SDL_GetPerformanceFrequency());
							const float ms = (freq > 0.0)
								? static_cast<float>(static_cast<double>(task_t1 - task_t0) * 1000.0 / freq)
								: 0.0f;
							Debug::PerformancePanel::RecordTaskSpike(task->GetCode(), ms);
						}
#else
						task->vCall();
#endif
					}
#ifdef		__USE_PROFILER__			
#ifdef __PLAT_NGPS__
					if (time)
					{
						end_time = Tmr::GetTimeInUSeconds();
						
						int length = (int)(end_time - start_time);
						int	size;
						printf ("%6d %s\n",length,MemView_GetFunctionName((int)task->GetCode(), &size));
					}
#endif
#endif				
				}
			}

			if ( list_changed )
			{
				break;
			}
		}

	} while ( list_changed );

}


/******************************************************************/
// Mick Debugging: Dump out the contents of the task list
// 
/******************************************************************/

void		List::Dump( void )
{
	

	Lst::Search<BaseTask>	iterator;
	BaseTask*				next = iterator.FirstItem( list );

	list_changed = false; 

	while ( next )
	{
		BaseTask*	task = next;

		Dbg_AssertType( task, BaseTask );
		Dbg_AssertPtr( task );
		Dbg_AssertPtr( task->node );
		
		next = iterator.NextItem();		// get next task before we execute the current 	
										// one as the task might remove itself
#ifdef __PLAT_NGPS__
		if ( task->HaveAccess() )
		{
//				char *MemView_GetFunctionName(int pc, int *p_size);				
//				task->vCall();
				int size;
				printf ("Task %p %s\n",task->GetCode(),
				MemView_GetFunctionName((int)task->GetCode(), &size));
		}
#elif defined( __PLAT_NGC__ )
		if ( task->HaveAccess() )
		{
//				char *MemView_GetFunctionName(int pc, int *p_size);				
//				task->vCall();
				Dbg_Printf ("Task %p\n",task->GetCode() );
//				MemView_GetFunctionName((int)task->GetCode(), &size));
		}
#endif
	}
}


/******************************************************************/
/*                                                                */
/*                                                                */
/******************************************************************/

void		List::AddTask( BaseTask& task )
{
	Dbg_MsgAssert( !task.node->InList(),( "Task is already in a list" ));

	Forbid ();			// stop any processing while list is modified
	
	list.AddNode( task.node );
	task.tlist = this;
	task.stamp = stamp - 1;
	list_changed = true;

	Permit();
}

/******************************************************************/
/*                                                                */
/*                                                                */
/******************************************************************/

void		List::RemoveAllTasks( void )
{
	

	Lst::Search<BaseTask>	iterator;
	BaseTask*				next;

	Forbid();
	
	while (( next = iterator.FirstItem( list ) ) != nullptr)
	{
		next->Remove();
		list_changed = true;
	}
		
	Permit();

}

/******************************************************************/
/*                                                                */
/*                                                                */
/******************************************************************/

void		List::SignalListChange( void )
{
	list_changed = true;
}

/******************************************************************/
/*                                                                */
/*                                                                */
/******************************************************************/
} // namespace Tsk

