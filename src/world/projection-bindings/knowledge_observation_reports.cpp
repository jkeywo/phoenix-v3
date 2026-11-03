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
		};
		for (const auto child : ship.get<EquipmentIndex>().values)
		{
			const auto system = child.get<Equipment>().id;
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
