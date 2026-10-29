module;
#include <algorithm>
#include <functional>
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
void expand_combat_policies(Catalog &catalog)
{
	for (auto &[entity_id, entity] : catalog.entities)
	{
		if (!entity.contains("control_layout"))
			continue;
		for (auto &controller : entity["control_layout"]["controllers"])
		{
			if (controller.contains("widget"))
				continue; // Shared widget definitions declare their complete policy explicitly.
			if (controller.value("policy", std::string{}) != "combat")
				continue;
			const auto id = "phoenix.combat." + entity_id + "." + controller.at("console").get<std::string>() + "." +
			                controller.at("id").get<std::string>();
			if (catalog.trees.contains(id) || catalog.trees.contains(id + ".execute"))
				throw std::runtime_error("Reserved combat conversion tree collision");
			Json bindings = Json::array(), work = Json::array(), inputs = Json::array(),
			     selection = Json::array({"standing-duty"});
			std::string speed_channel, shield_channel;
			const auto observation = [](const char *name)
			{
				return Json{{"observation", name}};
			};
			for (const auto &command : controller.at("commands"))
			{
				const auto action = command.at("action").get<std::string>(),
				           channel = command.at("id").get<std::string>();
				Json value;
				if (action == "weapon_target" || action == "directed_target")
					value = observation("tactical.target");
				else if (action == "heading_mdeg")
					value = observation("tactical.bearing");
				else if (action == "speed_mm_s")
				{
					speed_channel = channel;
					value = 0;
				}
				else if (action == "repair_compartment")
					value = observation("repair.compartment");
				else if (action == "repair_system")
				{
					if (std::any_of(controller.at("commands").begin(), controller.at("commands").end(),
					                [](const auto &c)
					                {
						                return c.at("action") == "repair_compartment";
					                }))
						continue;
					value = observation("repair.system");
				}
				else if (action == "magazine_select")
					value = observation("tactical.magazine");
				else if (action == "weapon_mode")
					value = 1;
				else if (action == "shield_discharge")
				{
					shield_channel = channel;
					value = false;
				}
				else
					continue;
				if (value.is_object() && value.contains("observation"))
				{
					const auto input = "combat_" + channel;
					inputs.push_back({{"name", input}, {"type", "integer"}});
					selection.push_back(value);
					value = Json{{"observation", "input." + input}};
				}
				Json binding{{"channel", channel}, {"action", action}};
				if (action == "shield_discharge")
					binding["type"] = "boolean";
				bindings.push_back(binding);
				work.push_back({{"action", "write." + channel}, {"arguments", Json::array({value})}});
			}
			Json execution{{"id", id + ".execute"},
			               {"context", "controller"},
			               {"controller_action", {{"label", "Perform assigned combat duty"}, {"inputs", inputs}}},
			               {"command_bindings", bindings},
			               {"initial", "operate"},
			               {"states", Json::array({{{"id", "operate"}, {"running", work}}})}};
			const auto tuning = controller.value("ai", Json::object());
			if (!speed_channel.empty())
			{
				const auto near = tuning.value("near_range_mm", 35000), far = tuning.value("far_range_mm", 80000);
				if (near <= 0 || far <= near || far > 1000000000)
					throw std::runtime_error("Invalid engagement range tuning");
				Json states = Json::array();
				for (const auto &name : {"hold", "approach", "cruise"})
				{
					Json tasks = work;
					for (auto &task : tasks)
						if (task["action"] == "write." + speed_channel)
							task["arguments"][0] =
							    std::string(name) == "hold"
							        ? Json(0)
							        : observation(std::string(name) == "cruise" ? "drive.cruise_speed"
							                                                    : "drive.approach_speed");
					Json transitions = Json::array();
					for (const auto &[target, op, limit] : std::vector<std::tuple<std::string, std::string, int>>{
					         {"hold", "lt", near}, {"cruise", "gt", far}, {"approach", "ge", near}})
					{
						if (target == name)
							continue;
						Json conditions =
						    Json::array({{{"left", observation("tactical.range")}, {"op", op}, {"right", limit}}});
						if (target == "approach")
							conditions.push_back(
							    {{"left", observation("tactical.range")}, {"op", "le"}, {"right", far}});
						transitions.push_back({{"target", target}, {"conditions", conditions}});
					}
					states.push_back({{"id", name}, {"running", tasks}, {"transitions", transitions}});
				}
				execution["initial"] = "hold";
				execution["states"] = states;
			}
			if (!shield_channel.empty())
			{
				Json states = Json::array();
				for (const bool discharge : {false, true})
				{
					Json tasks = work;
					for (auto &t : tasks)
						if (t["action"] == "write." + shield_channel)
							t["arguments"][0] = discharge;
					Json transitions = Json::array();
					if (!discharge)
						transitions.push_back(
						    {{"target", "share"},
						     {"conditions", Json::array({{{"left", observation("shield.lowest")},
						                                  {"op", "lt"},
						                                  {"right", tuning.value("share_below", 500)}},
						                                 {{"left", observation("shield.difference")},
						                                  {"op", "gt"},
						                                  {"right", tuning.value("share_gap", 250)}}})}});
					else
						for (const auto &c : Json::array({{{"left", observation("shield.lowest")},
						                                   {"op", "ge"},
						                                   {"right", tuning.value("recover_above", 650)}},
						                                  {{"left", observation("shield.difference")},
						                                   {"op", "le"},
						                                   {"right", tuning.value("recover_gap", 100)}}}))
							transitions.push_back({{"target", "hold"}, {"conditions", Json::array({c})}});
					states.push_back(
					    {{"id", discharge ? "share" : "hold"}, {"running", tasks}, {"transitions", transitions}});
				}
				execution["initial"] = "hold";
				execution["states"] = states;
			}
			catalog.trees.emplace(id + ".execute", execution);
			Json policy{{"id", id},
			            {"context", "controller"},
			            {"command_bindings", bindings},
			            {"action_bindings", Json::array({{{"id", "duty"}, {"tree", id + ".execute"}}})},
			            {"initial", "orders"}};
			const Json select{{"action", "select.duty"}, {"arguments", selection}};
			policy["states"] = Json::array({{{"id", "orders"}, {"running", Json::array({select})}}});

			if (tuning.contains("requests"))
			{
				Json calls = Json::array();
				std::size_t request_index = 0;
				for (const auto &request : tuning.at("requests"))
				{
					const auto operation = request.at("operation").get<std::string>();
					const auto key = id + ".request." + std::to_string(request_index++);
					const char *fact = operation == "manoeuvre" ? "tactical.target"
					                   : operation == "repair"  ? "repair.compartment"
					                   : operation == "cooling" ? "support.cooling"
					                                            : "support.power";
					const Json target =
					    (operation == "manoeuvre" || operation == "repair") ? observation(fact) : Json(0);
					const Json amount = request.contains("amount")
					                        ? request.at("amount")
					                        : (operation == "power" ? observation(fact) : Json(1));
					const Json send{
					    {"action", "coordination.maintain"},
					    {"arguments",
					     Json::array({operation, request.at("console"), request.at("controller"), target, amount,
					                  request.value("priority", 1), request.value("expiry_ticks", 1200),
					                  request.value("retry_ticks", 40)})}};
					const Json withdraw{
					    {"action", "coordination.withdraw_from"},
					    {"arguments", Json::array({operation, request.at("console"), request.at("controller")})}};
					Json helper{
					    {"id", key},
					    {"context", "controller"},
					    {"initial", "idle"},
					    {"states",
					     Json::array(
					         {{{"id", "idle"},
					           {"transitions", Json::array({{{"target", "request"},
					                                         {"conditions", Json::array({{{"left", observation(fact)},
					                                                                      {"op", "gt"},
					                                                                      {"right", 0}}})}}})}},
					          {{"id", "request"},
					           {"running", Json::array({send})},
					           {"exit", Json::array({withdraw})},
					           {"transitions", Json::array({{{"target", "idle"},
					                                         {"conditions", Json::array({{{"left", observation(fact)},
					                                                                      {"op", "eq"},
					                                                                      {"right", 0}}})}}})}}})}};
					catalog.trees.emplace(key, helper);
					calls.push_back({{"call", key}});
				}
				policy["states"][0]["parent"] = "service";
				policy["states"].push_back({{"id", "service"}, {"initial", "orders"}, {"running", calls}});
				policy["initial"] = "service";
			}
			catalog.trees.emplace(id, std::move(policy));
			controller.erase("policy");
			controller.erase("fallback");
			controller["tree"] = id;
		}
	}
}
void expand_officer_policies(Catalog &catalog)
{
	for (auto &[entity_id, entity] : catalog.entities)
	{
		if (!entity.contains("control_layout"))
			continue;
		for (auto &controller : entity["control_layout"]["controllers"])
		{
			if (controller.contains("widget"))
				continue;
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
			// Only shared-action policies can delegate execution. Legacy direct-write packs retain their policy.
			if (controller.contains("tree") && !policy.contains("action_bindings"))
				continue;
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
			controller["tree"] = id;
			controller.erase("fallback");
			controller["officer_protocol"] = true;
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
		spec.tree = binding.at("tree");
		if (!names.insert(spec.id).second || result.size() >= 64)
			throw std::runtime_error("Duplicate/excessive Controller actions");
		auto definition = resolve_tree_definition(catalog.trees.at(spec.tree), catalog);
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
} // namespace phoenix
