// Compile-time gate for scattered diagnostic file-I/O writes.
//
// The ported shadow / particle path accumulated many ad-hoc
// `fopen("shadow_diag.log", "a")` and `fopen("frame_trace.log", "a")`
// call sites during debugging. Left on, they stream syscalls from the
// render thread and cost measurable frame time (observed: 429MB of
// trace output in 20 minutes of gameplay, matching a 60->58 FPS dip).
//
// Every call site already uses the `FILE *f = ...; if (f) { ... }`
// idiom, so funnelling the opens through these helpers and returning
// nullptr when the gate is off silences the whole mechanism with no
// other source changes. Define TONYRE_FRAME_DIAG to re-enable.
#pragma once

#include <cstdio>

namespace FrameDiag
{

inline std::FILE *OpenShadow()
{
#ifdef TONYRE_FRAME_DIAG
	return std::fopen( "shadow_diag.log", "a" );
#else
	return nullptr;
#endif
}

inline std::FILE *OpenFrameTrace()
{
#ifdef TONYRE_FRAME_DIAG
	return std::fopen( "frame_trace.log", "a" );
#else
	return nullptr;
#endif
}

} // namespace FrameDiag
