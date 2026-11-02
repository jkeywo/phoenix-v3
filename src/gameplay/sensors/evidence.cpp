module;
#include <nlohmann/json.hpp>
#include <cstdint>
#include <map>
#include <set>
#include <span>
#include <string>
#include <tuple>
#include <variant>
#include <utility>
module phoenix.gameplay.sensors.evidence;
import phoenix.knowledge.decision_combat;
import phoenix.knowledge;
namespace phoenix
{
std::map<EntityId, Json> collect_sensor_evidence(const ConsoleAddress &address,
                                                 std::span<const SensorRuntime *const> sources,
                                                 const GameplayKnowledge &knowledge, std::uint64_t tick)
{
	std::map<EntityId, Json> contacts;
	std::map<EntityId, std::uint64_t> positions;
	auto contact = [&](EntityId target) -> Json &
	{
		auto &row = contacts[target];
		if (row.is_null())
			row = Json{{"id", target}, {"findings", Json::object()}};
		return row;
	};
	auto finding = [&](EntityId target, const std::string &cap, const Json &data, std::uint64_t observed, bool live)
	{
		auto &value = contact(target)["findings"][cap];
		if (value.is_null() || value.at("observed") < observed ||
		    (value.at("observed") == observed && live && !value.at("live").get<bool>()))
			value = Json{{"data", data}, {"observed", observed}, {"live", live}};
	};
	for (const auto *source : sources)
		for (const auto &[target, track] : source->tracks)
		{
			auto &row = contact(target);
			if (!positions.contains(target) || positions.at(target) < track.seen)
			{
				row["x"] = track.x;
				row["y"] = track.y;
				positions[target] = track.seen;
			}
			for (const auto &[cap, sample] : track.samples)
				if (!sample.data.is_null())
					finding(target, cap, sample.data, sample.observed, sample.observed == tick);
		}
	if (knowledge.contains_ship(address.ship))
	{
		const auto &kd = knowledge.definition();
		std::map<std::pair<EntityId, std::string>, const KnowledgeRecord *> candidates;
		KnowledgeStreamSelection streams;
		const auto records = knowledge.sensor_records(address);
		for (auto entry = records.rbegin(); entry != records.rend(); ++entry)
		{
			const auto *view = *entry;
			const auto &record = *view;
			// Select a stream's latest report before checking validity or fields:
			// an expired report or withdrawal must not resurrect its older payload.
			if (!streams.take_latest(record) || record.value.withdrawn || knowledge.stale(record, tick))
				continue;
			const auto &name = kd.schemas.at(record.value.schema - 1).name;
			if (!name.starts_with("phoenix.sensor."))
				continue;
			const auto field = kd.field(record.value.schema, "payload");
			if (!record.value.fields.contains(field))
				continue;
			const auto cap = name.substr(15);
			if (cap != "findings")
			{
				// Local samples win equal timestamps, including retained samples.
				// Independent reports use provenance quality and stable source identity.
				const auto target = contacts.find(record.value.subject);
				if (target != contacts.end())
				{
					const auto &findings = target->second.at("findings");
					const auto current = findings.find(cap);
					if (current != findings.end() && current->at("observed") >= record.value.provenance.observed_tick)
						continue;
				}
				auto &candidate = candidates[{record.value.subject, cap}];
				const auto quality = [](const KnowledgeRecord &value)
				{
					const auto &p = value.value.provenance;
					return knowledge_evidence_quality(p);
				};
				const auto identity = [](const KnowledgeRecord &value)
				{
					return std::tuple{value.value.provenance.source, value.publication, value.scope, value.owner};
				};
				if (!candidate || quality(*candidate) < quality(record) ||
				    (quality(*candidate) == quality(record) && identity(record) < identity(*candidate)))
					candidate = view;
				continue;
			}
			const auto payload = knowledge.decoded_field(record, field);
			if (cap == "findings")
				for (const auto &entry : *payload)
					finding(record.value.subject, "finding:" + entry.at("id").get<std::string>(),
					        Json{{"id", entry.at("id")}, {"text", entry.at("text")}}, entry.at("observed"), false);
		}
		for (const auto &[key, record] : candidates)
		{
			const auto payload = knowledge.decoded_field(*record, kd.field(record->value.schema, "payload"));
			finding(key.first, key.second, *payload, record->value.provenance.observed_tick, false);
		}
	}
	return contacts;
}
std::map<EntityId, SensorContactDecision>
collect_sensor_decision_evidence(const ConsoleAddress &address, std::span<const SensorRuntime *const> sources,
                                 const GameplayKnowledge &knowledge, std::uint64_t tick)
{
	std::map<EntityId, SensorContactDecision> out;
	const auto finding = [&](EntityId target, const std::string &cap, SensorFindingDecision value)
	{
		auto &c = out[target];
		c.id = target;
		auto at = c.findings.find(cap);
		if (at == c.findings.end() || at->second.observed < value.observed ||
		    (at->second.observed == value.observed && value.live && !at->second.live))
			c.findings[cap] = std::move(value);
	};
	for (const auto *source : sources)
		for (const auto &[target, track] : source->tracks)
		{
			out[target].id = target;
			for (const auto &[cap, sample] : track.samples)
				if (sample.decision.data_present)
				{
					auto v = sample.decision;
					v.live = v.observed == tick;
					finding(target, cap, std::move(v));
				}
		}
	if (!knowledge.contains_ship(address.ship))
		return out;
	std::map<std::pair<EntityId, std::string>, const KnowledgeRecord *> candidates;
	KnowledgeStreamSelection streams;
	const auto records = knowledge.sensor_records(address);
	for (auto at = records.rbegin(); at != records.rend(); ++at)
	{
		const auto &r = **at;
		if (!streams.take_latest(r) || r.value.withdrawn || knowledge.stale(r, tick))
			continue;
		const auto *payload = knowledge.decision_payload(r);
		const auto *data = payload ? std::get_if<SensorReportDecision>(payload) : nullptr;
		if (!data || !data->valid)
			continue;
		if (data->capability == "findings")
		{
			for (const auto &[cap, v] : data->findings)
				finding(r.value.subject, cap, v);
			continue;
		}
		auto existing = out.find(r.value.subject);
		if (existing != out.end())
		{
			auto f = existing->second.findings.find(data->capability);
			if (f != existing->second.findings.end() && f->second.observed >= r.value.provenance.observed_tick)
				continue;
		}
		auto &selected = candidates[{r.value.subject, data->capability}];
		const auto identity = [](const KnowledgeRecord &v)
		{
			return std::tuple{v.value.provenance.source, v.publication, v.scope, v.owner};
		};
		if (!selected ||
		    knowledge_evidence_quality(selected->value.provenance) < knowledge_evidence_quality(r.value.provenance) ||
		    (knowledge_evidence_quality(selected->value.provenance) == knowledge_evidence_quality(r.value.provenance) &&
		     identity(r) < identity(*selected)))
			selected = &r;
	}
	for (const auto &[key, r] : candidates)
		if (const auto *payload = knowledge.decision_payload(*r))
			if (const auto *data = std::get_if<SensorReportDecision>(payload))
				for (const auto &[cap, v] : data->findings)
					finding(key.first, cap, v);
	return out;
}
} // namespace phoenix
