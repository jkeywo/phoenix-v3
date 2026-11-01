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
std::vector<std::map<std::string, state_tree::Value>>
controller_armament_candidates(const ConsoleControllerRuntime &, const ControllerObservationSources &);
std::vector<std::map<std::string, state_tree::Value>> controller_scan_candidates(const ConsoleControllerRuntime &,
                                                                                 const ControllerObservationSources &);
namespace
{
using ObjectiveFacts = std::map<std::string, state_tree::Value>;
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
			const bool eligible = delegated && phase != "lost" && phase != "consumed";
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
	const auto repair_options = std::any_of(brain.objective_bindings.begin(), brain.objective_bindings.end(),
	                                        [](const auto &binding)
	                                        {
		                                        return binding.candidate_source == "repair";
	                                        })
	                                ? controller_repair_candidates(controller, input.observation, input.tick)
	                                : std::vector<ObjectiveFacts>{};
	const auto request_options = std::any_of(brain.objective_bindings.begin(), brain.objective_bindings.end(),
	                                         [](const auto &binding)
	                                         {
		                                         return binding.candidate_source == "requests";
	                                         })
	                                 ? request_objective_facts(controller, input.observation, input.tick)
	                                 : std::vector<ObjectiveFacts>{};
	const auto away_options = std::any_of(brain.objective_bindings.begin(), brain.objective_bindings.end(),
	                                      [](const auto &binding)
	                                      {
		                                      return binding.candidate_source == "away";
	                                      })
	                              ? away_objective_facts(controller, input.observation, input.tick)
	                              : std::vector<ObjectiveFacts>{};
	const auto contact_options = std::any_of(brain.objective_bindings.begin(), brain.objective_bindings.end(),
	                                         [](const auto &binding)
	                                         {
		                                         return binding.candidate_source == "contacts";
	                                         })
	                                 ? controller_contact_candidates(controller, input)
	                                 : std::vector<ObjectiveFacts>{};
	const auto armament_options = std::any_of(brain.objective_bindings.begin(), brain.objective_bindings.end(),
	                                          [](const auto &binding)
	                                          {
		                                          return binding.candidate_source == "armament";
	                                          })
	                                  ? controller_armament_candidates(controller, input)
	                                  : std::vector<ObjectiveFacts>{};
	const auto scan_options = std::any_of(brain.objective_bindings.begin(), brain.objective_bindings.end(),
	                                      [](const auto &binding)
	                                      {
		                                      return binding.candidate_source == "scans";
	                                      })
	                              ? controller_scan_candidates(controller, input)
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
			if (binding.candidate_source == "craft" || binding.candidate_source == "requests" ||
			    binding.candidate_source == "away" || binding.candidate_source == "repair" ||
			    binding.candidate_source == "contacts" || binding.candidate_source == "armament" ||
			    binding.candidate_source == "scans")
				for (const auto &facts : binding.candidate_source == "craft"
				                             ? craft_objective_facts(controller, input.observation, input.tick,
				                                                     number(*record, "target"), source)
				                         : binding.candidate_source == "requests" ? request_options
				                         : binding.candidate_source == "armament" ? armament_options
				                         : binding.candidate_source == "scans"    ? scan_options
				                         : binding.candidate_source == "contacts" ? contact_options
				                         : binding.candidate_source == "repair"   ? repair_options
				                                                                  : away_options)
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
				if ((binding.candidate_source == "craft" || binding.candidate_source == "requests" ||
				     binding.candidate_source == "away" || binding.candidate_source == "repair" ||
				     binding.candidate_source == "contacts" || binding.candidate_source == "armament" ||
				     binding.candidate_source == "scans") &&
				    !option.empty())
					values = option;
				const auto fact = [&](const char *name) -> const Value &
				{
					for (std::size_t i = 0; i < brain.program.registry.observations.size(); ++i)
						if (brain.program.registry.observations[i].name == name)
							return values[i];
					static const Value unavailable;
					return unavailable;
				};
				if ((binding.candidate_source == "contacts" || binding.candidate_source == "armament") &&
				    (!text(*record, "mission", "").empty() || !text(*record, "compartment", "").empty() ||
				     (!text(*record, "system", "").empty() &&
				      (binding.candidate_source != "armament" ||
				       text(*record, "system", "") != fact("armament.weapon").text)) ||
				     (number(*record, "target") && number(*record, "target") != fact("contact_candidate.id").number)))
					continue;
				if (binding.candidate_source == "scans" &&
				    (!text(*record, "mission", "").empty() || !text(*record, "compartment", "").empty() ||
				     (!text(*record, "system", "").empty() &&
				      text(*record, "system", "") != fact("scan_candidate.scanner").text) ||
				     (number(*record, "target") && number(*record, "target") != fact("scan_candidate.target").number)))
					continue;
				if (binding.candidate_source == "craft" &&
				    (!text(*record, "mission", "").empty() || !text(*record, "compartment", "").empty() ||
				     (!text(*record, "system", "").empty() && text(*record, "system", "") != fact("bay.id").text) ||
				     (behavior != "engage" && number(*record, "target") && number(*record, "target") != input.entity)))
					continue;
				if (binding.candidate_source == "repair" &&
				    (!text(*record, "mission", "").empty() ||
				     (number(*record, "target") && number(*record, "target") != input.entity) ||
				     (!text(*record, "system", "").empty() &&
				      text(*record, "system", "") != fact("repair_candidate.team").text) ||
				     (!text(*record, "compartment", "").empty() &&
				      text(*record, "compartment", "") != fact("repair_candidate.compartment").text)))
					continue;

