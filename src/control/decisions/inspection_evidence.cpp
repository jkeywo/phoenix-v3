module;
#include <algorithm>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <tuple>
#include <variant>
module phoenix.control.decisions;
import phoenix.knowledge.decision_combat;
import phoenix.control.candidate_facts;
import phoenix.knowledge;
namespace phoenix
{
std::optional<DecisionObservation> controller_inspection_observation(const DecisionObservation &observed,
                                                                     const GameplayKnowledgeProjection &board,
                                                                     const CompiledKnowledge &definitions,
                                                                     KnowledgeTick tick)
{
	KnowledgeStreamSelection streams;
	using Selected = std::pair<const KnowledgeRecord *, const InspectionReportDecision *>;
	std::map<std::pair<std::string, bool>, Selected> rooms;
	for (const auto *records : {&board.records, &board.shared})
		for (auto row = records->rbegin(); row != records->rend(); ++row)
		{
			const auto *payload = board.decision_payload(*row);
			const auto *fields = payload ? std::get_if<InspectionReportDecision>(payload) : nullptr;
			if (!fields || !streams.take_latest(*row))
				continue;
			const auto &p = row->value.provenance;
			const auto &schema = definitions.schemas.at(row->value.schema - 1);
			if (!fields->valid || row->value.withdrawn || p.kind != EvidenceKind::Fact || p.observed_tick > tick ||
			    (row->expires_tick && tick >= row->expires_tick) ||
			    (schema.stale_after && tick - p.observed_tick > schema.stale_after) || fields->ship != observed.own.id)
				continue;
			const auto key = std::pair{fields->compartment, fields->medical};
			const auto previous = rooms.find(key);
			const auto identity = [](const KnowledgeRecord &r)
			{
				return std::tuple{r.value.provenance.source, r.publication, r.recipient_actor};
			};
			if (previous == rooms.end() ||
			    knowledge_evidence_quality(p) > knowledge_evidence_quality(previous->second.first->value.provenance) ||
			    (knowledge_evidence_quality(p) ==
			         knowledge_evidence_quality(previous->second.first->value.provenance) &&
			     identity(*row) < identity(*previous->second.first)))
				rooms[key] = {&*row, fields};
		}
	std::optional<DecisionObservation> result;
	const auto view = [&]() -> DecisionObservation &
	{
		if (!result)
		{
			result = observed;
			result->reported_patients.clear();
		}
		return *result;
	};
	for (const auto &[key, selected] : rooms)
	{
		const auto &[room, medical] = key;
		const auto &[record, fields] = selected;
		const auto &p = record->value.provenance;
		const auto stamp = [&](auto &row)
		{
			row.observed_tick = p.observed_tick;
			row.revision = p.revision;
			row.source = std::to_string(p.source);
			row.uncertainty = p.uncertainty;
			row.expires_tick = record->expires_tick;
		};
		if (medical)
		{
			bool disclosed = false, local = false;
			for (const auto &team : observed.teams)
			{
				if (team.work_operation != "medical")
					continue;
				const auto at = std::find(team.compartments.begin(), team.compartments.end(), room);
				if (at == team.compartments.end())
					continue;
				disclosed = true;
				local |=
				    team.available && !team.reserved && !team.travel && team.location == at - team.compartments.begin();
			}
			if (!disclosed || local)
				continue;
			for (auto patient : fields->patients)
			{
				stamp(patient);
				DecisionPatient v;
				v.id = patient.id;
				v.name = patient.name;
				v.condition = patient.condition;
				v.facility = patient.facility;
				v.escort = patient.escort;
				v.source = patient.source;
				v.location = patient.location;
				v.uncertainty = patient.uncertainty;
				v.stabilised = patient.stabilised;
				v.observed_tick = patient.observed_tick;
				v.revision = patient.revision;
				v.compartment = patient.compartment;
				v.expires_tick = patient.expires_tick;
				view().reported_patients.push_back(std::move(v));
			}
		}
		else if (observed.combat.present)
			for (std::size_t i = 0; i < observed.combat.compartments.size(); ++i)
			{
				if (observed.combat.compartments[i].id != room || observed.combat.compartments[i].inspected)
					continue;
				auto &c = view().combat.compartments[i];
				c.reported_inspection = true;
				c.inspection_tick = p.observed_tick;
				c.inspection_revision = p.revision;
				c.inspection_fault_count = static_cast<std::int64_t>(fields->faults.size());
				c.inspection_hp = fields->hp;
				c.inspection_maximum = fields->maximum;
				for (auto fault : fields->faults)
				{
					stamp(fault);
					fault.evidence_present = true;
					view().combat.faults.push_back(std::move(fault));
				}
			}
	}
	return result;
}
} // namespace phoenix
