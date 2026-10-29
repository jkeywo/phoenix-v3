module;
#include "native_headers.hpp"

module phoenix.simulation;
import :projection_bindings;

namespace phoenix::world_detail
{
auto Runtime::prune_displays() -> void
{
	std::erase_if(displays,
	              [&](const auto &entry)
	              {
		              const auto &d = entry.second;
		              return !ownership.claims().contains(d.claimant) ||
		                     (ownership.claims().at(d.claimant) != d.address &&
		                      console_operator(d.address) != d.claimant);
	              });
}
auto Runtime::display_widget(const ConsoleAddress &address, const std::string &id) const -> const Json *
{
	const auto spec = seat(address.ship, address.console);
	if (!spec || !console_layouts.contains(spec->definition))
		return nullptr;
	for (const auto &w : console_layouts.at(spec->definition).at("widgets"))
		if (w.at("id") == id &&
		    (w.at("kind") == "radar" || w.at("kind") == "navigation" || w.at("kind") == "conversations"))
			return &w;
	return nullptr;
}

auto Runtime::display_command(const ConsoleCommandContext &context, const OperatingCommand &command, bool execute)
    -> bool
{
	if (command.action != "display.toggle" && command.action != "display.update")
		return false;
	const auto &address = context.address;
	const auto claimant = context.claimant;
	if (!command.controller.empty() || !command.channel.empty() || command.request_id || command.value ||
	    !display_widget(address, command.system) || command.arguments.size() != 3 || command.arguments[2] < 1)
		return false;
	const auto widget = display_widget(address, command.system);
	if (widget->at("kind") == "radar")
	{
		const auto view = capture_console_observation(address);
		if (!select_system(view.systems, OperatingSystem::Radar, widget->value("system", std::string{}), false))
			return false;
	}
	for (auto arg : command.arguments)
		if (arg < -1000000000 || arg > 1000000000)
			return false;
	const auto found = displays.find(address.ship);
	const bool same = found != displays.end() && found->second.claimant == claimant &&
	                  found->second.address == address && found->second.widget == command.system;
	if (command.action == "display.update")
	{
		if (!same || command.generation != found->second.revision)
			return false;
		if (execute)
			found->second.camera = command.arguments;
	}
	else
	{
		if (command.generation || display_revision >= 9007199254740991ULL)
			return false;
		if (execute)
		{
			++display_revision;
			if (same)
				displays.erase(address.ship);
			else
				displays[address.ship] = {claimant, display_revision, address, command.system, command.arguments};
		}
	}
	return true;
}

} // namespace phoenix::world_detail
