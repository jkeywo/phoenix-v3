module;
#include <algorithm>
#include <nlohmann/json.hpp>
#include <vector>
#include <utility>
module phoenix.knowledge.decision_machinery;
namespace phoenix
{
Json encode_decision_properties(const DecisionProperties &properties)
{
	std::vector<const DecisionProperty *> ordered;
	for (const auto &[id, property] : properties)
		ordered.push_back(&property);
	std::stable_sort(ordered.begin(), ordered.end(),
	                 [](const auto *a, const auto *b)
	                 {
		                 return a->ordinal < b->ordinal;
	                 });
	Json rows = Json::array();
	for (const auto *property : ordered)
	{
		Json row{{"id", property->id},
		         {"label", property->label},
		         {"kind", property->kind},
		         {"unit", property->unit},
		         {"display_scale", property->display_scale}};
		if (property->value.type == state_tree::Type::Boolean)
			row["value"] = property->value.number != 0;
		else if (property->value.type == state_tree::Type::Text)
			row["value"] = property->value.text;
		else
			row["value"] = property->value.number;
		if (property->choice_label)
			row["choice_label"] = *property->choice_label;
		rows.push_back(std::move(row));
	}
	return rows;
}

namespace
{
DecisionProperties decode_properties(const Json &rows)
{
	DecisionProperties result;
	for (const auto &row : rows)
	{
		DecisionProperty p;
		p.ordinal = result.size();
		p.id = row.at("id");
		p.label = row.value("label", p.id);
		p.kind = row.value("kind", std::string{});
		p.unit = row.value("unit", std::string{});
		p.display_scale = row.value("display_scale", std::int64_t{1});
		if (row.contains("choice_label"))
			p.choice_label = row.at("choice_label").get<std::string>();
		const auto &value = row.at("value");
		if (value.is_boolean())
			p.value = state_tree::Value::boolean(value.get<bool>());
		else if (value.is_string())
			p.value = state_tree::Value::string(value.get<std::string>());
		else if (value.is_number_integer())
			p.value = state_tree::Value::integer(value.get<std::int64_t>());
		result.emplace(p.id, std::move(p));
	}
	return result;
}
} // namespace
MachineryDecisionSnapshot machinery_decision_from_json(const Json &gameplay, std::uint64_t observed_tick)
{
	MachineryDecisionSnapshot result;
	if (const auto facts = gameplay.find("ability_facts"); facts != gameplay.end())
		for (const auto &[id, row] : facts->items())
		{
			auto &fact = result.abilities[id];
			fact.phase = row.at("phase");
			fact.ready = row.at("ready");
			fact.blocked = row.at("blocked");
			fact.requested = row.at("requested");
			fact.preparation = row.at("preparation");
			fact.recovery = row.at("recovery");
			fact.output = row.at("output");
			fact.properties_available = row.contains("properties");
			if (fact.properties_available)
				fact.properties = decode_properties(row.at("properties"));
		}
	else if (const auto rows = gameplay.find("abilities"); rows != gameplay.end())
		for (const auto &row : *rows)
		{
			const auto id = row.at("control").get<std::string>();
			if (result.abilities.contains(id))
				continue;
			auto &fact = result.abilities[id];
			fact.phase = row.at("phase");
			fact.ready = fact.phase == "ready";
			fact.blocked = !row.at("blocker").get<std::string>().empty();
			fact.requested = row.at("requested");
			fact.preparation = row.at("progress");
			const auto deadline = row.at("ready").get<std::uint64_t>();
			fact.recovery = deadline > observed_tick ? deadline - observed_tick : 0;
			fact.output = row.at("output");
			fact.properties_available = row.contains("properties");
			if (fact.properties_available)
				fact.properties = decode_properties(row.at("properties"));
		}
	if (const auto facts = gameplay.find("process_facts"); facts != gameplay.end())
		for (const auto &[id, row] : facts->items())
		{
			auto &fact = result.processes[id];
			fact.phase = row.at("phase");
			fact.requested = row.at("requested");
			fact.running = row.at("running");
			fact.paused = row.at("paused");
			fact.blocked = row.at("blocked");
			fact.finite = row.at("finite");
			fact.driver = row.at("driver");
			if (row.contains("activity"))
				fact.activity = row.at("activity").get<std::int64_t>();
			if (row.contains("work"))
				fact.work = row.at("work").get<std::int64_t>();
			if (row.contains("target"))
				fact.target = row.at("target").get<std::string>();
			if (row.contains("progress"))
				fact.progress = row.at("progress").get<std::int64_t>();
			if (row.contains("required_work"))
				fact.required_work = row.at("required_work").get<std::int64_t>();
			if (row.contains("completed"))
				fact.completed = row.at("completed").get<bool>();
			if (row.contains("ready"))
				fact.ready = row.at("ready").get<bool>();
			if (row.contains("completion_delivered"))
				fact.completion_delivered = row.at("completion_delivered").get<bool>();
			if (row.contains("properties"))
				fact.properties = decode_properties(row.at("properties"));
		}
	else if (const auto rows = gameplay.find("processes"); rows != gameplay.end())
		for (const auto &row : *rows)
		{
			const auto id = row.at("control").get<std::string>();
			if (id.empty() || result.processes.contains(id))
				continue;
			DecisionProcessRow fact;
			fact.phase = row.at("phase");
			fact.requested = row.at("requested");
			fact.blocker = row.at("blocker");
			fact.output = row.at("output");
			fact.work = row.at("work");
			fact.finite = row.at("finite");
			fact.driver = row.at("driver");
			fact.details_available = row.at("details_available");
			fact.target = row.at("target");
			fact.progress = row.at("progress");
			fact.required_work = row.at("required_work");
			fact.completed = row.at("completed");
			fact.ready = row.at("ready");
			fact.completion_delivered = row.at("completion_delivered");
			fact.properties = decode_properties(row.at("properties"));
			result.processes[id] = process_decision_facts(fact);
		}
	return result;
}
} // namespace phoenix
