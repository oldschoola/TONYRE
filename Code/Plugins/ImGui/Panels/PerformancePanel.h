// Performance telemetry panel — 600-frame ring buffer of total frametime
// plus per-segment checkpoints (script/render/swap/audio/objects). Empirical
// spike detection: histogram + p50/p90/p99/p999/min/max stats, and a "spike
// rows" view that flags any frame > median * 1.3.
//
// Public static API: BeginFrame() at start of frame, RecordCheckpoint(name)
// at each segment boundary, EndFrame() before swap. All probes are zero-cost
// when the panel is hidden (`s_enabled` short-circuit).
#pragma once

#if defined(DEBUG_IMGUI)

#include <cstdint>

#include "../IDebugPanel.h"

namespace Debug
{

class PerformancePanel : public IDebugPanel
{
public:
	const char *	GetName() const override	{ return "Performance"; }
	PanelGroup		GetGroup() const override	{ return PanelGroup::Overlay; }
	void			Draw() override;

	// Frame-boundary probes. Safe to call before Init / when panel hidden —
	// `s_enabled` keeps the cost to a single bool load + branch.
	static void		BeginFrame();
	static void		EndFrame();
	static void		RecordCheckpoint(const char *name);

	// Latency probes. Engine code calls these from input/swap/wait sites so
	// the panel can report input → swap, GPU pre-render wait, and pacing slack
	// per frame. All cheap loads + branches when `s_enabled` is false.
	static void		RecordInputSampled();
	static void		RecordSwapIssued();
	static void		RecordGpuWaitForLastFrame(uint64_t ticks_waited);
	static void		RecordPacingWait(uint64_t ticks_waited);

	// Tuning toggles. Engine code reads via these getters once per frame; the
	// panel writes via the checkboxes/combo. Defaults preserve historical
	// behaviour (60 Hz cap on, 1-frame fence on, vsync off).
	static bool		IsFramerateUncapped()		{ return s_uncap_fps; }
	static bool		IsPrerenderQueueLimited()	{ return s_limit_prerender_queue; }
	static int		GetSwapInterval()			{ return s_swap_interval; }
	// Edge-triggered: returns true exactly once after the user changes the combo.
	static bool		ConsumePendingSwapInterval();

	// Per-task timing — call sites push when a single task->vCall takes >2ms.
	// `code_ptr` is the task's code function pointer for offline symbol lookup.
	static void		RecordTaskSpike(const void *code_ptr, float ms);

	// Tied to panel visibility so the engine pays nothing while hidden.
	static void		SetEnabled(bool e)	{ s_enabled = e; }
	static bool		IsEnabled()			{ return s_enabled; }

	static constexpr int kRingSize = 600;
	static constexpr int kMaxSegments = 8;
	static constexpr int kTaskSpikeRingSize = 64;
	static constexpr float kTaskSpikeThresholdMs = 2.0f;

private:
	struct FrameSample
	{
		float	total_ms;
		float	segment_ms[kMaxSegments];
		float	input_to_swap_ms;	// from RecordInputSampled to RecordSwapIssued
		float	gpu_wait_ms;		// next-frame fence wait, retroactively written here
		float	pacing_wait_ms;		// time inside WaitForNextFrame after swap
	};

	// Latency-row identifiers for ComputeStats. Negative range chosen so it
	// can't collide with a real segment index (0..kMaxSegments-1).
	static constexpr int kStatTotal			= -1;
	static constexpr int kStatInputToSwap	= -2;
	static constexpr int kStatGpuWait		= -3;
	static constexpr int kStatPacingWait	= -4;
	static constexpr int kStatEstLag		= -5;	// synthetic = input_to_swap + gpu_wait

	struct SegmentDef
	{
		const char *	name;
		uint64_t		start_ticks;	// SDL_GetPerformanceCounter at last checkpoint
	};

	static int		ResolveSegmentIndex(const char *name);
	static void		ComputeStats(int seg_idx, float &p50, float &p90, float &p99, float &p999, float &fmin, float &fmax);
	static void		GatherTotals(float *out, int max_count, int &count);

	void			DrawHistogram();
	void			DrawStatsTable();
	void			DrawLatencyTable();
	void			DrawSpikeRows();
	void			DrawTaskSpikes();
	void			DrawTuning();

	struct TaskSpike
	{
		const void *	code_ptr;
		float			ms;
		uint32_t		frame;
	};

	static bool			s_enabled;
	static FrameSample	s_ring[kRingSize];
	static int			s_ring_head;		// next write index
	static int			s_ring_count;		// 0..kRingSize
	static SegmentDef	s_segments[kMaxSegments];
	static int			s_num_segments;
	static uint64_t		s_frame_start_ticks;
	static uint64_t		s_last_checkpoint_ticks;
	static double		s_ticks_to_ms;		// 1000.0 / SDL_GetPerformanceFrequency()
	static TaskSpike	s_task_ring[kTaskSpikeRingSize];
	static int			s_task_ring_head;
	static int			s_task_ring_count;
	static uint32_t		s_frame_index;		// monotonic frame counter, used to tag spikes

	// Latency probe scratch state.
	static uint64_t		s_input_sample_ticks;	// SDL_GetPerformanceCounter at RecordInputSampled, 0 if not seen this frame

	// Tuning toggles (panel UI writes; engine reads via getters).
	static bool			s_uncap_fps;
	static bool			s_limit_prerender_queue;
	static int			s_swap_interval;		// 0 off, 1 vsync on, -1 adaptive
	static bool			s_swap_interval_pending;	// edge flag consumed by engine

	bool			m_show_spike_rows = false;
	bool			m_show_task_spikes = true;
};

} // namespace Debug

#endif // DEBUG_IMGUI
