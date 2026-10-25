#pragma once
namespace phoenix
{
struct EngineeringCard
{
	std::string id, title, summary;
	std::optional<int> heat, battery, condition;
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
inline std::vector<std::string> command_consumers(const Observation &view, const Json &descriptor)
{
	const auto controllers = view.gameplay.find("controllers");
	if (controllers == view.gameplay.end())
		return {};
	for (const auto &controller : *controllers)
		if (controller.value("id", "") == descriptor.value("controller", ""))
		{
			const auto &commands = controller.at("commands");
			const auto field = commands.find(descriptor.value("channel", ""));
			if (field != commands.end())
				return field->value("consumers", std::vector<std::string>{});
		}
	return {};
}
inline std::set<std::string> station_owned_systems(const Observation &view)
{
	std::set<std::string> owned_systems;
	for (const auto &controller : view.gameplay.value("controllers", Json::array()))
	{
		const auto channels = controller.value("commands", Json::object());
		for (const auto &[channel, command] : channels.items())
			for (const auto &consumer : command.value("consumers", std::vector<std::string>{}))
				owned_systems.insert(consumer);
	}
	if (!view.gameplay.contains("controllers"))
		for (const auto &system : view.systems)
			if (system.controlled)
				owned_systems.insert(system.id);
	return owned_systems;
}
inline std::vector<EngineeringCard> engineering_cards(const Observation &view)
{
	std::vector<EngineeringCard> result;
	const auto logistics = view.gameplay.find("logistics");
	const bool assisted_stock = logistics != view.gameplay.end() && !logistics->value("manual", true);
	std::set<std::string> placed_controls;
	const auto owned_systems = station_owned_systems(view);
	// Resolve immutable Controller bindings once, rather than copying and searching
	// the entire Controller board for every control on every resource card.
	std::vector<std::pair<const Json *, std::vector<std::string>>> commands;
	if (const auto controls = view.gameplay.find("controls"); controls != view.gameplay.end())
		for (const auto &descriptor : *controls)
			commands.emplace_back(&descriptor, command_consumers(view, descriptor));
	for (const auto &system : view.systems)
	{
		if (!owned_systems.contains(system.id))
			continue;
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
		for (const auto &[descriptor, consumers] : commands)
		{
			const auto &d = *descriptor;
			if (std::find(consumers.begin(), consumers.end(), system.id) == consumers.end())
				continue;
			const auto action = d.value("action", "");
			if (action.starts_with("message.") || action.starts_with("publish."))
				continue;
			if (const auto *spec = operating_action(action); spec && (spec->system != system.kind || spec->boolean))
				continue;
			const auto value = d.value("value", std::int64_t{});
			std::string label;
			if (action == "generator_output")
				label = localise("Reactor output");
			else if (action == "group_allocation")
				label = localise("Power");
			else if (action == "cooling_allocation")
				label = localise("Coolant");
			else if (action == "charge_limit")
				label = localise("Charge limit");
			else if (action == "discharge_limit")
				label = localise("Discharge limit");
			else
				label = localise(engineering_name(action));
			const auto *spec = operating_action(action);
			label += " " + (spec && spec->boolean ? localise(value ? "On" : "Off") : std::to_string(value));
			if (!placed_controls.insert(d.at("id").get<std::string>()).second)
				continue;
			if (consumers.size() > 1)
				label = localise("Shared control") + ": " + label;
			card.controls.emplace_back(descriptor_action(d.at("id")), std::move(label));
		}
		if (!assisted_stock || !card.controls.empty())
			result.push_back(std::move(card));
	}
	return result;
}
} // namespace phoenix
