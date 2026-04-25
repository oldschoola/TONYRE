#include "NpcsPanel.h"

#if defined(DEBUG_IMGUI)

#include "imgui.h"

#include <cstdio>
#include <cstring>

#include <Core/Defines.h>
#include <Core/math.h>
#include <Core/List/head.h>
#include <Core/List/search.h>

#include <Gel/object.h>
#include <Gel/Object/compositeobject.h>
#include <Gel/Object/basecomponent.h>
#include <Gel/Object/compositeobjectmanager.h>
#include <Gel/objman.h>
#include <Gel/Scripting/script.h>
#include <Gel/Scripting/struct.h>
#include <Gel/Scripting/array.h>
#include <Gel/Scripting/checksum.h>
#include <Gel/Scripting/symboltable.h>

#include <Sk/Modules/Skate/skate.h>
#include <Sk/Objects/skater.h>
#include <Sk/Objects/ped.h>

#include "../EditorPersistence.h"

namespace Debug
{

namespace
{

// Class checksums used by the NodeArray to identify pedestrian entries.
// Mirrors the dispatcher in Code/Sk/Scripting/cfuncs.cpp:5777 where
// CreatePed is called for either class value.
constexpr uint32 kClassPedestrian	= 0xa0dfac98;
constexpr uint32 kClassPed			= 0x061a741e;

const char *	kScriptChoices[] = {
	"ped_ai_skate",
	"ped_ai_walk",
	"ped_ai_idle",
	"ped_ai_flee",
};
constexpr int kNumScriptChoices = static_cast<int>(sizeof(kScriptChoices) / sizeof(kScriptChoices[0]));

bool IsPedClass(uint32 class_crc)
{
	return class_crc == kClassPedestrian || class_crc == kClassPed;
}

// Clone a CStruct by heap-allocating and AppendStructure'ing the source.
// Returns nullptr if src is null. Caller owns the returned pointer.
Script::CStruct *ClonePedTemplate(const Script::CStruct *src)
{
	if (src == nullptr)
		return nullptr;
	Script::CStruct *copy = new Script::CStruct;
	copy->AppendStructure(src);
	return copy;
}

// Walk the active level's NodeArray looking for the first pedestrian entry.
// Returns nullptr if the array is missing or contains no peds.
const Script::CStruct *FindPedestrianNode()
{
	Script::CArray *p_node_array = Script::GetArray(Crc::ConstCRC("NodeArray"));
	if (p_node_array == nullptr)
		return nullptr;

	for (size_t i = 0; i < p_node_array->GetSize(); ++i)
	{
		Script::CStruct *p_node = p_node_array->GetStructure(i);
		if (p_node == nullptr)
			continue;
		uint32 class_crc = 0;
		if (!p_node->GetChecksum(Crc::ConstCRC("Class"), &class_crc))
			continue;
		if (IsPedClass(class_crc))
			return p_node;
	}
	return nullptr;
}

} // anon namespace

void NpcsPanel::ResetToDefaults()
{
	m_selected_row = -1;
	m_selected_id = 0;
	m_edit_pos[0] = m_edit_pos[1] = m_edit_pos[2] = 0.0f;
	m_spawn_pos[0] = m_spawn_pos[1] = m_spawn_pos[2] = 0.0f;
	m_script_choice = 0;
	m_confirm_delete_open = false;
}

bool NpcsPanel::TryReadSkaterPos(float out[3])
{
	Mdl::Skate *skate = Mdl::Skate::Instance();
	if (skate == nullptr)
		return false;
	Obj::CSkater *local = skate->GetLocalSkater();
	if (local == nullptr)
		return false;
	const Mth::Vector &p = local->GetPos();
	out[0] = p.GetX();
	out[1] = p.GetY();
	out[2] = p.GetZ();
	return true;
}

void NpcsPanel::RefreshList()
{
	m_num_entries = 0;

	Obj::CCompositeObjectManager *mgr = Obj::CCompositeObjectManager::Instance();
	if (mgr == nullptr)
		return;

	Lst::Search<Obj::CObject> sh;
	Lst::Head<Obj::CObject> &list = mgr->GetRefObjectList();
	for (Obj::CObject *obj = sh.FirstItem(list); obj != nullptr; obj = sh.NextItem())
	{
		if (obj->GetType() != SKATE_TYPE_PED)
			continue;
		if (m_num_entries >= kMaxListRows)
			break;

		Obj::CCompositeObject *comp = static_cast<Obj::CCompositeObject *>(obj);
		const Mth::Vector &p = comp->GetPos();

		PedEntry &e = m_entries[m_num_entries++];
		e.id = comp->GetID();
		e.pos[0] = p.GetX();
		e.pos[1] = p.GetY();
		e.pos[2] = p.GetZ();

		// Try to reverse the CRC to the symbol table name; fall back to hex.
		const char *name = Script::FindChecksumName(e.id);
		if (name != nullptr && name[0] != '\0')
			std::snprintf(e.label, sizeof(e.label), "%s", name);
		else
			std::snprintf(e.label, sizeof(e.label), "0x%08X", e.id);
	}

	// Re-sync the selected index to the new list (id may have shifted rows).
	m_selected_row = -1;
	for (int i = 0; i < m_num_entries; ++i)
	{
		if (m_entries[i].id == m_selected_id)
		{
			m_selected_row = i;
			m_edit_pos[0] = m_entries[i].pos[0];
			m_edit_pos[1] = m_entries[i].pos[1];
			m_edit_pos[2] = m_entries[i].pos[2];
			break;
		}
	}
	if (m_selected_row < 0)
		m_selected_id = 0;
}

bool NpcsPanel::EnsureTemplate()
{
	if (m_template_captured)
		return true;
	if (m_template_tried)
		return false;

	m_template_tried = true;

	const Script::CStruct *src = FindPedestrianNode();
	if (src == nullptr)
		return false;

	m_template = ClonePedTemplate(src);
	m_template_captured = (m_template != nullptr);
	return m_template_captured;
}

void NpcsPanel::SpawnAt(float x, float y, float z)
{
	if (!EnsureTemplate())
		return;

	Obj::CCompositeObjectManager *mgr = Obj::CCompositeObjectManager::Instance();
	if (mgr == nullptr)
		return;

	// Build a one-shot spawn struct from the cached template. AppendStructure
	// merges the template fields; we then overwrite the ones the spawn needs
	// to differ (position, unique name/id).
	Script::CStruct params;
	params.AppendStructure(m_template);

	// Give the new ped a unique name so CGeneralManager::RegisterObject does
	// not collide with an existing object id. The counter persists across
	// panel uses within a session.
	char name_buf[48];
	std::snprintf(name_buf, sizeof(name_buf), "imgui_ped_%d", ++m_spawn_counter);
	uint32 new_id = Script::GenerateCRC(name_buf);
	params.RemoveComponent(Crc::ConstCRC("Name"));
	params.AddChecksum(Crc::ConstCRC("Name"), new_id);

	params.RemoveComponent(Crc::ConstCRC("Position"));
	params.AddVector(Crc::ConstCRC("Position"), x, y, z);
	params.RemoveComponent(Crc::ConstCRC("Pos"));
	params.AddVector(Crc::ConstCRC("Pos"), x, y, z);

	Obj::CreatePed(mgr, &params);
}

bool NpcsPanel::DeleteSelected()
{
	if (m_selected_id == 0)
		return false;

	Obj::CCompositeObjectManager *mgr = Obj::CCompositeObjectManager::Instance();
	if (mgr == nullptr)
		return false;

	Obj::CObject *obj = mgr->GetObjectByID(m_selected_id);
	if (obj == nullptr)
		return false;

	mgr->KillObject(*obj);
	m_selected_id = 0;
	m_selected_row = -1;
	return true;
}

void NpcsPanel::TeleportSelectedTo(float x, float y, float z)
{
	if (m_selected_id == 0)
		return;

	Obj::CCompositeObjectManager *mgr = Obj::CCompositeObjectManager::Instance();
	if (mgr == nullptr)
		return;

	Obj::CObject *obj = mgr->GetObjectByID(m_selected_id);
	if (obj == nullptr || obj->GetType() != SKATE_TYPE_PED)
		return;

	Obj::CCompositeObject *comp = static_cast<Obj::CCompositeObject *>(obj);
	Mth::Vector new_pos(x, y, z);
	comp->SetPos(new_pos);
	comp->SetTeleported(true);
}

void NpcsPanel::RunScriptOnSelected(uint32 script_crc)
{
	if (m_selected_id == 0 || script_crc == 0)
		return;

	Obj::CCompositeObjectManager *mgr = Obj::CCompositeObjectManager::Instance();
	if (mgr == nullptr)
		return;

	Obj::CObject *obj = mgr->GetObjectByID(m_selected_id);
	if (obj == nullptr)
		return;

	// Guard: only fire if the target script actually exists, else RunScript
	// asserts in debug builds.
	if (!Script::ScriptExists(script_crc))
		return;

	Script::RunScript(script_crc, nullptr, obj);
}

void NpcsPanel::DrawListSection()
{
	ImGui::SeparatorText("Peds in Level");

	if (ImGui::Button("Refresh"))
		RefreshList();
	ImGui::SameLine();
	ImGui::TextDisabled("%d listed (cap %d)", m_num_entries, kMaxListRows);

	if (ImGui::BeginListBox("##ped_list", ImVec2(-1.0f, 160.0f)))
	{
		for (int i = 0; i < m_num_entries; ++i)
		{
			const PedEntry &e = m_entries[i];
			bool selected = (i == m_selected_row);
			char row[80];
			std::snprintf(row, sizeof(row), "%s  (%.0f, %.0f, %.0f)", e.label, e.pos[0], e.pos[1], e.pos[2]);
			if (ImGui::Selectable(row, selected))
			{
				m_selected_row = i;
				m_selected_id = e.id;
				m_edit_pos[0] = e.pos[0];
				m_edit_pos[1] = e.pos[1];
				m_edit_pos[2] = e.pos[2];
			}
		}
		ImGui::EndListBox();
	}
}

void NpcsPanel::DrawSelectedSection()
{
	ImGui::SeparatorText("Selected");

	if (m_selected_id == 0)
	{
		ImGui::TextDisabled("no ped selected");
		return;
	}

	ImGui::Text("id: 0x%08X", m_selected_id);

	ImGui::DragFloat3("Position", m_edit_pos, 1.0f, -100000.0f, 100000.0f, "%.1f");
	if (ImGui::Button("Apply Teleport"))
		TeleportSelectedTo(m_edit_pos[0], m_edit_pos[1], m_edit_pos[2]);
	ImGui::SameLine();
	if (ImGui::Button("Teleport to Skater"))
	{
		float skater_pos[3];
		if (TryReadSkaterPos(skater_pos))
		{
			m_edit_pos[0] = skater_pos[0];
			m_edit_pos[1] = skater_pos[1];
			m_edit_pos[2] = skater_pos[2];
			TeleportSelectedTo(skater_pos[0], skater_pos[1], skater_pos[2]);
		}
	}

	ImGui::Separator();
	ImGui::Combo("Behavior Script", &m_script_choice, kScriptChoices, kNumScriptChoices);
	if (ImGui::Button("Run Script"))
		RunScriptOnSelected(Script::GenerateCRC(kScriptChoices[m_script_choice]));
	ImGui::SameLine();
	ImGui::TextDisabled("fires RunScript on selected ped");

	ImGui::Separator();
	if (ImGui::Button("Delete"))
		m_confirm_delete_open = true;

	if (m_confirm_delete_open)
	{
		ImGui::OpenPopup("Confirm Delete");
		m_confirm_delete_open = false;
	}

	if (ImGui::BeginPopupModal("Confirm Delete", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::Text("Kill ped 0x%08X?", m_selected_id);
		ImGui::TextDisabled("Async — removed next frame. Not reversible.");
		if (ImGui::Button("Confirm"))
		{
			DeleteSelected();
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel"))
			ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
	}
}

void NpcsPanel::DrawSpawnSection()
{
	ImGui::SeparatorText("Spawn Ped");

	if (!m_template_captured)
	{
		if (ImGui::Button("Capture template from NodeArray"))
			EnsureTemplate();
		ImGui::SameLine();
		if (m_template_tried && !m_template_captured)
			ImGui::TextDisabled("no pedestrian node found in level");
		else
			ImGui::TextDisabled("pulls the first Pedestrian entry as a template");
		return;
	}

	ImGui::TextDisabled("template ready");
	ImGui::Checkbox("Spawn at skater", &m_spawn_use_skater);
	if (!m_spawn_use_skater)
		ImGui::DragFloat3("Spawn XYZ", m_spawn_pos, 1.0f, -100000.0f, 100000.0f, "%.1f");

	if (ImGui::Button("Spawn"))
	{
		float target[3] = { m_spawn_pos[0], m_spawn_pos[1], m_spawn_pos[2] };
		if (m_spawn_use_skater)
			TryReadSkaterPos(target);
		SpawnAt(target[0], target[1], target[2]);
		// Refresh so the new ped shows up immediately.
		RefreshList();
	}
	ImGui::SameLine();
	ImGui::TextDisabled("spawn counter: %d", m_spawn_counter);
}

static void DrawNpcsSnapshotSection(NpcsPanel *self)
{
	using namespace EditorPersistence;

	ImGui::SeparatorText("Snapshot");
	ImGui::TextDisabled("writes userdata/editor/npcs.json");

	if (ImGui::Button("Export Snapshot"))
	{
		// Pull current list then dump their positions. Refresh first so the
		// cache reflects live peds, not a stale selection snapshot.
		self->ExportSnapshot();
	}
	ImGui::SameLine();
	if (ImGui::Button("Import Snapshot"))
		self->ImportSnapshot();
	ImGui::SameLine();
	ImGui::BeginDisabled(!NpcsSnapshotExists());
	if (ImGui::Button("Discard"))
		DiscardNpcs();
	ImGui::EndDisabled();

	if (NpcsSnapshotExists())
		ImGui::TextDisabled("snapshot present");
	else
		ImGui::TextDisabled("no snapshot");
}

void NpcsPanel::ExportSnapshot()
{
	RefreshList();
	EditorPersistence::PedRecord rows[kMaxListRows];
	for (int i = 0; i < m_num_entries; ++i)
	{
		rows[i].pos[0] = m_entries[i].pos[0];
		rows[i].pos[1] = m_entries[i].pos[1];
		rows[i].pos[2] = m_entries[i].pos[2];
	}
	EditorPersistence::WriteNpcs(rows, m_num_entries);
}

void NpcsPanel::ImportSnapshot()
{
	if (!EnsureTemplate())
		return;
	EditorPersistence::PedRecord rows[kMaxListRows];
	int n = 0;
	if (!EditorPersistence::ReadNpcs(rows, kMaxListRows, &n))
		return;
	for (int i = 0; i < n; ++i)
		SpawnAt(rows[i].pos[0], rows[i].pos[1], rows[i].pos[2]);
	RefreshList();
}

void NpcsPanel::Draw()
{
	if (!ImGui::Begin(GetName(), GetOpenFlag()))
	{
		ImGui::End();
		return;
	}

	DrawListSection();
	DrawSelectedSection();
	DrawSpawnSection();
	DrawNpcsSnapshotSection(this);

	ImGui::End();
}

} // namespace Debug

#endif // DEBUG_IMGUI
