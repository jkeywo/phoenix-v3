module;
#include <algorithm>
#include <functional>
#include <optional>
#include <set>
#include <tuple>
#include <stdexcept>
#include <nlohmann/json.hpp>
module phoenix.content.controllers;
import phoenix.content.catalog;
import phoenix.content.controllers.types;
import phoenix.content.knowledge;

namespace phoenix
{
void validate_controller_policy_sources(const Catalog &catalog)
{
	for (const auto &[id, widget] : catalog.control_widgets)
		if (widget.contains("policy_template"))
			throw std::runtime_error("Controller widget " + id +
			                         ": retired native combat policy template; fit an authored objective policy tree");
}
void expand_officer_policies(Catalog &catalog)
{
	for (auto &[entity_id, entity] : catalog.entities)
	{
		if (!entity.contains("control_layout"))
			continue;
		for (auto &instance : entity["control_layout"]["controllers"])
		{
			const auto &bound_instance = compiled_widget_instance(catalog, entity_id, instance);
			if (!instance.contains("widget"))
				continue;
			const auto widget = compile_control_widget(catalog, bound_instance.at("widget").get<std::string>());
			auto controller = bind_control_widget(widget, bound_instance);
			if (const auto ship = catalog.controller_policies.find(entity_id);
			    ship != catalog.controller_policies.end())
				if (const auto policy = ship->second.find({instance.at("console"), instance.at("id")});
				    policy != ship->second.end())
				{
					controller["tree"] = policy->second;
					controller.erase("fallback");
				}
			if (controller.value("officer_protocol", false))
				continue;
			std::map<std::string, Json> supported;
			for (const auto &action : {"generator_output", "cooling_allocation", "follow_contact", "repair_compartment",
			                           "speed_mm_s", "heading_mdeg"})
			{
				for (const auto &c : controller.at("commands"))
					if (c.at("action") == action)
					{
						const std::string kind = action;
						const std::string operation = kind == "generator_output"     ? "power"
						                              : kind == "cooling_allocation" ? "cooling"
						                              : kind == "repair_compartment" ? "repair"
						                                                             : "manoeuvre";
						supported.try_emplace(operation, c);
						break;
					}
			}
			if (supported.empty())
				continue;
			const auto id = "phoenix.officer." + entity_id + "." + controller.at("console").get<std::string>() + "." +
			                controller.at("id").get<std::string>();
			Json policy =
			    controller.contains("tree")
			        ? resolve_tree_definition(catalog.trees.at(controller.at("tree").get<std::string>()), catalog)
			        : Json{{"context", "controller"}, {"initial", "idle"}, {"states", Json::array({{{"id", "idle"}}})}};
			// Scored policies declare support actions explicitly; a legacy wrapper
			// must not inject a second, unscored selector around their arbitration.
			if (policy.contains("objective_bindings"))
				continue;
			// Only shared-action policies can delegate execution. Legacy direct-write packs retain their policy.
			if (controller.contains("tree") && !policy.contains("action_bindings"))
				continue;
			if (catalog.trees.contains(id) || catalog.trees.contains(id + ".routine") ||
			    catalog.trees.contains(id + ".support"))
				throw std::runtime_error("Reserved compiled officer tree collision");
			Json routine = policy;
			routine["id"] = id + ".routine";
			catalog.trees.emplace(id + ".routine", routine);
			policy["id"] = id;
			policy.erase("extends");
			policy.erase("overrides");
			auto &bindings = policy["command_bindings"];
			if (bindings.is_null())
				bindings = Json::array();
			for (const auto &command : controller.at("commands"))
			{
				const auto kind = command.at("action").get<std::string>(),
				           channel = command.at("id").get<std::string>();
				if (kind != "generator_output" && kind != "cooling_allocation" && kind != "repair_compartment" &&
				    kind != "follow_contact" && kind != "speed_mm_s" && kind != "heading_mdeg")
					continue;
				if (std::none_of(bindings.begin(), bindings.end(),
				                 [&](const auto &b)
				                 {
					                 return b.at("channel") == channel;
				                 }))
				{
					Json b{{"channel", channel}, {"action", kind}};
					if (kind == "follow_contact")
						b["type"] = "boolean";
					bindings.push_back(b);
				}
			}
			const auto obs = [](const char *n)
			{
				return Json{{"observation", std::string("coordination.") + n}};
			};
			const auto condition = [&](const char *n, const char *op, Json value)
			{
				return Json{{"left", obs(n)}, {"op", op}, {"right", value}};
			};
			Json action{{"id", id + ".support"},
			            {"context", "controller"},
			            {"command_bindings", bindings},
			            {"controller_action",
			             {{"label", "Respond to officer request"},
			              {"inputs", Json::array({{{"name", "officer_operation"}, {"type", "text"}},
			                                      {{"name", "officer_target"}, {"type", "integer"}},
			                                      {{"name", "officer_amount"}, {"type", "integer"}}})}}},
			            {"initial", "choose"},
			            {"states", Json::array({{{"id", "choose"}, {"transitions", Json::array()}}})}};
			for (const auto &[operation, command] : supported)
			{
				const auto kind = command.at("action").get<std::string>(),
				           channel = command.at("id").get<std::string>();
				Json arguments;
				if (kind == "follow_contact")
					arguments = {true, Json{{"observation", "input.officer_target"}},
					             Json{{"observation", "input.officer_amount"}},
					             Json{{"observation", "drive.cruise_speed"}}};
				else if (kind == "heading_mdeg")
					arguments = Json::array({obs("bearing")});
				else if (kind == "speed_mm_s")
					arguments = Json::array({Json{{"observation", "drive.approach_speed"}}});
				else
					arguments =
					    Json::array({Json{{"observation", kind == "repair_compartment" ? "input.officer_target"
					                                                                   : "input.officer_amount"}}});
				action["states"][0]["transitions"].push_back(
				    {{"target", operation},
				     {"conditions", Json::array({{{"left", {{"observation", "input.officer_operation"}}},
				                                  {"op", "eq"},
				                                  {"right", operation}}})}});
				Json state{{"id", operation},
				           {"running", Json::array({{{"action", "write." + channel}, {"arguments", arguments}}})}};
				if (operation == "power" || operation == "cooling")
					for (const auto &other : controller.at("commands"))
						if (other.at("action") == kind && other.at("id") != channel)
							state["running"].push_back(
							    {{"action", "write." + other.at("id").get<std::string>()}, {"arguments", arguments}});
				if (kind == "speed_mm_s")
					for (const auto &other : controller.at("commands"))
						if (other.at("action") == "heading_mdeg")
							state["running"].push_back({{"action", "write." + other.at("id").get<std::string>()},
							                            {"arguments", Json::array({obs("bearing")})}});
				if (kind == "speed_mm_s")
				{
					const Json stop{{"action", "write." + channel}, {"arguments", {0}}};
					state["exit"] = Json::array({stop});
					state["transitions"] = Json::array(
					    {{{"target", "manoeuvre_hold"},
					      {"conditions", Json::array({{{"left", obs("range")},
					                                   {"op", "le"},
					                                   {"right", {{"observation", "input.officer_amount"}}}}})}}});
					action["states"].push_back(
					    {{"id", "manoeuvre_hold"},
					     {"running", Json::array({stop})},
					     {"exit", Json::array({stop})},
					     {"transitions",
					      Json::array({{{"target", operation},
					                    {"conditions",
					                     Json::array({{{"left", obs("range")},
					                                   {"op", "gt"},
					                                   {"right", {{"observation", "input.officer_amount"}}}}})}}})}});
				}
				if (kind == "follow_contact")
					state["exit"] = Json::array({{{"action", "write." + channel}, {"arguments", {false, 0, 1, 1}}}});
				action["states"].push_back(std::move(state));
			}
			catalog.trees.emplace(id + ".support", action);
			if (!policy.contains("action_bindings"))
				policy["action_bindings"] = Json::array();
			policy["action_bindings"].push_back({{"id", "officer_support"}, {"tree", id + ".support"}});
			// Running tasks are evaluated before transitions. Keep the accepted request's
			// identity and inputs immutable so a newly selected request cannot execute
			// through the previous commitment before policy has accepted it.
			auto &memory = policy["memory"];
			if (memory.is_null())
				memory = Json::array();
			Json latch = Json::array();
			for (const auto &[field, type] : std::vector<std::pair<std::string, std::string>>{{"request", "integer"},
			                                                                                  {"invocation", "text"},
			                                                                                  {"operation", "text"},
			                                                                                  {"target", "integer"},
			                                                                                  {"limit", "integer"}})
			{
				memory.push_back({{"name", "phoenix_officer_" + field},
				                  {"type", type},
				                  {"initial", type == "text" ? Json("") : Json(0)}});
				latch.push_back({{"memory", "phoenix_officer_" + field}, {"value", obs(field.c_str())}});
			}
			Json select{{"action", "select.officer_support"},
			            {"arguments", Json::array({{{"memory", "phoenix_officer_invocation"}},
			                                       {{"memory", "phoenix_officer_operation"}},
			                                       {{"memory", "phoenix_officer_target"}},
			                                       {{"memory", "phoenix_officer_limit"}}})}};
			const auto response = [&](const char *status, const char *reason)
			{
				return Json{{"action", "coordination.respond"},
				            {"arguments", Json::array({status, obs("limit"), reason})}};
			};
			if (!policy.contains("decision_candidates"))
				policy["decision_candidates"] = Json::array();
			policy["decision_candidates"].push_back(
			    {{"id", "officer-accept"},
			     {"description", "Accept the pending officer request and execute the permitted support action."},
			     {"deviation", "Prioritising an officer support request."},
			     {"available", Json::array({condition("pending", "eq", true), condition("feasible", "eq", true)})},
			     {"tasks", Json::array({{{"action", "select.officer_support"},
			                             {"arguments", Json::array({obs("invocation"), obs("operation"), obs("target"),
			                                                        obs("limit")})}},
			                            response("accepted", "support_committed")})}});
			for (const auto *status : {"refused", "completed", "failed"})
				policy["decision_candidates"].push_back(
				    {{"id", std::string("officer-") + status},
				     {"description", std::string("Report pending officer request as ") + status +
				                         "; completion requires observed success."},
				     {"available", Json::array({condition("pending", "eq", true),
				                                condition("done", "eq", std::string(status) == "completed")})},
				     {"tasks", Json::array({response(status, "officer_judgement")})}});
			Json leave{{"target", "normal"}, {"conditions", Json::array({condition("pending", "eq", false)})}};
			Json changed_request{{"target", "normal"},
			                     {"conditions", Json::array({{{"left", {{"memory", "phoenix_officer_request"}}},
			                                                  {"op", "ne"},
			                                                  {"right", obs("request")}}})}};
			Json refuse{{"target", "failed"}, {"conditions", Json::array({condition("feasible", "eq", false)})}};
			Json new_request{{"target", "assist"},
			                 {"conditions", Json::array({condition("status", "eq", "requested")})}};
			Json changed_limit{{"target", "assist"},
			                   {"conditions", Json::array({condition("limit", "ne", obs("amount"))})}};
			Json action_failed{
			    {"target", "failed"},
			    {"conditions",
			     Json::array(
			         {Json{{"left", {{"observation", "action.status"}}}, {"op", "eq"}, {"right", "failed"}},
			          Json{{"left", {{"observation", "action.id"}}}, {"op", "eq"}, {"right", "officer_support"}},
			          Json{{"left", {{"elapsed", true}}}, {"op", "ge"}, {"right", 1}}})}};
			Json blocked{{"target", "failed"}, {"conditions", Json::array({condition("blocked", "eq", true)})}};
			Json enter{{"target", "assist"},
			           {"conditions", Json::array({condition("pending", "eq", true), condition("feasible", "eq", true),
			                                       condition("blocked", "eq", false)})}};
			Json reject{{"target", "refuse"}, {"conditions", Json::array({condition("pending", "eq", true)})}};
			latch.push_back(response("accepted", "support_committed"));
			policy["initial"] = "normal";
			policy["states"] = Json::array(
			    {{{"id", "normal"},
			      {"initial", "normal_work"},
			      {"running", Json::array({{{"call", id + ".routine"}}})}},
			     {{"id", "normal_work"}, {"parent", "normal"}, {"transitions", Json::array({enter, reject})}},
			     {{"id", "assist"},
			      {"entry", latch},
			      {"running", Json::array({select})},
			      {"transitions",
			       Json::array(
			           {leave, changed_request, refuse, blocked, action_failed, new_request, changed_limit,
			            Json{{"target", "done"}, {"conditions", Json::array({condition("done", "eq", true)})}}})}},
			     {{"id", "done"},
			      {"entry", Json::array({response("completed", "observed_outcome")})},
			      {"running", Json::array({select})},
			      {"transitions",
			       Json::array({leave, changed_request, refuse, blocked, action_failed, new_request, changed_limit})}},
			     {{"id", "refuse"},
			      {"parent", "normal"},
			      {"entry", Json::array({response("refused", "orders_or_capacity")})},
			      {"transitions", Json::array({{{"target", "normal_work"}}})}}});
			policy["states"].push_back(
			    {{"id", "failed"},
			     {"entry", Json::array({response("failed", "unable_to_continue"),
			                            Json{{"action", "action.cancel"}, {"arguments", {"commitment_failed"}}}})},
			     {"transitions", Json::array({{{"target", "normal"}}})}});
			catalog.trees.emplace(id, policy);
			catalog.controller_policies[entity_id][{instance.at("console"), instance.at("id")}] = id;
		}
	}
}
std::vector<ControllerCandidateSpec> compile_controller_candidates(const Json &source, const Catalog &catalog,
                                                                   const state_tree::Registry &registry)
{
	const auto tree = resolve_tree_definition(source, catalog);
	if (tree.value("decision_context", std::string{}).size() > 4096)
		throw std::runtime_error("Controller decision context exceeds 4096 bytes");
	std::vector<ControllerCandidateSpec> result;
	std::set<std::string> ids;
	for (const auto &candidate : tree.value("decision_candidates", Json::array()))
	{
		ControllerCandidateSpec spec;
		spec.id = candidate.at("id");
		spec.description = candidate.at("description");
		spec.deviation = candidate.value("deviation", std::string{});
		if (!valid_identifier(spec.id) || spec.id == "continue" || spec.id == "defer_to_tree" ||
		    !ids.insert(spec.id).second || ids.size() > 128 || spec.description.empty() ||
		    spec.description.size() > 2048 || spec.deviation.size() > 256)
			throw std::runtime_error("Invalid Controller decision candidate");
		Json definition{
		    {"context", "controller"},
		    {"parameters", tree.value("parameters", Json::object())},
		    {"initial", "check"},
		    {"states",
		     Json::array(
		         {{{"id", "check"},
		           {"transitions",
		            Json::array({{{"target", "offer"}, {"conditions", candidate.value("available", Json::array())}}})}},
		          {{"id", "offer"}, {"running", candidate.at("tasks")}}})}};
		spec.program = state_tree::compile(definition, registry);
		const auto &tasks = spec.program.states.back().running;
		if (tasks.empty() || tasks.size() > 4)
			throw std::runtime_error("Candidate needs 1..4 operations");
		unsigned selections = 0;
		for (const auto &task : tasks)
		{
			if (task.kind != state_tree::TaskKind::Action)
				throw std::runtime_error("Candidate cannot own memory or call policies");
			const auto &name = registry.actions.at(task.target).name;
			if (name.starts_with("select.") || name.starts_with("replace."))
				++selections;
			else if (!name.starts_with("coordination.") && name != "action.cancel" && name != "acknowledge_message")
				throw std::runtime_error("Candidate operation is not a policy decision: " + name);
			for (const auto &argument : task.arguments)
				if (argument.source != state_tree::Source::Literal &&
				    argument.source != state_tree::Source::Observation)
					throw std::runtime_error("Candidate inputs require permitted observations or constants");
		}
		if (selections > 1)
			throw std::runtime_error("Candidate selects several active actions");
		result.push_back(std::move(spec));
	}
	std::stable_partition(result.begin(), result.end(),
	                      [](const auto &candidate)
	                      {
		                      return candidate.id.starts_with("officer-");
	                      });
	return result;
}
std::vector<ControllerActionSpec> compile_controller_actions(const Json &source, const Catalog &catalog,
                                                             const state_tree::Registry &registry)
{
	const auto tree = resolve_tree_definition(source, catalog);
	std::vector<ControllerActionSpec> result;
	std::set<std::string> names;
	for (const auto &binding : tree.value("action_bindings", Json::array()))
	{
		ControllerActionSpec spec;
		spec.id = binding.at("id");
		spec.tree = binding.value("tree", spec.id + ".execution");
		if (!names.insert(spec.id).second || result.size() >= 64)
			throw std::runtime_error("Duplicate/excessive Controller actions");
		auto definition = controller_action_definition(binding, catalog);
		const auto channels = binding.value("channels", Json::object()),
		           observations = binding.value("observations", Json::object());
		if (!channels.is_object() || !observations.is_object() || channels.size() > 64 || observations.size() > 128)
			throw std::runtime_error("Invalid action capability bindings");
		for (const auto *mapping : {&channels, &observations})
			for (auto it = mapping->begin(); it != mapping->end(); ++it)
				if (!it.value().is_string() || it.key().empty() || it.value().template get<std::string>().empty())
					throw std::runtime_error("Invalid action binding name");
		std::function<void(Json &)> bind = [&](Json &node)
		{
			if (node.is_object())
			{
				if (node.contains("action") && node["action"].is_string())
				{
					const auto name = node["action"].template get<std::string>();
					if (name.starts_with("write.") && channels.contains(name.substr(6)))
						node["action"] = "write." + channels.at(name.substr(6)).template get<std::string>();
				}
				if (node.contains("channel") && node["channel"].is_string() &&
				    channels.contains(node["channel"].template get<std::string>()))
					node["channel"] = channels.at(node["channel"].template get<std::string>());
				if (node.contains("observation") && node["observation"].is_string())
				{
					auto name = node["observation"].template get<std::string>();
					if (observations.contains(name))
						node["observation"] = observations.at(name);
					else
						for (auto it = channels.begin(); it != channels.end(); ++it)
							if (name == "command." + it.key() || name.starts_with("command." + it.key() + "."))
							{
								node["observation"] = "command." + it.value().template get<std::string>() +
								                      name.substr(8 + it.key().size());
								break;
							}
				}
				for (auto &child : node)
					bind(child);
			}
			else if (node.is_array())
				for (auto &child : node)
					bind(child);
		};
		state_tree::Catalog execution_sources;
		std::function<void(const Json &)> include = [&](const Json &node)
		{
			if (node.is_object() && node.contains("call"))
			{
				const auto id = node.at("call").template get<std::string>();
				if (!execution_sources.contains(id))
				{
					auto child = resolve_tree_definition(catalog.trees.at(id), catalog);
					execution_sources.emplace(id, child);
					include(child);
					bind(execution_sources.at(id));
				}
			}
			if (node.is_structured())
				for (const auto &child : node)
					include(child);
		};
		include(definition);
		bind(definition);
		for (const auto &command : definition.value("command_bindings", Json::array()))
		{
			const auto name = "write." + command.at("channel").template get<std::string>();
			if (std::none_of(registry.actions.begin(), registry.actions.end(),
			                 [&](const auto &a)
			                 {
				                 return a.name == name;
			                 }))
				throw std::runtime_error("Missing action command capability: " + name);
		}

		const auto &metadata = definition.at("controller_action");
		spec.label = metadata.at("label");
		if (spec.label.empty() || spec.label.size() > 256)
			throw std::runtime_error("Controller action needs a bounded label");
		std::set<std::string> inputs;
		for (const auto &input : metadata.value("inputs", Json::array()))
		{
			const auto name = input.at("name").get<std::string>();
			if (!inputs.insert(name).second || inputs.size() > 32)
				throw std::runtime_error("Duplicate/excessive action inputs");
			const auto found = std::find_if(registry.observations.begin(), registry.observations.end(),
			                                [&](const auto &f)
			                                {
				                                return f.name == "input." + name;
			                                });
			if (found == registry.observations.end())
				throw std::runtime_error("Unbound action input");
			spec.inputs.push_back(found - registry.observations.begin());
		}
		spec.program = state_tree::compile(definition, registry, execution_sources);
		std::function<void(const state_tree::Program &)> validate = [&](const auto &program)
		{
			for (const auto &state : program.states)
				for (const auto *tasks : {&state.entry, &state.running, &state.exit})
					for (const auto &task : *tasks)
						if (task.kind == state_tree::TaskKind::Action &&
						    (registry.actions.at(task.target).name.starts_with("select.") ||
						     registry.actions.at(task.target).name.starts_with("replace.") ||
						     registry.actions.at(task.target).name == "action.cancel"))
							throw std::runtime_error("Action execution cannot invoke its scheduler");
			for (const auto &child : program.references)
				validate(child);
		};
		validate(spec.program);
		Json available{
		    {"context", "controller"},
		    {"initial", "check"},
		    {"states", Json::array({{{"id", "check"},
		                             {"transitions",
		                              Json::array({{{"target", "available"},
		                                            {"conditions", metadata.value("available", Json::array())}}})}},
		                            {{"id", "available"}}})}};
		spec.availability = state_tree::compile(available, registry);
		result.push_back(std::move(spec));
	}
	if (!result.empty())
	{
		const auto policy = state_tree::compile(tree, registry, catalog.trees);
		std::function<void(const state_tree::Program &)> check = [&](const auto &p)
		{
			for (const auto &s : p.states)
				for (const auto *tasks : {&s.entry, &s.running, &s.exit})
					for (const auto &t : *tasks)
						if (t.kind == state_tree::TaskKind::Action &&
						    (registry.actions.at(t.target).name.starts_with("write.") ||
						     registry.actions.at(t.target).name == "action.succeed" ||
						     registry.actions.at(t.target).name == "action.fail"))
							throw std::runtime_error("Action policy cannot write commands or complete its executor");
			for (const auto &child : p.references)
				check(child);
		};
		check(policy);
		(void)compile_controller_candidates(tree, catalog, registry);
	}
	return result;
}
std::vector<ControllerObjectiveBinding> compile_controller_objectives(const Json &source, const Catalog &catalog,
                                                                      const state_tree::Registry &registry)
{
	const auto tree = resolve_tree_definition(source, catalog);
	const auto bindings = tree.value("objective_bindings", Json::array());
	if (!bindings.is_array() || bindings.size() > 64)
		throw std::runtime_error("Expected bounded objective behavior bindings");
	std::vector<ControllerObjectiveBinding> result;
	std::set<std::string> identities;
	const auto observation = [](const char *name)
	{
		return Json{{"observation", name}};
	};
	for (const auto &binding : bindings)
	{
		ControllerObjectiveBinding spec;
		spec.behavior = binding.at("behavior");
		spec.action = binding.at("action");
		spec.id = binding.value("id", spec.behavior + "." + spec.action);
		spec.candidate_source = binding.value("candidate_source", std::string("single"));
		if (spec.candidate_source != "single" && spec.candidate_source != "medical" &&
		    spec.candidate_source != "craft" && spec.candidate_source != "requests" &&
		    spec.candidate_source != "away" && spec.candidate_source != "repair" &&
		    spec.candidate_source != "contacts" && spec.candidate_source != "armament" &&
		    spec.candidate_source != "scans" && spec.candidate_source != "reports" &&
		    spec.candidate_source != "docks" && spec.candidate_source != "delivery" &&
		    spec.candidate_source != "personnel")
			throw std::runtime_error("Unsupported objective candidate source");
		if (spec.candidate_source == "personnel" && !tree.value("personnel_observations", false))
			throw std::runtime_error("Personnel candidates require declared departmental roster observations");
		if (spec.candidate_source == "delivery" && !tree.value("delivery_observations", false))
			throw std::runtime_error("Delivery candidates require declared delivery observations");
		if (spec.candidate_source == "armament" &&
		    (!tree.value("contact_observations", false) || !tree.value("armament_observations", false)))
			throw std::runtime_error("Armament candidates require declared contact and armament observations");
		if (spec.candidate_source == "contacts" && !tree.value("contact_observations", false))
			throw std::runtime_error("Contact candidates require declared contact observations");
		if (spec.candidate_source == "scans" && !tree.value("scan_observations", false))
			throw std::runtime_error("Scan candidates require declared scan observations");
		if (spec.candidate_source == "repair" && !tree.value("repair_observations", false))
			throw std::runtime_error("Repair candidates require declared repair observations");
		if (spec.candidate_source == "medical" && !tree.value("medical_observations", false))
			throw std::runtime_error("Medical candidates require declared Medical observations");
		if (spec.candidate_source == "craft" && !tree.value("craft_dispatch_observations", false))
			throw std::runtime_error("Craft candidates require declared dispatch observations");
		if (spec.candidate_source == "away" && !tree.value("away_preparation_observations", false))
			throw std::runtime_error("Away candidates require declared away preparation observations");
		if (spec.candidate_source == "requests" && !tree.value("request_observations", false))
			throw std::runtime_error("Request candidates require declared request observations");
		if (spec.candidate_source == "docks")
		{
			spec.docking_mode = binding.value("docking_mode", std::string{});
			const std::set<std::string> modes{"assist", "connect", "cancel", "undock", "emergency", "continue"};
			if (!tree.value("docking_observations", false) || !modes.contains(spec.docking_mode))
				throw std::runtime_error(
				    "Dock candidates require declared docking observations and a supported docking_mode");
		}
		else if (binding.contains("docking_mode"))
			throw std::runtime_error("docking_mode belongs to dock candidates");
		if (binding.contains("navigation"))
		{
			const auto &navigation = binding.at("navigation");
			if (!navigation.is_object())
				throw std::runtime_error("Objective navigation must be an authored geometry object");
			spec.navigation_mode = navigation.value("mode", std::string{});
			const auto distance = navigation.value("distance", Json(0));
			if (!distance.is_number_integer() || distance < 0 || distance > 1000000000 ||
			    (spec.navigation_mode != "destination" && spec.navigation_mode != "approach" &&
			     spec.navigation_mode != "withdraw" && spec.navigation_mode != "relative" &&
			     spec.navigation_mode != "avoid") ||
			    ((spec.navigation_mode == "destination" || spec.navigation_mode == "avoid")
			         ? spec.candidate_source != "single"
			         : spec.candidate_source != "contacts"))
				throw std::runtime_error("Objective navigation requires a supported source/mode and bounded distance");
			if (spec.navigation_mode == "avoid" && distance == 0)
				throw std::runtime_error("Avoidance geometry requires a positive authored distance");
			spec.navigation_distance = distance.get<std::int64_t>();
			const auto forward = navigation.value("forward", Json(0)), lateral = navigation.value("lateral", Json(0));
			if (!forward.is_number_integer() || !lateral.is_number_integer() || forward < -1000000000 ||
			    forward > 1000000000 || lateral < -1000000000 || lateral > 1000000000 ||
			    (spec.navigation_mode == "relative" ? distance != 0
			                                        : navigation.contains("forward") || navigation.contains("lateral")))
				throw std::runtime_error("Relative navigation requires bounded authored forward/lateral offsets");
			spec.navigation_forward = forward.get<std::int64_t>();
			spec.navigation_lateral = lateral.get<std::int64_t>();

			spec.navigation_distance_parameter = navigation.value("distance_parameter", std::string{});
			if (!spec.navigation_distance_parameter.empty() &&
			    ((spec.navigation_mode != "approach" && spec.navigation_mode != "withdraw") ||
			     (spec.navigation_distance_parameter != "radius" &&
			      spec.navigation_distance_parameter != "approach_distance")))
				throw std::runtime_error("Navigation distance parameter must name a supported objective distance");
		}
		if (spec.candidate_source == "reports")
		{
			spec.report_template = binding.value("message", std::string{});
			if (!tree.value("evidence_observations", false) || !catalog.messages.contains(spec.report_template) ||
			    !catalog.messages.at(spec.report_template).value("evidence_report", false))
				throw std::runtime_error(
				    "Report candidates require declared evidence observations and an evidence message");
			spec.report_schema = catalog.messages.at(spec.report_template).at("schema").get<std::string>();
			spec.report_delay_ticks = std::max<KnowledgeTick>(
			    1, catalog.messages.at(spec.report_template).value("delay_ticks", KnowledgeTick{1}));
			spec.report_expiry_ticks =
			    catalog.messages.at(spec.report_template).value("expires_after_ticks", KnowledgeTick{0});
			(void)catalog.knowledge_definition.schema(spec.report_schema);
			for (const auto &parameter : catalog.messages.at(spec.report_template).value("parameters", Json::object()))
				if (parameter.is_object() && parameter.contains("evidence"))
				{
					const auto field = parameter.at("evidence").get<std::string>();
					if (!field.starts_with("record."))
						spec.report_fields.insert(field);
				}

			if (binding.value("arguments", Json::array()) !=
			    Json::array({{{"observation", "report_candidate.record"}}}))
				throw std::runtime_error("Report candidates pass their selected record to the shared reporting action");
			const auto &actions = tree.at("action_bindings");
			const auto action = std::find_if(actions.begin(), actions.end(),
			                                 [&](const auto &value)
			                                 {
				                                 return value.at("id") == spec.action;
			                                 });
			if (action == actions.end())
				throw std::runtime_error("Report candidate requires a declared shared action");
			const auto execution = controller_action_definition(*action, catalog);
			const auto inputs = execution.at("controller_action").value("inputs", Json::array());
			if (inputs.size() != 1 || inputs[0].at("type") != "integer")
				throw std::runtime_error("A reporting action receives exactly one typed evidence record");
			const auto argument = Json::array({{{"observation", "input." + inputs[0].at("name").get<std::string>()}}});
			bool sends = false;
			std::function<void(const Json &)> inspect = [&](const Json &value)
			{
				if (value.is_object())
				{
					if (value.value("action", std::string{}) == "send." + spec.report_template)
					{
						if (value.value("arguments", Json::array()) != argument)
							throw std::runtime_error("Evidence sends must report the selected action input");
						sends = true;
					}
					for (const auto &[name, child] : value.items())
						if (child.is_structured())
							inspect(child);
				}
				else if (value.is_array())
					for (const auto &child : value)
						inspect(child);
			};
			std::function<void(const Json &)> scope = [&](const Json &value)
			{
				if (value.is_object())
				{
					if (value.contains("observation") && value.at("observation").is_string())
					{
						const auto name = value.at("observation").get<std::string>();
						if (name.starts_with("report_payload.") && !name.starts_with("report_payload." + spec.id + "."))
							throw std::runtime_error("Report scoring reads its selected evidence payload");
					}
					for (const auto &[name, child] : value.items())
						if (child.is_structured())
							scope(child);
				}
				else if (value.is_array())
					for (const auto &child : value)
						scope(child);
			};
			scope(binding);
			inspect(execution.at("states"));
			if (!sends)
				throw std::runtime_error("Reporting action must send its declared evidence template");
		}
		const auto retry = binding.value("retry_ticks", Json(20));
		if (!retry.is_number_integer() || retry < 2 || retry > 1000000)
			throw std::runtime_error("Objective retry_ticks must be an integer from 2 to 1000000");
		spec.retry_ticks = retry.get<std::uint64_t>();
		if (!valid_identifier(spec.behavior) || !valid_identifier(spec.action) || !valid_identifier(spec.id) ||
		    !identities.insert(spec.id).second)
			throw std::runtime_error("Invalid or duplicate objective behavior/action binding");
		const auto action = std::find_if(registry.actions.begin(), registry.actions.end(),
		                                 [&](const auto &candidate)
		                                 {
			                                 return candidate.name == "select." + spec.action;
		                                 });
		if (action == registry.actions.end())
			throw std::runtime_error("Objective behavior requires a declared shared action");
		const auto arguments = binding.value("arguments", Json::array());
		if (!arguments.is_array() || arguments.size() + 1 != action->arguments.size())
			throw std::runtime_error("Objective behavior arguments differ from its shared action");
		auto evaluator = registry;
		state_tree::ActionSpec offer{"objective.offer", 4, {}, false};
		Json inputs = Json::array({binding.value("target", observation("objective.record.target"))});
		offer.arguments.push_back(state_tree::Type::Integer);
		const auto factors = binding.value("factors", Json::object());
		if (!factors.is_object())
			throw std::runtime_error("Invalid objective scoring factors");
		for (const auto &[name, expression] : factors.items())
			if (name != "urgency" && name != "progress" && name != "risk" && name != "travel" && name != "resources")
				throw std::runtime_error("Unknown objective scoring factor: " + name);
		for (const auto *name : {"urgency", "progress", "risk", "travel", "resources"})
		{
			inputs.push_back(factors.value(name, std::string(name) == "urgency"
			                                         ? observation("objective.record.urgency")
			                                     : std::string(name) == "progress" ? Json(1000)
			                                                                       : Json(0)));
			offer.arguments.push_back(state_tree::Type::Integer);
		}
		inputs.push_back(binding.value("commitment", Json(false)));
		offer.arguments.push_back(state_tree::Type::Boolean);
		for (std::size_t i = 0; i < arguments.size(); ++i)
		{
			inputs.push_back(arguments[i]);
			offer.arguments.push_back(action->arguments[i + 1]);
		}
		evaluator.actions.push_back(std::move(offer));
		const auto common = binding.value("available", Json::array());
		const auto alternatives = binding.value("available_any", Json::array({Json::array()}));
		if (!common.is_array() || !alternatives.is_array() || alternatives.empty() || alternatives.size() > 16)
			throw std::runtime_error("Objective availability requires bounded condition alternatives");
		Json branches = Json::array();
		for (const auto &alternative : alternatives)
		{
			if (!alternative.is_array() || common.size() + alternative.size() > 128)
				throw std::runtime_error("Objective availability alternatives require bounded condition arrays");
			auto conditions = common;
			for (const auto &condition : alternative)
				conditions.push_back(condition);
			branches.push_back({{"conditions", conditions},
			                    {"tasks", Json::array({{{"action", "objective.offer"}, {"arguments", inputs}}})}});
		}
		const Json definition{
		    {"context", "controller"},
		    {"initial", "offer"},
		    {"states", Json::array({{{"id", "offer"}, {"running", Json::array({{{"attempt", branches}}})}}})}};
		spec.program = state_tree::compile(definition, evaluator);
		result.push_back(std::move(spec));
	}
	if (!result.empty())
	{
		const auto policy = state_tree::compile(tree, registry, catalog.trees);
		std::function<void(const state_tree::Program &)> check;
		check = [&](const state_tree::Program &program)
		{
			std::function<void(const std::vector<state_tree::Task> &)> tasks;
			tasks = [&](const auto &items)
			{
				for (const auto &task : items)
				{
					if (task.kind == state_tree::TaskKind::Action)
					{
						const auto &name = program.registry.actions.at(task.target).name;
						if (name.starts_with("select.") || name.starts_with("replace.") || name.starts_with("write.") ||
						    name == "action.succeed" || name == "action.fail" || name == "action.cancel")
							throw std::runtime_error(
							    tree.value("id", std::string("Controller tree")) +
							    ": Objective policies select actions through their scored bindings only");
					}
					for (const auto &branch : task.branches)
						tasks(branch);
				}
			};
			for (const auto &state : program.states)
			{
				tasks(state.entry);
				tasks(state.running);
				tasks(state.exit);
			}
			for (const auto &reference : program.references)
				check(reference);
		};
		check(policy);
	}
	return result;
}
} // namespace phoenix
