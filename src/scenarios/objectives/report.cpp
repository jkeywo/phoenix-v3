module;
#include <algorithm>
#include <nlohmann/json.hpp>
#include <string>
#include <utility>

module phoenix.scenarios.objectives;

namespace phoenix
{
const char *objective_status_name(ObjectiveStatus s)
{
	static constexpr const char *names[]{"pending", "active", "completed", "failed", "cancelled"};
	return names[static_cast<unsigned>(s)];
}
Json build_mission_report(const ScenarioRuntime &r, const ScenarioReportDefinition &spec, ScenarioId instance_id,
                          const ScenarioFacts &facts, const MissionLedger &mission_ledger, EntityId ship,
                          const MissionReportDetails &details, Json legacy)
{
	legacy["version"] = 2;
	legacy["title"] = spec.title_key;
	legacy["result"] = spec.fallback;
	legacy["result_title"] = spec.fallback_title_key;
	legacy["explanation"] = spec.fallback_explanation_key;
	legacy["facts"] = Json::array();
	legacy["personnel"] = Json::array();
	legacy["objectives"] = Json::array();
	legacy["assessments"] = Json::array();
	legacy["log"] = Json::array();
	legacy["crew"] = details.crew;
	legacy["ship_condition"] = details.ship_condition;
	for (const auto &entry : mission_ledger.transitions)
		if (entry.source == ship && entry.kind == "mission.log")
			legacy["log"].push_back({{"tick", entry.tick}, {"text", entry.detail}, {"name", ""}});
		else if (entry.source == ship && entry.kind == "mission.casualty")
			legacy["log"].push_back({{"tick", entry.tick},
			                         {"text", entry.detail == "dead" ? "Duty officer killed" : "Duty officer injured"},
			                         {"name", details.asset_labels.at("officer:" + entry.operation)}});
	for (const auto &[event, text] : spec.log_events)
		for (const auto &entry : r.events(event))
			legacy["log"].push_back({{"tick", entry.tick}, {"text", text}, {"name", ""}});
	std::stable_sort(legacy["log"].begin(), legacy["log"].end(),
	                 [](const auto &a, const auto &b)
	                 {
		                 return a.at("tick") < b.at("tick");
	                 });
	for (const auto &a : spec.assessments)
	{
		Json result{{"title", a.title_key},
		            {"result", a.fallback},
		            {"result_title", a.fallback_title_key},
		            {"explanation", a.fallback_explanation_key}};
		for (const auto &rule : a.rules)
			if (scenario_condition(rule.when, facts).value_or(false))
			{
				result["result"] = rule.result;
				result["result_title"] = rule.title_key;
				result["explanation"] = rule.explanation_key;
				break;
			}
		legacy["assessments"].push_back(std::move(result));
	}
	for (const auto &rule : spec.rules)
		if (scenario_condition(rule.when, facts).value_or(false))
		{
			legacy["result"] = rule.result;
			legacy["result_title"] = rule.title_key;
			legacy["explanation"] = rule.explanation_key;
			break;
		}
	for (const auto &name : spec.objectives)
	{
		const auto &d = r.definition().objectives.at(name);
		const ScenarioObjective *latest = nullptr;
		for (const auto &[id, o] : r.objectives())
			if (o.instance == instance_id && o.definition == name && (!latest || o.id > latest->id))
				latest = &o;
		legacy["objectives"].push_back({{"id", name},
		                                {"title", d.title_key},
		                                {"status", latest ? objective_status_name(latest->status) : "pending"},
		                                {"optional", d.optional}});
	}
	for (const auto &[name, label] : spec.facts)
	{
		Json value = nullptr;
		const auto &v = facts.at(name);
		if (v.type == state_tree::Type::Text)
			value = v.text;
		else if (v.type == state_tree::Type::Boolean)
			value = v.number != 0;
		else if (v.type == state_tree::Type::Integer)
			value = v.number;
		legacy["facts"].push_back({{"label", label}, {"value", value}});
	}
	legacy["personnel"] = details.personnel;
	return legacy;
}
} // namespace phoenix
