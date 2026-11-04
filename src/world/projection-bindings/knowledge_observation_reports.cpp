module;
#include "native_headers.hpp"

module phoenix.simulation;
import :projection_bindings;

namespace phoenix::world_detail
{
auto Runtime::publish_observation_reports() -> void
{
	const auto now = ecs.get<Clock>().tick;
	const auto &contract = knowledge.definition();
	for (const auto &[id, ship] : entities)
	{
		if (!knowledge.contains_ship(id) || !ship.has<EquipmentIndex>() || departed(id) || truly_destroyed(id))
			continue;
		const auto &data = definition(id);
		if (const auto *power = ship.try_get<ShipResources>(); power && data.layout)
		{
			constexpr auto charging_status = std::numeric_limits<KnowledgeKey>::max() - 2;
			constexpr auto shortfall_status = charging_status - 1;
			constexpr auto rationing_status = charging_status - 2;
			constexpr auto overheating_status = charging_status - 3;
			const auto &network = power->definition.network;
			const auto &bindings = power->definition.systems;
			const auto allocator = [&](ResourceIndex group)
			{
				for (; group != no_resource; group = network.groups.at(group).parent)
					if (const auto at = std::find_if(bindings.begin(), bindings.end(),
					                                 [&](const auto &binding)
					                                 {
						                                 return binding.group == group;
					                                 });
					    at != bindings.end())
						return at;
				return bindings.end();
			};
			const auto alert_owners = [&](const std::string &source, const std::string &affected,
			                              const std::string &event, const std::string &target)
			{
				for (const auto &recipient : equipment_alert_recipients(id, source, affected))
					knowledge.emit_equipment_alert(id, source, recipient, event, target, {}, now);
			};
			const auto distributor = std::find_if(bindings.begin(), bindings.end(),
			                                      [](const auto &binding)
			                                      {
				                                      return binding.group != no_resource;
			                                      });
			const bool rationing = std::any_of(power->network.groups().begin(), power->network.groups().end(),
			                                   [](const auto &group)
			                                   {
				                                   return group.cut;
			                                   });
			if (distributor != bindings.end() &&
			    knowledge.status_change(id, distributor->id, rationing_status, rationing, now, 0, rate, 0, false) &&
			    rationing)
				knowledge.emit_equipment_alert(id, distributor->id, std::nullopt, "power-rationing", distributor->id,
				                               {}, now);
			for (std::size_t i = 0; i < bindings.size(); ++i)
			{
				const auto &binding = bindings[i];
				if (binding.store != no_resource)
				{
					const auto &grant = power->grants.at(i);
					const bool shortfall = grant.requested > grant.paid;
					if (knowledge.status_change(id, binding.id, shortfall_status, shortfall, now, rate, rate, 0,
					                            false) &&
					    shortfall)
						if (const auto source = allocator(network.stores.at(binding.store).group);
						    source != bindings.end())
							alert_owners(source->id, binding.id, "power-shortfall", binding.id);
				}
				if (binding.generator == no_resource)
					continue;
				const auto store = network.generators.at(binding.generator).store;
				const auto &limits = network.stores.at(store);
				const auto &state = power->network.stores().at(store);
				const bool hot = limits.stop_heat > 0 &&
				                 (state.thermal_stop || state.heat >= limits.stop_heat - limits.stop_heat / 10);
				if (knowledge.status_change(id, binding.id, overheating_status, hot, now, rate, rate, 0, false) && hot)
					for (const auto &cooling : bindings)
						if (cooling.cooling != no_resource && network.cooling_links.at(cooling.cooling).store == store)
							alert_owners(binding.id, cooling.id, "reactor-overheating", binding.id);
			}
			for (const auto child : ship.get<EquipmentIndex>().values)
				if (const auto *ftl = child.try_get<FtlReserve>())
				{
					const auto &equipment = child.get<Equipment>();
					const auto store = ftl->definition.store;
					const bool charging =
					    equipment.enabled && power->network.telemetry().at(store).accepted_incoming > 0;
					if (!knowledge.status_change(id, equipment.id, charging_status, charging, now, 0, rate, 0, false) ||
					    !charging)
						continue;
					for (const auto &controller : data.layout->controllers)
					{
						bool allocates = false, owns_drive = false;
						for (const auto &command : controller.commands)
							for (const auto consumer : command.consumers)
							{
								const auto &fitted = data.fitted->values.at(consumer);
								owns_drive |= fitted.id == equipment.id;
								const auto binding =
								    std::find_if(power->definition.systems.begin(), power->definition.systems.end(),
								                 [&](const auto &row)
								                 {
									                 return row.id == fitted.id;
								                 });
								if (binding == power->definition.systems.end())
									continue;
								allocates |= command.action == OperatingAction::ChargeLimit && binding->store == store;
								if (command.action == OperatingAction::GroupAllocation && binding->group != no_resource)
									for (auto group = network.stores.at(store).group; group != no_resource;
									     group = network.groups.at(group).parent)
										allocates |= group == binding->group;
							}
						if (allocates && !owns_drive)
							knowledge.emit_equipment_alert(id, equipment.id,
							                               ControllerAddress{id, controller.console, controller.id},
							                               "ftl-charging", equipment.id, "FTL drive", now);
					}
				}
		}
		if (const auto *shields = ship.try_get<ShieldComponent>(); shields && data.layout)
		{
			constexpr auto failed_status = std::numeric_limits<KnowledgeKey>::max();
			constexpr auto low_status = failed_status - 1;
			for (const auto child : ship.get<EquipmentIndex>().values)
			{
				const auto &equipment = child.get<Equipment>();
				if (equipment.kind != OperatingSystem::Shield)
					continue;
				std::int64_t strength = 0, capacity = 0;
				for (const auto &shield : shields->contributions)
					if (shield.equipment == equipment.id && shield.operating)
					{
						strength += shield.state.strength;
						capacity += shield.definition.shield.reserve.capacity;
					}
				const bool holding = strength > 0;
				if (knowledge.status_change(id, equipment.id, failed_status, holding, now, 0, 0, 0, false) &&
				    !holding && equipment.enabled)
					knowledge.emit_equipment_alert(id, equipment.id, std::nullopt, "shield-failed", equipment.id,
					                               "Shield", now);
				const bool low = holding && strength <= capacity / 4;
				if (!knowledge.status_change(id, equipment.id, low_status, low, now, 0, 0, 0, false) || !low)
					continue;
				std::set<ControllerAddress> recipients;
				for (const auto &fitting : data.fitted->values)
					if (fitting.kind == OperatingSystem::Steering)
					{
						const auto owners = equipment_alert_recipients(id, equipment.id, fitting.id);
						recipients.insert(owners.begin(), owners.end());
					}
				for (const auto &recipient : recipients)
					knowledge.emit_equipment_alert(id, equipment.id, recipient, "shield-low", equipment.id, "Shield",
					                               now);
			}
		}
		const auto publish = [&](const std::string &system, const char *schema_name, KnowledgeId subject,
		                         const std::map<std::string, KnowledgeValue> &fields, bool team,
		                         const std::string &department = std::string{}, bool owner_only = false)
		{
			KnowledgeRecordInput report;
			report.schema = contract.schema(schema_name);
			report.producer = contract.role("phoenix.scenario");
			report.subject = subject;
			report.provenance = {knowledge.system_id(id, system), now, now, 0, EvidenceKind::Fact};
			for (const auto &[key, value] : fields)
				report.fields[contract.field(report.schema, key)] = value;
			for (const auto &console : data.consoles)
				if ((team && enabled_system_owner(id, system).console == console.id) ||
				    (!owner_only && (std::find(console.feed_systems.begin(), console.feed_systems.end(), system) !=
				                         console.feed_systems.end() ||
				                     console.id == department)))
					knowledge.publish_scenario_console({id, console.id}, report, now);
			if (std::string_view(schema_name) == "phoenix.contact.report" && data.layout)
				for (const auto &controller : data.layout->controllers)
				{
					const auto &feeds = seat(id, controller.console)->feed_systems;
					if (std::find(feeds.begin(), feeds.end(), system) != feeds.end())
						continue;
					const bool uses_tracks =
					    std::any_of(controller.commands.begin(), controller.commands.end(),
					                [&](const auto &command)
					                {
						                return std::any_of(command.consumers.begin(), command.consumers.end(),
						                                   [&](auto index)
						                                   {
							                                   const auto kind = data.fitted->values.at(index).kind;
							                                   return kind == OperatingSystem::Engine ||
							                                          kind == OperatingSystem::Steering ||
							                                          kind == OperatingSystem::Weapon;
						                                   });
					                });
					if (uses_tracks)
						knowledge.publish_scenario_controller({id, controller.console, controller.id}, report, now,
						                                      now + 180);
				}
		};
		for (const auto child : ship.get<EquipmentIndex>().values)
		{
			const auto system = child.get<Equipment>().id;
			if (child.get<Equipment>().kind == OperatingSystem::Radar && child.has<SensorRuntime>())
				report_incoming_ordnance(id, child);
			if (child.get<Equipment>().kind == OperatingSystem::Weapon && child.has<CombatMechanism>())
				report_weapon_ammunition(id, child);
			if (const auto *sensor = child.try_get<SensorRuntime>())
				for (const auto &[target, track] : sensor->tracks)
					if (track.seen == now)
					{
						std::string classification = "Unidentified contact", disposition = "unknown",
						            category = "contact";
						std::int64_t identity_tick = 0;
						if (const auto identity = track.samples.find("basic_identity");
						    identity != track.samples.end() && !identity->second.data.is_null())
						{
							classification = identity->second.data.value("class", classification);
							disposition = identity->second.data.value("disposition", disposition);
							category = identity->second.data.value("category", category);
							identity_tick = static_cast<std::int64_t>(identity->second.observed);
						}
						// These are measured track fields and acquired identity samples.
						// Never add a hidden body definition, projectile target or ghost flag.
						publish(system, "phoenix.contact.report", target,
						        {{"x", track.x},
						         {"y", track.y},
						         {"vx", track.vx},
						         {"vy", track.vy},
						         {"heading", track.heading},
						         {"altitude", track.altitude},
						         {"identity_tick", identity_tick},
						         {"classification", classification},
						         {"category", category},
						         {"disposition", disposition}},
						        false);
					}
			const auto *mechanism = child.try_get<CombatMechanism>();
			if (!mechanism || mechanism->definition.kind != OperatingSystem::Team || !child.get<Equipment>().enabled ||
			    !team_available(*mechanism) || structurally_disabled(ship, system) ||
			    away_reserved(id, "team:" + system))
				continue;
			const auto &state = mechanism->get<TeamState>();
			if (state.travel || state.location < 0)
				continue;
			const auto &team = mechanism->definition.get<TeamDefinition>();
			std::vector<std::string> rooms = data.personnel.compartments;
			const auto *structure = ship.try_get<CombatStructure>();
			if (rooms.empty() && structure)
				for (const auto &room : structure->definition.compartments)
					rooms.push_back(room.id);
			if (static_cast<std::size_t>(state.location) >= rooms.size())
				continue;
			const auto &room = rooms.at(static_cast<std::size_t>(state.location));
			std::optional<LocalRepairObservation> local;
			if (structure && structure->definition.compartment_ids.contains(room))
				local = capture_local_repair_observation(*structure, structure->definition.compartment_ids.at(room));
			for (const bool medical : {false, true})
			{
				if (!team.capabilities.contains(medical ? "medical" : "repair"))
					continue;
				Json faults = Json::array(), patients = Json::array();
				if (!medical && local)
					for (const auto &fault : local->faults)
						if (fault.active)
						{
							const auto &[equipment, index] = fault.id;
							const auto &observed = structure->faults.at(equipment).at(index);
							const auto &fitted = structure->definition.faults.at(equipment).at(index);
							faults.push_back({{"system", equipment},
							                  {"id", fitted.id},
							                  {"symptom", fitted.symptom},
							                  {"diagnosed", observed.diagnosed},
							                  {"diagnosis", observed.diagnosed ? fitted.diagnosis : std::string{}},
							                  {"destroyed", fault.destroys || fault.system_destroyed},
							                  {"work", observed.work / (rate * 1000)},
							                  {"required_work", fitted.repair_work},
							                  {"compartment", room},
							                  {"compartment_index", local->compartment + 1}});
						}
				if (medical && ship.has<PersonnelState>())
					for (const auto &patient : clinical_patients(id))
						if (!patient.away && patient.state.location == state.location &&
						    patient.state.facility != "away")
							patients.push_back({{"id", patient.id},
							                    {"name", patient.name},
							                    {"team", patient.team},
							                    {"condition", patient.state.condition},
							                    {"stabilised", patient.state.stabilised},
							                    {"location", patient.state.location},
							                    {"compartment", room},
							                    {"facility", patient.state.facility},
							                    {"escort", patient.state.escort}});
				// Distinct report subjects preserve independent repair/Medical streams,
				// including a complete empty snapshot that supersedes earlier needs.
				const auto subject =
				    id * 1024 + (static_cast<std::uint64_t>(state.location) + 1) * 2 + (medical ? 1 : 0);
				publish(system, "phoenix.team.inspection", subject,
				        {{"ship", static_cast<std::int64_t>(id)},
				         {"compartment", room},
				         {"compartment_index",
				          static_cast<std::int64_t>(local ? local->compartment + 1 : state.location + 1)},
				         {"repair", !medical},
				         {"medical", medical},
				         {"hp", local ? local->hp : 0},
				         {"maximum", local ? local->maximum : 0},
				         {"disabled", local && local->disabled},
				         {"faults", faults.dump()},
				         {"patients", patients.dump()},
				         {"fault_count", static_cast<std::int64_t>(faults.size())},
				         {"patient_count", static_cast<std::int64_t>(patients.size())}},
				        true, !medical && structure ? structure->definition.repair_console : std::string{}, medical);
			}
		}
	}
}
} // namespace phoenix::world_detail
