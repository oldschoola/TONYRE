#include "GoalsPanel.h"

#if defined(DEBUG_IMGUI)

#include "imgui.h"

#include <cstdio>
#include <cstring>

#include <Core/Defines.h>
#include <Core/math.h>

#include <Gel/Scripting/script.h>
#include <Gel/Scripting/struct.h>
#include <Gel/Scripting/checksum.h>
#include <Gel/Scripting/symboltable.h>

#include <Sk/Modules/Skate/GoalManager.h>
#include <Sk/Modules/Skate/Goal.h>

#include "../EditorPersistence.h"

namespace Debug
{

namespace
{

// Curated starter types drawn from the goal family — refine later by
// grepping the live `.qb` goal_descs. These map directly to the `type`
// checksum field the engine inspects in CGoal::Init().
const char *	kTypeChoices[] = {
	"score",
	"highcombo",
	"collect",
	"trickspot",
	"tour",
	"horse",
	"race",
	"minigame",
};
constexpr int kNumTypeChoices = static_cast<int>(sizeof(kTypeChoices) / sizeof(kTypeChoices[0]));

// Best-effort readable label for a goal. Prefers the `name` string, falls
// back to reversing the goal id, and finally gives up with the hex id.
void FormatGoalLabel(char *out, size_t out_size, uint32 id, Script::CStruct *params)
{
	if (params != nullptr)
	{
		const char *p_text = nullptr;
		if (params->GetString(Crc::ConstCRC("name"), &p_text) && p_text != nullptr && p_text[0] != '\0')
		{
			std::snprintf(out, out_size, "%s", p_text);
			return;
		}
		if (params->GetText(Crc::ConstCRC("display_name"), &p_text) && p_text != nullptr && p_text[0] != '\0')
		{
			std::snprintf(out, out_size, "%s", p_text);
			return;
		}
	}

	const char *reverse = Script::FindChecksumName(id);
	if (reverse != nullptr && reverse[0] != '\0')
	{
		std::snprintf(out, out_size, "%s", reverse);
		return;
	}

	std::snprintf(out, out_size, "0x%08X", id);
}

} // anon namespace

void GoalsPanel::ResetToDefaults()
{
	m_selected_row = -1;
	m_selected_id = 0;
	m_new_id_name[0] = '\0';
	m_new_type_choice = 0;
	m_new_score = 1000;
	m_new_time_limit = 60;
	m_new_pos[0] = m_new_pos[1] = m_new_pos[2] = 0.0f;
	m_confirm_remove_open = false;
	m_confirm_win_open = false;
}

void GoalsPanel::RefreshList()
{
	m_num_entries = 0;

	Game::CGoalManager *mgr = Game::GetGoalManager();
	if (mgr == nullptr)
		return;

	const int total = mgr->GetNumGoals();
	for (int i = 0; i < total && m_num_entries < kMaxListRows; ++i)
	{
		Game::CGoal *goal = mgr->GetGoalByIndex(i);
		if (goal == nullptr)
			continue;

		GoalEntry &e = m_entries[m_num_entries++];
		e.id = goal->GetGoalId();
		e.active = goal->IsActive();
		e.won = goal->HasWonGoal();
		FormatGoalLabel(e.label, sizeof(e.label), e.id, goal->GetParams());
	}

	// Re-sync the selected row after the refresh — ids may have shifted
	// if goals were added / removed.
	m_selected_row = -1;
	for (int i = 0; i < m_num_entries; ++i)
	{
		if (m_entries[i].id == m_selected_id)
		{
			m_selected_row = i;
			break;
		}
	}
	if (m_selected_row < 0)
		m_selected_id = 0;
}

void GoalsPanel::ActivateSelected()
{
	if (m_selected_id == 0)
		return;
	Game::CGoalManager *mgr = Game::GetGoalManager();
	if (mgr == nullptr)
		return;
	mgr->ActivateGoal(m_selected_id, /*dontAssert*/ true);
}

void GoalsPanel::DeactivateSelected()
{
	if (m_selected_id == 0)
		return;
	Game::CGoalManager *mgr = Game::GetGoalManager();
	if (mgr == nullptr)
		return;
	mgr->DeactivateGoal(m_selected_id);
}

void GoalsPanel::WinSelected()
{
	if (m_selected_id == 0)
		return;
	Game::CGoalManager *mgr = Game::GetGoalManager();
	if (mgr == nullptr)
		return;
	mgr->WinGoal(m_selected_id);
}

void GoalsPanel::LoseSelected()
{
	if (m_selected_id == 0)
		return;
	Game::CGoalManager *mgr = Game::GetGoalManager();
	if (mgr == nullptr)
		return;
	mgr->LoseGoal(m_selected_id);
}

void GoalsPanel::RemoveSelected()
{
	if (m_selected_id == 0)
		return;
	Game::CGoalManager *mgr = Game::GetGoalManager();
	if (mgr == nullptr)
		return;
	mgr->RemoveGoal(m_selected_id);
	m_selected_id = 0;
	m_selected_row = -1;
}

bool GoalsPanel::TryAddGoal()
{
	if (m_new_id_name[0] == '\0')
		return false;

	Game::CGoalManager *mgr = Game::GetGoalManager();
	if (mgr == nullptr)
		return false;

	// Build a minimal params struct. Engine-defined defaults cover the rest;
	// if a goal type requires extra fields the AddGoal call will log / assert
	// and the user can refine the form.
	Script::CStruct params;
	params.AddChecksum(Crc::ConstCRC("name"), Script::GenerateCRC(m_new_id_name));
	params.AddChecksum(Crc::ConstCRC("type"), Script::GenerateCRC(kTypeChoices[m_new_type_choice]));
	params.AddInteger(Crc::ConstCRC("score"), m_new_score);
	params.AddInteger(Crc::ConstCRC("time_limit"), m_new_time_limit);
	params.AddVector(Crc::ConstCRC("pos"), m_new_pos[0], m_new_pos[1], m_new_pos[2]);

	const uint32 goal_id = Script::GenerateCRC(m_new_id_name);
	return mgr->AddGoal(goal_id, &params);
}

void GoalsPanel::DrawListSection()
{
	ImGui::SeparatorText("Goals");

	if (ImGui::Button("Refresh"))
		RefreshList();
	ImGui::SameLine();
	ImGui::TextDisabled("%d listed (cap %d)", m_num_entries, kMaxListRows);

	if (ImGui::BeginListBox("##goal_list", ImVec2(-1.0f, 160.0f)))
	{
		for (int i = 0; i < m_num_entries; ++i)
		{
			const GoalEntry &e = m_entries[i];
			bool selected = (i == m_selected_row);

			char row[96];
			std::snprintf(row, sizeof(row), "%s  [%s%s]",
				e.label,
				e.active ? "active" : "idle",
				e.won ? ",won" : "");

			if (ImGui::Selectable(row, selected))
			{
				m_selected_row = i;
				m_selected_id = e.id;
			}
		}
		ImGui::EndListBox();
	}
}

void GoalsPanel::DrawSelectedSection()
{
	ImGui::SeparatorText("Selected");

	if (m_selected_id == 0)
	{
		ImGui::TextDisabled("no goal selected");
		return;
	}

	Game::CGoalManager *mgr = Game::GetGoalManager();
	Game::CGoal *goal = (mgr != nullptr) ? mgr->GetGoal(m_selected_id, /*assert*/ false) : nullptr;

	ImGui::Text("id:     0x%08X", m_selected_id);
	if (goal != nullptr)
	{
		ImGui::Text("active: %s", goal->IsActive() ? "yes" : "no");
		ImGui::Text("won:    %s", goal->HasWonGoal() ? "yes" : "no");

		Script::CStruct *p = goal->GetParams();
		if (p != nullptr)
		{
			uint32 type_crc = 0;
			if (p->GetChecksum(Crc::ConstCRC("type"), &type_crc))
			{
				const char *type_name = Script::FindChecksumName(type_crc);
				ImGui::Text("type:   %s", (type_name != nullptr && type_name[0] != '\0') ? type_name : "<crc>");
			}
			int score = 0;
			if (p->GetInteger(Crc::ConstCRC("score"), &score))
				ImGui::Text("score:  %d", score);
			int time_limit = 0;
			if (p->GetInteger(Crc::ConstCRC("time_limit"), &time_limit))
				ImGui::Text("time:   %d", time_limit);
			Mth::Vector pos(0.0f, 0.0f, 0.0f);
			if (p->GetVector(Crc::ConstCRC("pos"), &pos))
				ImGui::Text("pos:    (%.1f, %.1f, %.1f)", pos.GetX(), pos.GetY(), pos.GetZ());
		}
	}
	else
	{
		ImGui::TextDisabled("goal vanished — refresh");
	}

	ImGui::Separator();
	if (ImGui::Button("Activate"))
		ActivateSelected();
	ImGui::SameLine();
	if (ImGui::Button("Deactivate"))
		DeactivateSelected();
	ImGui::SameLine();
	if (ImGui::Button("Win"))
		m_confirm_win_open = true;
	ImGui::SameLine();
	if (ImGui::Button("Lose"))
		LoseSelected();
	ImGui::SameLine();
	if (ImGui::Button("Remove"))
		m_confirm_remove_open = true;

	if (m_confirm_win_open)
	{
		ImGui::OpenPopup("Confirm Win Goal");
		m_confirm_win_open = false;
	}
	if (ImGui::BeginPopupModal("Confirm Win Goal", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::Text("Force-win goal 0x%08X?", m_selected_id);
		ImGui::TextDisabled("Fires the win callback scripts.");
		if (ImGui::Button("Confirm"))
		{
			WinSelected();
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel"))
			ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
	}

	if (m_confirm_remove_open)
	{
		ImGui::OpenPopup("Confirm Remove Goal");
		m_confirm_remove_open = false;
	}
	if (ImGui::BeginPopupModal("Confirm Remove Goal", nullptr, ImGuiWindowFlags_AlwaysAutoResize))
	{
		ImGui::Text("Remove goal 0x%08X?", m_selected_id);
		ImGui::TextDisabled("Deletes the CGoal entry. Not reversible within session.");
		if (ImGui::Button("Confirm"))
		{
			RemoveSelected();
			ImGui::CloseCurrentPopup();
		}
		ImGui::SameLine();
		if (ImGui::Button("Cancel"))
			ImGui::CloseCurrentPopup();
		ImGui::EndPopup();
	}
}

void GoalsPanel::DrawAddGoalSection()
{
	ImGui::SeparatorText("Add Goal");

	ImGui::InputText("id_name", m_new_id_name, sizeof(m_new_id_name));
	ImGui::Combo("type", &m_new_type_choice, kTypeChoices, kNumTypeChoices);
	ImGui::InputInt("score", &m_new_score);
	ImGui::InputInt("time_limit", &m_new_time_limit);
	ImGui::DragFloat3("pos", m_new_pos, 1.0f, -100000.0f, 100000.0f, "%.1f");

	const bool can_add = (m_new_id_name[0] != '\0');
	ImGui::BeginDisabled(!can_add);
	if (ImGui::Button("Add Goal"))
	{
		if (TryAddGoal())
			RefreshList();
	}
	ImGui::EndDisabled();
	ImGui::SameLine();
	ImGui::TextDisabled("minimal params — engine fills defaults");
}

void GoalsPanel::ExportSnapshot()
{
	Game::CGoalManager *mgr = Game::GetGoalManager();
	if (mgr == nullptr) return;

	EditorPersistence::GoalRecord rows[kMaxListRows];
	int count = 0;

	const int total = mgr->GetNumGoals();
	for (int i = 0; i < total && count < kMaxListRows; ++i)
	{
		Game::CGoal *g = mgr->GetGoalByIndex(i);
		if (g == nullptr) continue;

		EditorPersistence::GoalRecord &r = rows[count++];
		std::memset(&r, 0, sizeof(r));
		r.id = g->GetGoalId();

		Script::CStruct *p = g->GetParams();
		if (p != nullptr)
		{
			p->GetChecksum(Crc::ConstCRC("type"), &r.type_crc);
			p->GetInteger(Crc::ConstCRC("score"), &r.score);
			p->GetInteger(Crc::ConstCRC("time_limit"), &r.time_limit);
			Mth::Vector pos(0.0f, 0.0f, 0.0f);
			if (p->GetVector(Crc::ConstCRC("pos"), &pos))
			{
				r.pos[0] = pos.GetX();
				r.pos[1] = pos.GetY();
				r.pos[2] = pos.GetZ();
			}
			const char *name = nullptr;
			if (p->GetString(Crc::ConstCRC("name"), &name) && name != nullptr)
				std::snprintf(r.name, sizeof(r.name), "%s", name);
		}

		if (r.name[0] == '\0')
		{
			const char *sym = Script::FindChecksumName(r.id);
			std::snprintf(r.name, sizeof(r.name), "%s",
				(sym != nullptr && sym[0] != '\0') ? sym : "unknown_goal");
		}
	}

	EditorPersistence::WriteGoals(rows, count);
}

void GoalsPanel::ImportSnapshot()
{
	Game::CGoalManager *mgr = Game::GetGoalManager();
	if (mgr == nullptr) return;

	EditorPersistence::GoalRecord rows[kMaxListRows];
	int n = 0;
	if (!EditorPersistence::ReadGoals(rows, kMaxListRows, &n))
		return;

	for (int i = 0; i < n; ++i)
	{
		const EditorPersistence::GoalRecord &r = rows[i];
		Script::CStruct params;
		// Use the stored name crc when we have it; otherwise regenerate from
		// the text name so the two stay aligned if the snapshot was edited.
		const uint32 name_crc = (r.id != 0) ? r.id : Script::GenerateCRC(r.name);
		params.AddChecksum(Crc::ConstCRC("name"), name_crc);
		if (r.type_crc != 0)
			params.AddChecksum(Crc::ConstCRC("type"), r.type_crc);
		if (r.score != 0)
			params.AddInteger(Crc::ConstCRC("score"), r.score);
		if (r.time_limit != 0)
			params.AddInteger(Crc::ConstCRC("time_limit"), r.time_limit);
		params.AddVector(Crc::ConstCRC("pos"), r.pos[0], r.pos[1], r.pos[2]);
		mgr->AddGoal(name_crc, &params);
	}
	RefreshList();
}

static void DrawGoalsSnapshotSection(GoalsPanel *self)
{
	using namespace EditorPersistence;

	ImGui::SeparatorText("Snapshot");
	ImGui::TextDisabled("writes userdata/editor/goals.json");

	if (ImGui::Button("Export Snapshot"))
		self->ExportSnapshot();
	ImGui::SameLine();
	if (ImGui::Button("Import Snapshot"))
		self->ImportSnapshot();
	ImGui::SameLine();
	ImGui::BeginDisabled(!GoalsSnapshotExists());
	if (ImGui::Button("Discard"))
		DiscardGoals();
	ImGui::EndDisabled();

	if (GoalsSnapshotExists())
		ImGui::TextDisabled("snapshot present");
	else
		ImGui::TextDisabled("no snapshot");
}

void GoalsPanel::Draw()
{
	if (!ImGui::Begin(GetName(), GetOpenFlag()))
	{
		ImGui::End();
		return;
	}

	DrawListSection();
	DrawSelectedSection();
	DrawAddGoalSection();
	DrawGoalsSnapshotSection(this);

	ImGui::End();
}

} // namespace Debug

#endif // DEBUG_IMGUI
