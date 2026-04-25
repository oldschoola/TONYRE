#include "MissionPanel.h"

#if defined(DEBUG_IMGUI)

#include "imgui.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include <Core/Defines.h>
#include <Core/math.h>

#include <Gel/Scripting/struct.h>
#include <Gel/Scripting/checksum.h>
#include <Gel/Scripting/symboltable.h>
#include <Gel/Scripting/script.h>

#include <Sk/Modules/Skate/GoalManager.h>

#include "../EditorPersistence.h"
#include "WaypointsPanel.h"

namespace Debug
{

MissionPanel *MissionPanel::s_instance = nullptr;

MissionPanel::MissionPanel()
{
	s_instance = this;
}

namespace
{

// Goal type choices — the first entry is the default. "race" is the only one
// that consumes waypoints today; others ignore them on the engine side but we
// still persist them so switching a saved mission between types is lossless.
const char *	kTypeChoices[] = {
	"race",
	"score",
	"highcombo",
	"collect",
	"trickspot",
	"tour",
	"horse",
	"minigame",
};
constexpr int kNumTypeChoices = static_cast<int>(sizeof(kTypeChoices) / sizeof(kTypeChoices[0]));

bool IsSafeIdChar(char c)
{
	return (c >= 'a' && c <= 'z')
		|| (c >= 'A' && c <= 'Z')
		|| (c >= '0' && c <= '9')
		|| c == '_' || c == '-';
}

void LowerAndSanitiseId(char *id, std::size_t cap)
{
	for (std::size_t i = 0; i < cap && id[i] != '\0'; ++i)
	{
		char c = id[i];
		if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
		if (!IsSafeIdChar(c)) c = '_';
		id[i] = c;
	}
}

// Hand-rolled JSON escaper — permits description text with embedded quotes,
// newlines, and backslashes. Keeps output ASCII-safe; control chars other
// than newline/tab are dropped.
void EscapeJson(const char *in, std::string &out)
{
	for (std::size_t i = 0; in[i] != '\0'; ++i)
	{
		unsigned char c = static_cast<unsigned char>(in[i]);
		switch (c)
		{
			case '"':  out += "\\\""; break;
			case '\\': out += "\\\\"; break;
			case '\n': out += "\\n";  break;
			case '\r': out += "\\r";  break;
			case '\t': out += "\\t";  break;
			default:
				if (c >= 0x20) out += static_cast<char>(c);
				break;
		}
	}
}

void UnescapeJson(const char *in, std::size_t len, char *out, std::size_t cap)
{
	std::size_t w = 0;
	for (std::size_t i = 0; i < len && w + 1 < cap; ++i)
	{
		char c = in[i];
		if (c == '\\' && i + 1 < len)
		{
			char n = in[i + 1];
			switch (n)
			{
				case '"':  out[w++] = '"';  break;
				case '\\': out[w++] = '\\'; break;
				case 'n':  out[w++] = '\n'; break;
				case 'r':  out[w++] = '\r'; break;
				case 't':  out[w++] = '\t'; break;
				default:   out[w++] = n;    break;
			}
			++i;
		}
		else
		{
			out[w++] = c;
		}
	}
	out[w] = '\0';
}

// Extracts a JSON string value for a given key. Returns false if not found
// or malformed. Handles escapes.
bool ExtractString(const char *body, const char *key, std::string &out)
{
	std::string needle = "\"";
	needle += key;
	needle += "\":";
	const char *p = std::strstr(body, needle.c_str());
	if (p == nullptr) return false;
	p += needle.size();
	while (*p == ' ' || *p == '\t') ++p;
	if (*p != '"') return false;
	++p;
	const char *q = p;
	while (*q != '\0')
	{
		if (*q == '\\' && q[1] != '\0') q += 2;
		else if (*q == '"') break;
		else ++q;
	}
	if (*q != '"') return false;
	out.clear();
	for (const char *r = p; r < q; ++r)
	{
		if (*r == '\\' && r + 1 < q)
		{
			char n = r[1];
			switch (n)
			{
				case '"':  out += '"';  break;
				case '\\': out += '\\'; break;
				case 'n':  out += '\n'; break;
				case 'r':  out += '\r'; break;
				case 't':  out += '\t'; break;
				default:   out += n;    break;
			}
			++r;
		}
		else
		{
			out += *r;
		}
	}
	return true;
}

bool ExtractInt(const char *body, const char *key, int &out)
{
	std::string needle = "\"";
	needle += key;
	needle += "\":";
	const char *p = std::strstr(body, needle.c_str());
	if (p == nullptr) return false;
	p += needle.size();
	while (*p == ' ' || *p == '\t') ++p;
	out = static_cast<int>(std::strtol(p, nullptr, 10));
	return true;
}

} // anon namespace

uint32_t MissionPanel::GoalId() const
{
	return Script::GenerateCRC(m_form.id);
}

const char *MissionPanel::GetMissionListId(int idx) const
{
	if (idx < 0 || idx >= m_mission_count) return nullptr;
	return m_mission_ids[idx];
}

void MissionPanel::ResetToDefaults()
{
	m_form = {};
	std::snprintf(m_form.id, sizeof(m_form.id), "mission_1");
	std::snprintf(m_form.display_name, sizeof(m_form.display_name), "My Mission");
	m_form.description[0] = '\0';
	m_form.type_choice = 0;
	m_form.reward_cash = 100;
	m_form.reward_skill_points = 1;
	m_form.has_waypoints = false;
	m_form.waypoints.clear();
	m_mission_count = 0;
	m_selected_list_index = -1;
	m_confirm_revoke_open = false;
	m_confirm_discard_open = false;
	m_status_message[0] = '\0';
}

void MissionPanel::SnapshotWaypointsFromPanel()
{
	m_form.waypoints.clear();
	m_form.has_waypoints = false;

	WaypointsPanel *wp = WaypointsPanel::Get();
	if (wp == nullptr) return;

	const std::size_t count = wp->GetWaypointCount();
	for (std::size_t i = 0; i < count; ++i)
	{
		const WaypointsPanel::Waypoint *w = wp->GetWaypoint(i);
		if (w == nullptr) continue;
		m_form.waypoints.push_back(w->pos[0]);
		m_form.waypoints.push_back(w->pos[1]);
		m_form.waypoints.push_back(w->pos[2]);
	}
	m_form.has_waypoints = !m_form.waypoints.empty();
}

void MissionPanel::BuildGoalParams(Script::CStruct &out_params)
{
	const uint32_t goal_id = GoalId();
	out_params.AddChecksum(Crc::ConstCRC("name"), goal_id);
	out_params.AddChecksum(Crc::ConstCRC("type"),
		Script::GenerateCRC(kTypeChoices[m_form.type_choice]));

	// full_name — the engine's speech box uses this to stamp the speaker
	// header above the description. Passing the user's display name as text
	// lets the engine render it verbatim; localisation keys would require
	// pre-registered string tables.
	out_params.AddString(Crc::ConstCRC("full_name"), m_form.display_name);

	// goal_description — body of the speech box. Engine accepts string or
	// array of strings; single-string form covers our use.
	out_params.AddString(Crc::ConstCRC("goal_description"), m_form.description);

	// trigger_obj_id — the NPC object this goal is attached to. Optional
	// (empty string means a free-floating goal).
	if (m_form.giver_npc[0] != '\0')
	{
		out_params.AddChecksum(Crc::ConstCRC("trigger_obj_id"),
			Script::GenerateCRC(m_form.giver_npc));
	}

	out_params.AddInteger(Crc::ConstCRC("cash_reward"), m_form.reward_cash);
	out_params.AddInteger(Crc::ConstCRC("skill_points"), m_form.reward_skill_points);

	// race_waypoints attachment is deferred — engine's CRaceGoal expects
	// race_waypoints to be an array of CStructs, each carrying a `flag`
	// checksum that resolves to a flag node in the level's NodeArray.
	// NodeArray is read-only post-level-load (asserts on mutation) so we
	// can't create runtime flag nodes to back the waypoints authored in
	// our UI. Our waypoint list remains in the mission JSON for future
	// use once a runtime-friendly waypoint path exists.
}

bool MissionPanel::PublishMission()
{
	m_status_message[0] = '\0';

	if (m_form.id[0] == '\0')
	{
		std::snprintf(m_status_message, sizeof(m_status_message), "id required");
		return false;
	}

	Game::CGoalManager *mgr = Game::GetGoalManager();
	if (mgr == nullptr)
	{
		std::snprintf(m_status_message, sizeof(m_status_message),
			"GoalManager unavailable — load a level first");
		return false;
	}

	const uint32_t goal_id = GoalId();

	// Remove any prior incarnation — editing a published mission should
	// replace, not stack.
	if (mgr->GetGoal(goal_id, /*assert*/ false) != nullptr)
		mgr->RemoveGoal(goal_id);

	SnapshotWaypointsFromPanel();

	Script::CStruct params;
	BuildGoalParams(params);

	if (!mgr->AddGoal(goal_id, &params))
	{
		std::snprintf(m_status_message, sizeof(m_status_message),
			"AddGoal rejected — check logs");
		return false;
	}

	// Activate so the engine plays its own speech-box with the description.
	mgr->ActivateGoal(goal_id, /*dontAssert*/ true);
	std::snprintf(m_status_message, sizeof(m_status_message),
		"published '%s' (goal id 0x%08X)", m_form.id, goal_id);
	return true;
}

bool MissionPanel::RevokeMission()
{
	m_status_message[0] = '\0';

	Game::CGoalManager *mgr = Game::GetGoalManager();
	if (mgr == nullptr) return false;
	const uint32_t goal_id = GoalId();
	if (mgr->GetGoal(goal_id, /*assert*/ false) == nullptr)
	{
		std::snprintf(m_status_message, sizeof(m_status_message),
			"no live goal '%s' to revoke", m_form.id);
		return false;
	}
	mgr->RemoveGoal(goal_id);
	std::snprintf(m_status_message, sizeof(m_status_message),
		"revoked '%s'", m_form.id);
	return true;
}

bool MissionPanel::ExportMission()
{
	m_status_message[0] = '\0';
	if (m_form.id[0] == '\0') return false;

	// Take a fresh waypoint snapshot so the export reflects the live list.
	SnapshotWaypointsFromPanel();

	std::string body;
	body.reserve(1024);

	std::string escaped;

	body += "{\n";
	body += "  \"version\": 1,\n";

	escaped.clear(); EscapeJson(m_form.id, escaped);
	body += "  \"id\": \""; body += escaped; body += "\",\n";

	escaped.clear(); EscapeJson(m_form.display_name, escaped);
	body += "  \"display_name\": \""; body += escaped; body += "\",\n";

	escaped.clear(); EscapeJson(m_form.giver_npc, escaped);
	body += "  \"giver_npc\": \""; body += escaped; body += "\",\n";

	escaped.clear(); EscapeJson(m_form.description, escaped);
	body += "  \"description\": \""; body += escaped; body += "\",\n";

	escaped.clear(); EscapeJson(kTypeChoices[m_form.type_choice], escaped);
	body += "  \"type\": \""; body += escaped; body += "\",\n";

	char scratch[64];
	std::snprintf(scratch, sizeof(scratch), "  \"reward_cash\": %d,\n", m_form.reward_cash);
	body += scratch;
	std::snprintf(scratch, sizeof(scratch), "  \"reward_skill_points\": %d,\n", m_form.reward_skill_points);
	body += scratch;

	body += "  \"waypoints\": [";
	const int count = static_cast<int>(m_form.waypoints.size() / 3);
	for (int i = 0; i < count; ++i)
	{
		std::snprintf(scratch, sizeof(scratch),
			"%s[%.3f, %.3f, %.3f]",
			(i == 0) ? "\n    " : ",\n    ",
			m_form.waypoints[i * 3 + 0],
			m_form.waypoints[i * 3 + 1],
			m_form.waypoints[i * 3 + 2]);
		body += scratch;
	}
	body += (count > 0) ? "\n  ]\n" : "]\n";
	body += "}\n";

	const bool ok = EditorPersistence::WriteMission(m_form.id, body.data(), body.size());
	if (ok)
		std::snprintf(m_status_message, sizeof(m_status_message),
			"exported '%s'", m_form.id);
	else
		std::snprintf(m_status_message, sizeof(m_status_message),
			"export failed — see log");
	return ok;
}

bool MissionPanel::ImportMission(const char *id)
{
	m_status_message[0] = '\0';
	if (id == nullptr || id[0] == '\0') return false;

	constexpr std::size_t kCap = 8192;
	std::vector<char> buf(kCap);
	std::size_t size = 0;
	if (!EditorPersistence::ReadMission(id, buf.data(), kCap, &size))
	{
		std::snprintf(m_status_message, sizeof(m_status_message),
			"import failed — '%s' not found", id);
		return false;
	}
	buf[size < kCap ? size : kCap - 1] = '\0';
	const char *body = buf.data();

	Mission loaded = {};
	std::string tmp;

	if (ExtractString(body, "id", tmp))
		std::snprintf(loaded.id, sizeof(loaded.id), "%s", tmp.c_str());
	if (ExtractString(body, "display_name", tmp))
		std::snprintf(loaded.display_name, sizeof(loaded.display_name), "%s", tmp.c_str());
	if (ExtractString(body, "giver_npc", tmp))
		std::snprintf(loaded.giver_npc, sizeof(loaded.giver_npc), "%s", tmp.c_str());
	if (ExtractString(body, "description", tmp))
		std::snprintf(loaded.description, sizeof(loaded.description), "%s", tmp.c_str());

	loaded.type_choice = 0;
	if (ExtractString(body, "type", tmp))
	{
		for (int i = 0; i < kNumTypeChoices; ++i)
			if (tmp == kTypeChoices[i]) { loaded.type_choice = i; break; }
	}

	ExtractInt(body, "reward_cash", loaded.reward_cash);
	ExtractInt(body, "reward_skill_points", loaded.reward_skill_points);

	// Waypoints: "[\n    [x,y,z], ...]". Walk past the opening bracket,
	// pull triples until the closing bracket.
	const char *w_key = std::strstr(body, "\"waypoints\":");
	if (w_key != nullptr)
	{
		const char *p = std::strchr(w_key, '[');
		const char *end_outer = (p != nullptr) ? std::strchr(p, ']') : nullptr;
		if (p != nullptr && end_outer != nullptr)
		{
			++p; // past outer '['
			while (p < end_outer)
			{
				const char *open = std::strchr(p, '[');
				if (open == nullptr || open > end_outer) break;
				const char *close = std::strchr(open, ']');
				if (close == nullptr || close > end_outer) break;
				float v[3] = { 0.0f, 0.0f, 0.0f };
				const char *s = open + 1;
				for (int i = 0; i < 3; ++i)
				{
					while (s < close && (*s == ' ' || *s == ',' || *s == '\t' || *s == '\n'))
						++s;
					char *next = nullptr;
					v[i] = std::strtof(s, &next);
					s = (next != nullptr) ? next : s + 1;
				}
				loaded.waypoints.push_back(v[0]);
				loaded.waypoints.push_back(v[1]);
				loaded.waypoints.push_back(v[2]);
				p = close + 1;
			}
		}
	}
	loaded.has_waypoints = !loaded.waypoints.empty();

	m_form = loaded;
	std::snprintf(m_status_message, sizeof(m_status_message),
		"imported '%s'", id);
	return true;
}

bool MissionPanel::DiscardMission(const char *id)
{
	m_status_message[0] = '\0';
	if (id == nullptr || id[0] == '\0') return false;
	const bool ok = EditorPersistence::DiscardMission(id);
	if (ok)
		std::snprintf(m_status_message, sizeof(m_status_message),
			"discarded '%s'", id);
	else
		std::snprintf(m_status_message, sizeof(m_status_message),
			"discard failed for '%s'", id);
	return ok;
}

void MissionPanel::RefreshMissionList()
{
	m_mission_count = EditorPersistence::ListMissions(
		&m_mission_ids[0][0], kMaxMissionIds, kMaxMissionIdLen);
	if (m_mission_count < 0) m_mission_count = 0;
	if (m_selected_list_index >= m_mission_count)
		m_selected_list_index = (m_mission_count > 0) ? 0 : -1;
}

bool MissionPanel::LoadMissionByIndex(int idx)
{
	if (idx < 0 || idx >= m_mission_count) return false;
	return ImportMission(m_mission_ids[idx]);
}

void MissionPanel::DrawFormSection()
{
	ImGui::SeparatorText("Mission");

	ImGui::InputText("id", m_form.id, sizeof(m_form.id));
	ImGui::SameLine();
	if (ImGui::SmallButton("sanitise##id"))
		LowerAndSanitiseId(m_form.id, sizeof(m_form.id));

	ImGui::InputText("display name", m_form.display_name, sizeof(m_form.display_name));
	ImGui::InputText("giver npc", m_form.giver_npc, sizeof(m_form.giver_npc));
	ImGui::SetItemTooltip("object_name of an NPC spawned from the Npcs panel. "
		"Leave empty for a free-floating goal.");

	ImGui::Combo("type", &m_form.type_choice, kTypeChoices, kNumTypeChoices);

	ImGui::InputTextMultiline("description", m_form.description,
		sizeof(m_form.description), ImVec2(-1.0f, 100.0f));
	ImGui::SetItemTooltip("Shown verbatim in the engine's native speech box "
		"when the mission activates.");

	ImGui::InputInt("cash reward", &m_form.reward_cash);
	ImGui::InputInt("skill points", &m_form.reward_skill_points);
}

void MissionPanel::DrawActionsSection()
{
	ImGui::SeparatorText("Actions");

	WaypointsPanel *wp = WaypointsPanel::Get();
	const std::size_t live_wp = (wp != nullptr) ? wp->GetWaypointCount() : 0;
	ImGui::TextDisabled("live waypoints: %zu (persisted with mission)", live_wp);
	ImGui::TextDisabled("deferred — engine race sequencing needs NodeArray flag refs");

	if (ImGui::Button("Publish", ImVec2(120.0f, 0.0f)))
		PublishMission();
	ImGui::SameLine();
	if (ImGui::Button("Revoke", ImVec2(120.0f, 0.0f)))
		m_confirm_revoke_open = true;

	ImGui::SameLine();
	ImGui::TextDisabled("goal id: 0x%08X", GoalId());

	if (m_confirm_revoke_open)
	{
		ImGui::OpenPopup("Revoke mission?");
		m_confirm_revoke_open = false;
	}
	if (ImGui::BeginPopupModal("Revoke mission?", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::TextWrapped("Remove goal '%s' from the live GoalManager?", m_form.id);
		ImGui::Separator();
		if (ImGui::Button("Revoke", ImVec2(120.0f, 0.0f)))
		{
			RevokeMission();
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f)))
			ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
	}

