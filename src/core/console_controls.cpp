module;
#include <nlohmann/json.hpp>
#include <algorithm>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

module phoenix.console_controls;

namespace phoenix
{
namespace
{
const Json *section(const Observation &view, const std::string &id)
{
	const Json *found = nullptr;
	if (!view.gameplay.contains("controllers") || !view.gameplay.at("controllers").is_array())
		return nullptr;
	for (const auto &s : view.gameplay.at("controllers"))
		if (s.at("id") == id)
		{
			if (found)
				return nullptr;
			found = &s;
		}
	return found && found->at("console") == view.console ? found : nullptr;
}
std::string binding(const std::string &owner, const std::string &channel)
{
	return "controller/" + owner + "/" + channel;
}
bool overlaps(const Json &left, const Json &right)
{
	for (const auto &a : left.at("consumers"))
		for (const auto &b : right.at("consumers"))
			if (a == b)
				return true;
	return false;
}
bool conversation_allowed(const Observation &view, const OperatingCommand &command)
{
	if (!command.action.starts_with("comms.") || !command.system.empty() || !command.channel.empty() ||
	    !command.arguments.empty() || command.text.size() > 4096 || !view.gameplay.contains("conversations"))
		return false;
	const auto *owner = section(view, command.controller);
	if (!owner || !owner->at("human").get<bool>() || owner->at("generation") != command.generation)
		return false;
	bool authority = false;
	for (const auto &channel : owner->at("commands"))
		for (const auto &consumer : channel.at("consumers"))
			for (const auto &system : view.systems)
				if (system.kind == OperatingSystem::Comms && system.id == consumer.get<std::string>())
					authority = true;
	if (!authority)
		return false;
	const auto &board = view.gameplay.at("conversations");
	if (command.action == "comms.hail" && command.value == 0 && !command.request_id)
		for (const auto &p : board.at("directory"))
			if (p.at("id") == command.text && p.at("available") == true)
				return true;
	for (const auto &thread : board.at("threads"))
		if (thread.at("id") == command.value)
		{
			if (command.action == "comms.select")
				return command.text.empty() && !command.request_id;
			if (command.action == "comms.text")
				return !command.text.empty() && !command.request_id && thread.at("text_allowed") == true;
			if (command.action == "comms.answer" && command.request_id && thread.at("prompt") == command.request_id)
				for (const auto &choice : thread.at("choices"))
					if (choice.at("id") == command.text)
						return true;
		}
	return false;
}
std::string manual_binding(const Observation &view, const Json &layout, const std::string &action,
                           const std::string &anchor_action)
{
	const Json *anchor = nullptr;
	const Json *owner = nullptr;
	for (const auto &widget : layout.at("widgets"))
		if (widget.value("kind", "") == (anchor_action == "speed_mm_s" ? "throttle" : "heading") &&
		    widget.contains("controller") && widget.contains("channel"))
		{
			const auto *s = section(view, widget.at("controller"));
			if (!s || !s->at("human").get<bool>())
				return {};
			const auto &c = s->at("commands").at(widget.at("channel").get<std::string>());
			if (c.at("action") != anchor_action)
				continue;
			if (anchor && (owner != s || anchor != &c))
				return {};
			anchor = &c;
			owner = s;
		}
	if (!anchor)
		return {};
	std::string result;
	for (auto it = owner->at("commands").begin(); it != owner->at("commands").end(); ++it)
		if (it.value().at("action") == action && it.value().at("semantics") == "setting" &&
		    overlaps(*anchor, it.value()))
		{
			if (!result.empty())
				return {};
			result = binding(owner->at("id"), it.key());
		}
	return result;
}
std::optional<EligibleConsoleCommand> resolve(const Observation &view, const Json &layout, const Json &public_board,
                                              OperatingCommand command)
{
	if (command.target != view.own.id || (!command.console.empty() && command.console != view.console))
		return {};
	command.console = view.console;
	if (command.system.starts_with("controller/"))
	{
		const auto split = command.system.find('/', 11);
		if (split == std::string::npos || !command.controller.empty() || !command.channel.empty() ||
		    command.generation || command.request_id || !command.arguments.empty())
			return {};
		command.controller = command.system.substr(11, split - 11);
		command.channel = command.system.substr(split + 1);
		const auto *s = section(view, command.controller);
		if (!s)
			return {};
		command.generation = s->at("generation");
		command.system.clear();
	}
	Json matched;
	bool projected = false;
	for (const auto &descriptor : view.gameplay.value("controls", Json::array()))
	{
		const auto id = descriptor.at("id").get<std::string>();
		if (id.empty() || id.size() > 256)
			continue;
		const auto d = decode_operating_command(descriptor);
		if (d.action == command.action && d.system == command.system && d.controller == command.controller &&
		    d.channel == command.channel && d.generation == command.generation && d.request_id == command.request_id &&
		    d.arguments == command.arguments && d.value == command.value)
		{
			if (descriptor.value("enabled", true) != true)
				return {};
			// Repeated commands can legitimately have different labels. Duplicate
			// descriptor identities, however, cannot identify a unique action.
			std::size_t count = 0;
			for (const auto &other : view.gameplay.at("controls"))
				count += other.at("id") == id;
			if (count != 1)
				return {};
			projected = true;
			matched = descriptor;
		}
	}
	if (command.action.starts_with("comms."))
		return conversation_allowed(view, command) ? std::optional<EligibleConsoleCommand>{{command, matched}}
		                                           : std::nullopt;
	if (command.action == "display.toggle" || command.action == "display.update")
	{
		if (command.value || !command.controller.empty() || !command.channel.empty() || command.request_id ||
		    command.arguments.size() != 3 || command.arguments[2] <= 0 ||
		    std::any_of(command.arguments.begin(), command.arguments.end(),
		                [](auto n)
		                {
			                return n < -1000000000 || n > 1000000000;
		                }))
			return {};
		std::size_t sources = 0;
		for (const auto &w : layout.at("widgets"))
			if (w.at("id") == command.system &&
			    (w.at("kind") == "radar" || w.at("kind") == "navigation" || w.at("kind") == "conversations"))
				++sources;
		if (sources != 1)
			return {};
		if (command.action == "display.toggle")
			return !command.generation ? std::optional<EligibleConsoleCommand>{{command, matched}} : std::nullopt;
		const auto d = public_board.value("display", Json(nullptr));
		return d.is_object() && d.at("console") == view.console && d.at("widget") == command.system &&
		               d.at("own").at("id") == view.own.id && d.at("revision") == command.generation
		           ? std::optional<EligibleConsoleCommand>{{command, matched}}
		           : std::nullopt;
	}
	if (!command.controller.empty())
	{
		const auto *s = section(view, command.controller);
		if (!s || !s->at("human").get<bool>() || s->at("generation") != command.generation)
			return {};
		if (!command.channel.empty())
		{
			const auto &c = s->at("commands").at(command.channel);
			if (!c.at("minimum").is_number_integer() || !c.at("maximum").is_number_integer() ||
			    c.at("action") != command.action || command.value < c.at("minimum") || command.value > c.at("maximum"))
				return {};
			if (command.system.empty() && !command.request_id && command.arguments.empty() &&
			    c.at("semantics") == "setting")
			{
				for (const auto &w : layout.at("widgets"))
					if (w.value("controller", "") == command.controller && w.value("channel", "") == command.channel &&
					    ((w.at("kind") == "throttle" && command.action == "speed_mm_s") ||
					     (w.at("kind") == "heading" && command.action == "heading_mdeg")))
						projected = true;
				if (command.action == "assisted" || command.action == "longitudinal_thrust" ||
				    command.action == "manual_steering")
					for (const auto *anchor : {"speed_mm_s", "heading_mdeg"})
						projected |= manual_binding(view, layout, command.action, anchor) ==
						             binding(command.controller, command.channel);
				if (command.action == "lateral_thrust" || command.action == "boost")
					projected = true;
			}
		}
	}
	const auto *spec = operating_action(command.action);
	if ((!spec || !command.controller.empty()) && !projected)
		return {};
	if (spec && command.controller.empty())
	{
		if (!operating_value_valid(*spec, command.value))
			return {};
		if (command.system.empty())
		{
			const auto selected = select_system(view.systems, spec->system);
			if (!selected)
				return {};
			command.system = *selected;
		}
		else if (!projected && std::none_of(view.systems.begin(), view.systems.end(),
		                                    [&](const SystemView &s)
		                                    {
			                                    return s.id == command.system && s.controlled;
		                                    }))
			return {};
	}
	return EligibleConsoleCommand{std::move(command), std::move(matched)};
}
} // namespace
std::optional<EligibleConsoleCommand> eligible_console_command(const Observation &view, const Json &layout,
                                                               const Json &public_board, OperatingCommand command)
{
	try
	{
		return resolve(view, layout, public_board, std::move(command));
	}
	catch (const Json::exception &)
	{
		return {};
	}
	catch (const std::runtime_error &)
	{
		return {};
	}
}
std::string manual_console_binding(const Observation &view, const Json &layout, const std::string &action,
                                   const std::string &anchor_action)
{
	try
	{
		return manual_binding(view, layout, action, anchor_action);
	}
	catch (const Json::exception &)
	{
		return {};
	}
}
} // namespace phoenix
