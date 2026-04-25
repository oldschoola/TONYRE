// Editor-snapshot I/O — each F2 panel exports/imports its own tiny slice
// as a separate file under userdata/editor/. Files are intentionally
// panel-scoped (not one combined file) so panels don't need to see each
// other's data and we avoid a JSON merge pass.
//
// Peds + goals are tiny hand-rolled JSON; rails ship as the raw compressed
// buffer the engine already maintains (no point base64-wrapping it).
//
// Writes are atomic via temp-file + rename so an interrupted export can't
// leave a truncated snapshot.
#pragma once

#if defined(DEBUG_IMGUI)

#include <cstddef>
#include <cstdint>

namespace Debug::EditorPersistence
{

struct PedRecord
{
	float	pos[3];
};

struct GoalRecord
{
	uint32_t	id;
	uint32_t	type_crc;
	int			score;
	int			time_limit;
	float		pos[3];
	char		name[48];
};

bool WriteNpcs(const PedRecord *records, int count);
bool ReadNpcs(PedRecord *records, int cap, int *count);
bool DiscardNpcs();
bool NpcsSnapshotExists();

bool WriteGoals(const GoalRecord *records, int count);
bool ReadGoals(GoalRecord *records, int cap, int *count);
bool DiscardGoals();
bool GoalsSnapshotExists();

bool WriteRails(const unsigned char *buf, std::size_t size);
bool ReadRails(unsigned char *buf, std::size_t cap, std::size_t *size);
bool DiscardRails();
bool RailsSnapshotExists();

// -- Phase 12 stubs (real bodies land in Phases 14/15/17). -------------------
// Mission / object / model-cache I/O takes opaque JSON bodies rather than
// typed structs: mission payloads are too variable (description text,
// variable-length waypoint arrays, per-type goal params) to pin down a C
// shape that wouldn't immediately need extending. Panels own their schema
// and hand a ready-built string to the writer; the reader returns the raw
// bytes on the other side.

// Missions are filed per-id. id must be a short [A-Za-z0-9_] string — no
// path separators; treated as the filename stem.
bool WriteMission(const char *id, const char *body, std::size_t size);
bool ReadMission(const char *id, char *buf, std::size_t cap, std::size_t *size);
bool DiscardMission(const char *id);
bool MissionExists(const char *id);

// Fills out_ids with zero-terminated strings. Returns the number written,
// or -1 on error. Caller sizes out_ids[N][cap_per_id].
int ListMissions(char *out_ids, int max_count, std::size_t cap_per_id);

// Objects snapshot is bucketed by level_id (typically the active level's
// checksum rendered as hex). One file per level so switching levels doesn't
// need a merge pass.
bool WriteObjects(const char *level_id, const char *body, std::size_t size);
bool ReadObjects(const char *level_id, char *buf, std::size_t cap, std::size_t *size);
bool DiscardObjects(const char *level_id);
bool ObjectsSnapshotExists(const char *level_id);

// Model-cache is a single global file: the ObjectsPanel's filesystem scan
// of Game/Data/models. Rescanned on-demand; not per-level.
bool WriteModelCache(const char *body, std::size_t size);
bool ReadModelCache(char *buf, std::size_t cap, std::size_t *size);
bool ModelCacheExists();

} // namespace Debug::EditorPersistence

#endif // DEBUG_IMGUI
