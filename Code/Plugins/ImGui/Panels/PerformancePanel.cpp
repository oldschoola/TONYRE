#include "PerformancePanel.h"

#if defined(DEBUG_IMGUI)

#include <algorithm>
#include <cstring>
#include <cstdio>

#include <SDL.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <dbghelp.h>
#pragma comment(lib, "Dbghelp.lib")
#endif

#include "../imgui.h"

namespace Debug
{

bool						PerformancePanel::s_enabled = false;
PerformancePanel::FrameSample PerformancePanel::s_ring[PerformancePanel::kRingSize] = {};
int							PerformancePanel::s_ring_head = 0;
int							PerformancePanel::s_ring_count = 0;
PerformancePanel::SegmentDef PerformancePanel::s_segments[PerformancePanel::kMaxSegments] = {};
int							PerformancePanel::s_num_segments = 0;
uint64_t					PerformancePanel::s_frame_start_ticks = 0;
uint64_t					PerformancePanel::s_last_checkpoint_ticks = 0;
double						PerformancePanel::s_ticks_to_ms = 0.0;
PerformancePanel::TaskSpike	PerformancePanel::s_task_ring[PerformancePanel::kTaskSpikeRingSize] = {};
int							PerformancePanel::s_task_ring_head = 0;
int							PerformancePanel::s_task_ring_count = 0;
uint32_t					PerformancePanel::s_frame_index = 0;
uint64_t					PerformancePanel::s_input_sample_ticks = 0;
bool						PerformancePanel::s_uncap_fps = false;
bool						PerformancePanel::s_limit_prerender_queue = true;
int							PerformancePanel::s_swap_interval = 0;	// matches existing SDL_GL_SetSwapInterval(0)
bool						PerformancePanel::s_swap_interval_pending = false;

int PerformancePanel::ResolveSegmentIndex(const char *name)
{
	if (name == nullptr)
		return -1;

	for (int i = 0; i < s_num_segments; ++i)
	{
		if (s_segments[i].name != nullptr && std::strcmp(s_segments[i].name, name) == 0)
			return i;
	}

	if (s_num_segments >= kMaxSegments)
		return -1;

	int idx = s_num_segments++;
	s_segments[idx].name = name;	// caller passes string literal — no copy needed
	s_segments[idx].start_ticks = 0;
	return idx;
}

void PerformancePanel::BeginFrame()
{
	if (!s_enabled)
		return;

	if (s_ticks_to_ms == 0.0)
	{
		const uint64_t freq = SDL_GetPerformanceFrequency();
		s_ticks_to_ms = (freq > 0) ? (1000.0 / static_cast<double>(freq)) : 0.0;
	}

	s_frame_start_ticks = SDL_GetPerformanceCounter();
	s_last_checkpoint_ticks = s_frame_start_ticks;

	// Reset per-frame segment accumulators in the next slot we'll write to.
	FrameSample &slot = s_ring[s_ring_head];
	slot.total_ms = 0.0f;
	for (int i = 0; i < kMaxSegments; ++i)
		slot.segment_ms[i] = 0.0f;
	slot.input_to_swap_ms = 0.0f;
	slot.gpu_wait_ms = 0.0f;
	slot.pacing_wait_ms = 0.0f;

	// Cleared each frame so RecordSwapIssued never reads a stale timestamp
	// from a frame where input was sampled but the panel was disabled mid-frame.
	s_input_sample_ticks = 0;
}

void PerformancePanel::RecordInputSampled()
{
	if (!s_enabled || s_frame_start_ticks == 0 || s_ticks_to_ms == 0.0)
		return;
	s_input_sample_ticks = SDL_GetPerformanceCounter();
}

void PerformancePanel::RecordSwapIssued()
{
	if (!s_enabled || s_input_sample_ticks == 0 || s_ticks_to_ms == 0.0)
		return;

	const uint64_t now = SDL_GetPerformanceCounter();
	const uint64_t delta = now - s_input_sample_ticks;
	s_ring[s_ring_head].input_to_swap_ms =
		static_cast<float>(static_cast<double>(delta) * s_ticks_to_ms);
}

void PerformancePanel::RecordGpuWaitForLastFrame(uint64_t ticks_waited)
{
	if (!s_enabled || s_ticks_to_ms == 0.0 || s_ring_count == 0)
		return;

	// EndFrame already advanced s_ring_head past the previous frame's slot —
	// step back one to write the wait we just observed for that frame.
	const int last_slot = (s_ring_head - 1 + kRingSize) % kRingSize;
	s_ring[last_slot].gpu_wait_ms =
		static_cast<float>(static_cast<double>(ticks_waited) * s_ticks_to_ms);
}

void PerformancePanel::RecordPacingWait(uint64_t ticks_waited)
{
	if (!s_enabled || s_ticks_to_ms == 0.0 || s_ring_count == 0)
		return;

	// Pacing happens after EndFrame, same retroactive write as gpu_wait.
	const int last_slot = (s_ring_head - 1 + kRingSize) % kRingSize;
	s_ring[last_slot].pacing_wait_ms =
		static_cast<float>(static_cast<double>(ticks_waited) * s_ticks_to_ms);
}

bool PerformancePanel::ConsumePendingSwapInterval()
{
	if (!s_swap_interval_pending)
		return false;
	s_swap_interval_pending = false;
	return true;
}

void PerformancePanel::RecordCheckpoint(const char *name)
{
	if (!s_enabled || s_frame_start_ticks == 0)
		return;

	const int seg_idx = ResolveSegmentIndex(name);
	if (seg_idx < 0)
		return;

	const uint64_t now = SDL_GetPerformanceCounter();
	const uint64_t delta = now - s_last_checkpoint_ticks;
	s_last_checkpoint_ticks = now;

	const float ms = static_cast<float>(static_cast<double>(delta) * s_ticks_to_ms);
	s_ring[s_ring_head].segment_ms[seg_idx] += ms;
}

void PerformancePanel::EndFrame()
{
	if (!s_enabled || s_frame_start_ticks == 0)
		return;

	const uint64_t now = SDL_GetPerformanceCounter();
	const uint64_t total = now - s_frame_start_ticks;
	s_ring[s_ring_head].total_ms = static_cast<float>(static_cast<double>(total) * s_ticks_to_ms);

	s_ring_head = (s_ring_head + 1) % kRingSize;
	if (s_ring_count < kRingSize)
		++s_ring_count;

	s_frame_start_ticks = 0;
	++s_frame_index;
}

namespace
{
#if defined(_WIN32)
	bool g_sym_initialised = false;

