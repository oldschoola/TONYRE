#include "ObjectsPanel.h"

#if defined(DEBUG_IMGUI)

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>

#include "imgui.h"

#include <Core/Defines.h>
#include <Core/math.h>

#include <Gel/Object/compositeobject.h>
#include <Gel/Object/compositeobjectmanager.h>
#include <Gel/Scripting/array.h>
#include <Gel/Scripting/checksum.h>
#include <Gel/Scripting/script.h>
#include <Gel/Scripting/struct.h>
#include <Gel/Scripting/symboltable.h>

#include <Sk/GameNet/GameNet.h>
#include <Sk/Modules/Skate/skate.h>
#include <Sk/Objects/skater.h>

#include "../EditorPersistence.h"

namespace Debug
{

ObjectsPanel *ObjectsPanel::s_instance = nullptr;

ObjectsPanel::ObjectsPanel()
{
	s_instance = this;
}

namespace
{

// Models live under Game/Data/models. Wn32 cwd at game start is Game/, so
// the relative root we scan and the relative root the engine's model
// component expects match up — we store paths relative to "models/".
const char *	kModelsRoot			= "Data/models";
constexpr int	kMaxModels			= 4096;
constexpr int	kMaxPlacements		= 256;

// Buffers big enough to hold reasonably large JSON snapshots. Objects
// snapshots are linear in number of placements; 128 placements ~= 25 KB.
constexpr std::size_t kPlacementsBlobCap	= 128 * 1024;
constexpr std::size_t kModelCacheBlobCap	= 2 * 1024 * 1024;

bool CaseInsensitiveContains(const char *haystack, const char *needle)
{
	if (needle == nullptr || needle[0] == '\0') return true;
	if (haystack == nullptr) return false;
	const size_t hlen = std::strlen(haystack);
	const size_t nlen = std::strlen(needle);
	if (nlen > hlen) return false;
	for (size_t i = 0; i + nlen <= hlen; ++i)
	{
		size_t j = 0;
		for (; j < nlen; ++j)
		{
			const char a = static_cast<char>(std::tolower(static_cast<unsigned char>(haystack[i + j])));
			const char b = static_cast<char>(std::tolower(static_cast<unsigned char>(needle[j])));
			if (a != b) break;
		}
		if (j == nlen) return true;
	}
	return false;
}

void NormaliseSlashes(std::string &path)
{
	for (char &c : path)
	{
		if (c == '\\')
			c = '/';
	}
}

void EscapeForJson(const char *in, std::string &out)
{
	for (const char *p = in; *p != '\0'; ++p)
	{
		unsigned char c = static_cast<unsigned char>(*p);
		if (c == '"' || c == '\\')
		{
			out.push_back('\\');
			out.push_back(static_cast<char>(c));
		}
		else if (c < 0x20)
		{
			// Skip control chars — the values we emit are either filenames
			// or ascii identifiers; non-printable bytes shouldn't exist.
		}
		else
		{
			out.push_back(static_cast<char>(c));
		}
	}
}

} // anon namespace

void ObjectsPanel::ResetToDefaults()
{
	m_placements.clear();
	m_selected_placement_index = -1;
	m_spawn_use_skater = true;
	m_spawn_pos[0] = m_spawn_pos[1] = m_spawn_pos[2] = 0.0f;
	std::snprintf(m_spawn_name_stem, sizeof(m_spawn_name_stem), "object");
	m_spawn_counter = 0;
	m_confirm_delete_open = false;
	m_confirm_clear_placements_open = false;
}

void ObjectsPanel::BuildLevelId(char *out, std::size_t cap) const
{
	if (cap == 0) return;
	out[0] = '\0';

	uint32_t level_crc = 0;
	GameNet::Manager *gn = GameNet::Manager::Instance();
	if (gn != nullptr)
		level_crc = gn->GetNetworkLevelId();

	if (level_crc == 0)
	{
		std::snprintf(out, cap, "default");
		return;
	}
	std::snprintf(out, cap, "level_%08x", static_cast<unsigned int>(level_crc));
}

bool ObjectsPanel::TryReadSkaterPos(float out[3]) const
{
	Mdl::Skate *skate = Mdl::Skate::Instance();
	if (skate == nullptr) return false;
	Obj::CSkater *local = skate->GetLocalSkater();
	if (local == nullptr) return false;
	const Mth::Vector &p = local->GetPos();
	out[0] = p.GetX();
	out[1] = p.GetY();
	out[2] = p.GetZ();
	return true;
}

void ObjectsPanel::ScanModels()
{
	m_models.clear();
	m_selected_model_index = -1;

	std::error_code ec;
	if (!std::filesystem::exists(kModelsRoot, ec) || ec)
		return;

	for (const auto &entry :
		std::filesystem::recursive_directory_iterator(kModelsRoot, ec))
	{
		if (ec) break;
		if (!entry.is_regular_file()) continue;

		const auto ext = entry.path().extension().string();
		const bool is_skin = (ext == ".skin" || ext == ".SKIN");
		const bool is_mdl  = (ext == ".mdl"  || ext == ".MDL");
		if (!is_skin && !is_mdl)
			continue;

		// Relative to "Data/models/" — that's the form the engine's model
		// component passes through to AddGeom (which prepends "models/").
		std::string rel = std::filesystem::relative(entry.path(), kModelsRoot, ec).string();
		if (ec) continue;
		NormaliseSlashes(rel);
		m_models.push_back(std::move(rel));
		if (static_cast<int>(m_models.size()) >= kMaxModels)
			break;
	}

	std::sort(m_models.begin(), m_models.end());
	m_cache_loaded = true;
	m_filter_dirty = true;
	SaveCacheToDisk();
}

bool ObjectsPanel::LoadCacheFromDisk()
{
	if (!EditorPersistence::ModelCacheExists())
		return false;

	std::vector<char> buf(kModelCacheBlobCap);
	std::size_t written = 0;
	if (!EditorPersistence::ReadModelCache(buf.data(), buf.size(), &written))
		return false;
	if (written == 0)
		return false;

	m_models.clear();
	m_selected_model_index = -1;

	// Hand-parse each "  \"path/to.skin\"" line; same layout as SaveCacheToDisk.
	const char *p		= buf.data();
	const char *end		= p + written;
	while (p < end)
	{
		while (p < end && *p != '"') ++p;
		if (p >= end) break;
		++p;
		const char *start = p;
		while (p < end && *p != '"') ++p;
		if (p >= end) break;
		m_models.emplace_back(start, static_cast<std::size_t>(p - start));
		++p;
		if (static_cast<int>(m_models.size()) >= kMaxModels) break;
	}
	m_cache_loaded = !m_models.empty();
	m_filter_dirty = true;
	return m_cache_loaded;
}

bool ObjectsPanel::SaveCacheToDisk()
{
	std::string body;
	body.reserve(m_models.size() * 48 + 64);
	body += "{\n  \"version\": 1,\n  \"models\": [\n";
	for (std::size_t i = 0; i < m_models.size(); ++i)
	{
		body += "    \"";
		EscapeForJson(m_models[i].c_str(), body);
		body += "\"";
		if (i + 1 < m_models.size()) body += ",";
		body += "\n";
	}
	body += "  ]\n}\n";
	return EditorPersistence::WriteModelCache(body.data(), body.size());
}

bool ObjectsPanel::SpawnPlacement(const Placement &p)
{
	Obj::CCompositeObjectManager *mgr = Obj::CCompositeObjectManager::Instance();
	if (mgr == nullptr) return false;

	// Components array: motion for position, model for geometry. The model
	// component reads its path from the "model" text key on the params.
	Script::CArray *p_components = new Script::CArray;
	p_components->SetSizeAndType(2, ESYMBOLTYPE_STRUCTURE);

	Script::CStruct *p_motion = new Script::CStruct;
	p_motion->AddChecksum(Crc::ConstCRC("component"), Crc::ConstCRC("motion"));
	p_components->SetStructure(0, p_motion);

	Script::CStruct *p_model = new Script::CStruct;
	p_model->AddChecksum(Crc::ConstCRC("component"), Crc::ConstCRC("model"));
	p_model->AddString(Crc::ConstCRC("model"), p.model_path);
	p_components->SetStructure(1, p_model);

	// Params struct: unique name checksum + world-space position.
	Script::CStruct *p_params = new Script::CStruct;
	const uint32 name_crc = Script::GenerateCRC(p.object_name);
	p_params->AddChecksum(Crc::ConstCRC("name"), name_crc);
	p_params->AddVector(Crc::ConstCRC("pos"), p.pos[0], p.pos[1], p.pos[2]);

	Obj::CCompositeObject *pObj = mgr->CreateCompositeObjectFromNode(p_components, p_params);

	// CArray of Structure type asserts on destruction if any slot still
	// holds a non-null CStruct*. CleanUpArray deletes the two children
	// and zeroes the slots before the CArray dtor runs.
	Script::CleanUpArray(p_components);
	delete p_components;
	delete p_params;

	return pObj != nullptr;
}

void ObjectsPanel::AddAndSpawn(const char *model_path, float x, float y, float z)
{
	if (model_path == nullptr || model_path[0] == '\0')
		return;
	if (static_cast<int>(m_placements.size()) >= kMaxPlacements)
		return;

	Placement p;
	std::memset(&p, 0, sizeof(p));
	std::snprintf(p.model_path, sizeof(p.model_path), "%s", model_path);
	std::snprintf(p.object_name, sizeof(p.object_name),
		"%s_%d", m_spawn_name_stem, m_spawn_counter++);
	p.pos[0] = x; p.pos[1] = y; p.pos[2] = z;

	if (!SpawnPlacement(p))
		return;	// engine refused — don't record a placement we couldn't create.

	m_placements.push_back(p);
	m_selected_placement_index = static_cast<int>(m_placements.size()) - 1;
}

void ObjectsPanel::DeletePlacement(int index)
{
	if (index < 0 || index >= static_cast<int>(m_placements.size()))
		return;

	// Kill the in-game object too if the object manager still has it.
	Obj::CCompositeObjectManager *mgr = Obj::CCompositeObjectManager::Instance();
	if (mgr != nullptr)
	{
		const uint32 name_crc = Script::GenerateCRC(m_placements[index].object_name);
		Obj::CObject *obj = mgr->GetObjectByID(name_crc);
		if (obj != nullptr)
			mgr->KillObject(*obj);
	}

	m_placements.erase(m_placements.begin() + index);
	if (m_placements.empty())
		m_selected_placement_index = -1;
	else if (m_selected_placement_index >= static_cast<int>(m_placements.size()))
		m_selected_placement_index = static_cast<int>(m_placements.size()) - 1;
}

int ObjectsPanel::SpawnAllPlacements()
{
	int ok = 0;
	for (const Placement &p : m_placements)
	{
		if (SpawnPlacement(p))
			++ok;
	}
	return ok;
}

int ObjectsPanel::KillAllPlacements()
{
	int killed = 0;
	Obj::CCompositeObjectManager *mgr = Obj::CCompositeObjectManager::Instance();
	if (mgr == nullptr) return 0;
	for (const Placement &p : m_placements)
	{
		const uint32 name_crc = Script::GenerateCRC(p.object_name);
		Obj::CObject *obj = mgr->GetObjectByID(name_crc);
		if (obj != nullptr)
		{
			mgr->KillObject(*obj);
			++killed;
		}
	}
	return killed;
}

void ObjectsPanel::ClearPlacementList()
{
	KillAllPlacements();
	m_placements.clear();
	m_selected_placement_index = -1;
}

void ObjectsPanel::ExportSnapshot()
{
	char level_id[32];
	BuildLevelId(level_id, sizeof(level_id));

	std::string body;
	body.reserve(m_placements.size() * 192 + 128);
	body += "{\n  \"version\": 1,\n  \"level_id\": \"";
	EscapeForJson(level_id, body);
	body += "\",\n  \"placements\": [\n";
	for (std::size_t i = 0; i < m_placements.size(); ++i)
	{
		const Placement &p = m_placements[i];
		body += "    { \"model\": \"";
		EscapeForJson(p.model_path, body);
		body += "\", \"name\": \"";
		EscapeForJson(p.object_name, body);
		body += "\", \"pos\": [";
		char buf[96];
		std::snprintf(buf, sizeof(buf), "%.3f, %.3f, %.3f", p.pos[0], p.pos[1], p.pos[2]);
		body += buf;
		body += "] }";
		if (i + 1 < m_placements.size()) body += ",";
		body += "\n";
	}
	body += "  ]\n}\n";

	EditorPersistence::WriteObjects(level_id, body.data(), body.size());
}

void ObjectsPanel::ImportSnapshot()
{
	char level_id[32];
	BuildLevelId(level_id, sizeof(level_id));

	std::vector<char> buf(kPlacementsBlobCap);
	std::size_t written = 0;
	if (!EditorPersistence::ReadObjects(level_id, buf.data(), buf.size(), &written))
		return;
	if (written == 0) return;

	// Tiny hand parse — matches ExportSnapshot's layout exactly. Each
	// placement lives on its own "{...}" line and uses flat values only.
	const char *p	= buf.data();
	const char *end = p + written;

	auto skip_ws = [](const char *&c, const char *e) {
		while (c < e && (*c == ' ' || *c == '\t' || *c == '\n' || *c == '\r')) ++c;
	};

	auto parse_string = [](const char *&c, const char *e, char *out, std::size_t cap) -> bool {
		while (c < e && *c != '"') ++c;
		if (c >= e) return false;
		++c;
		std::size_t w = 0;
		while (c < e && *c != '"' && w + 1 < cap)
		{
			out[w++] = *c++;
		}
		out[w] = '\0';
		while (c < e && *c != '"') ++c;
		if (c >= e) return false;
		++c;
		return true;
	};

	auto parse_vec3 = [&](const char *&c, const char *e, float v[3]) -> bool {
		while (c < e && *c != '[') ++c;
		if (c >= e) return false;
		++c;
		for (int i = 0; i < 3; ++i)
		{
			skip_ws(c, e);
			char *endp = nullptr;
			v[i] = std::strtof(c, &endp);
			if (endp == c) return false;
			c = endp;
			while (c < e && (*c == ' ' || *c == ',')) ++c;
		}
		while (c < e && *c != ']') ++c;
		if (c < e) ++c;
		return true;
	};

	m_placements.clear();

	while (p < end)
	{
		while (p < end && *p != '{') ++p;
		if (p >= end) break;
		const char *record_end = p;
		while (record_end < end && *record_end != '}') ++record_end;
		if (record_end >= end) break;
		++p;

		Placement placement;
		std::memset(&placement, 0, sizeof(placement));

		// Scan keys in order: "model", "name", "pos".
		const char *cursor = p;
		const char *rec_end = record_end;

		const char *model_tag = std::strstr(cursor, "\"model\"");
		const char *name_tag  = std::strstr(cursor, "\"name\"");
		const char *pos_tag   = std::strstr(cursor, "\"pos\"");

		if (model_tag != nullptr && model_tag < rec_end)
		{
			const char *c = model_tag + 7;
			while (c < rec_end && *c != ':') ++c;
			if (c < rec_end) { ++c; parse_string(c, rec_end, placement.model_path, sizeof(placement.model_path)); }
		}
		if (name_tag != nullptr && name_tag < rec_end)
		{
			const char *c = name_tag + 6;
			while (c < rec_end && *c != ':') ++c;
			if (c < rec_end) { ++c; parse_string(c, rec_end, placement.object_name, sizeof(placement.object_name)); }
		}
		if (pos_tag != nullptr && pos_tag < rec_end)
		{
			const char *c = pos_tag + 5;
			while (c < rec_end && *c != ':') ++c;
			if (c < rec_end) { ++c; parse_vec3(c, rec_end, placement.pos); }
		}

		if (placement.model_path[0] != '\0' && placement.object_name[0] != '\0')
		{
			if (SpawnPlacement(placement))
				m_placements.push_back(placement);
		}

		p = record_end + 1;
		if (static_cast<int>(m_placements.size()) >= kMaxPlacements)
			break;
	}
}

// ---------------------------------------------------------------- UI

void ObjectsPanel::DrawScanSection()
{
	ImGui::Text("Model Cache: %d entries", static_cast<int>(m_models.size()));
	ImGui::SameLine();
	if (ImGui::Button("Rescan"))
		ScanModels();
	ImGui::SameLine();
	if (!m_cache_loaded && ImGui::Button("Load From Disk"))
	{
		if (!LoadCacheFromDisk())
			ScanModels();
	}

	if (m_models.empty())
	{
		ImGui::TextDisabled("No models scanned yet.");
	}
}

void ObjectsPanel::DrawModelListSection()
{
	if (m_models.empty())
		return;

	ImGui::SetNextItemWidth(-FLT_MIN);
	ImGui::InputTextWithHint("##filter", "filter…",
		m_filter, sizeof(m_filter));

	// Build filtered index list so every matching entry is reachable — no
	// more 512-row cap that was hiding ~300 of the 800+ scanned models.
	// ImGuiListClipper handles the virtualisation so rendering stays cheap.
	// Cache the filtered list so we don't rescan 800+ strings per frame when
	// the filter string and scan haven't changed.
	if (m_filter_dirty || std::strcmp(m_filter, m_last_filter) != 0)
	{
		m_filtered_indices.clear();
		m_filtered_indices.reserve(m_models.size());
		for (int i = 0; i < static_cast<int>(m_models.size()); ++i)
		{
			if (m_filter[0] == '\0' ||
				CaseInsensitiveContains(m_models[i].c_str(), m_filter))
				m_filtered_indices.push_back(i);
		}
		std::snprintf(m_last_filter, sizeof(m_last_filter), "%s", m_filter);
		m_filter_dirty = false;
	}
	m_model_filter_match = static_cast<int>(m_filtered_indices.size());

	if (ImGui::BeginListBox("##models", ImVec2(-FLT_MIN, 220.0f)))
	{
		ImGuiListClipper clipper;
		clipper.Begin(static_cast<int>(m_filtered_indices.size()));
		while (clipper.Step())
		{
			for (int row = clipper.DisplayStart; row < clipper.DisplayEnd; ++row)
			{
				const int i = m_filtered_indices[row];
				const std::string &s = m_models[i];
				const bool sel = (m_selected_model_index == i);
				ImGui::PushID(i);
				if (ImGui::Selectable(s.c_str(), sel))
					m_selected_model_index = i;
				ImGui::PopID();
			}
		}
		ImGui::EndListBox();
	}
	ImGui::TextDisabled("matching: %d", m_model_filter_match);
}

void ObjectsPanel::DrawPreviewSection()
{
	ImGui::SeparatorText("Preview");
	ImGui::Checkbox("Show preview", &m_preview_show);

	m_preview.SetVisible(m_preview_show);

	if (!m_preview_show)
		return;

	// Re-select on index change so we don't thrash the FBO every frame.
	if (m_selected_model_index != m_preview_selected_index)
	{
		m_preview_selected_index = m_selected_model_index;
		const bool valid = (m_selected_model_index >= 0) &&
			(m_selected_model_index < static_cast<int>(m_models.size()));
		m_preview.SetModelPath(valid ? m_models[m_selected_model_index].c_str() : "");
	}

	// Render into the FBO this frame then display.
	m_preview.Render();
	const float size = static_cast<float>(ModelPreview::kSize);
	m_preview.ImGuiDraw(size, size);
}

void ObjectsPanel::DrawSpawnSection()
{
	ImGui::SeparatorText("Spawn");

	ImGui::InputText("Name Stem", m_spawn_name_stem, sizeof(m_spawn_name_stem));
	ImGui::Checkbox("Use Skater Pos", &m_spawn_use_skater);
	ImGui::BeginDisabled(m_spawn_use_skater);
	ImGui::DragFloat3("XYZ", m_spawn_pos, 1.0f);
	ImGui::EndDisabled();

	const bool can_spawn = (m_selected_model_index >= 0) &&
		(m_selected_model_index < static_cast<int>(m_models.size()));

	ImGui::BeginDisabled(!can_spawn);
	if (ImGui::Button("Spawn"))
	{
		float pos[3] = { m_spawn_pos[0], m_spawn_pos[1], m_spawn_pos[2] };
		if (m_spawn_use_skater)
		{
			if (!TryReadSkaterPos(pos))
			{
				// Leave at m_spawn_pos if no skater — better than doing nothing.
			}
		}
		AddAndSpawn(m_models[m_selected_model_index].c_str(),
			pos[0], pos[1], pos[2]);
	}
	ImGui::EndDisabled();
	if (!can_spawn)
	{
		ImGui::SameLine();
		ImGui::TextDisabled("select a model first");
	}
}

void ObjectsPanel::DrawPlacementsSection()
{
	char level_id[32];
	BuildLevelId(level_id, sizeof(level_id));

	ImGui::SeparatorText("Placements");
	ImGui::Text("Level: %s  |  Count: %d",
		level_id, static_cast<int>(m_placements.size()));

	if (ImGui::Button("Export"))	ExportSnapshot();
	ImGui::SameLine();
	if (ImGui::Button("Import"))	ImportSnapshot();
	ImGui::SameLine();
	if (ImGui::Button("Spawn all") && !m_placements.empty())
		SpawnAllPlacements();
	ImGui::SameLine();
	if (ImGui::Button("Clear (scene+list)") && !m_placements.empty())
		m_confirm_clear_placements_open = true;

	if (m_placements.empty())
	{
		ImGui::TextDisabled("No placements yet.");
		return;
	}

	if (ImGui::BeginTable("placements", 4,
		ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH |
		ImGuiTableFlags_SizingStretchProp))
	{
		ImGui::TableSetupColumn("Name",		ImGuiTableColumnFlags_WidthStretch, 0.3f);
		ImGui::TableSetupColumn("Model",	ImGuiTableColumnFlags_WidthStretch, 0.55f);
		ImGui::TableSetupColumn("Pos",		ImGuiTableColumnFlags_WidthStretch, 0.25f);
		ImGui::TableSetupColumn("X",		ImGuiTableColumnFlags_WidthFixed, 28.0f);
		ImGui::TableHeadersRow();

		int pending_delete = -1;

		for (int i = 0; i < static_cast<int>(m_placements.size()); ++i)
		{
			const Placement &p = m_placements[i];
			ImGui::PushID(i);
			ImGui::TableNextRow();

			ImGui::TableSetColumnIndex(0);
			const bool sel = (m_selected_placement_index == i);
			if (ImGui::Selectable(p.object_name, sel,
				ImGuiSelectableFlags_SpanAllColumns))
				m_selected_placement_index = i;

			ImGui::TableSetColumnIndex(1);
			ImGui::TextUnformatted(p.model_path);

			ImGui::TableSetColumnIndex(2);
			ImGui::Text("%.0f, %.0f, %.0f", p.pos[0], p.pos[1], p.pos[2]);

			ImGui::TableSetColumnIndex(3);
			if (ImGui::SmallButton("x"))
				pending_delete = i;

			ImGui::PopID();
		}
		ImGui::EndTable();

		if (pending_delete >= 0)
			DeletePlacement(pending_delete);
	}
}

void ObjectsPanel::Draw()
{
	ImGui::SetNextWindowPos(ImVec2(660.0f, 20.0f), ImGuiCond_FirstUseEver);
	ImGui::SetNextWindowSize(ImVec2(560.0f, 560.0f), ImGuiCond_FirstUseEver);

	if (!ImGui::Begin(GetName(), GetOpenFlag()))
	{
		ImGui::End();
		return;
	}

	// First draw — best-effort load cache from disk so we don't trip the
	// filesystem scan on every boot.
	if (!m_cache_loaded && !m_models.empty())
		m_cache_loaded = true;
	if (!m_cache_loaded && m_models.empty() && EditorPersistence::ModelCacheExists())
		LoadCacheFromDisk();

	DrawScanSection();
	ImGui::Separator();
	DrawModelListSection();
	DrawPreviewSection();
	DrawSpawnSection();
	DrawPlacementsSection();

	ImGui::End();

	if (m_confirm_clear_placements_open)
	{
		ImGui::OpenPopup("Clear placements?");
		m_confirm_clear_placements_open = false;
	}
	if (ImGui::BeginPopupModal("Clear placements?", nullptr,
			ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::TextWrapped("Remove every placement from this level? "
			"The in-game objects will be killed and the list reset.");
		ImGui::Separator();
		if (ImGui::Button("Clear", ImVec2(120.0f, 0.0f)))
		{
			Obj::CCompositeObjectManager *mgr = Obj::CCompositeObjectManager::Instance();
			if (mgr != nullptr)
			{
				for (const Placement &p : m_placements)
				{
					const uint32 name_crc = Script::GenerateCRC(p.object_name);
					Obj::CObject *obj = mgr->GetObjectByID(name_crc);
					if (obj != nullptr)
						mgr->KillObject(*obj);
				}
			}
			m_placements.clear();
			m_selected_placement_index = -1;
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f)))
			ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
	}
}

} // namespace Debug

#endif // DEBUG_IMGUI
