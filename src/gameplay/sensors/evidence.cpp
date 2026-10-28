module;
#include <nlohmann/json.hpp>
#include <cstdint>
#include <map>
#include <span>
#include <string>
#include <utility>
module phoenix.gameplay.sensors.evidence;
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
		for (const auto *view : knowledge.sensor_records(address))
		{
			const auto &record = *view;
			const auto &name = kd.schemas.at(record.value.schema - 1).name;
			if (!name.starts_with("phoenix.sensor."))
				continue;
			const auto field = kd.field(record.value.schema, "payload");
			if (!record.value.fields.contains(field))
				continue;
			const auto cap = name.substr(15);
			if (cap != "findings")
			{
				// Select the same first maximum timestamp before decoding. Local
				// samples win equal timestamps, including retained non-live samples.
				const auto target = contacts.find(record.value.subject);
				if (target != contacts.end())
				{
					const auto &findings = target->second.at("findings");
					const auto current = findings.find(cap);
					if (current != findings.end() && current->at("observed") >= record.value.provenance.observed_tick)
						continue;
				}
				auto &candidate = candidates[{record.value.subject, cap}];
				if (!candidate || candidate->value.provenance.observed_tick < record.value.provenance.observed_tick)
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
} // namespace phoenix
