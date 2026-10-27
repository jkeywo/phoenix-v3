#pragma once
namespace phoenix::test
{
// Predecessor comparisons retain every persisted native body field: motion,
// actuator/charge remainders, native resource counters, heat, coolant, stocks,
// recipe claims and progress. Schema, content archive identities and the new
// top-level execution-owner records are outside this physical checkpoint.
inline Json machinery_physical_checkpoint(const Json &saved)
{
	Json bodies = saved.at("bodies");
	// These two counters were introduced for process throughput scaling. At the
	// unchanged baseline rate their documented neutral value is exactly zero;
	// retain nonzero values so this comparison cannot hide altered arithmetic.
	for (auto &body : bodies)
		if (body.contains("resources"))
			for (const auto *kind : {"generators", "cooling_links"})
				for (auto &owner : body["resources"]["network"][kind])
					if (owner.contains("throughput_remainder") && owner.at("throughput_remainder") == 0)
						owner.erase("throughput_remainder");
	// Producer-input telemetry has no predecessor field. Its neutral zero value
	// is metadata; any nonzero amount remains visible to this physical check.
	for (auto &body : bodies)
		if (body.contains("resources"))
			for (const auto *kind : {"stores", "telemetry"})
				for (auto &store : body["resources"]["network"][kind])
					if (store.contains("producer_consumed") && store.at("producer_consumed") == 0)
						store.erase("producer_consumed");
	// Flight persists its authored definition as JSON text. Only process-owner
	// metadata is new; keep every existing fitted property and physical value.
	for (auto &body : bodies)
		if (body.contains("gameplay"))
			for (auto &equipment : body["gameplay"])
				if (equipment.contains("flight"))
				{
					auto &flight = equipment["flight"];
					if (flight.contains("work_remainder") && flight.at("work_remainder") == 0)
						flight.erase("work_remainder");
					if (flight.contains("definition"))
					{
						auto definition = Json::parse(flight.at("definition").get<std::string>());
						definition.erase("processes");
						flight["definition"] = definition.dump();
					}
				}
	// Finite servicing adds execution attribution to native transfer records.
	// Keep quantities, routing, claims, IDs and all other physical fields.
	for (auto &body : bodies)
		if (body.contains("logistics"))
			for (const auto *kind : {"requests", "batches"})
				if (body["logistics"].contains(kind))
					for (auto &transfer : body["logistics"][kind])
						transfer.erase("service");
	Json result{{"tick", saved.at("tick")}, {"bodies", std::move(bodies)}};
	// Finite work also owns real material commitments, bay service fractions and
	// away progress outside body components. Preserve all native fields here.
	for (const auto *key : {"logistics", "craft", "away_missions"})
		if (saved.contains(key))
			result[key] = saved.at(key);
	if (result.contains("logistics"))
		for (const auto *kind : {"requests", "batches"})
			if (result["logistics"].contains(kind))
				for (auto &transfer : result["logistics"][kind])
					transfer.erase("service");
	return result;
}
inline Json machinery_physical_trace(const Json &trace)
{
	Json result = Json::array();
	for (const auto &entry : trace)
		if (entry.is_object() && entry.contains("bodies"))
			result.push_back(machinery_physical_checkpoint(entry));
		else
			result.push_back(entry);
	return result;
}
} // namespace phoenix::test
