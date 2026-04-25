#include "EditorPersistence.h"

#if defined(DEBUG_IMGUI)

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace Debug::EditorPersistence
{

namespace
{

// Snapshots live next to the game exe so they travel with a manual copy.
// Hard-coded relative path; process cwd at game start is already Game/.
const char *	kSubDir		= "userdata/editor";
const char *	kNpcsFile	= "userdata/editor/npcs.json";
const char *	kGoalsFile	= "userdata/editor/goals.json";
const char *	kRailsFile	= "userdata/editor/rails.bin";

bool EnsureDir()
{
	std::error_code ec;
	std::filesystem::create_directories(kSubDir, ec);
	return !ec;
}

bool AtomicWrite(const char *final_path, const void *data, std::size_t size)
{
	if (!EnsureDir())
		return false;

	std::string tmp_path = final_path;
	tmp_path += ".tmp";

	// Write to tmp, rename into place. Rename is atomic on Windows too when
	// source and dest are on the same volume (always true here).
	{
		std::ofstream out(tmp_path, std::ios::binary | std::ios::trunc);
		if (!out.is_open())
			return false;
		out.write(static_cast<const char *>(data), static_cast<std::streamsize>(size));
		if (!out.good())
			return false;
	}

	std::error_code ec;
	std::filesystem::rename(tmp_path, final_path, ec);
	if (ec)
	{
		// Fall back to copy+remove if rename couldn't overwrite an existing
		// file on this platform.
		std::filesystem::copy_file(tmp_path, final_path,
			std::filesystem::copy_options::overwrite_existing, ec);
		std::filesystem::remove(tmp_path);
		if (ec)
			return false;
	}
	return true;
}

bool ReadAll(const char *path, std::vector<char> &out)
{
	std::ifstream in(path, std::ios::binary);
	if (!in.is_open())
		return false;
	in.seekg(0, std::ios::end);
	const std::streampos size = in.tellg();
	if (size <= 0)
	{
		out.clear();
		return true;
	}
	in.seekg(0, std::ios::beg);
	out.resize(static_cast<size_t>(size));
	in.read(out.data(), size);
	return static_cast<std::size_t>(in.gcount()) == out.size();
}

void EscapeJsonString(const char *in, char *out, size_t out_size)
{
	// Tiny escaper — the only characters we emit in strings are goal-name
	// identifiers, which in practice are alnum + underscore. We still guard
	// the obvious JSON syntax characters (", \, control) so a hostile name
	// can't break the writer.
	size_t w = 0;
	for (size_t i = 0; in[i] != '\0' && w + 2 < out_size; ++i)
	{
		unsigned char c = static_cast<unsigned char>(in[i]);
		if (c == '"' || c == '\\')
		{
			if (w + 3 >= out_size) break;
			out[w++] = '\\';
			out[w++] = static_cast<char>(c);
		}
		else if (c < 0x20)
		{
			// Drop control chars.
		}
		else
		{
			out[w++] = static_cast<char>(c);
		}
	}
	out[w] = '\0';
}

// Minimal line-oriented JSON reader. Each record is expected to be on its
// own "{...}" line — matches the layout the writer below emits. Returns
// the number of records parsed. cb is called once per record with the
// in-place body (everything between '{' and '}') and a scratch buffer.
template <typename Cb>
int ForEachObjectLine(const std::vector<char> &buf, Cb &&cb)
{
	int count = 0;
	size_t i = 0;
	while (i < buf.size())
	{
		// find '{'
		while (i < buf.size() && buf[i] != '{') ++i;
		if (i >= buf.size()) break;
		size_t open_pos = i + 1;
		// find matching '}' (no nesting — records are flat)
		size_t close_pos = open_pos;
		while (close_pos < buf.size() && buf[close_pos] != '}') ++close_pos;
		if (close_pos >= buf.size()) break;
		std::string body(buf.data() + open_pos, close_pos - open_pos);
		if (cb(body))
			++count;
		i = close_pos + 1;
	}
	return count;
}

bool FindKey(const std::string &body, const char *key, std::string &out)
{
	// Returns the raw substring that is the value of "key", stopping at the
	// next comma or end-of-body. Works for numbers, strings, and arrays since
	// we only emit flat types.
	std::string needle = "\"";
	needle += key;
	needle += "\":";
	size_t pos = body.find(needle);
	if (pos == std::string::npos)
		return false;
	pos += needle.size();
	// skip whitespace
	while (pos < body.size() && (body[pos] == ' ' || body[pos] == '\t')) ++pos;
	if (pos >= body.size()) return false;

	if (body[pos] == '"')
	{
		size_t end = body.find('"', pos + 1);
		if (end == std::string::npos) return false;
		out = body.substr(pos + 1, end - pos - 1);
		return true;
	}
	if (body[pos] == '[')
	{
		size_t end = body.find(']', pos + 1);
		if (end == std::string::npos) return false;
		out = body.substr(pos, end - pos + 1);
		return true;
	}
	// plain scalar — to next comma or end
	size_t end = pos;
	while (end < body.size() && body[end] != ',') ++end;
	out = body.substr(pos, end - pos);
	// trim trailing whitespace
	while (!out.empty() && (out.back() == ' ' || out.back() == '\t' || out.back() == '\n' || out.back() == '\r'))
		out.pop_back();
	return true;
}

bool ParseVec3(const std::string &raw, float out[3])
{
	// Expects "[x, y, z]".
	const char *s = raw.c_str();
	while (*s == ' ' || *s == '[') ++s;
	char *end = nullptr;
	for (int i = 0; i < 3; ++i)
	{
		out[i] = std::strtof(s, &end);
		if (s == end) return false;
		s = end;
		while (*s == ' ' || *s == ',') ++s;
	}
	return true;
}

} // anon namespace

// ---------------------------------------------------------------- NPCs

bool WriteNpcs(const PedRecord *records, int count)
{
	std::string body;
	body += "{\n  \"version\": 1,\n  \"npcs\": [\n";
	for (int i = 0; i < count; ++i)
	{
		char line[128];
		std::snprintf(line, sizeof(line),
			"    { \"pos\": [%.3f, %.3f, %.3f] }%s\n",
			records[i].pos[0], records[i].pos[1], records[i].pos[2],
			(i + 1 < count) ? "," : "");
		body += line;
	}
	body += "  ]\n}\n";
	return AtomicWrite(kNpcsFile, body.data(), body.size());
}

bool ReadNpcs(PedRecord *records, int cap, int *count)
{
	if (count) *count = 0;
	std::vector<char> buf;
	if (!ReadAll(kNpcsFile, buf) || buf.empty()) return false;

	int parsed = 0;
	ForEachObjectLine(buf, [&](const std::string &body) -> bool {
		if (parsed >= cap) return false;
		std::string pos_raw;
		if (!FindKey(body, "pos", pos_raw)) return false;
		if (!ParseVec3(pos_raw, records[parsed].pos)) return false;
		++parsed;
		return true;
	});
	if (count) *count = parsed;
	return parsed > 0;
}

bool DiscardNpcs()
{
	std::error_code ec;
	std::filesystem::remove(kNpcsFile, ec);
	return !ec;
}

bool NpcsSnapshotExists()
{
	std::error_code ec;
	return std::filesystem::exists(kNpcsFile, ec);
}

// ---------------------------------------------------------------- Goals

bool WriteGoals(const GoalRecord *records, int count)
{
	std::string body;
	body += "{\n  \"version\": 1,\n  \"goals\": [\n";
	for (int i = 0; i < count; ++i)
	{
		char name_esc[64];
		EscapeJsonString(records[i].name, name_esc, sizeof(name_esc));

		char line[320];
		std::snprintf(line, sizeof(line),
			"    { \"id\": %u, \"type_crc\": %u, \"name\": \"%s\", \"score\": %d, \"time_limit\": %d, \"pos\": [%.3f, %.3f, %.3f] }%s\n",
			records[i].id,
			records[i].type_crc,
			name_esc,
			records[i].score,
			records[i].time_limit,
			records[i].pos[0], records[i].pos[1], records[i].pos[2],
			(i + 1 < count) ? "," : "");
		body += line;
	}
	body += "  ]\n}\n";
	return AtomicWrite(kGoalsFile, body.data(), body.size());
}

bool ReadGoals(GoalRecord *records, int cap, int *count)
{
	if (count) *count = 0;
	std::vector<char> buf;
	if (!ReadAll(kGoalsFile, buf) || buf.empty()) return false;

	int parsed = 0;
	ForEachObjectLine(buf, [&](const std::string &body) -> bool {
		if (parsed >= cap) return false;
		GoalRecord &r = records[parsed];
		std::memset(&r, 0, sizeof(r));

		std::string v;
		if (!FindKey(body, "id", v)) return false;
		r.id = static_cast<uint32_t>(std::strtoul(v.c_str(), nullptr, 10));
		if (!FindKey(body, "type_crc", v)) return false;
		r.type_crc = static_cast<uint32_t>(std::strtoul(v.c_str(), nullptr, 10));
		if (FindKey(body, "name", v))
			std::snprintf(r.name, sizeof(r.name), "%s", v.c_str());
		if (FindKey(body, "score", v))
			r.score = std::atoi(v.c_str());
		if (FindKey(body, "time_limit", v))
			r.time_limit = std::atoi(v.c_str());
		if (FindKey(body, "pos", v))
			ParseVec3(v, r.pos);

		++parsed;
		return true;
	});
	if (count) *count = parsed;
	return parsed > 0;
}

bool DiscardGoals()
{
	std::error_code ec;
	std::filesystem::remove(kGoalsFile, ec);
	return !ec;
}

bool GoalsSnapshotExists()
{
	std::error_code ec;
	return std::filesystem::exists(kGoalsFile, ec);
}

// ---------------------------------------------------------------- Rails

bool WriteRails(const unsigned char *buf, std::size_t size)
{
	return AtomicWrite(kRailsFile, buf, size);
}

bool ReadRails(unsigned char *buf, std::size_t cap, std::size_t *size)
{
	if (size) *size = 0;
	std::vector<char> raw;
	if (!ReadAll(kRailsFile, raw) || raw.empty()) return false;
	const std::size_t n = (raw.size() < cap) ? raw.size() : cap;
	std::memcpy(buf, raw.data(), n);
	if (size) *size = n;
	return n > 0;
}

bool DiscardRails()
{
	std::error_code ec;
	std::filesystem::remove(kRailsFile, ec);
	return !ec;
}

bool RailsSnapshotExists()
{
	std::error_code ec;
	return std::filesystem::exists(kRailsFile, ec);
}

// ---------------------------------------------------------------- Objects / Missions / ModelCache

namespace
{

bool IsSafeIdChar(char c)
{
	// Restrict mission/level ids to a strict filename-safe alphabet so the
	// id can't escape userdata/editor/. We use this because mission ids come
	// from user input in the editor UI.
	return (c >= 'a' && c <= 'z') ||
		   (c >= 'A' && c <= 'Z') ||
		   (c >= '0' && c <= '9') ||
		   c == '_' || c == '-';
}

bool SanitisedIdPath(const char *dir, const char *id, const char *ext,
	std::string &out)
{
	if (id == nullptr || id[0] == '\0')
		return false;
	std::string safe;
	for (const char *p = id; *p != '\0'; ++p)
	{
		if (!IsSafeIdChar(*p))
			return false;
		safe.push_back(*p);
	}
	out = dir;
	out += "/";
	out += safe;
	out += ext;
	return true;
}

bool WriteTextBlob(const char *path, const char *body, std::size_t size)
{
	if (body == nullptr)
		return false;
	return AtomicWrite(path, body, size);
}

bool ReadTextBlob(const char *path, char *buf, std::size_t cap, std::size_t *size)
{
	if (size) *size = 0;
	if (buf == nullptr || cap == 0)
		return false;
	std::vector<char> raw;
	if (!ReadAll(path, raw) || raw.empty())
		return false;
	const std::size_t n = (raw.size() < cap - 1) ? raw.size() : cap - 1;
	std::memcpy(buf, raw.data(), n);
	buf[n] = '\0';
	if (size) *size = n;
	return n > 0;
}

} // anon namespace

const char *	kMissionsDir		= "userdata/editor/missions";
const char *	kObjectsDir			= "userdata/editor/objects";
const char *	kModelCacheFile		= "userdata/editor/model_cache.json";

bool WriteMission(const char *id, const char *body, std::size_t size)
{
	std::string path;
	if (!SanitisedIdPath(kMissionsDir, id, ".json", path))
		return false;
	std::error_code ec;
	std::filesystem::create_directories(kMissionsDir, ec);
	return WriteTextBlob(path.c_str(), body, size);
}

bool ReadMission(const char *id, char *buf, std::size_t cap, std::size_t *size)
{
	if (size) *size = 0;
	std::string path;
	if (!SanitisedIdPath(kMissionsDir, id, ".json", path))
		return false;
	return ReadTextBlob(path.c_str(), buf, cap, size);
}

bool DiscardMission(const char *id)
{
	std::string path;
	if (!SanitisedIdPath(kMissionsDir, id, ".json", path))
		return false;
	std::error_code ec;
	std::filesystem::remove(path, ec);
	return !ec;
}

bool MissionExists(const char *id)
{
	std::string path;
	if (!SanitisedIdPath(kMissionsDir, id, ".json", path))
		return false;
	std::error_code ec;
	return std::filesystem::exists(path, ec);
}

int ListMissions(char *out_ids, int max_count, std::size_t cap_per_id)
{
	if (out_ids == nullptr || max_count <= 0 || cap_per_id == 0)
		return 0;

	std::error_code ec;
	if (!std::filesystem::exists(kMissionsDir, ec))
		return 0;

	int written = 0;
	for (const auto &entry : std::filesystem::directory_iterator(kMissionsDir, ec))
	{
		if (ec) break;
		if (!entry.is_regular_file())
			continue;
		const auto path = entry.path();
		if (path.extension() != ".json")
			continue;

		const std::string stem = path.stem().string();
		if (stem.size() >= cap_per_id)
			continue;

		char *dest = out_ids + (static_cast<std::size_t>(written) * cap_per_id);
		std::memcpy(dest, stem.data(), stem.size());
		dest[stem.size()] = '\0';
		++written;
		if (written >= max_count)
			break;
	}
	return written;
}

bool WriteObjects(const char *level_id, const char *body, std::size_t size)
{
	std::string path;
	if (!SanitisedIdPath(kObjectsDir, level_id, ".json", path))
		return false;
	std::error_code ec;
	std::filesystem::create_directories(kObjectsDir, ec);
	return WriteTextBlob(path.c_str(), body, size);
}

bool ReadObjects(const char *level_id, char *buf, std::size_t cap, std::size_t *size)
{
	if (size) *size = 0;
	std::string path;
	if (!SanitisedIdPath(kObjectsDir, level_id, ".json", path))
		return false;
	return ReadTextBlob(path.c_str(), buf, cap, size);
}

bool DiscardObjects(const char *level_id)
{
	std::string path;
	if (!SanitisedIdPath(kObjectsDir, level_id, ".json", path))
		return false;
	std::error_code ec;
	std::filesystem::remove(path, ec);
	return !ec;
}

bool ObjectsSnapshotExists(const char *level_id)
{
	std::string path;
	if (!SanitisedIdPath(kObjectsDir, level_id, ".json", path))
		return false;
	std::error_code ec;
	return std::filesystem::exists(path, ec);
}

bool WriteModelCache(const char *body, std::size_t size)
{
	return WriteTextBlob(kModelCacheFile, body, size);
}

bool ReadModelCache(char *buf, std::size_t cap, std::size_t *size)
{
	return ReadTextBlob(kModelCacheFile, buf, cap, size);
}

bool ModelCacheExists()
{
	std::error_code ec;
	return std::filesystem::exists(kModelCacheFile, ec);
}

} // namespace Debug::EditorPersistence

#endif // DEBUG_IMGUI
