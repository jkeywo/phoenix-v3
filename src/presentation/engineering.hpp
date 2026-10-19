#pragma once
namespace phoenix
{
struct EngineeringCard
{
	std::string id, title, summary;
	std::optional<int> heat, battery;
	std::vector<std::pair<std::string, std::string>> controls;
};
inline std::string engineering_name(std::string name)
{
	for (auto &c : name)
		if (c == '.' || c == '-' || c == '_')
			c = ' ';
	if (!name.empty() && name[0] >= 'a' && name[0] <= 'z')
		name[0] -= 'a' - 'A';
	return name;
}
inline std::string descriptor_action(const std::string &id)
{
	std::string result = "game.control/";
	for (unsigned char c : id)
	{
		result += "0123456789abcdef"[c >> 4];
		result += "0123456789abcdef"[c & 15];
	}
	return result;
}
inline std::vector<EngineeringCard> engineering_cards(const Observation &view)
{
	std::vector<EngineeringCard> result;
	for (const auto &system : view.systems)
	{
		if (system.kind != OperatingSystem::Generator && system.kind != OperatingSystem::Storage &&
		    system.kind != OperatingSystem::Distribution && system.kind != OperatingSystem::Coolant)
			continue;
		EngineeringCard card{system.id, engineering_name(system.id), {}};
		const auto &t = system.telemetry;
		if (t.contains("heat") && t.contains("heat_capacity") && t.at("heat_capacity") > 0)
			card.heat = static_cast<int>(std::clamp<std::int64_t>(t.at("heat") * 100 / t.at("heat_capacity"), 0, 100));
		if (t.contains("stored_energy") && t.contains("energy_capacity") && t.at("energy_capacity") > 0)
			card.battery = static_cast<int>(
			    std::clamp<std::int64_t>(t.at("stored_energy") * 100 / t.at("energy_capacity"), 0, 100));
		const auto metric = [&](const char *key, const char *label, const char *capacity = nullptr)
		{
			if (!t.contains(key))
				return;
			if (!card.summary.empty())
				card.summary += " | ";
			card.summary += localise(label) + ": " + std::to_string(t.at(key));
			if (capacity && t.contains(capacity) && t.at(capacity) > 0)
				card.summary += " / " + std::to_string(t.at(capacity)) + " (" +
				                std::to_string(t.at(key) * 100 / t.at(capacity)) + "%)";
		};
		metric("heat", "Heat", "heat_capacity");
		metric("stored_energy", "Battery", "energy_capacity");
		metric("actual_output", "Reactor output / s");
		metric("incoming_power", "Incoming power / s");
		metric("used_power", "Power used / s");
		metric("group_allocation", "Power allocation / s");
		metric("cooling_allocation", "Coolant allocation / s");
		metric("actual_cooling", "Heat removed / s");
		metric("coolant_stock", "Coolant remaining");
		if (t.contains("thermal_stopped") && t.at("thermal_stopped"))
			card.summary += " | " + localise("THERMAL SHUTDOWN");
		if (t.contains("supply_limited") && t.at("supply_limited"))
			card.summary += " | " + localise("Supply limited");
		if (card.summary.empty())
			card.summary = localise("Awaiting telemetry");
		for (const auto &d : view.gameplay.value("controls", Json::array()))
		{
			if (d.value("system", "") != system.id)
				continue;
			const auto action = d.value("action", "");
			if (action.starts_with("message.") || action.starts_with("publish."))
				continue;
			if (const auto *spec = operating_action(action); spec && (spec->system != system.kind || spec->boolean))
				continue;
			const auto value = d.value("value", std::int64_t{});
			std::string label;
			if (action == "delegate")
				label = localise(value ? "Use automatic control" : "Use manual controls");
			else if (action == "generator_output")
				label = localise("Reactor output");
			else if (action == "group_allocation" || action == "summary.power")
				label = localise("Power");
			else if (action == "cooling_allocation" || action == "summary.coolant")
				label = localise("Coolant");
			else if (action == "summary.output")
				label = localise("Output intent");
			else if (action == "charge_limit")
				label = localise("Charge limit");
			else if (action == "discharge_limit")
				label = localise("Discharge limit");
			else
				label = localise(engineering_name(action));
			if (action != "delegate")
			{
				const auto *spec = operating_action(action);
				label += " " + (spec && spec->boolean ? localise(value ? "On" : "Off") : std::to_string(value));
			}
			card.controls.emplace_back(descriptor_action(d.at("id")), std::move(label));
		}
		result.push_back(std::move(card));
	}
	return result;
}
} // namespace phoenix