	void EnsureSymInit()
	{
		if (g_sym_initialised) return;
		HANDLE proc = GetCurrentProcess();
		SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);
		if (SymInitialize(proc, nullptr, TRUE))
			g_sym_initialised = true;
	}

	bool ResolveSymbol(const void *addr, char *out, size_t out_size)
	{
		EnsureSymInit();
		if (!g_sym_initialised || addr == nullptr || out_size == 0)
			return false;

		alignas(SYMBOL_INFO) char buffer[sizeof(SYMBOL_INFO) + 256] = {};
		SYMBOL_INFO *info = reinterpret_cast<SYMBOL_INFO *>(buffer);
		info->SizeOfStruct = sizeof(SYMBOL_INFO);
		info->MaxNameLen = 256;

		DWORD64 displacement = 0;
		if (SymFromAddr(GetCurrentProcess(), reinterpret_cast<DWORD64>(addr), &displacement, info))
		{
			std::snprintf(out, out_size, "%s+0x%llx", info->Name, static_cast<unsigned long long>(displacement));
			return true;
		}
		return false;
	}
#else
	bool ResolveSymbol(const void *, char *, size_t) { return false; }
#endif

	FILE *g_perf_task_log = nullptr;

	void AppendTaskSpikeToLog(uint32_t frame, float ms, const void *addr, const char *sym)
	{
		if (g_perf_task_log == nullptr)
		{
			g_perf_task_log = std::fopen("perf_tasks.log", "w");
			if (g_perf_task_log != nullptr)
			{
				std::fprintf(g_perf_task_log, "# frame ms addr symbol\n");
				std::fflush(g_perf_task_log);
			}
		}
		if (g_perf_task_log == nullptr)
			return;

		std::fprintf(g_perf_task_log, "%u\t%.2f\t0x%p\t%s\n", frame, ms, addr, sym ? sym : "(unresolved)");
		std::fflush(g_perf_task_log);
	}
}

void PerformancePanel::RecordTaskSpike(const void *code_ptr, float ms)
{
	if (!s_enabled || ms < kTaskSpikeThresholdMs)
		return;

	TaskSpike &slot = s_task_ring[s_task_ring_head];
	slot.code_ptr = code_ptr;
	slot.ms = ms;
	slot.frame = s_frame_index;

	s_task_ring_head = (s_task_ring_head + 1) % kTaskSpikeRingSize;
	if (s_task_ring_count < kTaskSpikeRingSize)
		++s_task_ring_count;

	char symbuf[320];
	const char *sym = ResolveSymbol(code_ptr, symbuf, sizeof(symbuf)) ? symbuf : nullptr;
	AppendTaskSpikeToLog(s_frame_index, ms, code_ptr, sym);
}