	if (m_status_message[0] != '\0')
	{
		ImGui::Separator();
		ImGui::TextWrapped("%s", m_status_message);
	}
}

void MissionPanel::DrawPersistenceSection()
{
	ImGui::SeparatorText("Persistence");

	if (ImGui::Button("Export", ImVec2(120.0f, 0.0f)))
		ExportMission();
	ImGui::SameLine();
	if (ImGui::Button("Re-import", ImVec2(120.0f, 0.0f)))
		ImportMission(m_form.id);
	ImGui::SameLine();
	if (ImGui::Button("Discard saved", ImVec2(120.0f, 0.0f)))
		m_confirm_discard_open = true;

	if (m_confirm_discard_open)
	{
		ImGui::OpenPopup("Discard saved mission?");
		m_confirm_discard_open = false;
	}
	if (ImGui::BeginPopupModal("Discard saved mission?", nullptr,
			ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::TextWrapped("Delete the on-disk snapshot for '%s'? "
			"The live goal (if any) stays — use Revoke for that.", m_form.id);
		ImGui::Separator();
		if (ImGui::Button("Discard", ImVec2(120.0f, 0.0f)))
		{
			DiscardMission(m_form.id);
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f)))
			ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
	}
}

void MissionPanel::DrawListSection()
{
	ImGui::SeparatorText("Saved missions");

	if (ImGui::Button("Refresh list"))
		RefreshMissionList();
	ImGui::SameLine();
	ImGui::TextDisabled("%d on disk (cap %d)", m_mission_count, kMaxMissionIds);

	if (ImGui::BeginListBox("##mission_list", ImVec2(-1.0f, 100.0f)))
	{
		for (int i = 0; i < m_mission_count; ++i)
		{
			bool selected = (i == m_selected_list_index);
			if (ImGui::Selectable(m_mission_ids[i], selected))
				m_selected_list_index = i;
			if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(0))
				LoadMissionByIndex(i);
		}
		ImGui::EndListBox();
	}

	const bool has_sel = (m_selected_list_index >= 0 && m_selected_list_index < m_mission_count);
	if (ImGui::Button("Load into form", ImVec2(140.0f, 0.0f)) && has_sel)
		LoadMissionByIndex(m_selected_list_index);
	ImGui::SameLine();
	if (ImGui::Button("Publish selected", ImVec2(140.0f, 0.0f)) && has_sel)
	{
		if (LoadMissionByIndex(m_selected_list_index))
			PublishMission();
	}
}

void MissionPanel::Draw()
{
	if (!IsOpen()) return;

	ImGui::SetNextWindowPos(ImVec2(340.0f, 200.0f), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(440.0f, 560.0f), ImGuiCond_FirstUseEver);

	bool open = true;
	if (!ImGui::Begin("Mission", &open))
	{
		ImGui::End();
		if (!open) SetOpen(false);
		return;
	}

	// Phase 17 — first-visible-frame pulls the saved-missions list so the
	// panel is already populated when the user opens it.
	if (!m_first_draw_refresh)
	{
		m_first_draw_refresh = true;
		RefreshMissionList();
	}

	DrawFormSection();
	DrawActionsSection();
	DrawPersistenceSection();
	DrawListSection();

	ImGui::End();
	if (!open) SetOpen(false);
}

} // namespace Debug

#endif // DEBUG_IMGUI
