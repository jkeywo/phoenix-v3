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
std::vector<CandidateFacts> controller_armament_candidates(const ConsoleControllerRuntime &,
                                                           const ControllerObservationSources &);
std::vector<CandidateFacts> controller_scan_candidates(const ConsoleControllerRuntime &,
                                                       const ControllerObservationSources &);
std::vector<CandidateFacts> controller_evidence_candidates(const ConsoleControllerRuntime &,
                                                           const ControllerObservationSources &,
                                                           const ControllerObjectiveBinding &);
std::vector<CandidateFacts> controller_docking_candidates(const ConsoleControllerRuntime &,
                                                          const ControllerObservationSources &,
                                                          const ControllerObjectiveBinding &, const KnowledgeRecord &);
std::vector<CandidateFacts> controller_personnel_candidates(const ConsoleControllerRuntime &,
                                                            const ControllerObservationSources &,
                                                            const KnowledgeRecord &);
std::vector<CandidateFacts> controller_delivery_candidates(const ConsoleControllerRuntime &,
                                                           const ControllerObservationSources &,
                                                           const KnowledgeRecord &);
namespace
{
std::vector<ObjectiveFacts> away_objective_facts(const ConsoleControllerRuntime &controller,
                                                 const DecisionObservation &observed, KnowledgeTick tick)
{
	using state_tree::Value;
	std::vector<ObjectiveFacts> result;
	for (const auto &row : observed.away_missions)
	{
		const auto mission = row.id, phase = row.phase;
		const auto department = row.department.value_or(AwayDepartmentDecision{});
		const auto dispatch = row.dispatch_system;
		const bool staging = phase == "open" || phase == "recovered";
		const bool contributing = department.contributing;
		bool requested = false;
		for (const auto &request : controller.incoming_requests)
			requested |= request.operation == OfficerOperation::Manifest && request.mission == mission &&
			             request.target == row.target && request.expires > tick &&
			             request.status == OfficerStatus::Accepted;
		const auto delegated = [&](const std::string &operation, const std::string &system)
		{
			return controller_operation_owns(controller, operation, system) &&
			       !controller_operation_human(controller, operation);
		};
		const auto offer = [&](const std::string &operation, const std::string &system, const std::string &selection,
		                       bool eligible, bool required = false, const std::string &peer_console = {},
		                       const std::string &peer_controller = {}, const std::string &request_status = "none",
		                       std::int64_t retry_age = 1000000)
		{
			ObjectiveFacts facts;
			const auto text = [&](CandidateField key, const std::string &value)
			{
				facts[key] = Value::string(value);
			};
			const auto number = [&](CandidateField key, std::int64_t value)
			{
				facts[key] = Value::integer(value);
			};
			const auto flag = [&](CandidateField key, bool value)
			{
				facts[key] = Value::boolean(value);
			};
			text(CandidateField::away_candidate_mission, mission);
			text(CandidateField::away_candidate_phase, phase);
			text(CandidateField::away_candidate_operation, operation);
			text(CandidateField::away_candidate_selection, selection);
			text(CandidateField::away_candidate_system, system);
			text(CandidateField::away_candidate_peer_console, peer_console);
			text(CandidateField::away_candidate_peer_controller, peer_controller);
			text(CandidateField::away_candidate_request_status, request_status);
			text(CandidateField::away_candidate_reason,
			     eligible ? "permitted_manifest_choice" : "manifest_work_unavailable");
			number(CandidateField::away_candidate_target, row.target);
			number(CandidateField::away_candidate_revision, row.revision);
			number(CandidateField::away_candidate_amount, 1);
			number(CandidateField::away_candidate_retry_age, retry_age);
			number(CandidateField::away_candidate_travel, 0);
			number(CandidateField::away_candidate_cost, 0);
			flag(CandidateField::away_candidate_eligible, eligible);
			flag(CandidateField::away_candidate_requested, requested);
			flag(CandidateField::away_candidate_contributing, contributing);
			flag(CandidateField::away_candidate_ready, department.ready);
			flag(CandidateField::away_candidate_required, required);
			flag(CandidateField::away_candidate_offered, row.ai_dispatch || (staging && contributing && row.dispatch));
			result.push_back(std::move(facts));
		};
		offer("open", dispatch, "", phase == "available" && delegated("mission", dispatch));
		offer("contribute", "", "1", staging && !contributing && delegated("contribution", {}));
		offer("send", dispatch, "", phase == "open" && department.launch_ready && delegated("mission_send", dispatch));
		offer("recall", dispatch, "",
		      phase == "deployed" && (row.work_complete || department.recall_requested) &&
		          delegated("mission", dispatch));
		for (const auto &peer : department.peers)
		{
			const auto peer_console = peer.console;
			const auto peer_controller = peer.controller;
			std::string status = "none";
			std::int64_t retry_age = 1000000;
			std::uint64_t completed_revision = 0;
			// Peer routes are declared identities, resolved within the same ship.
			// The continuation stores the delivery actor, not another Controller's state.
			const KnowledgeId peer_id = peer.id;
			for (const auto &request : controller.outgoing_requests)
				if (request.operation == OfficerOperation::Manifest && request.peer == peer_id &&
				    request.mission == mission)
				{
					status = officer_statuses.at(static_cast<std::size_t>(request.status));
					completed_revision = request.evidence_revision;
					retry_age = std::clamp<std::int64_t>(tick - request.reply_tick, 0, 1000000);
				}
			const bool waiting = status == "requested" || status == "accepted" ||
			                     (status == "completed" && completed_revision >= row.revision);
			offer(
			    "request", dispatch, "",
			    !waiting && (phase == "available" || staging) &&
			        (delegated("mission", dispatch) || (staging && contributing && row.dispatch &&
			                                            controller_operation_owns(controller, "mission", dispatch))) &&
			        delegated("coordination", {}),
			    false, peer_console, peer_controller, status, retry_age);
		}
		const bool recovery = phase == "deployed" && department.transport_released && department.recall_requested;
		const auto transports = department.transport_systems;
		if ((staging && contributing && (row.transport.empty() || row.transport == "ship")) || recovery)
			for (const auto &[selection, system] : transports)
			{
				bool suitable = !recovery;
				if (recovery)
					for (const auto &craft : observed.craft)
						if ("craft:" + craft.id == selection)
							suitable = craft.role == "shuttle" && craft.recovery_available &&
							           craft.transport_capacity >=
							               row.accompanying.size() + std::count_if(row.slots.begin(), row.slots.end(),
							                                                       [](const auto &slot)
							                                                       {
								                                                       return slot.assigned != "";
							                                                       });
				if (suitable)
					offer("transport", system, selection, delegated("transport", system), true);
			}
		if (!staging || !contributing)
			continue;
		for (const auto &slot : row.slots)
			if (slot.assigned.empty())
				for (const auto &choice : slot.choices)
				{
					const auto asset = choice;
					const bool officer = asset.starts_with("officer:");
					const auto system = officer ? std::string{} : asset.substr(5);
					offer("assign", system, slot.id + "/" + asset,
					      delegated(officer ? "mission_staffing" : "transport", system), slot.required);
				}
		for (const auto &slot : row.craft_slots)
			if (slot.assigned.empty())
				for (const auto &choice : slot.choices)
				{
					const auto asset = choice;
					// Bay ownership is carried by the frozen craft report.
					for (const auto &craft : observed.craft)
						if ("craft:" + craft.id == asset)
							offer("escort", craft.bay, slot.id + "/" + asset, delegated("transport", craft.bay),
							      slot.required);
				}
	}
	return result;
}
std::vector<ObjectiveFacts> request_objective_facts(const ConsoleControllerRuntime &controller,
                                                    const DecisionObservation &observed, KnowledgeTick tick,
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
			if (const auto slot = controller.brain->observation_layout->summary_level;
			    slot && source[*slot].type == state_tree::Type::Integer)
				limit = std::min(limit, source[*slot].number);
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
							blocked |=
							    (t.contains(TelemetryField::enabled) && !t.at(TelemetryField::enabled)) ||
							    (t.contains(TelemetryField::thermal_stopped) && t.at(TelemetryField::thermal_stopped));
							const auto field =
							    power ? TelemetryField::actual_output : TelemetryField::actual_coolant_used;
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
			    {{CandidateField::request_operation, Value::string(power ? "power" : "cooling")},
			     {CandidateField::request_status,
			      Value::string(officer_statuses.at(static_cast<std::size_t>(request.status)))},
			     {CandidateField::request_mission, Value::string("")},
			     {CandidateField::request_reason, Value::string(!supported || !delegated ? "capabilities_unavailable"
			                                                    : !observed_all          ? "supply_evidence_unavailable"
			                                                    : blocked                ? "supply_blocked"
			                                                    : done                   ? "supply_observed"
			                                                    : needed                 ? "supply_command_required"
			                                                                             : "awaiting_supply_evidence")},
			     {CandidateField::request_id, Value::integer(request.id)},
			     {CandidateField::request_target, Value::integer(request.target)},
			     {CandidateField::request_amount, Value::integer(request.amount)},
			     {CandidateField::request_limit, Value::integer(limit)},
			     {CandidateField::request_priority, Value::integer(request.priority)},
			     {CandidateField::request_expires, Value::integer(request.expires)},
			     {CandidateField::request_revision, Value::integer(observed.observed_tick)},
			     {CandidateField::request_request_revision, Value::integer(request.request_revision)},
			     {CandidateField::request_evidence_revision, Value::integer(observed.observed_tick)},
			     {CandidateField::request_conflict_priority, Value::integer(conflict_priority)},
			     {CandidateField::request_conflict_id, Value::integer(conflict_id)},
			     {CandidateField::request_feasible, Value::boolean(feasible)},
			     {CandidateField::request_done, Value::boolean(feasible && done)},
			     {CandidateField::request_blocked, Value::boolean(!feasible)},
			     {CandidateField::request_command_needed, Value::boolean(needed)},
			     {CandidateField::request_continuing,
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
						if (system.id == id && system.telemetry.contains(TelemetryField::enabled) &&
						    !system.telemetry.at(TelemetryField::enabled))
							blocked = true;
			}
			const auto &slots = controller.brain->observation_layout->manoeuvre;
			if (slots[0] && source[*slots[0]].type == state_tree::Type::Integer)
				approach = source[*slots[0]].number;
			if (slots[1] && source[*slots[1]].type == state_tree::Type::Boolean)
				blocked |= source[*slots[1]].number != 0;
			blocked |= observed.authority.stance == "hold";
			delegated &= controller_operation_owns(controller, "coordination", {}) &&
			             !controller_operation_human(controller, "coordination");
			const bool feasible =
			    delegated && known && speed && approach > 0 && !blocked && (heading || bearing == observed.own.heading);
			result.push_back({{CandidateField::request_operation, Value::string("manoeuvre")},
			                  {CandidateField::request_status,
			                   Value::string(officer_statuses.at(static_cast<std::size_t>(request.status)))},
			                  {CandidateField::request_mission, Value::string("")},
			                  {CandidateField::request_reason,
			                   Value::string(!known                                        ? "target_not_observed"
			                                 : !delegated || !speed                        ? "capabilities_unavailable"
			                                 : blocked                                     ? "navigation_blocked"
			                                 : !heading && bearing != observed.own.heading ? "heading_not_delegated"
			                                 : distance <= request.amount ? "requested_range_observed"
			                                                              : "addressed_manoeuvre_work")},
			                  {CandidateField::request_id, Value::integer(request.id)},
			                  {CandidateField::request_target, Value::integer(request.target)},
			                  {CandidateField::request_amount, Value::integer(request.amount)},
			                  {CandidateField::request_limit, Value::integer(request.amount)},
			                  {CandidateField::request_priority, Value::integer(request.priority)},
			                  {CandidateField::request_expires, Value::integer(request.expires)},
			                  {CandidateField::request_bearing, Value::integer(bearing)},
			                  {CandidateField::request_distance, Value::integer(distance)},
			                  {CandidateField::request_speed, Value::integer(approach)},
			                  {CandidateField::request_evidence_revision, Value::integer(observed.observed_tick)},
			                  {CandidateField::request_feasible, Value::boolean(feasible)},
			                  {CandidateField::request_done, Value::boolean(feasible && distance <= request.amount)},
			                  {CandidateField::request_blocked, Value::boolean(!feasible)}});
		}
	for (const auto &request : controller.incoming_requests)
		if (request.operation == OfficerOperation::Manifest && request.expires > tick &&
		    (request.status == OfficerStatus::Requested || request.status == OfficerStatus::Accepted))
		{
			const AwayMissionDecision *mission = nullptr;
			for (const auto &row : observed.away_missions)
				if (row.id == request.mission && row.target == request.target)
					mission = &row;
			const bool delegated = controller_operation_owns(controller, "contribution", {}) &&
			                       !controller_operation_human(controller, "contribution") &&
			                       controller_operation_owns(controller, "coordination", {}) &&
			                       !controller_operation_human(controller, "coordination");
			const bool active =
			    mission && (mission->phase == "available" || mission->phase == "open" || mission->phase == "recovered");
			const auto department =
			    mission ? mission->department.value_or(AwayDepartmentDecision{}) : AwayDepartmentDecision{};
			const bool done = delegated && department.ready && department.assigned >= request.amount;
			const bool feasible =
			    delegated && mission && (done || (active && request.status == OfficerStatus::Requested));
			const auto reason = !delegated                                  ? "capabilities_unavailable"
			                    : !mission                                  ? "mission_not_disclosed"
			                    : done                                      ? "preparation_observed"
			                    : !active                                   ? "mission_inactive"
			                    : request.status == OfficerStatus::Accepted ? "awaiting_preparation_evidence"
			                                                                : "addressed_preparation_request";
			result.push_back(
			    {{CandidateField::request_operation, Value::string("manifest")},
			     {CandidateField::request_status,
			      Value::string(officer_statuses.at(static_cast<std::size_t>(request.status)))},
			     {CandidateField::request_mission, Value::string(request.mission)},
			     {CandidateField::request_reason, Value::string(reason)},
			     {CandidateField::request_id, Value::integer(request.id)},
			     {CandidateField::request_target, Value::integer(request.target)},
			     {CandidateField::request_amount, Value::integer(request.amount)},
			     {CandidateField::request_priority, Value::integer(request.priority)},
			     {CandidateField::request_expires, Value::integer(request.expires)},
			     {CandidateField::request_revision, Value::integer(mission ? mission->revision : 0)},
			     {CandidateField::request_request_revision, Value::integer(request.request_revision)},
			     {CandidateField::request_evidence_revision, Value::integer(request.evidence_revision)},
			     {CandidateField::request_feasible, Value::boolean(feasible)},
			     {CandidateField::request_done, Value::boolean(done)},
			     {CandidateField::request_blocked, Value::boolean(!delegated || !mission || (!active && !done))}});
		}
	for (const auto &request : controller.incoming_requests)
		if (request.operation == OfficerOperation::Repair && request.expires > tick &&
		    (request.status == OfficerStatus::Requested || request.status == OfficerStatus::Accepted))
		{
			bool known = false, inspected = false, damaged = false, delegated = false;
			std::string compartment;
			KnowledgeTick inspection_revision = observed.observed_tick;
			for (const auto &room : observed.combat.compartments)
				if (room.index == request.target)
				{
					known = true;
					compartment = room.id;
					inspected = room.inspected ||
					            (room.reported_inspection && room.inspection_tick >= request.reply_tick &&
					             room.inspection_fault_count == 0 && room.inspection_hp >= room.inspection_maximum);
					if (!room.inspected && inspected)
						inspection_revision = room.inspection_revision;
					damaged = room.hp < room.maximum;
				}
			for (const auto &fault : observed.combat.faults)
				damaged |= fault.compartment_index == request.target;
			for (const auto &team : observed.teams)
				if (team.repair && team.available && !team.reserved && !team.assignment_authority &&
				    controller_operation_owns(controller, "team_assignment", team.id) &&
				    !controller_operation_human(controller, "team_assignment"))
				{
					const auto &rooms = team.compartments;
					for (std::size_t target = 0; target < rooms.size(); ++target)
						if (rooms[target] == compartment)
							for (const auto &route : team.routes)
								delegated |= route.compartment == target && route.reachable;
				}
			const bool coordination = controller_operation_owns(controller, "coordination", {}) &&
			                          !controller_operation_human(controller, "coordination");
			const bool done = known && inspected && !damaged;
			const bool feasible =
			    coordination && known && (done || (delegated && request.status == OfficerStatus::Requested));
			result.push_back(
			    {{CandidateField::request_operation, Value::string("repair")},
			     {CandidateField::request_status,
			      Value::string(officer_statuses.at(static_cast<std::size_t>(request.status)))},
			     {CandidateField::request_mission, Value::string("")},
			     {CandidateField::request_reason,
			      Value::string(done                          ? "permitted_inspection_complete"
			                    : !known                      ? "compartment_not_disclosed"
			                    : !coordination || !delegated ? "capabilities_or_route_unavailable"
			                                                  : "awaiting_repair_evidence")},
			     {CandidateField::request_id, Value::integer(request.id)},
			     {CandidateField::request_target, Value::integer(request.target)},
			     {CandidateField::request_amount, Value::integer(request.amount)},
			     {CandidateField::request_priority, Value::integer(request.priority)},
			     {CandidateField::request_expires, Value::integer(request.expires)},
			     {CandidateField::request_revision, Value::integer(0)},
			     {CandidateField::request_request_revision, Value::integer(request.request_revision)},
			     {CandidateField::request_evidence_revision, Value::integer(inspection_revision)},
			     {CandidateField::request_feasible, Value::boolean(feasible)},
			     {CandidateField::request_done, Value::boolean(done)},
			     {CandidateField::request_blocked, Value::boolean(!known || !coordination || (!delegated && !done))}});
		}
	for (const auto &request : controller.incoming_requests)
		if (request.operation == OfficerOperation::Scan && request.expires > tick &&
		    (request.status == OfficerStatus::Requested || request.status == OfficerStatus::Accepted))
		{
			std::set<std::string> attached;
			for (const auto &[role, systems] : controller.definition.equipment)
				attached.insert(systems.begin(), systems.end());
			bool supported = false, delegated = false;
			for (const auto &scanner : observed.sensors.scanners)
				supported |= attached.contains(scanner.id) && scanner.capabilities.contains(request.capability);
			for (std::size_t i = 0; i < controller.definition.commands.size(); ++i)
				delegated |= controller.definition.commands[i].action == OperatingAction::SensorTarget &&
				             !controller_command_human(controller, i);
			const bool coordination = controller_operation_owns(controller, "coordination", {}) &&
			                          !controller_operation_human(controller, "coordination");
			const auto proof = scan_completion_evidence(observed, request);
			const bool done = proof.has_value();
			result.push_back(
			    {{CandidateField::request_operation, Value::string("scan")},
			     {CandidateField::request_status,
			      Value::string(officer_statuses.at(static_cast<std::size_t>(request.status)))},
			     {CandidateField::request_mission, Value::string("")},
			     {CandidateField::request_capability, Value::string(request.capability)},
			     {CandidateField::request_reason, Value::string(done         ? "permitted_scan_evidence_complete"
			                                                    : !supported ? "scan_capability_unavailable"
			                                                                 : "awaiting_scan_evidence")},
			     {CandidateField::request_id, Value::integer(request.id)},
			     {CandidateField::request_target, Value::integer(request.target)},
			     {CandidateField::request_amount, Value::integer(request.amount)},
			     {CandidateField::request_priority, Value::integer(request.priority)},
			     {CandidateField::request_expires, Value::integer(request.expires)},
			     {CandidateField::request_revision, Value::integer(0)},
			     {CandidateField::request_request_revision, Value::integer(request.request_revision)},
			     {CandidateField::request_evidence_revision, Value::integer(proof.value_or(0))},
			     {CandidateField::request_feasible,
			      Value::boolean(coordination && delegated && supported &&
			                     (request.status == OfficerStatus::Requested || done))},
			     {CandidateField::request_done, Value::boolean(done)},
			     {CandidateField::request_blocked, Value::boolean(!coordination || !delegated || !supported)}});
		}
	if (result.empty())
	{
		auto empty = empty_candidate_facts(controller, CandidateGroup::request);
		empty[CandidateField::request_reason] = Value::string("no_pending_requests");
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
	if (const auto slot = controller.brain->observation_layout->bay_accepted_target;
	    slot && source[*slot].type == state_tree::Type::Integer)
		accepted_target = source[*slot].number;
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
	for (const auto &row : observed.craft)
	{
		const auto bay = row.bay;
		if (!controller_operation_owns(controller, "craft", bay))
			continue;
		const bool delegated = !controller_operation_human(controller, "craft");
		const auto phase = row.phase;
		const bool reserved = row.reserved || !row.mission.empty();
		const bool eligible = delegated && phase != "lost" && phase != "consumed";
		const auto age = tick >= row.report_tick ? tick - row.report_tick : 0;
		const auto energy = row.energy, capacity = row.capacity;
		const bool reserve_low = capacity > 0 && energy * 1000 <= capacity * row.return_reserve_permille;
		const auto id = row.id;
		bool order_target_known = row.target == observed.own.id;
		std::int64_t order_target_age = order_target_known ? 0 : 1000000;
		for (const auto &contact : known_contacts)
			if (contact.contains(CandidateField::contact_candidate_id) &&
			    contact.at(CandidateField::contact_candidate_available).number &&
			    contact.at(CandidateField::contact_candidate_id).number == row.target)
			{
				order_target_known = true;
				order_target_age = contact.at(CandidateField::contact_candidate_age).number;
			}
		ObjectiveFacts facts;
		const auto text = [&](CandidateField name, const std::string &value)
		{
			facts[name] = Value::string(value);
		};
		const auto number = [&](CandidateField name, std::int64_t value)
		{
			facts[name] = Value::integer(value);
		};
		const auto boolean = [&](CandidateField name, bool value)
		{
			facts[name] = Value::boolean(value);
		};
		text(CandidateField::bay_craft, id);
		text(CandidateField::bay_id, bay);
		text(CandidateField::bay_role, row.role);
		text(CandidateField::bay_phase, phase);
		text(CandidateField::bay_order, row.order);
		text(CandidateField::bay_pending_order, row.pending_order);
		text(CandidateField::bay_attack_text, target_present ? std::to_string(target) : std::string{});
		text(CandidateField::bay_reason, !delegated          ? "capabilities_unavailable"
		                                 : !eligible         ? "craft_unavailable"
		                                 : row.recall_active ? "accepted_recovery_in_progress"
		                                 : reserved          ? "craft_reserved"
		                                                     : "permitted_craft_report");
		number(CandidateField::bay_entity, row.entity);
		number(CandidateField::bay_revision, row.revision);
		number(CandidateField::bay_target, row.target);
		number(CandidateField::bay_attack_target, target_present ? target : 0);
		number(CandidateField::bay_report_age, std::min<KnowledgeTick>(1000000, age));
		number(CandidateField::bay_order_target_age, order_target_age);
		number(CandidateField::bay_energy, energy);
		number(CandidateField::bay_ammunition, row.ammunition);
		boolean(CandidateField::bay_eligible, eligible);
		boolean(CandidateField::bay_dispatch_available,
		        eligible && !reserved && (row.available || phase == "flying") && row.blocker.empty());
		boolean(CandidateField::bay_recall_available, eligible && !row.recall_active &&
		                                                  (!reserved || row.ftl_preparing) &&
		                                                  (phase != "docked" || row.pending) && phase != "recovering" &&
		                                                  !(row.role == "boarding_torpedo" && phase != "docked"));
		boolean(CandidateField::bay_pending, row.pending);
		boolean(CandidateField::bay_recall_active, row.recall_active);
		boolean(CandidateField::bay_linked, row.linked);
		boolean(CandidateField::bay_reserve_low, reserve_low);
		boolean(CandidateField::bay_threat_present, threat);
		boolean(CandidateField::bay_missile_threat, missile_threat);
		boolean(CandidateField::bay_target_present, target_present);
		boolean(CandidateField::bay_order_target_known, order_target_known);
		boolean(CandidateField::bay_ftl_preparing, row.ftl_preparing);
		boolean(CandidateField::bay_armed, row.armed);
		result.push_back(std::move(facts));
	}
	if (result.empty())
	{
		auto empty = empty_candidate_facts(controller, CandidateGroup::bay);
		empty[CandidateField::bay_reason] = Value::string("no_owned_craft");
		result.push_back(std::move(empty));
	}
	return result;
}

std::int64_t objective_target(const ConsoleControllerRuntime &controller, const ControllerObservationSources &input,
                              const KnowledgeRecord &record)
{
	return objective_number(controller, input, record, ObjectiveField::target);
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
		            craft_objective_facts(controller, input, objective_target(controller, input, record), source),
		            controller, source);
	        },
	        [](CandidateContext &context)
	        {
		        if (context.source() == CandidateSource::craft &&
		            (!context.text(*context.record, ObjectiveField::mission, "").empty() ||
		             !context.text(*context.record, ObjectiveField::compartment, "").empty() ||
		             (!context.text(*context.record, ObjectiveField::system, "").empty() &&
		              context.text(*context.record, ObjectiveField::system, "") !=
		                  context.fact(CandidateField::bay_id).text) ||
		             (context.behavior != "engage" && context.number(*context.record, ObjectiveField::target) &&
		              context.number(*context.record, ObjectiveField::target) != context.input.entity)))
			        return false;
		        return true;
	        },
	        [](CandidateContext &context, ObjectiveCandidate &candidate)
	        {
		        if (context.source() == CandidateSource::craft)
			        candidate.target = context.fact(CandidateField::bay_entity).number;
	        },
	        [](CandidateContext &context, ObjectiveCandidate &candidate)
	        {
		        if (context.source() == CandidateSource::craft &&
		            (!context.fact(CandidateField::bay_eligible).number ||
		             context.fact(CandidateField::bay_recall_active).number))
			        candidate.reason = context.fact(CandidateField::bay_reason).text;
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
		    if (context.source() == CandidateSource::requests && !context.binding.request_operation.empty() &&
		        context.fact(CandidateField::request_id).number > 0 &&
		        context.fact(CandidateField::request_operation).text != context.binding.request_operation)
			    return false;
		    if (context.source() == CandidateSource::requests)
		    {
			    const bool repair = context.fact(CandidateField::request_operation).text == "repair";
			    const bool scan = context.fact(CandidateField::request_operation).text == "scan";
			    const bool supply = context.fact(CandidateField::request_operation).text == "power" ||
			                        context.fact(CandidateField::request_operation).text == "cooling";
			    bool scan_system = false;
			    std::string compartment;
			    if (repair)
				    for (const auto &room : context.input.observation.combat.compartments)
					    if (room.index == context.fact(CandidateField::request_target).number)
						    compartment = room.id;
			    const auto system = context.text(*context.record, ObjectiveField::system, "");
			    if (scan && !system.empty())
				    for (const auto &scanner : context.input.observation.sensors.scanners)
					    scan_system |=
					        scanner.id == system &&
					        scanner.capabilities.contains(context.fact(CandidateField::request_capability).text);

			    if ((!context.text(*context.record, ObjectiveField::mission, "").empty() &&
			         context.text(*context.record, ObjectiveField::mission, "") !=
			             context.fact(CandidateField::request_mission).text) ||
			        (!system.empty() &&
			         (scan ? !scan_system
			               : !repair || !controller_operation_owns(context.controller, "team_assignment", system))) ||
			        (!context.text(*context.record, ObjectiveField::compartment, "").empty() &&
			         (!repair || context.text(*context.record, ObjectiveField::compartment, "") != compartment)) ||
			        (context.number(*context.record, ObjectiveField::target) &&
			         context.number(*context.record, ObjectiveField::target) !=
			             ((repair || supply) ? static_cast<std::int64_t>(context.input.entity)
			                                 : context.fact(CandidateField::request_target).number)))
				    return false;
			    if (repair && !system.empty() && !context.fact(CandidateField::request_done).number)
			    {
				    bool available = false;
				    for (const auto &team : context.input.observation.teams)
					    if (team.id == system && team.repair && team.available && !team.reserved &&
					        !team.assignment_authority)
						    for (std::size_t target = 0; target < team.compartments.size(); ++target)
							    if (team.compartments[target] == compartment)
								    for (const auto &route : team.routes)
									    available |= route.compartment == target && route.reachable;
				    if (!available)
				    {
					    const auto set = [&](CandidateField field, state_tree::Value value)
					    {
						    if (const auto slot =
						            context.brain.observation_layout->candidate_slots[static_cast<std::size_t>(field)])
							    context.values[*slot] = std::move(value);
					    };
					    set(CandidateField::request_feasible, state_tree::Value::boolean(false));
					    set(CandidateField::request_reason,
					        state_tree::Value::string("scoped_repair_team_unavailable"));
				    }
			    }
		    }
		    return true;
	    },
	    [](CandidateContext &context, ObjectiveCandidate &candidate)
	    {
		    if (context.source() == CandidateSource::requests)
			    candidate.target = context.fact(CandidateField::request_id).number;
	    },
	    [](CandidateContext &context, ObjectiveCandidate &candidate)
	    {
		    if (context.source() == CandidateSource::requests &&
		        (!context.fact(CandidateField::request_feasible).number ||
		         candidate.reason == "binding_not_applicable"))
			    candidate.reason = context.fact(CandidateField::request_reason).text;
	    },
	    [](CandidateContext &context) -> Json
	    {
		    if (context.source() == CandidateSource::requests)
		    {
			    Json evidence;
			    constexpr std::array entries{std::pair{"operation", CandidateField::request_operation},
			                                 std::pair{"status", CandidateField::request_status},
			                                 std::pair{"reason", CandidateField::request_reason},
			                                 std::pair{"id", CandidateField::request_id},
			                                 std::pair{"target", CandidateField::request_target},
			                                 std::pair{"amount", CandidateField::request_amount},
			                                 std::pair{"limit", CandidateField::request_limit},
			                                 std::pair{"priority", CandidateField::request_priority},
			                                 std::pair{"expires", CandidateField::request_expires},
			                                 std::pair{"evidence_revision", CandidateField::request_evidence_revision},
			                                 std::pair{"feasible", CandidateField::request_feasible},
			                                 std::pair{"done", CandidateField::request_done},
			                                 std::pair{"blocked", CandidateField::request_blocked},
			                                 std::pair{"command_needed", CandidateField::request_command_needed},
			                                 std::pair{"conflict_priority", CandidateField::request_conflict_priority},
			                                 std::pair{"conflict_id", CandidateField::request_conflict_id}};
			    for (const auto &[name, field] : entries)
			    {
				    const auto &value = context.fact(field);
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
		        if (context.source() == CandidateSource::away &&
		            ((!context.text(*context.record, ObjectiveField::mission, "").empty() &&
		              context.text(*context.record, ObjectiveField::mission, "") !=
		                  context.fact(CandidateField::away_candidate_mission).text) ||
		             (!context.text(*context.record, ObjectiveField::system, "").empty() &&
		              context.text(*context.record, ObjectiveField::system, "") !=
		                  context.fact(CandidateField::away_candidate_system).text) ||
		             !context.text(*context.record, ObjectiveField::compartment, "").empty() ||
		             (context.number(*context.record, ObjectiveField::target) &&
		              context.number(*context.record, ObjectiveField::target) !=
		                  context.fact(CandidateField::away_candidate_target).number)))
			        return false;
		        return true;
	        },
	        [](CandidateContext &context, ObjectiveCandidate &candidate)
	        {
		        if (context.source() == CandidateSource::away)
			        candidate.target = context.fact(CandidateField::away_candidate_target).number;
	        },
	        [](CandidateContext &context, ObjectiveCandidate &candidate)
	        {
		        if (context.source() == CandidateSource::away &&
		            !context.fact(CandidateField::away_candidate_eligible).number)
			        candidate.reason = context.fact(CandidateField::away_candidate_reason).text;
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
			if (const auto field =
			        controller.brain->observation_layout->candidate_slots[static_cast<std::size_t>(name)])
				values[*field] = value;
		for (const auto &[slot, value] : facts.bound)
			values[slot] = value;
		result.push_back({std::move(values), true});
	}
	return result;
}
const KnowledgeValue *objective_field(const ConsoleControllerRuntime &controller, const ControllerObservationSources &,
                                      const KnowledgeRecord &row, ObjectiveField field)
{
	const auto &layout = *controller.brain->observation_layout;
	if (row.value.schema != layout.objective_schema)
		return nullptr;
	const auto key = layout.objective_keys[static_cast<std::size_t>(field)];
	const auto found = row.value.fields.find(key);
	return found == row.value.fields.end() ? nullptr : &found->second;
}
std::string objective_text(const ConsoleControllerRuntime &controller, const ControllerObservationSources &input,
                           const KnowledgeRecord &row, ObjectiveField field, const std::string &fallback)
{
	const auto value = objective_field(controller, input, row, field);
	return value && std::get_if<std::string>(value) ? std::get<std::string>(*value) : fallback;
}
std::int64_t objective_number(const ConsoleControllerRuntime &controller, const ControllerObservationSources &input,
                              const KnowledgeRecord &row, ObjectiveField field, std::int64_t fallback)
{
	const auto value = objective_field(controller, input, row, field);
	return value && std::get_if<std::int64_t>(value) ? std::get<std::int64_t>(*value) : fallback;
}
CandidateFacts empty_candidate_facts(const ConsoleControllerRuntime &controller, CandidateGroup group)
{
	CandidateFacts facts;
	for (std::size_t i = 0; i < candidate_field_specs.size(); ++i)
	{
		const auto &spec = candidate_field_specs[i];
		if (spec.group != group || !controller.brain->observation_layout->candidate_slots[i])
			continue;
		facts[static_cast<CandidateField>(i)] = spec.type == state_tree::Type::Text ? state_tree::Value::string("")
		                                        : spec.type == state_tree::Type::Boolean
		                                            ? state_tree::Value::boolean(false)
		                                            : state_tree::Value::integer(0);
	}
	return facts;
}
const RuntimeObjectiveBinding &compiled_objective_binding(const ConsoleControllerRuntime &controller,
                                                          const ControllerObjectiveBinding &binding)
{
	return controller.brain->compiled_objectives.at(
	    static_cast<std::size_t>(&binding - controller.brain->objective_bindings.data()));
}
CandidateSource CandidateContext::source() const
{
	return compiled_objective_binding(controller, binding).source;
}
const state_tree::Value &CandidateContext::fact(CandidateField name) const
{
	if (const auto field = brain.observation_layout->candidate_slots[static_cast<std::size_t>(name)])
		return values[*field];
	static const state_tree::Value unavailable;
	return unavailable;
}
std::string CandidateContext::text(const KnowledgeRecord &row, ObjectiveField name, const std::string &fallback) const
{
	return objective_text(controller, input, row, name, fallback);
}
std::int64_t CandidateContext::number(const KnowledgeRecord &row, ObjectiveField name, std::int64_t fallback) const
{
	return objective_number(controller, input, row, name, fallback);
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
const CandidateAdapter &candidate_adapter(CandidateSource source)
{
	static const std::array adapters{single_adapter(),  medical_adapter(), craft_adapter(),    requests_adapter(),
	                                 away_adapter(),    repair_adapter(),  contacts_adapter(), armament_adapter(),
	                                 scans_adapter(),   reports_adapter(), docks_adapter(),    personnel_adapter(),
	                                 delivery_adapter()};
	return adapters.at(static_cast<std::size_t>(source));
}
} // namespace phoenix