void PerformancePanel::GatherTotals(float *out, int max_count, int &count)
{
	count = std::min(s_ring_count, max_count);
	for (int i = 0; i < count; ++i)
	{
		int idx = (s_ring_head - count + i + kRingSize) % kRingSize;
		out[i] = s_ring[idx].total_ms;
	}
}

namespace
{
	float Percentile(float *sorted, int n, float pct)
	{
		if (n <= 0) return 0.0f;
		int idx = static_cast<int>(pct * (n - 1));
		idx = std::clamp(idx, 0, n - 1);
		return sorted[idx];
	}
}

void PerformancePanel::ComputeStats(int seg_idx, float &p50, float &p90, float &p99, float &p999, float &fmin, float &fmax)
{
	float buf[kRingSize];
	int n = std::min(s_ring_count, kRingSize);
	for (int i = 0; i < n; ++i)
	{
		const int idx = (s_ring_head - n + i + kRingSize) % kRingSize;
		const FrameSample &s = s_ring[idx];
		float v = 0.0f;
		if (seg_idx >= 0)						v = s.segment_ms[seg_idx];
		else if (seg_idx == kStatTotal)			v = s.total_ms;
		else if (seg_idx == kStatInputToSwap)	v = s.input_to_swap_ms;
		else if (seg_idx == kStatGpuWait)		v = s.gpu_wait_ms;
		else if (seg_idx == kStatPacingWait)	v = s.pacing_wait_ms;
		else if (seg_idx == kStatEstLag)		v = s.input_to_swap_ms + s.gpu_wait_ms;
		buf[i] = v;
	}

	if (n <= 0)
	{
		p50 = p90 = p99 = p999 = fmin = fmax = 0.0f;
		return;
	}

	std::sort(buf, buf + n);
	fmin = buf[0];
	fmax = buf[n - 1];
	p50  = Percentile(buf, n, 0.50f);
	p90  = Percentile(buf, n, 0.90f);
	p99  = Percentile(buf, n, 0.99f);
	p999 = Percentile(buf, n, 0.999f);
}

void PerformancePanel::DrawHistogram()
{
	float totals[kRingSize];
	int count = 0;
	GatherTotals(totals, kRingSize, count);

	float scale_max = 33.4f;	// double the 16.7ms target so spikes are obvious
	for (int i = 0; i < count; ++i)
	{
		if (totals[i] > scale_max)
			scale_max = totals[i];
	}

	ImGui::PlotHistogram("##frametime", totals, count, 0, "frame ms", 0.0f, scale_max, ImVec2(0, 80));
}

void PerformancePanel::DrawStatsTable()
{
	static const ImGuiTableFlags table_flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit;

	if (!ImGui::BeginTable("perf_stats", 7, table_flags))
		return;

	ImGui::TableSetupColumn("segment");
	ImGui::TableSetupColumn("p50");
	ImGui::TableSetupColumn("p90");
	ImGui::TableSetupColumn("p99");
	ImGui::TableSetupColumn("p99.9");
	ImGui::TableSetupColumn("min");
	ImGui::TableSetupColumn("max");
	ImGui::TableHeadersRow();

	auto draw_row = [](const char *name, int seg_idx) {
		float p50, p90, p99, p999, fmin, fmax;
		ComputeStats(seg_idx, p50, p90, p99, p999, fmin, fmax);
		ImGui::TableNextRow();
		ImGui::TableNextColumn(); ImGui::TextUnformatted(name);
		ImGui::TableNextColumn(); ImGui::Text("%.2f", p50);
		ImGui::TableNextColumn(); ImGui::Text("%.2f", p90);
		ImGui::TableNextColumn(); ImGui::Text("%.2f", p99);
		ImGui::TableNextColumn(); ImGui::Text("%.2f", p999);
		ImGui::TableNextColumn(); ImGui::Text("%.2f", fmin);
		ImGui::TableNextColumn(); ImGui::Text("%.2f", fmax);
	};

	draw_row("total", kStatTotal);
	for (int i = 0; i < s_num_segments; ++i)
		draw_row(s_segments[i].name, i);

	ImGui::EndTable();
}

