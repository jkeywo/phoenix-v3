module;
#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>
#include <algorithm>
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
import phoenix.content.documents;
import phoenix.workshop.authoring;

namespace phoenix
{
struct TreeEditor::State : AuthoringControls
{
	std::string tree, inspected_controller;
	Json vocabulary = Json::object(), feedback = Json::object();
	int selected_state = -1;
	std::vector<std::string> vocabulary_ids(const std::string &context, const char *kind) const;
	std::string authoring_context(const Json &definition) const;
	Json action_signature(const std::string &context, const std::string &name) const;
	void binding(Json &parent, const char *key, const Json &definition,
	             const std::vector<std::string> &allowed = {"integer", "boolean", "text", "reference", "parameter",
	                                                        "observation", "intent", "memory", "elapsed"});
	void binding_map(Json &parent, const char *key, const Json &definition,
	                 const std::vector<std::string> &suggested = {},
	                 const std::vector<std::string> &allowed = {"integer", "boolean", "text", "reference", "parameter",
	                                                            "observation", "intent", "memory", "elapsed"});
	void tasks(Json &state, const char *phase, Json &definition, WorkshopDocument &document);
	void tree_graph(const Json &definition, std::string &selection);
	void state_inspector(Json &state, Json &definition, WorkshopDocument &document, bool patch = false);
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
	const auto current = parent.contains(key) ? parent.at(key) : Json(0);
	std::string mode = "integer";
	if (current.is_boolean())
		mode = "boolean";
	else if (current.is_string())
		mode = "text";
	else if (current.is_object())
	{
		for (const char *source : {"argument", "parameter", "observation", "intent", "memory", "elapsed"})
			if (current.contains(source))
				mode = source;
		if (current.contains("type"))
			mode = "reference";
	}
	Json selector{{"source", mode}};
	if (choice(selector, "source", allowed))
	{
		mode = selector.at("source").get<std::string>();
		if (mode == "integer")
			parent[key] = 0;
		else if (mode == "boolean")
			parent[key] = false;
		else if (mode == "text")
			parent[key] = "";
		else if (mode == "reference")
			parent[key] = {{"type", "entity"}, {"id", 1}};
		else if (mode == "elapsed")
			parent[key] = {{"elapsed", true}};
		else
			parent[key] = {{mode, ""}};
	}
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
			choice(parent[key], "type", {"entity", "system", "console", "objective", "region", "message"});
			integer(parent[key], "id", 1);
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
		if (!parent.contains(key))
			parent[key] = Json::object();
		if (!parent[key].contains(names[scope]))
			parent[key][names[scope]] = 0;
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
				state[phase] = Json::array();
			if (std::string(kind) == "action")
				state[phase].push_back({{"action", ""}, {"arguments", Json::array()}, {"once", false}});
			else if (std::string(kind) == "call")
				state[phase].push_back({{"call", ""}, {"parameters", Json::object()}});
			else if (std::string(kind) == "attempt")
				state[phase].push_back({{"attempt", Json::array({{{"tasks", Json::array()}}})}});
			else
				state[phase].push_back({{kind, ""}, {"value", 0}});
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
					string(task, "action");
					text("Action palette awaits the production context vocabulary. Verification rejects "
					     "unsupported names.");
				}
				boolean(task, "once", false);
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
						task["arguments"] = Json::array();
						for (const auto &argument : signature.at("arguments"))
						{
							const auto type = argument.value("type", std::string("integer"));
							task["arguments"].push_back(type == "boolean"   ? Json(false)
							                            : type == "text"    ? Json("")
							                            : type == "integer" ? Json(0)
							                                                : Json{{"type", type}, {"id", 1}});
						}
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
				records(task, "attempt", {{"conditions", Json::array()}, {"tasks", Json::array()}},
				        [&](Json &branch)
				        {
					        records(branch, "conditions", {{"left", 0}, {"op", "eq"}, {"right", 0}},
					                [&](Json &condition)
					                {
						                binding(condition, "left", definition);
						                choice(condition, "op", {"eq", "ne", "lt", "le", "gt", "ge"});
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
					string(task, "intent");
				binding(task, "value", definition);
			}
			else
				text("Legacy timed output retained. Edit its existing controller inspector or convert the tree "
				     "explicitly.");
			if (button("Earlier") && i > 0)
				std::swap(state[phase][i], state[phase][i - 1]);
			ImGui::SameLine();
			if (button("Later") && i + 1 < state.at(phase).size())
				std::swap(state[phase][i], state[phase][i + 1]);
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
	ScopedChild child("state-graph", {0, 290}, ImGuiWindowFlags_HorizontalScrollbar);
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
	for (std::size_t i = 0; i < states.size(); ++i)
	{
		const auto &state = states[i];
		std::size_t depth = 0;
		auto parent = state.value("parent", std::string{});
		for (; !parent.empty() && depth < states.size(); ++depth)
		{
			auto found = std::find_if(states.begin(), states.end(),
			                          [&](const auto &row)
			                          {
				                          return row.value("id", std::string{}) == parent;
			                          });
			if (found == states.end())
				break;
			parent = found->value("parent", std::string{});
		}
		positions[state.value("id", std::to_string(i))] = {
		    origin.x + static_cast<float>(std::min<std::size_t>(depth, 16)) * 190, origin.y + i * 65.f};
		width = std::max(width, static_cast<float>(std::min<std::size_t>(depth, 16)) * 190 + 180);
	}
	auto *draw = ImGui::GetWindowDrawList();
	for (const auto &state : states)
	{
		const auto id = state.value("id", std::string{});
		if (!positions.contains(id))
			continue;
		const auto start = positions.at(id);
		if (state.contains("transitions"))
			for (const auto &edge : state.at("transitions"))
			{
				const auto target = edge.value("target", std::string{});
				if (!positions.contains(target))
					continue;
				const auto end = positions.at(target);
				draw->AddBezierCubic({start.x + 170, start.y + 25}, {start.x + 205, start.y + 25},
				                     {end.x - 35, end.y + 25}, {end.x, end.y + 25},
				                     ImGui::GetColorU32(ImGuiCol_PlotLines), 2);
			}
	}
	for (std::size_t i = 0; i < states.size(); ++i)
	{
		const auto id = states[i].value("id", std::to_string(i));
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
		if (ImGui::Selectable((id + "###node").c_str(), selected_state == static_cast<int>(i), 0, {170, 45}))
		{
			selected_state = static_cast<int>(i);
			selection = "State trees";
		}
	}
	ImGui::SetCursorScreenPos(origin);
	ImGui::Dummy({width, std::max(260.f, states.size() * 65.f)});
}

void TreeEditor::State::state_inspector(Json &state, Json &definition, WorkshopDocument &document, bool patch)
{
	const auto states = definition.contains("states") ? ids(definition.at("states")) : std::vector<std::string>{};
	if (patch)
		choice(state, "state", states);
	else
		string(state, "id");
	choice(state, "parent", states, true);
	choice(state, "initial", states, true);
	for (const char *phase : {"entry", "running", "exit"})
		tasks(state, phase, definition, document);
	records(state, "transitions", {{"target", ""}, {"reason", ""}, {"conditions", Json::array()}},
	        [&](Json &edge)
	        {
		        choice(edge, "target", states);
		        string(edge, "reason");
		        records(edge, "conditions", {{"left", 0}, {"op", "eq"}, {"right", 0}},
		                [&](Json &condition)
		                {
			                binding(condition, "left", definition);
			                if (choice(condition, "op", {"eq", "ne", "lt", "le", "gt", "ge", "present"}) &&
			                    condition.at("op") == "present")
				                condition.erase("right");
			                if (condition.value("op", std::string("eq")) != "present")
				                binding(condition, "right", definition);
		                });
	        });
}

void TreeEditor::State::trees(WorkshopDocument &document, bool inspector, std::string &selection)
{
	auto &defs = document.definitions();
	if (!inspector)
	{
		static std::string derive_base;
		Json derivation{{"Base tree", derive_base}};
		if (choice(derivation, "Base tree", document.definition_choices(WorkshopDefinitionKind::Tree)))
			derive_base = derivation.at("Base tree").get<std::string>();
		if (button("Derive tree from base") && !derive_base.empty())
		{
			if (!defs.contains("trees"))
				defs["trees"] = Json::array();
			tree = fresh(defs.at("trees"), "tree");
			defs["trees"].push_back({{"id", tree}, {"extends", derive_base}});
			selected_state = -1;
			selection = "State trees";
		}
		if (button("New hierarchical tree"))
		{
			selected_state = -1;
			selection = "State trees";
			if (!defs.contains("trees"))
				defs["trees"] = Json::array();
			tree = fresh(defs.at("trees"), "tree");
			defs["trees"].push_back({{"id", tree},
			                         {"context", "controller"},
			                         {"initial", "operating"},
			                         {"states", Json::array({{{"id", "operating"},
			                                                  {"entry", Json::array()},
			                                                  {"running", Json::array()},
			                                                  {"exit", Json::array()},
			                                                  {"transitions", Json::array()}}})}});
		}
		if (!defs.contains("trees"))
			return;
		Json tree_choice{{"Tree", tree}};
		if (choice(tree_choice, "Tree", ids(defs.at("trees"))))
		{
			tree = tree_choice.at("Tree").get<std::string>();
			selected_state = -1;
			selection = "State trees";
		}
		for (const auto &definition : defs["trees"])
			if (definition.at("id") == tree)
			{
				auto resolved = document.preview_tree(definition);
				if (resolved.contains("context"))
				{
					tree_graph(resolved, selection);
				}
			}
		if (button("Inspect selected tree") && !tree.empty())
			selection = "State trees";
		return;
	}
	if (!defs.contains("trees"))
		return;
	for (auto &definition : defs["trees"])
		if (definition.at("id") == tree)
		{
			string(definition, "id");
			tree = definition.at("id").get<std::string>();
			Json resolved = document.preview_tree(definition);

			choice(definition, "extends", document.definition_choices(WorkshopDefinitionKind::Tree), true);
			if (definition.contains("extends"))
			{
				text("Inherited source");
				ImGui::TextUnformatted(definition.at("extends").get<std::string>().c_str());
			}
			Json context{{"context", resolved.at("context")}};
			if (choice(context, "context", {"controller", "scenario", "conversation", "away", "team"}))
				definition["context"] = context.at("context");
			if (context.at("context") == "away")
			{
				boolean(definition, "boarding", false);
				strings(definition, "flags");
				strings(definition, "team_capabilities");
				strings(definition, "events");
			}
			if (context.at("context") == "scenario")
				boolean(definition, "interdiction", false);
			strings(definition, "transport_capabilities");
			if (context.at("context") == "conversation")
			{
				optional_string(definition, "title_key");
				if (!definition.contains("lines") && button("Add dialogue lines"))
					definition["lines"] = {
					    {"greeting",
					     {{"speaker", "target"}, {"text_key", "Hello"}, {"delay_ticks", 0}, {"incoming_ticks", 20}}}};
				if (!definition.contains("prompts") && button("Add dialogue choices"))
					definition["prompts"] = {{"question",
					                          {{"speaker", "target"},
					                           {"text_key", "Your answer?"},
					                           {"choices", Json::array({{{"id", "yes"}, {"text_key", "Yes"}}})},
					                           {"ai_choice", "yes"}}}};
				structured_field(definition, "lines");
				structured_field(definition, "prompts");
			}
			integer(definition, "budget", resolved.value("budget", 4096));
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
						choice(parameter, "type",
						       {"integer", "boolean", "text", "entity", "system", "console", "objective", "region",
						        "message"});
						binding(parameter, "default", resolved, {"integer", "boolean", "text", "reference"});
					}
					else
					{
						structure(it.value(), false);
						if (button("Override parameter"))
						{
							if (!definition.contains("parameters"))
								definition["parameters"] = Json::object();
							definition["parameters"][it.key()] = it.value();
						}
					}
				}
			static std::string parameter_name;
			ImGui::InputText(label("Parameter name").c_str(), &parameter_name);
			if (button("Add parameter") && !parameter_name.empty())
			{
				if (!definition.contains("parameters"))
					definition["parameters"] = Json::object();
				if (!definition["parameters"].contains(parameter_name))
					definition["parameters"][parameter_name] = {{"type", "integer"}, {"default", 0}};
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
			records(definition, "memory", {{"name", "memory"}, {"type", "integer"}, {"initial", 0}},
			        [&](Json &memory)
			        {
				        string(memory, "name");
				        choice(memory, "type",
				               {"integer", "boolean", "text", "entity", "system", "console", "objective", "region",
				                "message"});
				        binding(memory, "initial", resolved, {"integer", "boolean", "text", "reference", "parameter"});
			        });

			if (definition.contains("states"))
			{
				if (button("Add state"))
				{
					definition["states"].push_back({{"id", fresh(definition.at("states"), "state")},
					                                {"entry", Json::array()},
					                                {"running", Json::array()},
					                                {"exit", Json::array()},
					                                {"transitions", Json::array()}});
					selected_state = static_cast<int>(definition.at("states").size() - 1);
				}
				if (selected_state >= 0 && selected_state < static_cast<int>(definition.at("states").size()))
				{
					ScopedId identity("selected-state");
					state_inspector(definition["states"][selected_state], resolved, document);
					if (button("Remove state"))
					{
						definition["states"].erase(definition["states"].begin() + selected_state);
						selected_state = -1;
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
						definition["overrides"] = Json::array();
					definition["overrides"].push_back({{"state", resolved.at("states")[selected_state].at("id")}});
				}
			}
			records(definition, "overrides", {{"state", ""}},
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
	records(tree, "record_bindings",
	        {{"name", "record"}, {"scope", "console"}, {"schema", ""}, {"field", ""}, {"subject", "self"}},
	        [&](Json &binding)
	        {
		        string(binding, "name");
		        choice(binding, "scope", {"console", "ship"});
		        choice(binding, "schema", ids(knowledge.at("schemas")));
		        std::vector<std::string> fields;
		        for (const auto &schema : knowledge.at("schemas"))
			        if (schema.at("id") == binding.value("schema", std::string{}))
				        fields = ids(schema.at("fields"));
		        choice(binding, "field", fields);
		        const auto subject = binding.contains("subject")             ? binding.at("subject")
		                             : binding.contains("subject_parameter") ? binding.at("subject_parameter")
		                                                                     : Json("self");
		        Json selected{{"Record subject", subject.is_string() ? subject.get<std::string>() : "entity"}};
		        const auto subjects = resolved.value("context", std::string("system")) == "scenario"
		                                  ? std::vector<std::string>{"self", "entity"}
		                              : resolved.value("context", std::string("system")) == "controller"
		                                  ? std::vector<std::string>{"self", "latest", "entity"}
		                                  : std::vector<std::string>{"self", "selected_contact", "entity"};
		        if (choice(selected, "Record subject", subjects))
		        {
			        const auto kind = selected.at("Record subject").get<std::string>();
			        binding.erase("subject_parameter");
			        binding["subject"] = kind == "entity" ? Json(1) : Json(kind);
		        }
		        if (binding.contains("subject") && binding.at("subject").is_number_integer())
			        integer(binding, "subject", 1);
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
		records(tree, "action_bindings", {{"id", "action"}, {"tree", ""}},
		        [&](Json &binding)
		        {
			        string(binding, "id");
			        choice(binding, "tree", document.definition_choices(WorkshopDefinitionKind::Tree));
			        for (const auto *key : {"channels", "observations"})
			        {
				        if (!binding.contains(key))
					        binding[key] = Json::object();
				        heading(key);
				        structure(binding[key], true);
			        }
		        });
	optional_string(tree, "decision_context");
	if (editable("decision_candidates"))
		records(tree, "decision_candidates",
		        {{"id", "candidate"},
		         {"description", "Describe this officer decision"},
		         {"available", Json::array()},
		         {"tasks", Json::array()}},
		        [&](Json &candidate)
		        {
			        string(candidate, "id");
			        string(candidate, "description");
			        string(candidate, "deviation");
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
		string(action, "label");
		string(action, "description");
		records(action, "inputs", {{"name", "target"}, {"type", "integer"}},
		        [&](Json &input)
		        {
			        string(input, "name");
			        string(input, "description");
			        string(input, "unit");
			        choice(input, "type", {"integer", "boolean", "text"});
		        });
		if (!action.contains("available"))
			action["available"] = Json::array();
		heading("Availability conditions");
		structure(action["available"], true);
	}
	if (editable("command_bindings"))
		records(tree, "command_bindings", {{"channel", "command"}, {"type", "integer"}, {"semantics", "setting"}},
		        [&](Json &binding)
		        {
			        string(binding, "channel");
			        choice(binding, "type", {"integer", "boolean"});
			        choice(binding, "semantics", {"setting", "request"});
			        choice(binding, "action", {"", "destination", "follow_contact", "ability_activate"});
			        if (binding.value("action", std::string{}).empty())
				        binding.erase("action");
		        });
	if (editable("ftl_system"))
		optional_string(tree, "ftl_system");
	if (editable("system_bindings"))
		records(tree, "system_bindings", {{"name", "observed"}, {"system", ""}, {"field", "enabled"}},
		        [&](Json &binding)
		        {
			        string(binding, "name");
			        string(binding, "system");
			        string(binding, "field");
		        });
	if (editable("command_reads"))
		records(tree, "command_reads", {{"name", "accepted"}, {"controller", ""}, {"channel", ""}, {"type", "integer"}},
		        [&](Json &binding)
		        {
			        string(binding, "name");
			        string(binding, "controller");
			        string(binding, "channel");
			        choice(binding, "type", {"integer", "boolean"});
		        });
	if (editable("ability_bindings"))
		strings(tree, "ability_bindings");
	if (editable("process_bindings"))
		strings(tree, "process_bindings");
	if (editable("process_property_bindings"))
		records(tree, "process_property_bindings", {{"endpoint", ""}, {"property", ""}},
		        [&](Json &binding)
		        {
			        string(binding, "endpoint");
			        choice(binding, "property", document.definition_choices(WorkshopDefinitionKind::Property));
		        });
	if (editable("ability_property_bindings"))
		records(tree, "ability_property_bindings", {{"endpoint", ""}, {"property", ""}},
		        [&](Json &binding)
		        {
			        string(binding, "endpoint");
			        choice(binding, "property", document.definition_choices(WorkshopDefinitionKind::Property));
		        });
	if (editable("contact_bindings"))
		records(tree, "contact_bindings", {{"name", "contact"}, {"controller", ""}, {"channel", ""}},
		        [&](Json &binding)
		        {
			        string(binding, "name");
			        string(binding, "controller");
			        string(binding, "channel");
		        });
	if (editable("publication_templates"))
		strings(tree, "publication_templates");
	if (editable("message_templates"))
		names(tree, "message_templates", document.definition_choices(WorkshopDefinitionKind::Message));
	const auto knowledge = document.preview_knowledge();
	if (editable("message_bindings"))
		records(tree, "message_bindings", {{"name", "payload"}, {"schema", ""}, {"field", ""}},
		        [&](Json &binding)
		        {
			        string(binding, "name");
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
	vocabulary["controller"] = {
	    {"actions", Json::array()}, {"observations", Json::array()}, {"intents", Json::array()}};
	try
	{
		const auto registry = document.preview_controller_registry(tree);
		const auto type_name = [](state_tree::Type type)
		{
			static const char *names[] = {"missing", "boolean", "integer",   "text",   "entity",
			                              "system",  "console", "objective", "region", "message"};
			return names[static_cast<unsigned>(type)];
		};
		for (const auto &field : registry.observations)
			vocabulary["controller"]["observations"].push_back({{"id", field.name}, {"type", type_name(field.type)}});
		for (const auto &field : registry.intents)
			vocabulary["controller"]["intents"].push_back({{"id", field.name}, {"type", type_name(field.type)}});
		for (const auto &action : registry.actions)
		{
			Json arguments = Json::array();
			for (std::size_t i = 0; i < action.arguments.size(); ++i)
				arguments.push_back({{"name", std::to_string(i + 1)}, {"type", type_name(action.arguments[i])}});
			vocabulary["controller"]["actions"].push_back(
			    {{"id", action.name}, {"name", action.name}, {"arguments", arguments}});
		}
	}
	catch (const std::exception &error)
	{
		ImGui::TextWrapped("%s", error.what());
	}
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
