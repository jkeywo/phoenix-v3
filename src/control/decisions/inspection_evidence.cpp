module;
#include <algorithm>
#include <cstdint>
#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <set>
#include <string>
#include <tuple>
#include <variant>

module phoenix.control.decisions;
import phoenix.knowledge;

namespace phoenix
{
std::optional<Observation> controller_inspection_observation(const Observation &observed,
                                                             const GameplayKnowledgeProjection &board,
                                                             const CompiledKnowledge &definitions, KnowledgeTick tick)
{
	const auto schema = std::find_if(definitions.schemas.begin(), definitions.schemas.end(),
	                                 [](const auto &value)
	                                 {
		                                 return value.name == "phoenix.team.inspection";
	                                 });
	if (schema == definitions.schemas.end())
		return {};
	const auto key = definitions.schema(schema->name);
	KnowledgeStreamSelection streams;
	std::map<std::pair<std::string, bool>, std::pair<const KnowledgeRecord *, Json>> rooms;
	const auto quality = [](const KnowledgeRecord &record)
	{
		const auto &p = record.value.provenance;
		return knowledge_evidence_quality(p);
	};
	for (const auto *records : {&board.records, &board.shared})
		for (auto row = records->rbegin(); row != records->rend(); ++row)
		{
			const auto &p = row->value.provenance;
			if (row->value.schema != key || !streams.take_latest(*row))
				continue;
			if (row->value.fields.size() != schema->fields.size() || row->value.withdrawn ||
			    p.kind != EvidenceKind::Fact || p.observed_tick > tick ||
			    (row->expires_tick && tick >= row->expires_tick) ||
			    (schema->stale_after && tick - p.observed_tick > schema->stale_after))
				continue;
			Json fields = Json::object();
			for (const auto &[field, value] : row->value.fields)
				std::visit(
				    [&](const auto &scalar)
				    {
					    fields[schema->fields.at(field - 1).name] = scalar;
				    },
				    value);
			if (fields.value("ship", EntityId{}) != observed.own.id)
				continue;
			const auto room = fields.value("compartment", std::string{});
			const bool medical = fields.value("medical", false);
			if (room.empty() || medical == fields.value("repair", false))
				continue;
			fields["faults"] = Json::parse(fields.value("faults", std::string{"[]"}), nullptr, false);
			fields["patients"] = Json::parse(fields.value("patients", std::string{"[]"}), nullptr, false);
			if (!fields["faults"].is_array() || !fields["patients"].is_array())
				continue;
			const auto patient_valid = [&](const Json &patient)
			{
				return patient.is_object() && patient.contains("id") && patient["id"].is_string() &&
				       patient.contains("condition") && patient["condition"].is_string() &&
				       patient.contains("location") && patient["location"].is_number_integer() &&
				       patient.contains("facility") && patient["facility"].is_string() &&
				       patient.value("compartment", std::string{}) == room;
			};
			const auto fault_valid = [&](const Json &fault)
			{
				return fault.is_object() && fault.value("compartment", std::string{}) == room &&
				       fault.contains("compartment_index") && fault["compartment_index"].is_number_integer() &&
				       fault.contains("system") && fault["system"].is_string() && fault.contains("destroyed") &&
				       fault["destroyed"].is_boolean();
			};
			if (!std::all_of(fields["patients"].begin(), fields["patients"].end(), patient_valid) ||
			    !std::all_of(fields["faults"].begin(), fields["faults"].end(), fault_valid) ||
			    fields.value("patient_count", std::size_t{}) != fields["patients"].size() ||
			    fields.value("fault_count", std::size_t{}) != fields["faults"].size())
				continue;
			const auto previous = rooms.find({room, medical});
			const auto identity = [](const KnowledgeRecord &record)
			{
				return std::tuple{record.value.provenance.source, record.publication, record.recipient_actor};
			};
			if (previous == rooms.end() || quality(*row) > quality(*previous->second.first) ||
			    (quality(*row) == quality(*previous->second.first) &&
			     identity(*row) < identity(*previous->second.first)))
				rooms[{room, medical}] = {&*row, std::move(fields)};
		}
	if (rooms.empty())
		return {};
	std::optional<Observation> result;
	const auto view = [&]() -> Observation &
	{
		if (!result)
		{
			result = observed;
			result->gameplay["reported_patients"] = Json::array();
		}
		return *result;
	};
	for (const auto &[room_kind, evidence] : rooms)
	{
		const auto &[room, medical] = room_kind;
		const auto &[record, fields] = evidence;
		const auto &p = record->value.provenance;
		const auto stamp = [&](Json &row)
		{
			row["observed_tick"] = p.observed_tick;
			row["revision"] = p.revision;
			row["source"] = std::to_string(p.source);
			row["uncertainty"] = p.uncertainty;
			row["expires_tick"] = record->expires_tick;
		};
		if (medical)
		{
			bool disclosed = false, local = false;
			for (const auto &team : observed.gameplay.value("teams", Json::array()))
			{
				if (team.value("work_operation", std::string{}) != "medical")
					continue;
				const auto compartments = team.value("compartments", Json::array());
				const auto at = std::find(compartments.begin(), compartments.end(), Json(room));
				if (at == compartments.end())
					continue;
				disclosed = true;
				local |= team.value("available", false) && !team.value("reserved", false) &&
				         !team.value("travel", 0LL) && team.at("location") == at - compartments.begin();
			}
			if (!disclosed || local)
				continue;
			for (auto patient : fields.at("patients"))
			{
				stamp(patient);
				view().gameplay["reported_patients"].push_back(std::move(patient));
			}
		}
		else if (const auto combat = observed.gameplay.find("combat"); combat != observed.gameplay.end())
			for (std::size_t index = 0; index < combat->at("compartments").size(); ++index)
				if (combat->at("compartments")[index].at("id") == room &&
				    !combat->at("compartments")[index].value("inspected", false))
				{
					auto &compartment = view().gameplay["combat"]["compartments"][index];
					compartment["reported_inspection"] = true;
					compartment["inspection_tick"] = p.observed_tick;
					compartment["inspection_revision"] = p.revision;
					compartment["inspection_fault_count"] = fields.at("fault_count");
					compartment["inspection_hp"] = fields.at("hp");
					compartment["inspection_maximum"] = fields.at("maximum");
					for (auto fault : fields.at("faults"))
					{
						stamp(fault);
						view().gameplay["combat"]["faults"].push_back(std::move(fault));
					}
				}
	}
	return result;
}
} // namespace phoenix