void PerformancePanel::DrawSpikeRows()
{
	float p50, p90, p99, p999, fmin, fmax;
	ComputeStats(kStatTotal, p50, p90, p99, p999, fmin, fmax);
	const float threshold = p50 * 1.3f;

	ImGui::Text("median = %.2f ms · spike threshold (median * 1.3) = %.2f ms", p50, threshold);

	// 2 fixed (frame, total) + per-segment + 3 latency columns (in→swap, gpu_wait, pacing).
	const int columns = 2 + s_num_segments + 3;
	if (!ImGui::BeginTable("perf_spikes", columns, ImGuiTableFlags_Borders | ImGuiTableFlags_ScrollY, ImVec2(0, 200)))
		return;

	ImGui::TableSetupColumn("frame");
	ImGui::TableSetupColumn("total");
	for (int i = 0; i < s_num_segments; ++i)
		ImGui::TableSetupColumn(s_segments[i].name);
	ImGui::TableSetupColumn("in→swap");
	ImGui::TableSetupColumn("gpu_wait");
	ImGui::TableSetupColumn("pacing");
	ImGui::TableHeadersRow();

	int n = std::min(s_ring_count, kRingSize);
	for (int i = 0; i < n; ++i)
	{
		int idx = (s_ring_head - n + i + kRingSize) % kRingSize;
		const FrameSample &s = s_ring[idx];
		if (s.total_ms < threshold)
			continue;

		ImGui::TableNextRow();
		ImGui::TableNextColumn(); ImGui::Text("%d", i);
		ImGui::TableNextColumn(); ImGui::Text("%.2f", s.total_ms);
		for (int j = 0; j < s_num_segments; ++j)
		{
			ImGui::TableNextColumn();
			ImGui::Text("%.2f", s.segment_ms[j]);
		}
		ImGui::TableNextColumn(); ImGui::Text("%.2f", s.input_to_swap_ms);
		ImGui::TableNextColumn(); ImGui::Text("%.2f", s.gpu_wait_ms);
		ImGui::TableNextColumn(); ImGui::Text("%.2f", s.pacing_wait_ms);
	}

	ImGui::EndTable();
}

void PerformancePanel::DrawLatencyTable()
{
	static const ImGuiTableFlags table_flags = ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingFixedFit;

	if (!ImGui::BeginTable("perf_latency", 7, table_flags))
		return;

	ImGui::TableSetupColumn("latency");
	ImGui::TableSetupColumn("p50");
	ImGui::TableSetupColumn("p90");
	ImGui::TableSetupColumn("p99");
	ImGui::TableSetupColumn("p99.9");
	ImGui::TableSetupColumn("min");
	ImGui::TableSetupColumn("max");
	ImGui::TableHeadersRow();

	auto draw_row = [](const char *name, int stat_idx) {
		float p50, p90, p99, p999, fmin, fmax;
		ComputeStats(stat_idx, p50, p90, p99, p999, fmin, fmax);
		ImGui::TableNextRow();
		ImGui::TableNextColumn(); ImGui::TextUnformatted(name);
		ImGui::TableNextColumn(); ImGui::Text("%.2f", p50);
		ImGui::TableNextColumn(); ImGui::Text("%.2f", p90);
		ImGui::TableNextColumn(); ImGui::Text("%.2f", p99);
		ImGui::TableNextColumn(); ImGui::Text("%.2f", p999);
		ImGui::TableNextColumn(); ImGui::Text("%.2f", fmin);
		ImGui::TableNextColumn(); ImGui::Text("%.2f", fmax);
	};

	draw_row("input → swap",   kStatInputToSwap);
	draw_row("gpu wait",       kStatGpuWait);
	draw_row("pacing wait",    kStatPacingWait);
	draw_row("est. cpu+gpu lag", kStatEstLag);

	ImGui::EndTable();

	ImGui::TextDisabled("input → swap: time from HID poll to SDL_GL_SwapWindow.");
	ImGui::TextDisabled("gpu wait: time blocked on previous frame's fence (only with pre-render limiter on).");
	ImGui::TextDisabled("pacing wait: idle time inside the 60 Hz limiter (drops to ~0 when uncapped).");
	ImGui::TextDisabled("est. lag = input→swap + gpu wait, excludes display refresh latency.");
}

