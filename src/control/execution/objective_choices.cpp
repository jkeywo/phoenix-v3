module;
#include <algorithm>
#include <cstdint>
#include <functional>
#include <map>
#include <limits>
#include <nlohmann/json.hpp>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <variant>
#include <vector>

module phoenix.control.execution;
import phoenix.gameplay.ordnance.contacts;
import phoenix.control.coordination;

namespace phoenix
{
namespace
{
using ObjectiveFacts = std::map<std::string, state_tree::Value>;
std::vector<ObjectiveFacts> request_objective_facts(const ConsoleControllerRuntime &controller,
                                                    const Observation &observed, KnowledgeTick tick)
{
	using state_tree::Value;
	std::vector<ObjectiveFacts> result;
	for (const auto &request : controller.incoming_requests)
		if (request.operation == OfficerOperation::Manifest && request.expires > tick &&
		    (request.status == OfficerStatus::Requested || request.status == OfficerStatus::Accepted))
		{
			const Json *mission = nullptr;
			if (const auto rows = observed.gameplay.find("away_missions"); rows != observed.gameplay.end())
				for (const auto &row : *rows)
					if (row.at("id") == request.mission && row.at("target") == request.target)
						mission = &row;
			const bool delegated = controller_operation_owns(controller, "contribution", {}) &&
			                       !controller_operation_human(controller, "contribution") &&
			                       controller_operation_owns(controller, "coordination", {}) &&
			                       !controller_operation_human(controller, "coordination");
			const bool active = mission && (mission->at("phase") == "available" || mission->at("phase") == "open" ||
			                                mission->at("phase") == "recovered");
			const auto department = mission ? mission->value("department", Json::object()) : Json::object();
			const bool done =
			    delegated && department.value("ready", false) && department.value("assigned", 0LL) >= request.amount;
			const bool feasible =
			    delegated && mission && (done || (active && request.status == OfficerStatus::Requested));
			const auto reason = !delegated                                  ? "capabilities_unavailable"
			                    : !mission                                  ? "mission_not_disclosed"
			                    : done                                      ? "preparation_observed"
			                    : !active                                   ? "mission_inactive"
			                    : request.status == OfficerStatus::Accepted ? "awaiting_preparation_evidence"
			                                                                : "addressed_preparation_request";
			result.push_back(
			    {{"request.operation", Value::string("manifest")},
			     {"request.status", Value::string(officer_statuses.at(static_cast<std::size_t>(request.status)))},
			     {"request.mission", Value::string(request.mission)},
			     {"request.reason", Value::string(reason)},
			     {"request.id", Value::integer(request.id)},
			     {"request.target", Value::integer(request.target)},
			     {"request.amount", Value::integer(request.amount)},
			     {"request.priority", Value::integer(request.priority)},
			     {"request.expires", Value::integer(request.expires)},
			     {"request.revision", Value::integer(mission ? mission->at("revision").get<std::int64_t>() : 0)},
			     {"request.request_revision", Value::integer(request.request_revision)},
			     {"request.evidence_revision", Value::integer(request.evidence_revision)},
			     {"request.feasible", Value::boolean(feasible)},
			     {"request.done", Value::boolean(done)},
			     {"request.blocked", Value::boolean(!delegated || !mission || (!active && !done))}});
		}
	if (result.empty())
	{
		ObjectiveFacts empty;
		for (const auto &field : controller.brain->program.registry.observations)
			if (field.name.starts_with("request."))
				empty[field.name] = field.type == state_tree::Type::Text      ? Value::string("")
				                    : field.type == state_tree::Type::Boolean ? Value::boolean(false)
				                                                              : Value::integer(0);
		empty["request.reason"] = Value::string("no_pending_requests");
		result.push_back(std::move(empty));
	}
	return result;
}
std::vector<ObjectiveFacts> craft_objective_facts(const ConsoleControllerRuntime &controller,
                                                  const Observation &observed, KnowledgeTick tick,
                                                  std::int64_t objective_target,
                                                  std::span<const state_tree::Value> source)
{
	using state_tree::Value;
	std::int64_t accepted_target = 0;
	const auto &registry = controller.brain->program.registry;
	for (std::size_t i = 0; i < registry.observations.size(); ++i)
		if (registry.observations[i].name == "bay.accepted_target" && source[i].type == state_tree::Type::Integer)
			accepted_target = source[i].number;
	const auto target = objective_target ? objective_target : accepted_target;
	bool threat = false, target_present = false;
	for (const auto &contact : observed.contacts)
		if (contact.id > 0 && contact.id <= 2000000000ULL && contact.disposition == "hostile")
		{
			threat |= !objective_target || contact.id == objective_target;
			target_present |= contact.id == target;
		}
	std::vector<PointDefenceCandidate> missiles;
	for (const auto &contact : observed.contacts)
		if (contact.id >= 7000000000000000ULL && contact.id < 8000000000000000ULL &&
		    contact.disposition != "friendly" && (!objective_target || contact.id == objective_target))
			missiles.push_back({contact.id, {contact.x, contact.y}, {contact.vx, contact.vy}, contact.altitude});
	const bool missile_threat = select_native_point_defence_target(
	                                {observed.own.x, observed.own.y}, {observed.own.vx, observed.own.vy},
	                                observed.own.altitude, std::numeric_limits<std::int64_t>::max(), missiles) != 0;
	threat |= missile_threat;
	std::vector<ObjectiveFacts> result;
	if (const auto rows = observed.gameplay.find("craft"); rows != observed.gameplay.end())
		for (const auto &row : *rows)
		{
			const auto bay = row.at("bay").get<std::string>();
			if (!controller_operation_owns(controller, "craft", bay))
				continue;
			const bool delegated = !controller_operation_human(controller, "craft");
			const auto phase = row.at("phase").get<std::string>();
			const bool reserved = row.value("reserved", false) || !row.value("mission", std::string{}).empty();
			const bool eligible = delegated && !reserved && phase != "lost" && phase != "consumed";
			const auto age = tick >= row.at("report_tick").get<KnowledgeTick>()
			                     ? tick - row.at("report_tick").get<KnowledgeTick>()
			                     : 0;
			const auto energy = row.at("energy").get<std::int64_t>(), capacity = row.at("capacity").get<std::int64_t>();
			const bool reserve_low =
			    capacity > 0 && energy * 1000 <= capacity * row.value("return_reserve_permille", 200LL);
			const auto id = row.at("id").get<std::string>();
			ObjectiveFacts facts;
			const auto text = [&](const char *name, const std::string &value)
			{
				facts[std::string("bay.") + name] = Value::string(value);
			};
			const auto number = [&](const char *name, std::int64_t value)
			{
				facts[std::string("bay.") + name] = Value::integer(value);
			};
			const auto boolean = [&](const char *name, bool value)
			{
				facts[std::string("bay.") + name] = Value::boolean(value);
			};
			text("craft", id);
			text("id", bay);
			text("role", row.at("role"));
			text("phase", phase);
			text("order", row.at("order"));
			text("pending_order", row.value("pending_order", std::string{}));
			text("attack_text", target_present ? std::to_string(target) : std::string{});
			text("reason", !delegated  ? "capabilities_unavailable"
			               : reserved  ? "craft_reserved"
			               : !eligible ? "craft_unavailable"
			                           : "permitted_craft_report");
			number("entity", std::stoll(id));
			number("revision", row.at("revision"));
			number("target", row.at("target"));
			number("attack_target", target_present ? target : 0);
			number("report_age", std::min<KnowledgeTick>(1000000, age));
			number("energy", energy);
			number("ammunition", row.at("ammunition"));
			boolean("eligible", eligible);
			boolean("dispatch_available", eligible && (row.at("available").get<bool>() || phase == "flying") &&
			                                  row.value("blocker", std::string{}).empty());
			boolean("recall_available",
			        eligible && (phase != "docked" || row.at("pending").get<bool>()) && phase != "recovering");
			boolean("pending", row.at("pending"));
			boolean("linked", row.at("linked"));
			boolean("reserve_low", reserve_low);
			boolean("threat_present", threat);
			boolean("missile_threat", missile_threat);
			boolean("target_present", target_present);
			boolean("ftl_preparing", row.value("ftl_preparing", false));
			boolean("armed", row.value("armed", false));
			result.push_back(std::move(facts));
		}
	if (result.empty())
	{
		ObjectiveFacts empty;
		for (const auto &field : registry.observations)
			if (field.name.starts_with("bay.") && field.name != "bay.accepted_target")
				empty[field.name] = field.type == state_tree::Type::Text      ? Value::string("")
				                    : field.type == state_tree::Type::Boolean ? Value::boolean(false)
				                                                              : Value::integer(0);
		empty["bay.reason"] = Value::string("no_owned_craft");
		result.push_back(std::move(empty));
	}
	return result;
}
} // namespace
std::optional<ControllerObjectiveChoice> evaluate_controller_objectives(const ConsoleControllerRuntime &controller,
                                                                        const ControllerObservationSources &input,
                                                                        std::span<const state_tree::Value> source,
                                                                        Json &diagnostics)
{
	using state_tree::Value;
	const auto &brain = *controller.brain;
	const auto schema = input.definitions.schema("phoenix.objective");
	const auto field = [&](const KnowledgeRecord &record, const char *name) -> const KnowledgeValue *
	{
		const auto value = record.value.fields.find(input.definitions.field(schema, name));
		return value == record.value.fields.end() ? nullptr : &value->second;
	};
	const auto text = [&](const KnowledgeRecord &record, const char *name, const std::string &fallback)
	{
		const auto value = field(record, name);
		return value && std::get_if<std::string>(value) ? std::get<std::string>(*value) : fallback;
	};
	const auto number = [&](const KnowledgeRecord &record, const char *name, std::int64_t fallback = 0)
	{
		const auto value = field(record, name);
		return value && std::get_if<std::int64_t>(value) ? std::get<std::int64_t>(*value) : fallback;
	};
	std::vector<ObjectiveCandidate> candidates;
	std::vector<ControllerObjectiveChoice> choices;
	std::set<KnowledgeId> seen;
	const auto medical_options = brain.medical_base ? controller_medical_candidates(controller, input.observation)
	                                                : std::vector<std::vector<Value>>{};
	const auto request_options = std::any_of(brain.objective_bindings.begin(), brain.objective_bindings.end(),
	                                         [](const auto &binding)
	                                         {
		                                         return binding.candidate_source == "requests";
	                                         })
	                                 ? request_objective_facts(controller, input.observation, input.tick)
	                                 : std::vector<ObjectiveFacts>{};
	for (auto record = input.board.shared.rbegin(); record != input.board.shared.rend(); ++record)
	{
		if (record->value.schema != schema || !seen.insert(record->value.subject).second)
			continue;
		const auto behavior = text(*record, "behavior", "observe");
		bool supported = false;
		for (const auto &binding : brain.objective_bindings)
		{
			if (binding.behavior != behavior)
				continue;
			supported = true;
			auto options = binding.candidate_source == "medical" ? medical_options : std::vector<std::vector<Value>>{};
			if (binding.candidate_source == "craft" || binding.candidate_source == "requests")
				for (const auto &facts : binding.candidate_source == "craft"
				                             ? craft_objective_facts(controller, input.observation, input.tick,
				                                                     number(*record, "target"), source)
				                             : request_options)
				{
					std::vector<Value> values(source.begin(), source.end());
					for (std::size_t i = 0; i < brain.program.registry.observations.size(); ++i)
						if (const auto fact = facts.find(brain.program.registry.observations[i].name);
						    fact != facts.end())
							values[i] = fact->second;
					options.push_back(std::move(values));
				}
			if (options.empty())
				options.emplace_back();
			const auto before_binding = candidates.size();
			for (const auto &option : options)
			{
				auto values = std::vector<Value>(source.begin(), source.end());
				if ((binding.candidate_source == "craft" || binding.candidate_source == "requests") && !option.empty())
					values = option;
				const auto fact = [&](const char *name) -> const Value &
				{
					for (std::size_t i = 0; i < brain.program.registry.observations.size(); ++i)
						if (brain.program.registry.observations[i].name == name)
							return values[i];
					static const Value unavailable;
					return unavailable;
				};
				if (binding.candidate_source == "craft" &&
				    (!text(*record, "mission", "").empty() || !text(*record, "compartment", "").empty() ||
				     (!text(*record, "system", "").empty() && text(*record, "system", "") != fact("bay.id").text) ||
				     (behavior != "engage" && number(*record, "target") && number(*record, "target") != input.entity)))
					continue;

				if (binding.candidate_source == "requests" &&
				    ((!text(*record, "mission", "").empty() &&
				      text(*record, "mission", "") != fact("request.mission").text) ||
				     !text(*record, "system", "").empty() || !text(*record, "compartment", "").empty() ||
				     (number(*record, "target") && number(*record, "target") != fact("request.target").number)))
					continue;
				if (binding.candidate_source == "medical" && !option.empty())
				{
					const auto base = *brain.medical_base;
					std::copy(option.begin(), option.end(), values.begin() + base);
					if (!text(*record, "mission", "").empty() ||
					    (number(*record, "target") &&
					     number(*record, "target") != static_cast<std::int64_t>(input.entity)) ||
					    (!text(*record, "compartment", "").empty() &&
					     text(*record, "compartment", "") != option[9].text) ||
					    (!text(*record, "system", "").empty() && text(*record, "system", "") != option[0].text))
						continue;
				}
				ObjectiveCandidate candidate;
				candidate.objective = record->value.subject;
				candidate.tier = objective_tier(text(*record, "tier", "objective_other"));
				candidate.marked = input.priority_objectives.contains(candidate.objective);
				candidate.action = binding.id + "/" + binding.action;
				candidate.reason = record->value.withdrawn                   ? "withdrawn"
				                   : input.stale(*record, input.tick)        ? "stale_evidence"
				                   : text(*record, "status", "") != "active" ? "objective_inactive"
				                                                             : "binding_not_applicable";
				ControllerObjectiveChoice choice;
				choice.objective = candidate.objective;
				choice.action = binding.action;
				choice.binding = binding.id;
				if (binding.candidate_source == "requests")
					candidate.target = fact("request.id").number;
				if (binding.candidate_source == "craft")
					candidate.target = fact("bay.entity").number;
				if (binding.candidate_source == "medical" && !option.empty())
					candidate.target = option[4].number;
				for (std::size_t i = 0; i < brain.program.registry.observations.size(); ++i)
				{
					const auto &name = brain.program.registry.observations[i].name;
					if (!name.starts_with("objective.record."))
						continue;
					const auto key = name.substr(17);
					if (key == "id")
						values[i] = Value::integer(candidate.objective);
					else if (key == "behavior" || key == "tier" || key == "operation" || key == "compartment" ||
					         key == "system" || key == "mission")
						values[i] = Value::string(text(*record, key.c_str(), key == "operation" ? behavior : ""));
					else if (key == "age")
						values[i] = Value::integer(
						    std::min<std::uint64_t>(1000000, input.tick >= record->value.provenance.observed_tick
						                                         ? input.tick - record->value.provenance.observed_tick
						                                         : 0));
					else if (key == "target" || key == "urgency")
						values[i] = Value::integer(number(*record, key.c_str()));
					else if (const auto value = field(*record, key.c_str()); value && std::get_if<std::int64_t>(value))
						values[i] = Value::integer(std::get<std::int64_t>(*value));
					else
						values[i] = {};
				}
				if (!record->value.withdrawn && !input.stale(*record, input.tick) &&
				    text(*record, "status", "") == "active")
				{
					state_tree::Instance scratch;
					const auto offered = state_tree::step(binding.program, scratch, values);
					if (!offered.outputs.empty())
					{
						const auto &arguments = offered.outputs.front().arguments;
						candidate.target = arguments[0].number >= 0 ? arguments[0].number : 0;
						candidate.factors = {arguments[1].number, arguments[2].number, arguments[3].number,
						                     arguments[4].number, arguments[5].number};
						choice.target = candidate.target;
						choice.arguments.assign(arguments.begin() + 7, arguments.end());
						const auto action = std::find_if(controller.actions.begin(), controller.actions.end(),
						                                 [&](const auto &spec)
						                                 {
							                                 return spec.id == binding.action;
						                                 });
						candidate.reason = "execution_unavailable";
						if (action != controller.actions.end())
						{
							for (std::size_t i = 0; i < action->inputs.size(); ++i)
								values[action->inputs[i]] = choice.arguments[i];
							state_tree::Instance availability;
							const auto eligible = state_tree::step(action->availability, availability, values);
							candidate.feasible =
							    !eligible.debug.active.empty() && eligible.debug.active.back() == "available";
							bool writes = false, delegated = false;
							std::function<void(const state_tree::Program &)> capabilities;
							capabilities = [&](const auto &program)
							{
								std::function<void(const std::vector<state_tree::Task> &)> visit;
								visit = [&](const auto &tasks)
								{
									for (const auto &task : tasks)
									{
										if (task.kind == state_tree::TaskKind::Action)
										{
											const auto &name = program.registry.actions.at(task.target).name;
											if (name.starts_with("write."))
												for (std::size_t channel = 0;
												     channel < controller.definition.commands.size(); ++channel)
													if (controller.definition.commands[channel].id == name.substr(6))
													{
														writes = true;
														delegated |= !controller_command_human(controller, channel);
													}
										}
										for (const auto &branch : task.branches)
											visit(branch);
									}
								};
								for (const auto &state : program.states)
								{
									visit(state.entry);
									visit(state.running);
									visit(state.exit);
								}
								for (const auto &reference : program.references)
									capabilities(reference);
							};
							capabilities(action->program);
							if (writes && !delegated)
							{
								candidate.feasible = false;
								candidate.reason = "capabilities_unavailable";
							}
							if (candidate.feasible)
								candidate.reason = "permitted_action";
						}
						Json identity = Json::array();
						for (const auto &argument : choice.arguments)
							identity.push_back({static_cast<unsigned>(argument.type), argument.number, argument.text});
						const auto details = identity.dump();
						// A stable hash keeps the persisted invocation bounded. Ties
						// compare the complete argument identity, never hash order.
						std::uint64_t hash = 14695981039346656037ULL;
						for (const unsigned char byte : details)
						{
							hash ^= byte;
							hash *= 1099511628211ULL;
						}
						choice.invocation = "objective/" + std::to_string(choice.objective) + "/" +
						                    std::to_string(choice.target) + "/" + binding.id + "/" + choice.action +
						                    "/" + std::to_string(hash);
						choice.commitment = choice.invocation;
						choice.execution = "execution/" + choice.action + "/" + std::to_string(choice.target) + "/" +
						                   std::to_string(hash);
						choice.retry_ticks = binding.retry_ticks;
						std::uint64_t attempt = 0;
						if (const auto retry = controller.objective_retries.find(choice.execution);
						    retry != controller.objective_retries.end() && retry->second.action == choice.action &&
						    retry->second.arguments == choice.arguments)
						{
							attempt = retry->second.attempt;
							if (retry->second.not_before > input.tick)
							{
								candidate.feasible = false;
								candidate.reason = "execution_backoff";
							}
						}
						choice.invocation += "/attempt/" + std::to_string(attempt);
						candidate.action += "/" + details;
						candidate.committed =
						    arguments[6].number != 0 ||
						    (controller.objective_commitment == choice.commitment &&
						     controller.action.status == "running" && controller.action.id == choice.action &&
						     controller.action.inputs == choice.arguments);
					}
					else if (binding.candidate_source == "medical" && brain.medical_base &&
					         !values[*brain.medical_base + 3].number)
						candidate.reason = values[*brain.medical_base + 10].text;
					else if (binding.candidate_source == "craft" && !fact("bay.eligible").number)
						candidate.reason = fact("bay.reason").text;
					else if (binding.candidate_source == "requests" && !fact("request.feasible").number)
						candidate.reason = fact("request.reason").text;
				}
				const auto policy = brain.objective_scoring.find(text(*record, "scoring_policy", "default"));
				if (policy == brain.objective_scoring.end())
				{
					candidate.feasible = false;
					candidate.reason = "unsupported_scoring_policy";
				}
				else
				{
					const auto &w = policy->second;
					candidate.policy = {w[0], w[1], w[2], w[3], w[4], w[5]};
				}
				candidates.push_back(std::move(candidate));
				choices.push_back(std::move(choice));
			}
			if (candidates.size() == before_binding)
			{
				ObjectiveCandidate candidate;
				candidate.objective = record->value.subject;
				candidate.action = binding.id;
				candidate.tier = objective_tier(text(*record, "tier", "objective_other"));
				candidate.marked = input.priority_objectives.contains(candidate.objective);
				candidate.reason =
				    binding.candidate_source == "medical" ? "objective_parameter_outside_known_medical_scope"
				    : binding.candidate_source == "craft" ? "objective_parameter_outside_known_craft_scope"
				                                          : "objective_parameter_outside_addressed_request_scope";
				candidates.push_back(std::move(candidate));
				choices.emplace_back();
			}
		}
		if (!supported)
		{
			ObjectiveCandidate candidate;
			candidate.objective = record->value.subject;
			candidate.action = behavior;
			candidate.reason = "unsupported_behavior";
			candidates.push_back(std::move(candidate));
			choices.emplace_back();
		}
	}
	const auto selected = select_objective(candidates);
	diagnostics = Json::array();
	for (std::size_t i = 0; i < candidates.size(); ++i)
	{
		const auto &candidate = candidates[i];
		const auto score = score_objective(candidate);
		const auto retry = controller.objective_retries.find(choices[i].execution);
		const auto not_before = retry == controller.objective_retries.end() ? 0 : retry->second.not_before;
		const auto attempt = retry == controller.objective_retries.end() ? 0 : retry->second.attempt;
		diagnostics.push_back({{"objective", candidate.objective},
		                       {"target", candidate.target},
		                       {"action", choices[i].action.empty() ? candidate.action : choices[i].action},
		                       {"binding", choices[i].binding},
		                       {"tier", objective_tier_name(candidate.tier)},
		                       {"marked", candidate.marked},
		                       {"feasible", candidate.feasible},
		                       {"reason", candidate.reason},
		                       {"score", score.total},
		                       {"terms", score.terms},
		                       {"commitment", choices[i].commitment},
		                       {"retry_not_before", not_before},
		                       {"attempt", attempt},
		                       {"selected", selected && *selected == i}});
	}
	return selected ? std::optional<ControllerObjectiveChoice>{choices[*selected]} : std::nullopt;
}
} // namespace phoenix
