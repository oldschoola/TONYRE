#include "RailsPanel.h"

#if defined(DEBUG_IMGUI)

#include "imgui.h"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <vector>

#include <Core/Defines.h>
#include <Core/math.h>

#include <Gel/Object/compositeobjectmanager.h>
#include <Gel/Object/compositeobject.h>
#include <Gel/Scripting/checksum.h>
#include <Gel/Scripting/struct.h>
#include <Gel/Scripting/array.h>

#include <Sk/Components/RailEditorComponent.h>

#include "../EditorPersistence.h"

namespace Debug
{

namespace
{

// Lookup matches GetRailEditor() in RailEditorComponent.cpp:2859 but without
// the asserts — this panel draws even when the object isn't present.
Obj::CRailEditorComponent *LookupRailEditor()
{
	Obj::CCompositeObjectManager *mgr = Obj::CCompositeObjectManager::Instance();
	if (mgr == nullptr)
		return nullptr;
	Obj::CObject *obj = mgr->GetObjectByID(Crc::ConstCRC("RailEditor"));
	if (obj == nullptr)
		return nullptr;
	Obj::CCompositeObject *comp = static_cast<Obj::CCompositeObject *>(obj);
	return GetRailEditorComponentFromObject(comp);
}

} // anon namespace

Obj::CRailEditorComponent *RailsPanel::GetEditor()
{
	return LookupRailEditor();
}

void RailsPanel::ResetToDefaults()
{
	RestoreBaseline();
	m_selected_rail = -1;
	m_confirm_clear_open = false;
}

void RailsPanel::CaptureBaseline()
{
	if (m_baseline_valid || m_baseline_tried)
		return;
	m_baseline_tried = true;

	Obj::CRailEditorComponent *editor = GetEditor();
	if (editor == nullptr)
		return;

	uint8 *src = editor->GetCompressedRailsBuffer();
	if (src == nullptr)
		return;

	const size_t size = editor->GetCompressedRailsBufferSize();
	m_baseline = static_cast<unsigned char *>(std::malloc(size));
	if (m_baseline == nullptr)
		return;
	std::memcpy(m_baseline, src, size);
	m_baseline_size = static_cast<int>(size);
	m_baseline_valid = true;
}

void RailsPanel::RestoreBaseline()
{
	if (!m_baseline_valid || m_baseline == nullptr)
		return;
	Obj::CRailEditorComponent *editor = GetEditor();
	if (editor == nullptr)
		return;
	editor->SetCompressedRailsBuffer(m_baseline);
	editor->InitUsingCompressedRailsBuffer();
}

void RailsPanel::RefreshList()
{
	m_num_rails = 0;
	m_num_points = 0;
	m_selected_rail = -1;

	Obj::CRailEditorComponent *editor = GetEditor();
	if (editor == nullptr)
		return;

	// Dump rails into a throwaway CStruct — the engine's own serialiser gives
	// us {CreatedRails: [{Points: [{Pos, HasPost}...]}...]} without needing
	// access to the private mp_edited_rails list.
	Script::CStruct info;
	editor->WriteIntoStructure(&info);

	Script::CArray *p_rails = nullptr;
	if (!info.GetArray(Crc::ConstCRC("CreatedRails"), &p_rails) || p_rails == nullptr)
		return;

	const size_t num_rails = p_rails->GetSize();
	for (size_t ri = 0; ri < num_rails && m_num_rails < kMaxRails; ++ri)
	{
		Script::CStruct *p_rail = p_rails->GetStructure(ri);
		if (p_rail == nullptr)
			continue;

		Script::CArray *p_points = nullptr;
		p_rail->GetArray(Crc::ConstCRC("Points"), &p_points);

		RailRow &r = m_rails[m_num_rails++];
		r.first_point_index = m_num_points;
		r.num_points = 0;

		if (p_points == nullptr)
			continue;

		const size_t num_points = p_points->GetSize();
		for (size_t pi = 0; pi < num_points && m_num_points < kMaxPoints; ++pi)
		{
			Script::CStruct *p_point = p_points->GetStructure(pi);
			if (p_point == nullptr)
				continue;

			PointRow &pr = m_points[m_num_points++];
			Mth::Vector pos(0.0f, 0.0f, 0.0f);
			if (p_point->GetVector(Crc::ConstCRC("Pos"), &pos))
			{
				pr.pos[0] = pos.GetX();
				pr.pos[1] = pos.GetY();
				pr.pos[2] = pos.GetZ();
			}
			else
			{
				pr.pos[0] = pr.pos[1] = pr.pos[2] = 0.0f;
			}
			pr.has_post = p_point->ContainsFlag(Crc::ConstCRC("HasPost"));
			++r.num_points;
		}
	}
}

void RailsPanel::DrawListSection()
{
	ImGui::SeparatorText("Rails");

	if (ImGui::Button("Refresh"))
		RefreshList();
	ImGui::SameLine();
	ImGui::TextDisabled("%d rails / %d points (caps %d / %d)",
		m_num_rails, m_num_points, kMaxRails, kMaxPoints);

	if (ImGui::BeginListBox("##rails_list", ImVec2(-1.0f, 160.0f)))
	{
		for (int i = 0; i < m_num_rails; ++i)
		{
			bool selected = (i == m_selected_rail);
			char row[64];
			std::snprintf(row, sizeof(row), "Rail %d  (%d pts)", i, m_rails[i].num_points);
			if (ImGui::Selectable(row, selected))
				m_selected_rail = i;
		}
		ImGui::EndListBox();
	}
}

void RailsPanel::DrawSelectedSection()
{
	ImGui::SeparatorText("Selected Rail");

	if (m_selected_rail < 0 || m_selected_rail >= m_num_rails)
	{
		ImGui::TextDisabled("no rail selected");
		return;
	}

	const RailRow &r = m_rails[m_selected_rail];
	ImGui::Text("points: %d", r.num_points);

	if (ImGui::BeginChild("##rail_points", ImVec2(-1.0f, 160.0f), ImGuiChildFlags_Border))
	{
		for (int i = 0; i < r.num_points; ++i)
		{
			const int idx = r.first_point_index + i;
			if (idx < 0 || idx >= m_num_points)
				break;
			const PointRow &p = m_points[idx];
			ImGui::Text("  %02d  (%.1f, %.1f, %.1f)%s",
				i, p.pos[0], p.pos[1], p.pos[2], p.has_post ? "  post" : "");
		}
	}
	ImGui::EndChild();

	ImGui::TextDisabled("per-rail edit deferred — engine keeps mp_edited_rails private");
}

void RailsPanel::DrawActionsSection()
{
	ImGui::SeparatorText("Actions");

	Obj::CRailEditorComponent *editor = GetEditor();
	const bool have_editor = (editor != nullptr);

	ImGui::BeginDisabled(!have_editor);
	if (ImGui::Button("New Empty Rail"))
	{
		editor->NewRail();
		RefreshList();
	}
	ImGui::SameLine();
	if (ImGui::Button("Clear All Rails"))
		m_confirm_clear_open = true;
	ImGui::SameLine();
	if (ImGui::Button("Reset to Baseline"))
		RestoreBaseline();
	ImGui::EndDisabled();

	if (!have_editor)
		ImGui::TextDisabled("RailEditor object not in scene — actions disabled");
	else if (!m_baseline_valid)
		ImGui::TextDisabled("baseline not captured yet (first refresh snapshots it)");
	else
		ImGui::TextDisabled("baseline: %d bytes", m_baseline_size);

	if (m_confirm_clear_open)
	{
		ImGui::OpenPopup("Confirm Clear Rails");
		m_confirm_clear_open = false;
	}
	if (ImGui::BeginPopupModal("Confirm Clear Rails", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::Text("Wipe every edited rail in the scene?");
		ImGui::TextDisabled("Baseline restore still works afterwards.");
		if (ImGui::Button("Confirm") && editor != nullptr)
		{
			editor->Clear();
			RefreshList();
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel"))
			ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
	}
}

void RailsPanel::ExportSnapshot()
{
	Obj::CRailEditorComponent *editor = GetEditor();
	if (editor == nullptr) return;
	uint8 *buf = editor->GetCompressedRailsBuffer();
	if (buf == nullptr) return;
	const std::size_t size = editor->GetCompressedRailsBufferSize();
	EditorPersistence::WriteRails(buf, size);
}

void RailsPanel::ImportSnapshot()
{
	Obj::CRailEditorComponent *editor = GetEditor();
	if (editor == nullptr) return;

	const std::size_t cap = editor->GetCompressedRailsBufferSize();
	std::vector<unsigned char> tmp(cap);
	std::size_t size = 0;
	if (!EditorPersistence::ReadRails(tmp.data(), cap, &size) || size == 0)
		return;

	editor->SetCompressedRailsBuffer(tmp.data());
	editor->InitUsingCompressedRailsBuffer();
	RefreshList();
}

static void DrawRailsSnapshotSection(RailsPanel *self)
{
	using namespace EditorPersistence;

	ImGui::SeparatorText("Snapshot");
	ImGui::TextDisabled("writes userdata/editor/rails.bin");

	if (ImGui::Button("Export Snapshot"))
		self->ExportSnapshot();
	ImGui::SameLine();
	if (ImGui::Button("Import Snapshot"))
		self->ImportSnapshot();
	ImGui::SameLine();
	ImGui::BeginDisabled(!RailsSnapshotExists());
	if (ImGui::Button("Discard"))
		DiscardRails();
	ImGui::EndDisabled();

	if (RailsSnapshotExists())
		ImGui::TextDisabled("snapshot present");
	else
		ImGui::TextDisabled("no snapshot");
}

void RailsPanel::Draw()
{
	if (!ImGui::Begin(GetName(), GetOpenFlag()))
	{
		ImGui::End();
		return;
	}

	// Baseline on first draw — only fires once the RailEditor object is live.
	CaptureBaseline();

	DrawListSection();
	DrawSelectedSection();
	DrawActionsSection();
	DrawRailsSnapshotSection(this);

	ImGui::End();
}

} // namespace Debug

#endif // DEBUG_IMGUI