void PerformancePanel::DrawTuning()
{
	ImGui::Checkbox("Uncap framerate", &s_uncap_fps);
	if (ImGui::IsItemHovered())
		ImGui::SetTooltip("Render uncapped. Game logic still ticks at fixed 60 Hz\nso scripts and animation run at original speed.");
	ImGui::SameLine();
	ImGui::Checkbox("Limit pre-render queue (1-frame fence)", &s_limit_prerender_queue);

	static const char *kVsyncLabels[] = { "Off", "On", "Adaptive" };
	static const int   kVsyncValues[] = { 0, 1, -1 };
	int current = 0;
	for (int i = 0; i < 3; ++i)
		if (kVsyncValues[i] == s_swap_interval) { current = i; break; }
	ImGui::SetNextItemWidth(120.0f);
	if (ImGui::Combo("VSync", &current, kVsyncLabels, IM_ARRAYSIZE(kVsyncLabels)))
	{
		const int desired = kVsyncValues[current];
		if (desired != s_swap_interval)
		{
			s_swap_interval = desired;
			s_swap_interval_pending = true;
		}
	}

	// Live FPS readout from p50 of total. Useful instant feedback when the
	// user toggles the cap — they see the number move before reading the table.
	float p50, p90, p99, p999, fmin, fmax;
	ComputeStats(kStatTotal, p50, p90, p99, p999, fmin, fmax);
	const float fps = (p50 > 0.0f) ? (1000.0f / p50) : 0.0f;
	ImGui::Text("FPS (1 / p50 total): %.1f   |   p50 frame: %.2f ms", fps, p50);
	if (s_uncap_fps)
		ImGui::TextDisabled("Game logic ticks at fixed 60 Hz; rendering runs uncapped.");
}

void PerformancePanel::Draw()
{
	// Tie probe enable to panel open state. Panel hidden ⇒ probes no-op.
	s_enabled = m_open;

	ImGui::SetNextWindowPos(ImVec2(20.0f, 160.0f), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(620.0f, 500.0f), ImGuiCond_FirstUseEver);

	if (!ImGui::Begin("Performance", &m_open))
	{
		ImGui::End();
		return;
	}

	ImGui::Text("samples: %d / %d", s_ring_count, kRingSize);
	ImGui::SameLine();
	if (ImGui::SmallButton("clear"))
	{
		s_ring_head = 0;
		s_ring_count = 0;
		std::memset(s_ring, 0, sizeof(s_ring));
	}

	ImGui::Separator();
	DrawTuning();

	ImGui::Separator();
	DrawHistogram();

	ImGui::Separator();
	DrawStatsTable();

	ImGui::Separator();
	DrawLatencyTable();
	ImGui::Separator();
	ImGui::Checkbox("show frame spike rows", &m_show_spike_rows);
	if (m_show_spike_rows)
		DrawSpikeRows();

	ImGui::Separator();
	ImGui::Checkbox("show slow tasks", &m_show_task_spikes);
	if (m_show_task_spikes)
		DrawTaskSpikes();

	ImGui::End();
}

void PerformancePanel::DrawTaskSpikes()
{
	ImGui::Text("tasks > %.1f ms (latest %d, ring %d)", kTaskSpikeThresholdMs, s_task_ring_count, kTaskSpikeRingSize);

	if (!ImGui::BeginTable("perf_task_spikes", 4, ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY | ImGuiTableFlags_Resizable, ImVec2(0, 220)))
		return;

	ImGui::TableSetupColumn("frame", ImGuiTableColumnFlags_WidthFixed, 60.0f);
	ImGui::TableSetupColumn("ms", ImGuiTableColumnFlags_WidthFixed, 60.0f);
	ImGui::TableSetupColumn("symbol", ImGuiTableColumnFlags_WidthStretch);
	ImGui::TableSetupColumn("addr", ImGuiTableColumnFlags_WidthFixed, 140.0f);
	ImGui::TableHeadersRow();

	char symbuf[320];
	int n = std::min(s_task_ring_count, kTaskSpikeRingSize);
	for (int i = 0; i < n; ++i)
	{
		int idx = (s_task_ring_head - n + i + kTaskSpikeRingSize) % kTaskSpikeRingSize;
		const TaskSpike &t = s_task_ring[idx];

		const char *sym = "(unresolved)";
		if (ResolveSymbol(t.code_ptr, symbuf, sizeof(symbuf)))
			sym = symbuf;

		ImGui::TableNextRow();
		ImGui::TableNextColumn(); ImGui::Text("%u", t.frame);
		ImGui::TableNextColumn(); ImGui::Text("%.2f", t.ms);
		ImGui::TableNextColumn(); ImGui::TextUnformatted(sym);
		ImGui::TableNextColumn(); ImGui::Text("0x%p", t.code_ptr);
	}

	ImGui::EndTable();
}

} // namespace Debug

#endif // DEBUG_IMGUI
