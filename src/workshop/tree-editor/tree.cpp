module;
#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

module phoenix.workshop.tree;
import phoenix.workshop.authoring_core;
import phoenix.content.documents;
import phoenix.workshop.authoring;

namespace phoenix
{
struct TreeEditor::State : AuthoringControls
{
	std::string tree, inspected_controller, local_path, selected_state_id;
	bool database = false;
	WorkshopTarget local_target;
	std::set<std::string> collapsed;
	float zoom = 1.f;
	bool fit_tree = false;
	std::string reparent_source, reparent_target;
	Json vocabulary = Json::object(), feedback = Json::object();
	int selected_state = -1;
	void resolve_selected_state(const Json &definition)
	{
		selected_state = -1;
		if (definition.contains("states"))
			for (std::size_t i = 0; i < definition.at("states").size(); ++i)
				if (definition.at("states")[i].at("id") == selected_state_id)
					selected_state = static_cast<int>(i);
	}
	std::vector<std::string> vocabulary_ids(const std::string &context, const char *kind) const;
	std::string authoring_context(const Json &definition) const;
	Json action_signature(const std::string &context, const std::string &name) const;
	void binding(Json &parent, const char *key, const Json &definition, const std::vector<std::string> &allowed = {});
	void binding_map(Json &parent, const char *key, const Json &definition,
	                 const std::vector<std::string> &suggested = {}, const std::vector<std::string> &allowed = {});
	void tasks(Json &state, const char *phase, Json &definition, WorkshopDocument &document);
	void tree_graph(const Json &definition, std::string &selection);
	void state_inspector(Json &state, Json &definition, WorkshopDocument &document, bool patch = false,
	                     Json *owner = nullptr);
	void trees(WorkshopDocument &document, bool inspector, std::string &selection);
	void record_bindings(Json &tree, WorkshopDocument &document, const Json &resolved);
	void controller_bindings(Json &tree, WorkshopDocument &document, const Json &resolved);
	void controller_palette(const Json &tree, WorkshopDocument &document);
};

std::vector<std::string> TreeEditor::State::vocabulary_ids(const std::string &context, const char *kind) const
{
	if (vocabulary.contains(context) && vocabulary.at(context).contains(kind))
		return ids(vocabulary.at(context).at(kind));
	return {};
}

std::string TreeEditor::State::authoring_context(const Json &definition) const
{
	return definition.value("interdiction", false) ? "interdiction"
	                                               : definition.value("context", std::string("system"));
}

Json TreeEditor::State::action_signature(const std::string &context, const std::string &name) const
{
	if (vocabulary.contains(context) && vocabulary.at(context).contains("actions"))
		for (const auto &action : vocabulary.at(context).at("actions"))
			if (action.at("id") == name)
				return action;
	return Json::object();
}

void TreeEditor::State::binding(Json &parent, const char *key, const Json &definition,
                                const std::vector<std::string> &allowed)
{
	ScopedId id_scope_11(key);
	text(key);
	const Json control_context{{"field", key}, {"allowed", allowed}};
	edit_shared_control(label("source").c_str(), parent, "binding.source", control_context);
	const auto mode = workshop_control("binding.source", parent, control_context).at("value").get<std::string>();

	if (mode == "integer")
		integer(parent, key);
	else if (mode == "boolean")
		boolean(parent, key);
	else if (mode == "text")
		string(parent, key);
	else if (mode == "reference")
	{
		if (parent.contains(key))
		{
			edit_registered(parent[key], "tree-editor.tree.type");
			edit_registered(parent[key], "tree-editor.tree.id");
		}
	}
	else if (mode != "elapsed")
	{
		std::vector<std::string> options;
		if (mode == "parameter" && definition.contains("parameters"))
			options = ids(definition.at("parameters"));
		if (mode == "memory" && definition.contains("memory"))
			for (const auto &row : definition.at("memory"))
				options.push_back(row.value("name", std::string{}));
		if (mode == "observation")
			options = vocabulary_ids(authoring_context(definition), "observations");
		if (mode == "observation" && definition.contains("record_bindings"))
			for (const auto &field : definition.at("record_bindings"))
				options.push_back(field.at("name").get<std::string>());
		if (mode == "observation" && definition.contains("summary_bindings"))
			for (const auto &field : definition.at("summary_bindings"))
				options.push_back(field.at("name").get<std::string>());
		if (mode == "argument")
			options = {"amount", "flag", "detail"};
		if (mode == "evidence" && definition.contains("evidence_fields"))
			options = definition.at("evidence_fields").get<std::vector<std::string>>();
		if (mode == "intent")
			options = vocabulary_ids(authoring_context(definition), "intents");
		if (parent.contains(key))
		{
			if (!options.empty())
				choice(parent[key], mode.c_str(), options);
			else
				string(parent[key], mode.c_str());
		}
	}
}

void TreeEditor::State::binding_map(Json &parent, const char *key, const Json &definition,
                                    const std::vector<std::string> &suggested, const std::vector<std::string> &allowed)
{
	ScopedId id_scope_12(key);
	heading(key);
	std::string remove;
	if (parent.contains(key) && parent.at(key).is_object())
		for (auto it = parent[key].begin(); it != parent[key].end(); ++it)
		{
			ScopedId id_scope_13(it.key().c_str());
			binding(parent[key], it.key().c_str(), definition, allowed);
			if (button("Remove binding"))
				remove = it.key();
		}
	if (!remove.empty())
		parent[key].erase(remove);
	static std::map<std::string, std::string> names;
	const auto scope = std::to_string(ImGui::GetID(key));
	if (suggested.empty())
		ImGui::InputText(label("Binding name").c_str(), &names[scope]);
	else
	{
		Json pick{{"Binding name", names[scope]}};
		if (choice(pick, "Binding name", suggested))
			names[scope] = pick.at("Binding name").get<std::string>();
	}
	if (button("Add binding") && !names[scope].empty())
	{
		(void)workshop_custom_action(parent, "tree.binding.add", {{"field", key}, {"name", names[scope]}});
		names[scope].clear();
	}
}

void TreeEditor::State::tasks(Json &state, const char *phase, Json &definition, WorkshopDocument &document)
{
	ScopedId id_scope_14(phase);
	heading(phase);
	for (const char *kind : {"action", "intent", "memory", "call", "attempt"})
	{
		if (std::string(kind) == "attempt" && authoring_context(definition) != "controller")
			continue;
		ScopedId id_scope_15(kind);
		if (button(kind))
		{
			if (!state.contains(phase))
				(void)workshop_custom_action(state, "field.ensure", {{"field", phase}, {"type", "array"}});
			state[phase].push_back(workshop_tree_task(kind));
		}

		ImGui::SameLine();
	}
	ImGui::NewLine();
	int remove = -1;
	if (state.contains(phase))
		for (std::size_t i = 0; i < state.at(phase).size(); ++i)
		{
			auto &task = state[phase][i];
			ScopedId id_scope_16(static_cast<int>(i));
			ImGui::Text("%zu", i + 1);
			if (task.contains("action"))
			{
				const auto actions = vocabulary_ids(authoring_context(definition), "actions");
				if (!actions.empty())
					choice(task, "action", actions);
				else
				{
					edit_registered(task, "tree-editor.tree.action");
					text("Action palette awaits the production context vocabulary. Verification rejects "
					     "unsupported names.");
				}
				edit_registered(task, "tree-editor.tree.once");
				const auto signature =
				    action_signature(authoring_context(definition), task.at("action").get<std::string>());
				if (signature.contains("arguments"))
				{
					text("Expected arguments in order");
					for (const auto &argument : signature.at("arguments"))
						ImGui::Text("%s: %s", argument.value("name", std::string{}).c_str(),
						            argument.value("type", std::string{}).c_str());
					if (button("Use action signature"))
					{
						task["arguments"] = workshop_action_arguments(signature);
					}
				}
				int remove_argument = -1;
				for (std::size_t n = 0; n < task.at("arguments").size(); ++n)
				{
					ScopedId id_scope_17(static_cast<int>(n));
					Json wrapper{{"argument", task.at("arguments")[n]}};
					binding(wrapper, "argument", definition);
					if (wrapper.at("argument") != task.at("arguments")[n])
						task["arguments"][n] = wrapper.at("argument");
					if (button("Remove argument"))
						remove_argument = static_cast<int>(n);
				}
				if (remove_argument >= 0)
					task["arguments"].erase(task["arguments"].begin() + remove_argument);
				if (button("Add argument"))
					task["arguments"].push_back(0);
			}
			else if (task.contains("attempt"))
			{
				text("Branches run in order. A refused command stops that branch and tries the next in the same tick. "
				     "Earlier successful commands remain staged.");
				std::vector<std::string> outcomes;
				for (const auto &memory : definition.value("memory", Json::array()))
					if (memory.value("type", std::string{}) == "boolean")
						outcomes.push_back(memory.at("name").get<std::string>());
				choice(task, "result", outcomes, true);
				records(task, "attempt", workshop_record("tree-editor.tree.attempt"),
				        [&](Json &branch)
				        {
					        records(branch, "conditions", workshop_record("tree-editor.tree.conditions"),
					                [&](Json &condition)
					                {
						                binding(condition, "left", definition);
						                edit_registered(condition, "tree-editor.tree.op");
						                binding(condition, "right", definition);
					                });
					        tasks(branch, "tasks", definition, document);
				        });
			}
			else if (task.contains("call"))
			{
				choice(task, "call", document.definition_choices(WorkshopDefinitionKind::Tree));
				binding_map(task, "parameters", definition, {},
				            {"integer", "boolean", "text", "reference", "parameter"});
			}
			else if (task.contains("memory"))
			{
				std::vector<std::string> memory;
				if (definition.contains("memory"))
					for (const auto &row : definition.at("memory"))
						memory.push_back(row.value("name", std::string{}));
				choice(task, "memory", memory);
				binding(task, "value", definition);
			}
			else if (task.contains("intent"))
			{
				const auto intents = vocabulary_ids(definition.value("context", std::string("system")), "intents");
				if (!intents.empty())
					choice(task, "intent", intents);
				else
					edit_registered(task, "tree-editor.tree.intent");
				binding(task, "value", definition);
			}
			else
				text("Legacy timed output retained. Edit its existing controller inspector or convert the tree "
				     "explicitly.");
			if (button("Earlier") && i > 0)
				(void)workshop_custom_action(state[phase], "array.move", {{"from", i}, {"to", i - 1}});
			ImGui::SameLine();
			if (button("Later") && i + 1 < state.at(phase).size())
				(void)workshop_custom_action(state[phase], "array.move", {{"from", i}, {"to", i + 1}});
			ImGui::SameLine();
			if (button("Remove task"))
				remove = static_cast<int>(i);
			ImGui::Separator();
		}
	if (remove >= 0)
		state[phase].erase(state[phase].begin() + remove);
}

void TreeEditor::State::tree_graph(const Json &definition, std::string &selection)
{
	if (!definition.contains("states"))
		return;
	const auto &states = definition.at("states");
	if (button("Fit tree"))
	{
		zoom = 1;
		fit_tree = true;
	}
	ImGui::SameLine();
	ImGui::SliderFloat(label("Tree zoom").c_str(), &zoom, .4f, 2.f);
	ScopedChild child("state-graph", {0, 0}, ImGuiWindowFlags_HorizontalScrollbar);
	if (fit_tree)
	{
		ImGui::SetScrollX(0);
		ImGui::SetScrollY(0);
		fit_tree = false;
	}
	if (ImGui::IsWindowHovered() && ImGui::IsMouseDragging(2))
	{
		ImGui::SetScrollX(ImGui::GetScrollX() - ImGui::GetIO().MouseDelta.x);
		ImGui::SetScrollY(ImGui::GetScrollY() - ImGui::GetIO().MouseDelta.y);
	}
	const auto origin = ImGui::GetCursorScreenPos();
	std::map<std::string, ImVec2> positions;
	float width = 500;
	const Json *runtime = &feedback;
	if (feedback.contains("controllers"))
	{
		runtime = nullptr;
		for (const auto &controller : feedback.at("controllers"))
			if (identity(controller) == inspected_controller)
				runtime = &controller;
		if (runtime && !runtime->contains("tree"))
			runtime = nullptr;
	}
	if (runtime && runtime->contains("tree") && runtime->at("tree") != definition.value("id", std::string{}))
		runtime = nullptr;
	std::size_t visible_row = 0;
	std::set<std::string> visited;
	std::function<void(const Json &, std::size_t)> place;
	place = [&](const Json &state, std::size_t depth)
	{
		const auto id = state.value("id", std::string{});
		if (!visited.insert(id).second)
			return;
		positions[id] = {origin.x + std::min<std::size_t>(depth, 64) * 190.f * zoom,
		                 origin.y + visible_row++ * 65.f * zoom};
		width = std::max(width, std::min<std::size_t>(depth, 64) * 190.f * zoom + 180.f * zoom);
		if (collapsed.contains(id))
			return;
		for (const auto &child : states)
			if (child.value("parent", std::string{}) == id)
				place(child, depth + 1);
	};
	for (const auto &state : states)
		if (state.value("parent", std::string{}).empty())
			place(state, 0);
	// Orphans remain discoverable while a draft is being repaired.
	for (const auto &state : states)
		if (!visited.contains(state.value("id", std::string{})) && !state.contains("parent"))
			place(state, 0);
	auto *draw = ImGui::GetWindowDrawList();
	for (const auto &state : states)
	{
		const auto id = state.value("id", std::string{});
		if (!positions.contains(id))
			continue;
		const auto start = positions.at(id);
		const auto parent = state.value("parent", std::string{});
		if (positions.contains(parent))
			draw->AddLine({positions.at(parent).x + 85 * zoom, positions.at(parent).y + 45 * zoom}, start,
			              IM_COL32(140, 180, 200, 255), 2);
		if (selected_state >= 0 && selected_state < static_cast<int>(states.size()) &&
		    states[selected_state].at("id") == id && state.contains("transitions"))
			for (const auto &edge : state.at("transitions"))
			{
				const auto target = edge.value("target", std::string{});
				if (!positions.contains(target))
					continue;
				const auto end = positions.at(target);
				draw->AddBezierCubic({start.x + 170, start.y + 25}, {start.x + 205, start.y + 25},
				                     {end.x - 35, end.y + 25}, {end.x, end.y + 25},
				                     ImGui::GetColorU32(ImGuiCol_PlotLines), 2);
				const ImVec2 tip{end.x, end.y + 25};
				draw->AddTriangleFilled(tip, {tip.x - 8, tip.y - 5}, {tip.x - 8, tip.y + 5},
				                        ImGui::GetColorU32(ImGuiCol_PlotLines));
				draw->AddText({(start.x + end.x) * .5f, (start.y + end.y) * .5f}, ImGui::GetColorU32(ImGuiCol_Text),
				              edge.value("reason", std::string{}).c_str());
			}
	}
	for (std::size_t i = 0; i < states.size(); ++i)
	{
		const auto id = states[i].value("id", std::to_string(i));
		if (!positions.contains(id))
			continue;
		ImGui::SetCursorScreenPos(positions.at(id));
		ScopedId id_scope_18(static_cast<int>(i));
		if (runtime && runtime->contains("active_states") && runtime->at("active_states").is_array() &&
		    std::find(runtime->at("active_states").begin(), runtime->at("active_states").end(), Json(id)) !=
		        runtime->at("active_states").end())
		{
			const auto position = positions.at(id);
			draw->AddRect({position.x - 2, position.y - 2}, {position.x + 174, position.y + 48},
			              ImGui::GetColorU32(ImGuiCol_CheckMark), 2, 0, 3);
		}
		if (ImGui::Selectable((id + "###node").c_str(), selected_state == static_cast<int>(i),
		                      ImGuiSelectableFlags_AllowDoubleClick, {170 * zoom, 45 * zoom}))
		{
			if (ImGui::IsMouseDoubleClicked(0))
			{
				if (collapsed.contains(id))
					collapsed.erase(id);
				else
					collapsed.insert(id);
			}
			selected_state = static_cast<int>(i);
			selected_state_id = id;
			selection = "State trees";
		}
		if (ImGui::BeginDragDropSource())
		{
			ImGui::SetDragDropPayload("PHOENIX_STATE", id.c_str(), id.size() + 1);
			text(id.c_str());
			ImGui::EndDragDropSource();
		}
		if (ImGui::BeginDragDropTarget())
		{
			if (const auto *payload = ImGui::AcceptDragDropPayload("PHOENIX_STATE"))
			{
				reparent_source = static_cast<const char *>(payload->Data);
				reparent_target = id;
			}
			ImGui::EndDragDropTarget();
		}
	}
	ImGui::SetCursorScreenPos(origin);
	ImGui::Dummy({width, std::max(260.f, visible_row * 65.f * zoom)});
}

void TreeEditor::State::state_inspector(Json &state, Json &definition, WorkshopDocument &document, bool patch,
                                        Json *owner)
{
	const auto states = definition.contains("states") ? ids(definition.at("states")) : std::vector<std::string>{};
	if (patch)
		choice(state, "state", states);
	else
	{
		const auto old = state.value("id", std::string{});
		if (edit_registered(state, "tree-editor.tree.id.2") && owner)
		{
			const auto name = state.at("id").get<std::string>();
			if (name.empty() || std::any_of(owner->at("states").begin(), owner->at("states").end(),
			                                [&](const Json &row)
			                                {
				                                return &row != &state && row.at("id") == name;
			                                }))
				state["id"] = old;
			else
			{
				if (selected_state_id == old)
					selected_state_id = name;
				state["id"] = old;
				auto renamed = *owner;
				workshop_tree_rename(renamed, old, name);
				// Preserve the active state's address while updating repaired rows.
				if (renamed.contains("initial"))
					(*owner)["initial"] = renamed.at("initial");
				for (std::size_t i = 0; i < (*owner)["states"].size(); ++i)
					(*owner)["states"][i] = renamed.at("states")[i];
			}
		}
	}
	const auto previous_parent = state.value("parent", Json());
	if (choice(state, "parent", states, true) && owner)
	{
		const auto desired = state.value("parent", std::string{}), id = state.at("id").get<std::string>();
		if (previous_parent.is_null())
			state.erase("parent");
		else
			state["parent"] = previous_parent;
		try
		{
			WorkshopDocument::reparent_tree_state(*owner, id, desired);
		}
		catch (const std::exception &error)
		{
			text(error.what());
		}
		return;
	}
	choice(state, "initial", states, true);
	for (const char *phase : {"entry", "running", "exit"})
		tasks(state, phase, definition, document);
	records(state, "transitions", workshop_record("tree-editor.tree.transitions"),
	        [&](Json &edge)
	        {
		        choice(edge, "target", states);
		        edit_registered(edge, "tree-editor.tree.reason");
		        records(edge, "conditions", workshop_record("tree-editor.tree.conditions"),
		                [&](Json &condition)
		                {
			                binding(condition, "left", definition);
			                if (edit_registered(condition, "tree-editor.tree.op.2") && condition.at("op") == "present")
				                condition.erase("right");
			                if (condition.value("op", std::string("eq")) != "present")
				                binding(condition, "right", definition);
		                });
	        });
}

void TreeEditor::State::trees(WorkshopDocument &document, bool inspector, std::string &selection)
{
	auto &defs = document.definitions();
	if (!inspector && database)
	{
		static std::string derive_base;
		Json derivation{{"Base tree", derive_base}};
		if (choice(derivation, "Base tree", document.definition_choices(WorkshopDefinitionKind::Tree)))
			derive_base = derivation.at("Base tree").get<std::string>();
		if (button("Derive tree from base") && !derive_base.empty())
		{
			tree = fresh(defs["trees"], "tree");
			(void)workshop_apply(document, "create", {{"kind", "trees"}, {"id", tree}, {"parent", derive_base}});
			selected_state = -1;
			selected_state_id.clear();
			selection = "State trees";
		}
		if (button("New hierarchical tree"))
		{
			selected_state = -1;
			selected_state_id.clear();
			selection = "State trees";
			tree = fresh(defs["trees"], "tree");
			(void)workshop_apply(document, "create", {{"kind", "trees"}, {"id", tree}});
		}
		for (const auto &definition : defs.value("trees", Json::array()))
			if (ImGui::Selectable(definition.at("id").get_ref<const std::string &>().c_str(),
			                      local_path.empty() && definition.at("id") == tree,
			                      ImGuiSelectableFlags_AllowDoubleClick))
			{
				tree = definition.at("id");
				local_path.clear();
				selected_state = -1;
				selected_state_id.clear();
				selection = "State trees";
				if (ImGui::IsMouseDoubleClicked(0))
					ImGui::SetWindowFocus(tool_label("State tree editor").c_str());
			}
		for (const auto &[id, definition] : document.libraries().trees)
			if (!id.starts_with("phoenix."))
				if (ImGui::Selectable((id + " (dependency)").c_str(), tree == id && local_path.empty(),
				                      ImGuiSelectableFlags_AllowDoubleClick))
				{
					tree = id;
					if (ImGui::IsMouseDoubleClicked(0))
						ImGui::SetWindowFocus(tool_label("State tree editor").c_str());
					local_path.clear();
					selected_state = -1;
					selected_state_id.clear();
					selection = "State trees";
				}
		std::function<void(Json &, const std::string &)> local;
		local = [&](Json &value, const std::string &path)
		{
			if (value.is_array())
			{
				for (std::size_t i = 0; i < value.size(); ++i)
					local(value[i], path + "/" + std::to_string(i));
			}
			else if (value.is_object())
				for (auto &[key, child] : value.items())
				{
					const auto target = path + "/" + key;
					if ((key == "tree" || key == "dialogue" || key == "default_conversation" || key == "hail_tree" ||
					     key == "policy" || key == "member_policy") &&
					    ((child.is_string() && key != "policy" && key != "member_policy") ||
					     (child.is_object() && (child.contains("states") || child.contains("extends")))))
					{
						ScopedId scope(target.c_str());
						const auto caption = target + (child.is_object() ? " (local)" : " (reference)");
						if (ImGui::Selectable(caption.c_str(), local_path == target,
						                      ImGuiSelectableFlags_AllowDoubleClick))
						{
							local_path = target;
							local_target = WorkshopTarget::capture(defs, target);
							if (ImGui::IsMouseDoubleClicked(0))
								ImGui::SetWindowFocus(tool_label("State tree editor").c_str());
							tree.clear();
							selected_state = -1;
							selected_state_id.clear();
							selection = "State trees";
						}
						if (local_path == target && child.is_string())
						{
							if (button("Copy referenced tree locally"))
							{
								child = workshop_tree_localize(document, child, "controller", true);
							}
							if (button("New local tree"))
								child = workshop_tree_localize(document, child,
								                               key == "dialogue" || key == "default_conversation" ||
								                                       key == "hail_tree"
								                                   ? "conversation"
								                               : path.starts_with("/maps/") ? "scenario"
								                                                            : "controller",
								                               false);
						}
					}
					else
						local(child, target);
				}
		};
		for (const auto *kind : {"entities", "maps", "equipment", "control_widgets"})
			if (defs.contains(kind))
				local(defs[kind], std::string("/") + kind);
		if (button("Inspect selected tree") && !tree.empty())
			selection = "State trees";
		return;
	}
	Json dependency;
	Json *target = nullptr;
	if (!local_path.empty())
	{
		auto *found = local_target.resolve(defs);
		if (!found)
		{
			local_path.clear();
			selected_state = -1;
			selected_state_id.clear();
			return;
		}
		auto &local = *found;
		if (local.is_object())
			target = &local;
		else if (local.is_string())
		{
			dependency = document.preview_tree(local.get<std::string>());
			target = &dependency;
		}
	}
	else if (defs.contains("trees"))
		for (auto &row : defs["trees"])
			if (row.at("id") == tree)
				target = &row;
	if (!target && document.libraries().trees.contains(tree))
	{
		dependency = document.preview_tree(tree);
		target = &dependency;
	}
	if (!target)
		return;
	if (!inspector)
	{
		auto resolved = workshop_preview_tree(document, *target);
		resolve_selected_state(resolved);
		tree_graph(resolved, selection);
		if (!reparent_source.empty() && target->contains("states") && dependency.is_null())
		{
			try
			{
				WorkshopDocument::reparent_tree_state(*target, reparent_source, reparent_target);
			}
			catch (const std::exception &error)
			{
				text(error.what());
			}
		}
		reparent_source.clear();
		reparent_target.clear();
		return;
	}
	if (!dependency.is_null())
	{
		text("Referenced or inherited tree: create a local copy to edit.");
		structure(dependency, false);
		return;
	}
	for (auto *selected : std::vector<Json *>{target})
		if (auto &definition = *selected; true)
		{
			if (local_path.empty())
			{
				Json identity{{"id", definition.at("id")}};
				if (edit_registered(identity, "tree-editor.tree.id.2"))
				{
					try
					{
						(void)workshop_apply(
						    document, "rename",
						    {{"kind", "trees"}, {"id", definition.at("id")}, {"to", identity.at("id")}});
					}
					catch (const std::exception &error)
					{
						text(error.what());
					}
				}
				tree = definition.at("id");
			}
			else
			{
				text(local_path.c_str());
				if (button("Promote to reusable tree"))
				{
					const auto id = fresh(defs["trees"], "tree");
					(void)workshop_tree_promote(document, definition, id,
					                            local_path.ends_with("/policy") ||
					                                local_path.ends_with("/member_policy"));
					local_path.clear();
					tree = id;
					return;
				}
			}
			Json resolved = workshop_preview_tree(document, definition);
			resolve_selected_state(resolved);

			choice(definition, "extends", document.definition_choices(WorkshopDefinitionKind::Tree), true);
			if (definition.contains("extends"))
			{
				text("Inherited source");
				ImGui::TextUnformatted(definition.at("extends").get<std::string>().c_str());
			}
			Json context{{"context", resolved.at("context")}};
			if (edit_registered(context, "tree-editor.tree.context"))
				definition["context"] = context.at("context");
			if (context.at("context") == "away")
			{
				edit_registered(definition, "tree-editor.tree.boarding");
				strings(definition, "flags");
				strings(definition, "team_capabilities");
				strings(definition, "events");
			}
			if (context.at("context") == "scenario")
				edit_registered(definition, "tree-editor.tree.interdiction");
			strings(definition, "transport_capabilities");
			if (context.at("context") == "conversation")
			{
				optional_string(definition, "title_key");
				if (!definition.contains("lines") && button("Add dialogue lines"))
					definition["lines"] = workshop_seed("tree-editor.tree.lines");
				if (!definition.contains("prompts") && button("Add dialogue choices"))
					definition["prompts"] = workshop_seed("tree-editor.tree.prompts");
				structured_field(definition, "lines");
				structured_field(definition, "prompts");
			}
			edit_shared_control(label("budget").c_str(), definition, "tree.budget", {{"resolved", resolved}});
			Json initial{{"initial", resolved.value("initial", std::string{})}};
			if (choice(initial, "initial", ids(resolved.at("states"))))
				definition["initial"] = initial.at("initial");
			heading("Parameters");
			if (resolved.contains("parameters"))
				for (auto it = resolved["parameters"].begin(); it != resolved["parameters"].end(); ++it)
				{
					ScopedId identity(it.key().c_str());
					ImGui::TextUnformatted(it.key().c_str());
					const bool local =
					    definition.contains("parameters") && definition.at("parameters").contains(it.key());
					if (local)
					{
						auto &parameter = definition["parameters"][it.key()];
						edit_registered(parameter, "tree-editor.tree.type.2");
						binding(parameter, "default", resolved, {"integer", "boolean", "text", "reference"});
					}
					else
					{
						structure(it.value(), false);
						if (button("Override parameter"))
						{
							if (!definition.contains("parameters"))
								definition["parameters"] = workshop_seed("tree-editor.tree.parameters");
							definition["parameters"][it.key()] = it.value();
						}
					}
				}
			static std::string parameter_name;
			tool_prepare_action("Add parameter");
			ImGui::InputText(label("Parameter name").c_str(), &parameter_name);
			tool_action_line("Add parameter");
			if (button("Add parameter") && !parameter_name.empty())
			{
				(void)workshop_custom_action(definition, "tree.parameter.add", {{"name", parameter_name}});
				parameter_name.clear();
			}
			binding_map(definition, "arguments", resolved,
			            resolved.contains("parameters") ? ids(resolved.at("parameters")) : std::vector<std::string>{},
			            {"integer", "boolean", "text", "reference"});
			record_bindings(definition, document, resolved);
			if (definition.value("context", resolved.value("context", std::string{})) == "controller")
			{
				controller_bindings(definition, document, resolved);
				resolved = document.preview_tree(definition);
				controller_palette(resolved, document);
			}
			if (resolved.value("context", std::string{}) == "system")
				text("Legacy System tree: migrate bindings and actions to a Console Controller before use.");
			records(definition, "memory", workshop_record("tree-editor.tree.memory"),
			        [&](Json &memory)
			        {
				        edit_registered(memory, "tree-editor.tree.name");
				        edit_registered(memory, "tree-editor.tree.type.2");
				        binding(memory, "initial", resolved, {"integer", "boolean", "text", "reference", "parameter"});
			        });

			if (definition.contains("states"))
			{
				if (button("Add state"))
				{
					selected_state_id = workshop_tree_add_state(definition).get<std::string>();
					selected_state = static_cast<int>(definition.at("states").size() - 1);
				}
				if (selected_state >= 0 && selected_state < static_cast<int>(definition.at("states").size()))
				{
					ScopedId identity("selected-state");
					state_inspector(definition["states"][selected_state], resolved, document, false, &definition);
					const auto removing = definition["states"][selected_state].at("id");
					const bool referenced = !workshop_tree_removal_error(definition, removing).empty();
					ImGui::BeginDisabled(referenced);
					const bool remove = button("Remove state");
					ImGui::EndDisabled();
					if (referenced)
						text("Resolve initial, child and transition references before deleting this state.");
					if (remove)
					{
						workshop_tree_remove_state(definition, removing);
						selected_state = -1;
						selected_state_id.clear();
						selection = "State trees";
					}
				}
			}
			else if (selected_state >= 0 && selected_state < static_cast<int>(resolved.at("states").size()))
			{
				heading("Inherited state");
				structure(resolved["states"][selected_state], false);
				if (button("Override selected state"))
				{
					if (!definition.contains("overrides"))
						definition["overrides"] = workshop_seed("tree-editor.tree.overrides");
					definition["overrides"].push_back({{"state", resolved.at("states")[selected_state].at("id")}});
				}
			}
			records(definition, "overrides", workshop_record("tree-editor.tree.overrides"),
			        [&](Json &patch)
			        {
				        state_inspector(patch, resolved, document, true);
			        });
			return;
		}
}

void TreeEditor::State::record_bindings(Json &tree, WorkshopDocument &document, const Json &resolved)
{
	const auto knowledge = document.preview_knowledge();
	records(tree, "record_bindings", workshop_record("tree-editor.tree.record_bindings"),
	        [&](Json &binding)
	        {
		        edit_registered(binding, "tree-editor.tree.name");
		        edit_registered(binding, "tree-editor.tree.scope");
		        choice(binding, "schema", ids(knowledge.at("schemas")));
		        std::vector<std::string> fields;
		        for (const auto &schema : knowledge.at("schemas"))
			        if (schema.at("id") == binding.value("schema", std::string{}))
				        fields = ids(schema.at("fields"));
		        choice(binding, "field", fields);
		        edit_shared_control(label("Record subject").c_str(), binding, "record.subject",
		                            {{"tree_context", resolved.value("context", std::string("system"))}});

		        if (binding.contains("subject") && binding.at("subject").is_number_integer())
			        edit_registered(binding, "tree-editor.tree.subject");
		        text("Record type and read permissions come from the selected schema field. Ship records require "
		             "explicit publication.");
	        });
}

void TreeEditor::State::controller_bindings(Json &tree, WorkshopDocument &document, const Json &resolved)
{
	const auto editable = [&](const char *key)
	{
		ScopedId scope(key);
		if (!tree.contains(key) && resolved.contains(key))
		{
			heading(key);
			text("Inherited bindings. Override the complete group before editing it.");
			auto inherited = resolved.at(key);
			structure(inherited, false);
			if (button("Override inherited binding group"))
				tree[key] = std::move(inherited);
			else
				return false;
		}
		if (tree.contains(key) && button("Remove local binding group"))
		{
			tree.erase(key);
			return false;
		}
		return true;
	};

	if (editable("action_bindings"))
		records(tree, "action_bindings", workshop_record("tree-editor.tree.action_bindings"),
		        [&](Json &binding)
		        {
			        edit_registered(binding, "tree-editor.tree.id.2");
			        choice(binding, "tree", document.definition_choices(WorkshopDefinitionKind::Tree));
			        for (const auto *key : {"channels", "observations"})
			        {
				        if (!binding.contains(key))
					        (void)workshop_custom_action(binding, "field.ensure", {{"field", key}, {"type", "object"}});
				        heading(key);
				        structure(binding[key], true);
			        }
		        });
	edit_registered(tree, "tree-editor.tree.medical_observations");
	edit_registered(tree, "tree-editor.tree.repair_observations");
	edit_registered(tree, "tree-editor.tree.contact_observations");
	edit_registered(tree, "tree-editor.tree.armament_observations");
	edit_registered(tree, "tree-editor.tree.scan_observations");
	edit_registered(tree, "tree-editor.tree.evidence_observations");
	edit_registered(tree, "tree-editor.tree.craft_dispatch_observations");
	edit_registered(tree, "tree-editor.tree.request_observations");
	edit_registered(tree, "tree-editor.tree.away_preparation_observations");
	if (editable("objective_bindings"))
		records(tree, "objective_bindings", workshop_record("tree-editor.tree.objective_bindings"),
		        [&](Json &binding)
		        {
			        edit_registered(binding, "tree-editor.tree.id.2");
			        edit_registered(binding, "tree-editor.tree.objective_behavior");
			        edit_registered(binding, "tree-editor.tree.objective_retry_ticks");
			        edit_registered(binding, "tree-editor.tree.objective_candidate_source");
			        if (binding.value("candidate_source", "single") == "reports")
				        edit_registered(binding, "tree-editor.tree.report_message");
			        choice(binding, "action", ids(resolved.value("action_bindings", Json::array())));
			        if (!binding.contains("navigation") && button("Add permitted navigation geometry"))
				        binding["navigation"] = {{"mode", binding.value("candidate_source", "single") == "contacts"
				                                              ? "approach"
				                                              : "destination"},
				                                 {"distance", 0}};
			        for (const auto *key : {"available", "target", "arguments", "factors", "commitment", "navigation"})
				        if (binding.contains(key))
				        {
					        heading(key);
					        structure(binding[key], true);
				        }
		        });
	if (editable("objective_scoring"))
	{
		if (!tree.contains("objective_scoring") && button("Add objective scoring policy"))
			tree["objective_scoring"] = {{"default", Json::object()}};
		if (tree.contains("objective_scoring"))
			for (auto &[name, policy] : tree["objective_scoring"].items())
			{
				ScopedId policy_scope(name.c_str());
				heading(name.c_str());
				for (const auto *factor : {"urgency", "progress", "risk", "travel", "resources", "commitment"})
					edit_registered(policy, (std::string("tree-editor.tree.objective_weight.") + factor).c_str());
			}
	}
	optional_string(tree, "decision_context");
	if (editable("decision_candidates"))
		records(tree, "decision_candidates", workshop_record("tree-editor.tree.decision_candidates"),
		        [&](Json &candidate)
		        {
			        edit_registered(candidate, "tree-editor.tree.id.2");
			        edit_registered(candidate, "tree-editor.tree.description");
			        edit_registered(candidate, "tree-editor.tree.deviation");
			        heading("Availability from permitted observations");
			        structure(candidate["available"], true);
			        heading("Prepared action and coordination tasks");
			        structure(candidate["tasks"], true);
		        });
	if (tree.contains("controller_action") || button("Declare reusable Controller action"))
	{
		auto &action = tree["controller_action"];
		if (action.is_null())
			action = {{"label", "Controller action"}};
		edit_registered(action, "tree-editor.tree.label");
		edit_registered(action, "tree-editor.tree.description");
		records(action, "inputs", workshop_record("tree-editor.tree.inputs"),
		        [&](Json &input)
		        {
			        edit_registered(input, "tree-editor.tree.name");
			        edit_registered(input, "tree-editor.tree.description");
			        edit_registered(input, "tree-editor.tree.unit");
			        edit_registered(input, "tree-editor.tree.type.3");
		        });
		if (!action.contains("available"))
			action["available"] = workshop_seed("tree-editor.tree.available");
		heading("Availability conditions");
		structure(action["available"], true);
	}
	if (editable("command_bindings"))
		records(tree, "command_bindings", workshop_record("tree-editor.tree.command_bindings"),
		        [&](Json &binding)
		        {
			        edit_registered(binding, "tree-editor.tree.channel");
			        edit_registered(binding, "tree-editor.tree.type.4");
			        edit_registered(binding, "tree-editor.tree.semantics");
			        edit_registered(binding, "tree-editor.tree.action.2");
			        if (binding.value("action", std::string{}).empty())
				        binding.erase("action");
		        });
	if (editable("ftl_system"))
		optional_string(tree, "ftl_system");
	if (editable("system_bindings"))
		records(tree, "system_bindings", workshop_record("tree-editor.tree.system_bindings"),
		        [&](Json &binding)
		        {
			        edit_registered(binding, "tree-editor.tree.name");
			        edit_registered(binding, "tree-editor.tree.system");
			        edit_registered(binding, "tree-editor.tree.field");
		        });
	if (editable("command_reads"))
		records(tree, "command_reads", workshop_record("tree-editor.tree.command_reads"),
		        [&](Json &binding)
		        {
			        edit_registered(binding, "tree-editor.tree.name");
			        edit_registered(binding, "tree-editor.tree.command_source_console");
			        edit_registered(binding, "tree-editor.tree.controller");
			        edit_registered(binding, "tree-editor.tree.channel");
			        edit_registered(binding, "tree-editor.tree.command_source_field");
			        edit_registered(binding, "tree-editor.tree.type.4");
		        });
	if (editable("ability_bindings"))
		strings(tree, "ability_bindings");
	if (editable("process_bindings"))
		strings(tree, "process_bindings");
	if (editable("process_property_bindings"))
		records(tree, "process_property_bindings", workshop_record("tree-editor.tree.process_property_bindings"),
		        [&](Json &binding)
		        {
			        edit_registered(binding, "tree-editor.tree.endpoint");
			        choice(binding, "property", document.definition_choices(WorkshopDefinitionKind::Property));
		        });
	if (editable("ability_property_bindings"))
		records(tree, "ability_property_bindings", workshop_record("tree-editor.tree.ability_property_bindings"),
		        [&](Json &binding)
		        {
			        edit_registered(binding, "tree-editor.tree.endpoint");
			        choice(binding, "property", document.definition_choices(WorkshopDefinitionKind::Property));
		        });
	if (editable("contact_bindings"))
		records(tree, "contact_bindings", workshop_record("tree-editor.tree.contact_bindings"),
		        [&](Json &binding)
		        {
			        edit_registered(binding, "tree-editor.tree.name");
			        edit_registered(binding, "tree-editor.tree.controller");
			        edit_registered(binding, "tree-editor.tree.channel");
		        });
	if (editable("publication_templates"))
		strings(tree, "publication_templates");
	if (editable("message_templates"))
		names(tree, "message_templates", document.definition_choices(WorkshopDefinitionKind::Message));
	const auto knowledge = document.preview_knowledge();
	if (editable("message_bindings"))
		records(tree, "message_bindings", workshop_record("tree-editor.tree.message_bindings"),
		        [&](Json &binding)
		        {
			        edit_registered(binding, "tree-editor.tree.name");
			        choice(binding, "schema", ids(knowledge.at("schemas")));
			        std::vector<std::string> fields;
			        for (const auto &schema : knowledge.at("schemas"))
				        if (schema.at("id") == binding.value("schema", std::string{}))
					        fields = ids(schema.at("fields"));
			        choice(binding, "field", fields);
		        });
}

void TreeEditor::State::controller_palette(const Json &tree, WorkshopDocument &document)
{
	vocabulary["controller"] = workshop_seed("tree-editor.tree.controller");
	try
	{
		vocabulary["controller"] = workshop_tree_vocabulary(document, tree);
	}
	catch (const std::exception &error)
	{
		ImGui::TextWrapped("%s", error.what());
	}
}

bool TreeEditor::tree_binding(WorkshopDocument &document, Json &owner, const char *key, const char *context,
                              bool optional)
{
	ScopedId scope(key);
	bool changed = false;
	if (!owner.contains(key) || owner.at(key).is_string())
	{
		std::vector<std::string> compatible;
		for (const auto &id : document.definition_choices(WorkshopDefinitionKind::Tree))
			try
			{
				if (document.preview_tree(id).value("context", std::string{}) == context)
					compatible.push_back(id);
			}
			catch (const std::exception &)
			{
			}
		changed = state_->choice(owner, key, compatible, optional);
	}
	else
		state_->text("Owner-local tree");
	auto open = [&]
	{
		std::function<bool(Json &, const std::string &)> locate;
		locate = [&](Json &value, const std::string &path)
		{
			if (&value == &owner)
			{
				state_->local_path = path + "/" + key;
				state_->local_target = WorkshopTarget::capture(document.definitions(), state_->local_path);
				state_->tree.clear();
				state_->selected_state = -1;
				state_->selected_state_id.clear();
				ImGui::SetWindowFocus(tool_label("State tree editor").c_str());
				return true;
			}
			if (value.is_structured())
				for (auto it = value.begin(); it != value.end(); ++it)
				{
					const auto part = value.is_array() ? std::to_string(it - value.begin()) : it.key();
					if (locate(it.value(), path + "/" + part))
						return true;
				}
			return false;
		};
		locate(document.definitions(), "");
	};
	if (state_->button("New local tree"))
	{
		owner[key] = workshop_tree_localize(document, owner.value(key, Json()), context, false);
		changed = true;
		open();
	}
	if (owner.contains(key))
	{
		if (owner.at(key).is_string() && !owner.at(key).get<std::string>().empty() &&
		    state_->button("Copy referenced tree locally"))
		{
			owner[key] = workshop_tree_localize(document, owner.at(key), context, true);
			changed = true;
			open();
		}
		if (state_->button("Open tree editor"))
			open();
		if (optional && owner.at(key).is_object() && state_->button("Remove local tree"))
		{
			owner.erase(key);
			changed = true;
		}
	}
	return changed;
}
void TreeEditor::database(WorkshopDocument &document, std::string &selection)
{
	state_->database = true;
	try
	{
		state_->trees(document, false, selection);
	}
	catch (...)
	{
		state_->database = false;
		throw;
	}
	state_->database = false;
}
TreeEditor::TreeEditor() : state_(std::make_unique<State>())
{
}
TreeEditor::~TreeEditor() = default;
void TreeEditor::presentation(bool pseudo, bool probe)
{
	state_->pseudo = pseudo;
	state_->controller_probe = probe;
}
void TreeEditor::set_vocabulary(const Json &vocabulary)
{
	state_->vocabulary = vocabulary;
}
bool TreeEditor::has_vocabulary() const
{
	return !state_->vocabulary.empty();
}
void TreeEditor::runtime_feedback(const Json &feedback, const std::string &controller)
{
	state_->feedback = feedback;
	state_->inspected_controller = controller;
}
void TreeEditor::draw(WorkshopDocument &document, bool inspector, std::string &selection)
{
	state_->trees(document, inspector, selection);
}
void TreeEditor::controller_palette(const Json &tree, WorkshopDocument &document)
{
	state_->controller_palette(tree, document);
}
void TreeEditor::binding_map(Json &parent, const char *key, const Json &definition,
                             const std::vector<std::string> &suggested, const std::vector<std::string> &allowed)
{
	state_->binding_map(parent, key, definition, suggested, allowed);
}
} // namespace phoenix
