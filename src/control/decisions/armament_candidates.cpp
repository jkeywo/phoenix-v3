module;
#include <nlohmann/json.hpp>
#include <cstdint>
#include <span>
#include <map>
#include <set>
#include <string>
#include <vector>

module phoenix.control.decisions;

namespace phoenix
{
std::vector<std::map<std::string, state_tree::Value>>
controller_armament_candidates(const ConsoleControllerRuntime &controller, const ControllerObservationSources &input)
{
	using state_tree::Value;
	using Facts = std::map<std::string, Value>;
	const auto contacts = controller_contact_candidates(controller, input);
	std::set<std::string> owned;
	for (const auto &[role, systems] : controller.definition.equipment)
		owned.insert(systems.begin(), systems.end());
	std::vector<Facts> result;
	for (const auto &weapon : combat_observation(input.observation).at("weapons"))
	{
		const auto id = weapon.at("id").get<std::string>();
		if (!owned.contains(id))
			continue;
		std::int64_t accepted_target = 0;
		if (const auto orders = input.observation.gameplay.find("directed"); orders != input.observation.gameplay.end())
			for (const auto &order : *orders)
				if (order.at("id") == id)
					accepted_target = order.at("target");
		for (const auto &contact : contacts)
			for (const auto &option : weapon.at("ammunition_options"))
			{
				Facts facts = contact;
				const auto integer = [&](const char *name, std::int64_t value)
				{
					facts[std::string("armament.") + name] = Value::integer(value);
				};
				const auto boolean = [&](const char *name, bool value)
				{
					facts[std::string("armament.") + name] = Value::boolean(value);
				};
				const auto text = [&](const char *name, const std::string &value)
				{
					facts[std::string("armament.") + name] = Value::string(value);
				};
				const auto index = option.at("index").get<std::int64_t>();
				const auto range = option.at("range_mm").get<std::int64_t>();
				const bool available = option.at("available").get<bool>();
				const bool enabled = weapon.at("enabled").get<bool>();
				const bool current = contact.at("contact_candidate.current").number != 0;
				const bool hostile = contact.at("contact_candidate.hostile").number != 0;
				const bool in_range = contact.at("contact_candidate.distance").number <= range;
				text("weapon", id);
				text("magazine", option.at("id"));
				text("guidance", option.value("guidance", std::string{}));
				text("reason", !enabled     ? "weapon_unavailable"
				               : !available ? "ammunition_unavailable"
				               : !current   ? "current_targeting_unavailable"
				               : !hostile   ? "no_known_hostile"
				               : !in_range  ? "target_outside_payload_range"
				                            : "permitted_armament_option");
				integer("index", index);
				integer("target", accepted_target);
				integer("range", range);
				integer("quantity", option.value("quantity", std::int64_t{0}));
				integer("use", option.value("use", std::int64_t{0}));
				integer("retask_cost", weapon.value("selected", -1LL) == index ? 0 : 1);
				boolean("available", available);
				boolean("in_range", in_range);
				boolean("selected", weapon.value("selected", -1LL) == index);
				boolean("loaded", weapon.value("loaded", -1LL) == index);
				boolean("enabled", enabled);
				boolean("fire_allowed", true);
				result.push_back(std::move(facts));
			}
	}
	if (result.empty())
	{
		Facts empty;
		for (const auto &field : controller.brain->program.registry.observations)
			if (field.name.starts_with("contact_candidate.") || field.name.starts_with("armament."))
				empty[field.name] = field.type == state_tree::Type::Text      ? Value::string("")
				                    : field.type == state_tree::Type::Boolean ? Value::boolean(false)
				                                                              : Value::integer(0);
		empty["armament.reason"] = Value::string("no_owned_ammunition_options");
		result.push_back(std::move(empty));
	}
	return result;
}
} // namespace phoenix
