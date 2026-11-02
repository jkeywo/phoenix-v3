module;
#include <algorithm>
#include <array>
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

module phoenix.control.decisions;
import phoenix.gameplay.ordnance.contacts;
import phoenix.control.coordination;

namespace phoenix
{
#include "candidate_adapters.hpp"
std::vector<std::map<std::string, state_tree::Value>>
controller_armament_candidates(const ConsoleControllerRuntime &, const ControllerObservationSources &);
std::vector<std::map<std::string, state_tree::Value>> controller_scan_candidates(const ConsoleControllerRuntime &,
                                                                                 const ControllerObservationSources &);
std::vector<std::map<std::string, state_tree::Value>>
controller_evidence_candidates(const ConsoleControllerRuntime &, const ControllerObservationSources &,
                               const ControllerObjectiveBinding &);
std::vector<std::map<std::string, state_tree::Value>>
controller_docking_candidates(const ConsoleControllerRuntime &, const ControllerObservationSources &,
                              const ControllerObjectiveBinding &, const KnowledgeRecord &);
std::vector<std::map<std::string, state_tree::Value>>
controller_personnel_candidates(const ConsoleControllerRuntime &, const ControllerObservationSources &,
                                const KnowledgeRecord &);
std::vector<std::map<std::string, state_tree::Value>>
controller_delivery_candidates(const ConsoleControllerRuntime &, const ControllerObservationSources &,
                               const KnowledgeRecord &);
namespace
{
std::vector<ObjectiveFacts> away_objective_facts(const ConsoleControllerRuntime &controller,
                                                 const Observation &observed, KnowledgeTick tick)
{
	using state_tree::Value;
	std::vector<ObjectiveFacts> result;
	const auto rows = observed.gameplay.find("away_missions");
	if (rows != observed.gameplay.end())
		for (const auto &row : *rows)
		{
			const auto mission = row.at("id").get<std::string>(), phase = row.at("phase").get<std::string>();
			const auto department = row.value("department", Json::object());
			const auto dispatch = row.value("dispatch_system", std::string{});
			const bool staging = phase == "open" || phase == "recovered";
			const bool contributing = department.value("contributing", false);
			bool requested = false;
			for (const auto &request : controller.incoming_requests)
				requested |= request.operation == OfficerOperation::Manifest && request.mission == mission &&
				             request.target == row.at("target").get<std::int64_t>() && request.expires > tick &&
				             request.status == OfficerStatus::Accepted;
			const auto delegated = [&](const std::string &operation, const std::string &system)
			{
				return controller_operation_owns(controller, operation, system) &&
				       !controller_operation_human(controller, operation);
			};
			const auto offer = [&](const std::string &operation, const std::string &system,
			                       const std::string &selection, bool eligible, bool required = false,
			                       const std::string &peer_console = {}, const std::string &peer_controller = {},
			                       const std::string &request_status = "none", std::int64_t retry_age = 1000000)
			{
				ObjectiveFacts facts;
				const auto text = [&](const char *key, const std::string &value)
				{
					facts[std::string("away_candidate.") + key] = Value::string(value);
				};
				const auto number = [&](const char *key, std::int64_t value)
				{
					facts[std::string("away_candidate.") + key] = Value::integer(value);
				};
				const auto flag = [&](const char *key, bool value)
				{
					facts[std::string("away_candidate.") + key] = Value::boolean(value);
				};
				text("mission", mission);
				text("phase", phase);
				text("operation", operation);
				text("selection", selection);
				text("system", system);
				text("peer_console", peer_console);
				text("peer_controller", peer_controller);
				text("request_status", request_status);
				text("reason", eligible ? "permitted_manifest_choice" : "manifest_work_unavailable");
				number("target", row.at("target"));
				number("revision", row.at("revision"));
				number("amount", 1);
				number("retry_age", retry_age);
				number("travel", 0);
				number("cost", 0);
				flag("eligible", eligible);
				flag("requested", requested);
				flag("contributing", contributing);
				flag("ready", department.value("ready", false));
				flag("required", required);
				flag("offered",
				     row.value("ai_dispatch", false) || (staging && contributing && row.value("dispatch", false)));
				result.push_back(std::move(facts));
			};
			offer("open", dispatch, "", phase == "available" && delegated("mission", dispatch));
			offer("contribute", "", "1", staging && !contributing && delegated("contribution", {}));
			offer("send", dispatch, "",
			      phase == "open" && department.value("launch_ready", false) && delegated("mission_send", dispatch));
			offer("recall", dispatch, "",
			      phase == "deployed" &&
			          (row.value("work_complete", false) || department.value("recall_requested", false)) &&
			          delegated("mission", dispatch));
			for (const auto &peer : department.value("peers", Json::array()))
			{
				const auto peer_console = peer.at("console").get<std::string>();
				const auto peer_controller = peer.at("controller").get<std::string>();
				std::string status = "none";
				std::int64_t retry_age = 1000000;
				std::uint64_t completed_revision = 0;
				// Peer routes are declared identities, resolved within the same ship.
				// The continuation stores the delivery actor, not another Controller's state.
				const KnowledgeId peer_id = peer.at("id");
				for (const auto &request : controller.outgoing_requests)
					if (request.operation == OfficerOperation::Manifest && request.peer == peer_id &&
					    request.mission == mission)
					{
						status = officer_statuses.at(static_cast<std::size_t>(request.status));
						completed_revision = request.evidence_revision;
						retry_age = std::clamp<std::int64_t>(tick - request.reply_tick, 0, 1000000);
					}
				const bool waiting =
				    status == "requested" || status == "accepted" ||
				    (status == "completed" && completed_revision >= row.at("revision").get<std::uint64_t>());
				offer("request", dispatch, "",
				      !waiting && (phase == "available" || staging) &&
				          (delegated("mission", dispatch) ||
				           (staging && contributing && row.value("dispatch", false) &&
				            controller_operation_owns(controller, "mission", dispatch))) &&
				          delegated("coordination", {}),
				      false, peer_console, peer_controller, status, retry_age);
			}
			const bool recovery = phase == "deployed" && department.value("transport_released", false) &&
			                      department.value("recall_requested", false);
			const auto transports = department.value("transport_systems", Json::object());
			if ((staging && contributing && row.at("transport").get<std::string>().empty()) || recovery)
				for (const auto &[selection, system] : transports.items())
				{
					bool suitable = !recovery;
					if (recovery)
						for (const auto &craft : observed.gameplay.value("craft", Json::array()))
							if ("craft:" + craft.at("id").get<std::string>() == selection)
								suitable = craft.at("role") == "shuttle" && craft.value("recovery_available", false) &&
								           craft.value("transport_capacity", 0ULL) >=
								               row.at("accompanying").size() +
								                   std::count_if(row.at("slots").begin(), row.at("slots").end(),
								                                 [](const auto &slot)
								                                 {
									                                 return slot.at("assigned") != "";
								                                 });
					if (suitable)
						offer("transport", system.get<std::string>(), selection, delegated("transport", system), true);
				}
			if (!staging || !contributing)
				continue;
			for (const auto &slot : row.at("slots"))
				if (slot.at("assigned").get<std::string>().empty())
					for (const auto &choice : slot.at("choices"))
					{
						const auto asset = choice.get<std::string>();
						const bool officer = asset.starts_with("officer:");
						const auto system = officer ? std::string{} : asset.substr(5);
						offer("assign", system, slot.at("id").get<std::string>() + "/" + asset,
						      delegated(officer ? "mission_staffing" : "transport", system), slot.at("required"));
					}
			for (const auto &slot : row.at("craft_slots"))
				if (slot.at("assigned").get<std::string>().empty())
					for (const auto &choice : slot.at("choices"))
					{
						const auto asset = choice.get<std::string>();
						// Bay ownership is carried by the frozen craft report.
						for (const auto &craft : observed.gameplay.value("craft", Json::array()))
							if ("craft:" + craft.at("id").get<std::string>() == asset)
								offer("escort", craft.at("bay"), slot.at("id").get<std::string>() + "/" + asset,
								      delegated("transport", craft.at("bay")), slot.at("required"));
					}
		}
	return result;
}
std::vector<ObjectiveFacts> request_objective_facts(const ConsoleControllerRuntime &controller,
                                                    const Observation &observed, KnowledgeTick tick,
                                                    std::span<const state_tree::Value> source)
{
	using state_tree::Value;
	std::vector<ObjectiveFacts> result;
	for (const auto &request : controller.incoming_requests)
		if ((request.operation == OfficerOperation::Power || request.operation == OfficerOperation::Cooling) &&
		    request.expires > tick &&
		    (request.status == OfficerStatus::Requested || request.status == OfficerStatus::Accepted ||
		     request.status == OfficerStatus::Completed))
		{
			const bool power = request.operation == OfficerOperation::Power;
			bool supported = false, delegated = true, observed_all = true, done = true, needed = false, blocked = false;
			std::int64_t minimum = 0, maximum = 1000000000, limit = request.requested_amount;
			for (std::size_t i = 0; i < controller.brain->program.registry.observations.size(); ++i)
				if (controller.brain->program.registry.observations[i].name == "summary.level" &&
				    source[i].type == state_tree::Type::Integer)
					limit = std::min(limit, source[i].number);
			for (std::size_t i = 0; i < controller.definition.commands.size(); ++i)
			{
				const auto &command = controller.definition.commands[i];
				if (command.action != (power ? OperatingAction::GeneratorOutput : OperatingAction::CoolingAllocation))
					continue;
				supported = true;
				delegated &= !controller_command_human(controller, i);
				minimum = std::max(minimum, command.minimum);
				maximum = std::min(maximum, command.maximum);
			}
			if (minimum <= maximum)
				limit = std::clamp(limit, minimum, maximum);
			else
				blocked = true;
			blocked |= request.requested_amount < minimum;
			for (std::size_t i = 0; i < controller.definition.commands.size(); ++i)
			{
				const auto &command = controller.definition.commands[i];
				if (command.action != (power ? OperatingAction::GeneratorOutput : OperatingAction::CoolingAllocation))
					continue;
				needed |= controller.commands.at(i).value != limit;
				for (const auto &id : command.consumer_ids)
				{
					bool seen = false;
					for (const auto &system : observed.systems)
						if (system.id == id)
						{
							seen = true;
							const auto &t = system.telemetry;
							blocked |= (t.contains("enabled") && !t.at("enabled")) ||
							           (t.contains("thermal_stopped") && t.at("thermal_stopped"));
							const auto field = power ? "actual_output" : "actual_coolant_used";
							done &= t.contains(field) && (power ? t.at(field) == limit : t.at(field) >= limit);
						}
					observed_all &= seen;
				}
			}
			std::int64_t conflict_priority = -1, conflict_id = 0;
			for (const auto &other : controller.incoming_requests)
				if (other.id != request.id && other.operation == request.operation && other.expires > tick &&
				    (other.status == OfficerStatus::Accepted || other.status == OfficerStatus::Completed))
					if (other.priority > conflict_priority ||
					    (other.priority == conflict_priority && static_cast<std::int64_t>(other.id) < conflict_id))
					{
						conflict_priority = other.priority;
						conflict_id = other.id;
					}
			delegated &= controller_operation_owns(controller, "coordination", {}) &&
			             !controller_operation_human(controller, "coordination");
			const bool feasible = supported && delegated && observed_all && !blocked;
			result.push_back(
			    {{"request.operation", Value::string(power ? "power" : "cooling")},
			     {"request.status", Value::string(officer_statuses.at(static_cast<std::size_t>(request.status)))},
			     {"request.mission", Value::string("")},
			     {"request.reason", Value::string(!supported || !delegated ? "capabilities_unavailable"
			                                      : !observed_all          ? "supply_evidence_unavailable"
			                                      : blocked                ? "supply_blocked"
			                                      : done                   ? "supply_observed"
			                                      : needed                 ? "supply_command_required"
			                                                               : "awaiting_supply_evidence")},
			     {"request.id", Value::integer(request.id)},
			     {"request.target", Value::integer(request.target)},
			     {"request.amount", Value::integer(request.amount)},
			     {"request.limit", Value::integer(limit)},
			     {"request.priority", Value::integer(request.priority)},
			     {"request.expires", Value::integer(request.expires)},
			     {"request.revision", Value::integer(observed.observed_tick)},
			     {"request.request_revision", Value::integer(request.request_revision)},
			     {"request.evidence_revision", Value::integer(observed.observed_tick)},
			     {"request.conflict_priority", Value::integer(conflict_priority)},
			     {"request.conflict_id", Value::integer(conflict_id)},
			     {"request.feasible", Value::boolean(feasible)},
			     {"request.done", Value::boolean(feasible && done)},
			     {"request.blocked", Value::boolean(!feasible)},
			     {"request.command_needed", Value::boolean(needed)},
			     {"request.continuing",
			      Value::boolean(controller.action.status == "running" && !controller.action.inputs.empty() &&
			                     controller.action.inputs.front() == Value::integer(request.id))}});
		}

	for (const auto &request : controller.incoming_requests)
		if (request.operation == OfficerOperation::Manoeuvre && request.expires > tick &&
		    (request.status == OfficerStatus::Requested || request.status == OfficerStatus::Accepted))
		{
			bool known = false, heading = false, speed = false, delegated = true, blocked = false;
			std::int64_t bearing = observed.own.heading, distance = 0, approach = 0;
			for (const auto &contact : observed.contacts)
				if (contact.id == request.target && contact.disposition == "hostile")
				{
					known = true;
					distance = fixed::length({contact.x - observed.own.x, contact.y - observed.own.y});
					bearing = fixed::bearing({contact.x - observed.own.x, contact.y - observed.own.y});
				}
			for (std::size_t i = 0; i < controller.definition.commands.size(); ++i)
			{
				const auto &command = controller.definition.commands[i];
				if (command.action != OperatingAction::Speed && command.action != OperatingAction::Heading)
					continue;
				heading |= command.action == OperatingAction::Heading;
				speed |= command.action == OperatingAction::Speed;
				delegated &= !controller_command_human(controller, i);
				for (const auto &id : command.consumer_ids)
					for (const auto &system : observed.systems)
						if (system.id == id && system.telemetry.contains("enabled") && !system.telemetry.at("enabled"))
							blocked = true;
			}
			for (std::size_t i = 0; i < controller.brain->program.registry.observations.size(); ++i)
			{
				const auto &name = controller.brain->program.registry.observations[i].name;
				if (name == "drive.approach_speed" && source[i].type == state_tree::Type::Integer)
					approach = source[i].number;
				if (name == "navigation.blocked" && source[i].type == state_tree::Type::Boolean)
					blocked |= source[i].number != 0;
			}
			blocked |= observed.gameplay.value("stance", Json::object()).value("id", std::string{}) == "hold";
			delegated &= controller_operation_owns(controller, "coordination", {}) &&
			             !controller_operation_human(controller, "coordination");
			const bool feasible =
			    delegated && known && speed && approach > 0 && !blocked && (heading || bearing == observed.own.heading);
			result.push_back(
			    {{"request.operation", Value::string("manoeuvre")},
			     {"request.status", Value::string(officer_statuses.at(static_cast<std::size_t>(request.status)))},
			     {"request.mission", Value::string("")},
			     {"request.reason",
			      Value::string(!known                                        ? "target_not_observed"
			                    : !delegated || !speed                        ? "capabilities_unavailable"
			                    : blocked                                     ? "navigation_blocked"
			                    : !heading && bearing != observed.own.heading ? "heading_not_delegated"
			                    : distance <= request.amount                  ? "requested_range_observed"
			                                                                  : "addressed_manoeuvre_work")},
			     {"request.id", Value::integer(request.id)},
			     {"request.target", Value::integer(request.target)},
			     {"request.amount", Value::integer(request.amount)},
			     {"request.limit", Value::integer(request.amount)},
			     {"request.priority", Value::integer(request.priority)},
			     {"request.expires", Value::integer(request.expires)},
			     {"request.bearing", Value::integer(bearing)},
			     {"request.distance", Value::integer(distance)},
			     {"request.speed", Value::integer(approach)},
			     {"request.evidence_revision", Value::integer(observed.observed_tick)},
			     {"request.feasible", Value::boolean(feasible)},
			     {"request.done", Value::boolean(feasible && distance <= request.amount)},
			     {"request.blocked", Value::boolean(!feasible)}});
		}
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
	for (const auto &request : controller.incoming_requests)
		if (request.operation == OfficerOperation::Repair && request.expires > tick &&
		    (request.status == OfficerStatus::Requested || request.status == OfficerStatus::Accepted))
		{
			bool known = false, inspected = false, damaged = false, delegated = false;
			std::string compartment;
			KnowledgeTick inspection_revision = observed.observed_tick;
			for (const auto &room : combat_observation(observed).at("compartments"))
				if (room.at("index") == request.target)
				{
					known = true;
					compartment = room.at("id");
					inspected = room.value("inspected", false) ||
					            (room.value("reported_inspection", false) &&
					             room.value("inspection_tick", 0ULL) >= request.reply_tick &&
					             room.value("inspection_fault_count", -1LL) == 0 &&
					             room.value("inspection_hp", -1LL) >= room.value("inspection_maximum", 0LL));
					if (!room.value("inspected", false) && inspected)
						inspection_revision = room.at("inspection_revision");
					damaged = room.at("hp") < room.at("maximum");
				}
			for (const auto &fault : combat_observation(observed).at("faults"))
				damaged |= fault.value("compartment_index", 0LL) == request.target;
			for (const auto &team : observed.gameplay.value("teams", Json::array()))
				if (team.value("repair", false) && team.value("available", false) && !team.value("reserved", false) &&
				    !team.value("assignment_authority", false) &&
				    controller_operation_owns(controller, "team_assignment", team.at("id")) &&
				    !controller_operation_human(controller, "team_assignment"))
				{
					const auto &rooms = team.at("compartments");
					for (std::size_t target = 0; target < rooms.size(); ++target)
						if (rooms[target] == compartment)
							for (const auto &route : team.value("routes", Json::array()))
								delegated |= route.at("compartment") == target && route.value("reachable", false);
				}
			const bool coordination = controller_operation_owns(controller, "coordination", {}) &&
			                          !controller_operation_human(controller, "coordination");
			const bool done = known && inspected && !damaged;
			const bool feasible =
			    coordination && known && (done || (delegated && request.status == OfficerStatus::Requested));
			result.push_back(
			    {{"request.operation", Value::string("repair")},
			     {"request.status", Value::string(officer_statuses.at(static_cast<std::size_t>(request.status)))},
			     {"request.mission", Value::string("")},
			     {"request.reason", Value::string(done                          ? "permitted_inspection_complete"
			                                      : !known                      ? "compartment_not_disclosed"
			                                      : !coordination || !delegated ? "capabilities_or_route_unavailable"
			                                                                    : "awaiting_repair_evidence")},
			     {"request.id", Value::integer(request.id)},
			     {"request.target", Value::integer(request.target)},
			     {"request.amount", Value::integer(request.amount)},
			     {"request.priority", Value::integer(request.priority)},
			     {"request.expires", Value::integer(request.expires)},
			     {"request.revision", Value::integer(0)},
			     {"request.request_revision", Value::integer(request.request_revision)},
			     {"request.evidence_revision", Value::integer(inspection_revision)},
			     {"request.feasible", Value::boolean(feasible)},
			     {"request.done", Value::boolean(done)},
			     {"request.blocked", Value::boolean(!known || !coordination || (!delegated && !done))}});
		}
	for (const auto &request : controller.incoming_requests)
		if (request.operation == OfficerOperation::Scan && request.expires > tick &&
		    (request.status == OfficerStatus::Requested || request.status == OfficerStatus::Accepted))
		{
			std::set<std::string> attached;
			for (const auto &[role, systems] : controller.definition.equipment)
				attached.insert(systems.begin(), systems.end());
			bool supported = false, delegated = false;
			if (const auto sensors = observed.gameplay.find("sensors"); sensors != observed.gameplay.end())
				for (const auto &scanner : sensors->at("scanners"))
					supported |= attached.contains(scanner.at("id").get<std::string>()) &&
					             scanner.at("capabilities").contains(request.capability);
			for (std::size_t i = 0; i < controller.definition.commands.size(); ++i)
				delegated |= controller.definition.commands[i].action == OperatingAction::SensorTarget &&
				             !controller_command_human(controller, i);
			const bool coordination = controller_operation_owns(controller, "coordination", {}) &&
			                          !controller_operation_human(controller, "coordination");
			const auto proof = scan_completion_evidence(observed, request);
			const bool done = proof.has_value();
			result.push_back(
			    {{"request.operation", Value::string("scan")},
			     {"request.status", Value::string(officer_statuses.at(static_cast<std::size_t>(request.status)))},
			     {"request.mission", Value::string("")},
			     {"request.capability", Value::string(request.capability)},
			     {"request.reason", Value::string(done         ? "permitted_scan_evidence_complete"
			                                      : !supported ? "scan_capability_unavailable"
			                                                   : "awaiting_scan_evidence")},
			     {"request.id", Value::integer(request.id)},
			     {"request.target", Value::integer(request.target)},
			     {"request.amount", Value::integer(request.amount)},
			     {"request.priority", Value::integer(request.priority)},
			     {"request.expires", Value::integer(request.expires)},
			     {"request.revision", Value::integer(0)},
			     {"request.request_revision", Value::integer(request.request_revision)},
			     {"request.evidence_revision", Value::integer(proof.value_or(0))},
			     {"request.feasible", Value::boolean(coordination && delegated && supported &&
			                                         (request.status == OfficerStatus::Requested || done))},
			     {"request.done", Value::boolean(done)},
			     {"request.blocked", Value::boolean(!coordination || !delegated || !supported)}});
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
                                                  const ControllerObservationSources &input,
                                                  std::int64_t objective_target,
                                                  std::span<const state_tree::Value> source)
{
	using state_tree::Value;
	const auto &observed = input.observation;
	const auto tick = input.tick;
	const auto known_contacts = controller_contact_candidates(controller, input);
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
			const bool eligible = delegated && phase != "lost" && phase != "consumed";
			const auto age = tick >= row.at("report_tick").get<KnowledgeTick>()
			                     ? tick - row.at("report_tick").get<KnowledgeTick>()
			                     : 0;
			const auto energy = row.at("energy").get<std::int64_t>(), capacity = row.at("capacity").get<std::int64_t>();
			const bool reserve_low =
			    capacity > 0 && energy * 1000 <= capacity * row.value("return_reserve_permille", 200LL);
			const auto id = row.at("id").get<std::string>();
			bool order_target_known = row.at("target") == observed.own.id;
			std::int64_t order_target_age = order_target_known ? 0 : 1000000;
			for (const auto &contact : known_contacts)
				if (contact.contains("contact_candidate.id") && contact.at("contact_candidate.available").number &&
				    contact.at("contact_candidate.id").number == row.at("target").get<std::int64_t>())
				{
					order_target_known = true;
					order_target_age = contact.at("contact_candidate.age").number;
				}
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
			text("reason", !delegated                          ? "capabilities_unavailable"
			               : !eligible                         ? "craft_unavailable"
			               : row.value("recall_active", false) ? "accepted_recovery_in_progress"
			               : reserved                          ? "craft_reserved"
			                                                   : "permitted_craft_report");
			number("entity", std::stoll(id));
			number("revision", row.at("revision"));
			number("target", row.at("target"));
			number("attack_target", target_present ? target : 0);
			number("report_age", std::min<KnowledgeTick>(1000000, age));
			number("order_target_age", order_target_age);
			number("energy", energy);
			number("ammunition", row.at("ammunition"));
			boolean("eligible", eligible);
			boolean("dispatch_available", eligible && !reserved &&
			                                  (row.at("available").get<bool>() || phase == "flying") &&
			                                  row.value("blocker", std::string{}).empty());
			boolean("recall_available", eligible && !row.value("recall_active", false) &&
			                                (!reserved || row.value("ftl_preparing", false)) &&
			                                (phase != "docked" || row.at("pending").get<bool>()) &&
			                                phase != "recovering" &&
			                                !(row.at("role") == "boarding_torpedo" && phase != "docked"));
			boolean("pending", row.at("pending"));
			boolean("recall_active", row.value("recall_active", false));
			boolean("linked", row.at("linked"));
			boolean("reserve_low", reserve_low);
			boolean("threat_present", threat);
			boolean("missile_threat", missile_threat);
			boolean("target_present", target_present);
			boolean("order_target_known", order_target_known);
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

std::int64_t objective_target(const ControllerObservationSources &input, const KnowledgeRecord &record)
{
	const auto field = input.definitions.field(record.value.schema, "target");
	const auto found = record.value.fields.find(field);
	return found != record.value.fields.end() && std::get_if<std::int64_t>(&found->second)
	           ? std::get<std::int64_t>(found->second)
	           : 0;
}
} // namespace
CandidateAdapter single_adapter()
{
	return {"single",
	        "objective_parameter_outside_addressed_request_scope",
	        false,
	        [](const CandidateCache &cache, const ConsoleControllerRuntime &controller,
	           const ControllerObservationSources &input, const ControllerObjectiveBinding &binding,
	           const KnowledgeRecord &record,
	           std::span<const state_tree::Value> source) -> std::vector<DecisionCandidateFrame>
	        {
		        return {};
	        },
	        [](CandidateContext &context)
	        {
		        return true;
	        },
	        [](CandidateContext &context, ObjectiveCandidate &candidate)
	        {
	        },
	        [](CandidateContext &context, ObjectiveCandidate &candidate)
	        {
	        },
	        [](CandidateContext &context) -> Json
	        {
		        return {};
	        }};
}

CandidateAdapter craft_adapter()
{
	return {"craft",
	        "objective_parameter_outside_known_craft_scope",
	        false,
	        [](const CandidateCache &cache, const ConsoleControllerRuntime &controller,
	           const ControllerObservationSources &input, const ControllerObjectiveBinding &binding,
	           const KnowledgeRecord &record,
	           std::span<const state_tree::Value> source) -> std::vector<DecisionCandidateFrame>
	        {
		        return candidate_frames(
		            craft_objective_facts(controller, input, objective_target(input, record), source), controller,
		            source);
	        },
	        [](CandidateContext &context)
	        {
		        if (context.binding.candidate_source == "craft" &&
		            (!context.text(*context.record, "mission", "").empty() ||
		             !context.text(*context.record, "compartment", "").empty() ||
		             (!context.text(*context.record, "system", "").empty() &&
		              context.text(*context.record, "system", "") != context.fact("bay.id").text) ||
		             (context.behavior != "engage" && context.number(*context.record, "target") &&
		              context.number(*context.record, "target") != context.input.entity)))
			        return false;
		        return true;
	        },
	        [](CandidateContext &context, ObjectiveCandidate &candidate)
	        {
		        if (context.binding.candidate_source == "craft")
			        candidate.target = context.fact("bay.entity").number;
	        },
	        [](CandidateContext &context, ObjectiveCandidate &candidate)
	        {
		        if (context.binding.candidate_source == "craft" &&
		            (!context.fact("bay.eligible").number || context.fact("bay.recall_active").number))
			        candidate.reason = context.fact("bay.reason").text;
	        },
	        [](CandidateContext &context) -> Json
	        {
		        return {};
	        }};
}
CandidateAdapter requests_adapter()
{
	return {
	    "requests",
	    "objective_parameter_outside_addressed_request_scope",
	    false,
	    [](const CandidateCache &cache, const ConsoleControllerRuntime &controller,
	       const ControllerObservationSources &input, const ControllerObjectiveBinding &binding,
	       const KnowledgeRecord &record,
	       std::span<const state_tree::Value> source) -> std::vector<DecisionCandidateFrame>
	    {
		    return candidate_frames(cache.request_options(), controller, source);
	    },
	    [](CandidateContext &context)
	    {
		    if (context.binding.candidate_source == "requests" && !context.binding.request_operation.empty() &&
		        context.fact("request.id").number > 0 &&
		        context.fact("request.operation").text != context.binding.request_operation)
			    return false;
		    if (context.binding.candidate_source == "requests")
		    {
			    const bool repair = context.fact("request.operation").text == "repair";
			    const bool scan = context.fact("request.operation").text == "scan";
			    const bool supply = context.fact("request.operation").text == "power" ||
			                        context.fact("request.operation").text == "cooling";
			    bool scan_system = false;
			    std::string compartment;
			    if (repair)
				    for (const auto &room : combat_observation(context.input.observation).at("compartments"))
					    if (room.at("index") == context.fact("request.target").number)
						    compartment = room.at("id");
			    const auto system = context.text(*context.record, "system", "");
			    if (scan && !system.empty())
				    if (const auto sensors = context.input.observation.gameplay.find("sensors");
				        sensors != context.input.observation.gameplay.end())
					    for (const auto &scanner : sensors->at("scanners"))
						    scan_system |= scanner.at("id") == system &&
						                   scanner.at("capabilities").contains(context.fact("request.capability").text);

			    if ((!context.text(*context.record, "mission", "").empty() &&
			         context.text(*context.record, "mission", "") != context.fact("request.mission").text) ||
			        (!system.empty() &&
			         (scan ? !scan_system
			               : !repair || !controller_operation_owns(context.controller, "team_assignment", system))) ||
			        (!context.text(*context.record, "compartment", "").empty() &&
			         (!repair || context.text(*context.record, "compartment", "") != compartment)) ||
			        (context.number(*context.record, "target") &&
			         context.number(*context.record, "target") != ((repair || supply)
			                                                           ? static_cast<std::int64_t>(context.input.entity)
			                                                           : context.fact("request.target").number)))
				    return false;
			    if (repair && !system.empty() && !context.fact("request.done").number)
			    {
				    bool available = false;
				    for (const auto &team : context.input.observation.gameplay.value("teams", Json::array()))
					    if (team.at("id") == system && team.value("repair", false) && team.value("available", false) &&
					        !team.value("reserved", false) && !team.value("assignment_authority", false))
						    for (std::size_t target = 0; target < team.at("compartments").size(); ++target)
							    if (team.at("compartments")[target] == compartment)
								    for (const auto &route : team.value("routes", Json::array()))
									    available |=
									        route.at("compartment") == target && route.value("reachable", false);
				    if (!available)
					    for (std::size_t i = 0; i < context.brain.program.registry.observations.size(); ++i)
					    {
						    const auto &name = context.brain.program.registry.observations[i].name;
						    if (name == "request.feasible")
							    context.values[i] = state_tree::Value::boolean(false);
						    if (name == "request.reason")
							    context.values[i] = state_tree::Value::string("scoped_repair_team_unavailable");
					    }
			    }
		    }
		    return true;
	    },
	    [](CandidateContext &context, ObjectiveCandidate &candidate)
	    {
		    if (context.binding.candidate_source == "requests")
			    candidate.target = context.fact("request.id").number;
	    },
	    [](CandidateContext &context, ObjectiveCandidate &candidate)
	    {
		    if (context.binding.candidate_source == "requests" &&
		        (!context.fact("request.feasible").number || candidate.reason == "binding_not_applicable"))
			    candidate.reason = context.fact("request.reason").text;
	    },
	    [](CandidateContext &context) -> Json
	    {
		    if (context.binding.candidate_source == "requests")
		    {
			    Json evidence;
			    for (const auto *name : {"operation", "status", "reason", "id", "target", "amount", "limit", "priority",
			                             "expires", "evidence_revision", "feasible", "done", "blocked",
			                             "command_needed", "conflict_priority", "conflict_id"})
			    {
				    const auto &value = context.fact((std::string("request.") + name).c_str());
				    if (value.type == state_tree::Type::Text)
					    evidence[name] = value.text;
				    else if (value.type == state_tree::Type::Boolean)
					    evidence[name] = value.number != 0;
				    else if (value.type == state_tree::Type::Integer)
					    evidence[name] = value.number;
			    }
			    return evidence;
		    }
		    return {};
	    }};
}
CandidateAdapter away_adapter()
{
	return {"away",
	        "objective_parameter_outside_known_manifest_scope",
	        false,
	        [](const CandidateCache &cache, const ConsoleControllerRuntime &controller,
	           const ControllerObservationSources &input, const ControllerObjectiveBinding &binding,
	           const KnowledgeRecord &record,
	           std::span<const state_tree::Value> source) -> std::vector<DecisionCandidateFrame>
	        {
		        return candidate_frames(cache.away_options(), controller, source);
	        },
	        [](CandidateContext &context)
	        {
		        if (context.binding.candidate_source == "away" &&
		            ((!context.text(*context.record, "mission", "").empty() &&
		              context.text(*context.record, "mission", "") != context.fact("away_candidate.mission").text) ||
		             (!context.text(*context.record, "system", "").empty() &&
		              context.text(*context.record, "system", "") != context.fact("away_candidate.system").text) ||
		             !context.text(*context.record, "compartment", "").empty() ||
		             (context.number(*context.record, "target") &&
		              context.number(*context.record, "target") != context.fact("away_candidate.target").number)))
			        return false;
		        return true;
	        },
	        [](CandidateContext &context, ObjectiveCandidate &candidate)
	        {
		        if (context.binding.candidate_source == "away")
			        candidate.target = context.fact("away_candidate.target").number;
	        },
	        [](CandidateContext &context, ObjectiveCandidate &candidate)
	        {
		        if (context.binding.candidate_source == "away" && !context.fact("away_candidate.eligible").number)
			        candidate.reason = context.fact("away_candidate.reason").text;
	        },
	        [](CandidateContext &context) -> Json
	        {
		        return {};
	        }};
}

std::vector<DecisionCandidateFrame> candidate_frames(const std::vector<ObjectiveFacts> &options,
                                                     const ConsoleControllerRuntime &controller,
                                                     std::span<const state_tree::Value> source)
{
	std::vector<DecisionCandidateFrame> result;
	for (const auto &facts : options)
	{
		std::vector<state_tree::Value> values(source.begin(), source.end());
		for (const auto &[name, value] : facts)
			if (const auto field = controller.brain->observation_index(name))
				values[*field] = value;
		result.push_back({std::move(values), true});
	}
	return result;
}
const state_tree::Value &CandidateContext::fact(const char *name) const
{
	if (const auto field = brain.observation_index(name))
		return values[*field];
	static const state_tree::Value unavailable;
	return unavailable;
}
std::string CandidateContext::text(const KnowledgeRecord &row, const char *name, const std::string &fallback) const
{
	const auto found = row.value.fields.find(input.definitions.field(row.value.schema, name));
	return found != row.value.fields.end() && std::get_if<std::string>(&found->second)
	           ? std::get<std::string>(found->second)
	           : fallback;
}
std::int64_t CandidateContext::number(const KnowledgeRecord &row, const char *name, std::int64_t fallback) const
{
	const auto found = row.value.fields.find(input.definitions.field(row.value.schema, name));
	return found != row.value.fields.end() && std::get_if<std::int64_t>(&found->second)
	           ? std::get<std::int64_t>(found->second)
	           : fallback;
}
CandidateCache::CandidateCache(const ConsoleControllerRuntime &controller, const ControllerObservationSources &input,
                               std::span<const state_tree::Value> source)
    : controller(controller), input(input), source(source)
{
}
const std::vector<std::vector<state_tree::Value>> &CandidateCache::medical_options() const
{
	if (!medical_values)
		medical_values = controller.brain->medical_base ? controller_medical_candidates(controller, input.observation)
		                                                : std::vector<std::vector<state_tree::Value>>{};
	return *medical_values;
}
const std::vector<ObjectiveFacts> &CandidateCache::repair_options() const
{
	if (!repair_values)
		repair_values = controller_repair_candidates(controller, input.observation, input.tick);
	return *repair_values;
}
const std::vector<ObjectiveFacts> &CandidateCache::request_options() const
{
	if (!request_values)
		request_values = request_objective_facts(controller, input.observation, input.tick, source);
	return *request_values;
}
const std::vector<ObjectiveFacts> &CandidateCache::away_options() const
{
	if (!away_values)
		away_values = away_objective_facts(controller, input.observation, input.tick);
	return *away_values;
}
const std::vector<ObjectiveFacts> &CandidateCache::contact_options() const
{
	if (!contact_values)
		contact_values = controller_contact_candidates(controller, input);
	return *contact_values;
}
const std::vector<ObjectiveFacts> &CandidateCache::armament_options() const
{
	if (!armament_values)
		armament_values = controller_armament_candidates(controller, input);
	return *armament_values;
}
const std::vector<ObjectiveFacts> &CandidateCache::scan_options() const
{
	if (!scan_values)
		scan_values = controller_scan_candidates(controller, input);
	return *scan_values;
}
const CandidateAdapter &candidate_adapter(const std::string &name)
{
	static const std::array adapters{single_adapter(),  medical_adapter(), craft_adapter(),    requests_adapter(),
	                                 away_adapter(),    repair_adapter(),  contacts_adapter(), armament_adapter(),
	                                 scans_adapter(),   reports_adapter(), docks_adapter(),    personnel_adapter(),
	                                 delivery_adapter()};
	for (const auto &adapter : adapters)
		if (name == adapter.name)
			return adapter;
	return adapters.front();
}
} // namespace phoenix