				if (binding.candidate_source == "away" &&
				    ((!text(*record, "mission", "").empty() &&
				      text(*record, "mission", "") != fact("away_candidate.mission").text) ||
				     (!text(*record, "system", "").empty() &&
				      text(*record, "system", "") != fact("away_candidate.system").text) ||
				     !text(*record, "compartment", "").empty() ||
				     (number(*record, "target") && number(*record, "target") != fact("away_candidate.target").number)))
					continue;
				if (binding.candidate_source == "requests")
				{
					const bool repair = fact("request.operation").text == "repair";
					const bool scan = fact("request.operation").text == "scan";
					bool scan_system = false;
					std::string compartment;
					if (repair)
						for (const auto &room : combat_observation(input.observation).at("compartments"))
							if (room.at("index") == fact("request.target").number)
								compartment = room.at("id");
					const auto system = text(*record, "system", "");
					if (scan && !system.empty())
						if (const auto sensors = input.observation.gameplay.find("sensors");
						    sensors != input.observation.gameplay.end())
							for (const auto &scanner : sensors->at("scanners"))
								scan_system |= scanner.at("id") == system &&
								               scanner.at("capabilities").contains(fact("request.capability").text);

					if ((!text(*record, "mission", "").empty() &&
					     text(*record, "mission", "") != fact("request.mission").text) ||
					    (!system.empty() &&
					     (scan ? !scan_system
					           : !repair || !controller_operation_owns(controller, "team_assignment", system))) ||
					    (!text(*record, "compartment", "").empty() &&
					     (!repair || text(*record, "compartment", "") != compartment)) ||
					    (number(*record, "target") &&
					     number(*record, "target") !=
					         (repair ? static_cast<std::int64_t>(input.entity) : fact("request.target").number)))
						continue;
					if (repair && !system.empty() && !fact("request.done").number)
					{
						bool available = false;
						for (const auto &team : input.observation.gameplay.value("teams", Json::array()))
							if (team.at("id") == system && team.value("repair", false) &&
							    team.value("available", false) && !team.value("reserved", false) &&
							    !team.value("assignment_authority", false))
								for (std::size_t target = 0; target < team.at("compartments").size(); ++target)
									if (team.at("compartments")[target] == compartment)
										for (const auto &route : team.value("routes", Json::array()))
											available |=
											    route.at("compartment") == target && route.value("reachable", false);
						if (!available)
							for (std::size_t i = 0; i < brain.program.registry.observations.size(); ++i)
							{
								const auto &name = brain.program.registry.observations[i].name;
								if (name == "request.feasible")
									values[i] = Value::boolean(false);
								if (name == "request.reason")
									values[i] = Value::string("scoped_repair_team_unavailable");
							}
					}
				}
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
				if ((binding.candidate_source == "contacts" || binding.candidate_source == "armament") &&
				    candidate.reason == "binding_not_applicable")
				{
					if (binding.candidate_source == "armament" && fact("armament.weapon").text.empty())
						candidate.reason = "no_owned_ammunition_options";
					else if (!fact("contact_candidate.available").number)
						candidate.reason = "no_permitted_contact";
					else if (behavior == "engage" && !fact("contact_candidate.hostile").number)
						candidate.reason = "no_known_hostile";
				}
				if (binding.candidate_source == "scans" && candidate.reason == "binding_not_applicable")
				{
					if (!fact("scan_candidate.available").number)
						candidate.reason = fact("scan_candidate.reason").text;
					else if (behavior == "engage" && !fact("scan_candidate.hostile").number)
						candidate.reason = "no_known_hostile";
				}
				ControllerObjectiveChoice choice;
				choice.objective = candidate.objective;
				choice.action = binding.action;
				choice.binding = binding.id;
				if (binding.candidate_source == "contacts" || binding.candidate_source == "armament")
					candidate.target = fact("contact_candidate.id").number;
				if (binding.candidate_source == "scans")
					candidate.target = fact("scan_candidate.target").number;
				if (binding.candidate_source == "away")
					candidate.target = fact("away_candidate.target").number;
				if (binding.candidate_source == "requests")
					candidate.target = fact("request.id").number;
				if (binding.candidate_source == "craft")
					candidate.target = fact("bay.entity").number;
				if (binding.candidate_source == "repair")
					candidate.target = fact("repair_candidate.target").number;
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
					else if (key == "revision")
						values[i] = Value::integer(record->value.provenance.revision);
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
				if (!binding.navigation_mode.empty())
				{
					// Pure authored geometry over the frozen permitted candidate. No World lookup,
					// destination selection or hidden position extrapolation occurs here.
					const bool contact = binding.candidate_source == "contacts";
					fixed::Vec point{contact ? fact("contact_candidate.x").number : number(*record, "destination_x"),
					                 contact ? fact("contact_candidate.y").number : number(*record, "destination_y")};
					const auto own = fixed::Vec{input.observation.own.x, input.observation.own.y};
					const auto bounded = [](fixed::Vec p)
					{
						return p.x >= -1000000000 && p.x <= 1000000000 && p.y >= -1000000000 && p.y <= 1000000000;
					};
					bool valid = bounded(point) && bounded(own) &&
					             (contact ? fact("contact_candidate.available").number != 0
					                      : field(*record, "destination_x") && field(*record, "destination_y"));
					std::int64_t distance = 0;
					if (valid)
					{
						const fixed::Vec delta{point.x - own.x, point.y - own.y};
						distance = fixed::length(delta);
						if (binding.navigation_mode != "destination")
						{
							valid = distance > 0 || binding.navigation_distance == 0;
							if (valid)
							{
								const fixed::Vec offset{distance ? delta.x * binding.navigation_distance / distance : 0,
								                        distance ? delta.y * binding.navigation_distance / distance
								                                 : 0};
								const auto anchor = binding.navigation_mode == "withdraw" ? own : point;
								point = {anchor.x - offset.x, anchor.y - offset.y};
							}
						}
						valid = valid && bounded(point);
					}
					for (std::size_t i = 0; i < brain.program.registry.observations.size(); ++i)
					{
						const auto &name = brain.program.registry.observations[i].name;
						if (name == "navigation.x")
							values[i] = Value::integer(valid ? point.x : 0);
						else if (name == "navigation.y")
							values[i] = Value::integer(valid ? point.y : 0);
						else if (name == "navigation.distance")
							values[i] = Value::integer(distance);
						else if (name == "navigation.valid")
							values[i] = Value::boolean(valid);
					}
					if (!valid && candidate.reason == "binding_not_applicable")
						candidate.reason = "permitted_navigation_geometry_unavailable";
				}
				if (candidate.reason == "binding_not_applicable")
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
					else if (binding.candidate_source == "craft" &&
					         (!fact("bay.eligible").number || fact("bay.recall_active").number))
						candidate.reason = fact("bay.reason").text;
					else if (binding.candidate_source == "away" && !fact("away_candidate.eligible").number)
						candidate.reason = fact("away_candidate.reason").text;
					else if (binding.candidate_source == "requests" && !fact("request.feasible").number)
						candidate.reason = fact("request.reason").text;
					else if (binding.candidate_source == "armament" &&
					         fact("armament.reason").text != "permitted_armament_option")
						candidate.reason = fact("armament.reason").text;
					else if (binding.candidate_source == "repair" && !fact("repair_candidate.eligible").number)
						candidate.reason = fact("repair_candidate.reason").text.empty()
						                       ? "no_owned_repair_assignment"
						                       : fact("repair_candidate.reason").text;
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
				    binding.candidate_source == "medical"  ? "objective_parameter_outside_known_medical_scope"
				    : binding.candidate_source == "craft"  ? "objective_parameter_outside_known_craft_scope"
				    : binding.candidate_source == "away"   ? "objective_parameter_outside_known_manifest_scope"
				    : binding.candidate_source == "repair" ? "objective_parameter_outside_known_repair_scope"
				    : binding.candidate_source == "scans"  ? "objective_parameter_outside_known_scan_scope"
				    : binding.candidate_source == "contacts" || binding.candidate_source == "armament"
				        ? "objective_parameter_outside_known_contact_scope"
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
			candidate.tier = objective_tier(text(*record, "tier", "objective_other"));
			candidate.marked = input.priority_objectives.contains(candidate.objective);
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
