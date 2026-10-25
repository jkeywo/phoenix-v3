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
const Json &controller_rows(const Json &gameplay)
{
	static const Json empty = Json::array();
	const auto rows = gameplay.find("controllers");
	return rows == gameplay.end() ? empty : *rows;
}
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
	if (!owner || (!owner->at("human").get<bool>() && !owner->value("conversation_authority", false)) ||
	    owner->at("generation") != command.generation)
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
					if (choice.at("id") == command.text && choice.value("enabled", true))
						return true;
		}
	return false;
}
bool consumes(const Json &owner, const std::string &system)
{
	for (const auto &[key, channel] : owner.at("commands").items())
		for (const auto &consumer : channel.at("consumers"))
			if (consumer == system)
				return true;
	return false;
}
bool personnel_allowed(const Observation &view, const OperatingCommand &c)
{
	if (!c.controller.empty() || !c.channel.empty() || c.value || c.request_id || !c.arguments.empty() ||
	    !c.speaker_name.empty() || !c.speaker_avatar.empty())
		return false;
	const auto p = view.gameplay.value("personnel", Json::object());
	if (p.empty() || !p.value("manual", true) || p.at("revision") != c.generation)
		return false;
	for (const auto &o : p.at("officers"))
		if (o.at("id") == c.system)
		{
			if (o.at("condition") == "dead")
				return false;
			if (c.action == "personnel.relieve")
				return c.text.empty();
			if (c.action != "personnel.assign" && c.action != "personnel.swap")
				return false;
			if (o.at("condition") != "healthy" && o.at("condition") != "minor")
				return false;
			for (const auto &d : p.at("duties"))
				if (d.at("id") == c.text && d.at("department") == o.at("department"))
					return true;
		}
	return false;
}
bool team_allowed(const Observation &view, const OperatingCommand &c)
{
	if (!c.channel.empty() || c.request_id || !c.arguments.empty() || !c.speaker_name.empty() ||
	    !c.speaker_avatar.empty())
		return false;
	const auto *owner = section(view, c.controller);
	if (!owner || !owner->at("human").get<bool>() || owner->at("generation") != c.generation ||
	    !consumes(*owner, c.system))
		return false;
	for (const auto &t : view.gameplay.value("teams", Json::array()))
		if (t.at("id") == c.system && !t.at("reserved").get<bool>() && t.at("revision") == c.value)
		{
			if (c.action == "team.cancel")
				return c.text.empty();
			if (c.action != "team.order")
				return false;
			const auto slash = c.text.find('/');
			if (slash == std::string::npos)
				return false;
			const auto next = c.text.find('/', slash + 1);
			if (next != std::string::npos && (!t.value("patient_control", true) || c.text.substr(0, slash) == "auto"))
				return false;
			if (next != std::string::npos && c.text.find('/', next + 1) != std::string::npos)
				return false;
			const auto compartment =
			    c.text.substr(slash + 1, next == std::string::npos ? std::string::npos : next - slash - 1);
			for (const auto &order : t.at("orders"))
				if (order.at("id") == c.text.substr(0, slash))
					for (const auto &room :
					     view.gameplay.value("combat", Json::object()).value("compartments", Json::array()))
						if (room.at("id") == compartment &&
						    std::find(order.at("targets").begin(), order.at("targets").end(), room.at("index")) !=
						        order.at("targets").end())
						{
							if (next == std::string::npos)
								return true;
							for (const auto &patient : t.at("patients"))
								if (patient.at("id") == c.text.substr(next + 1) &&
								    patient.at("location").get<std::int64_t>() + 1 ==
								        room.at("index").get<std::int64_t>() &&
								    (patient.at("condition") == "minor" || patient.at("condition") == "severe"))
									return true;
						}
		}
	return false;
}
bool away_allowed(const Observation &view, const OperatingCommand &c)
{
	if (!c.channel.empty() || c.request_id || !c.arguments.empty() || !c.speaker_name.empty() ||
	    !c.speaker_avatar.empty())
		return false;
	const auto *owner = section(view, c.controller);
	if (!owner ||
	    (!owner->at("human").get<bool>() &&
	     !(owner->value("contribution_authority", false) &&
	       (c.action == "away.assign" || c.action == "away.withdraw" || c.action == "away.contribute")) &&
	     !((c.action == "away.transport" || c.action == "away.escort") &&
	       owner->value("transport_authority", false))) ||
	    owner->at("generation") != c.generation)
		return false;
	for (const auto &m : view.gameplay.value("away_missions", Json::array()))
		if (m.at("id") == c.system && m.at("revision") == c.value)
		{
			if (c.action == "away.quantity")
			{
				if (!m.contains("supply") || !m.at("dispatch").get<bool>() ||
				    !m.at("supply").at("manual").get<bool>() ||
				    !consumes(*owner, m.value("dispatch_system", std::string{})))
					return false;
				for (const auto &n : m.at("supply").at("quantities"))
					if (std::to_string(n.get<std::int64_t>()) == c.text)
						return true;
				return false;
			}
			if (c.action == "away.contribute")
				return owner->value("contribution_authority", false) && m.value("can_contribute", false) &&
				       (c.text == "1" || c.text == "0");
			if (c.action == "away.escort")
			{
				const auto slash = c.text.find('/');
				if (slash == std::string::npos)
					return false;
				for (const auto &slot : m.value("craft_slots", Json::array()))
					if (slot.at("id") == c.text.substr(0, slash))
					{
						const auto asset = c.text.substr(slash + 1).empty() ? slot.at("assigned").get<std::string>()
						                                                    : c.text.substr(slash + 1);
						bool authority = false;
						for (const auto &craft : view.gameplay.value("craft", Json::array()))
							authority |= asset == "craft:" + craft.at("id").get<std::string>() &&
							             consumes(*owner, craft.at("bay"));
						if (!authority)
							return false;
						return c.text.substr(slash + 1).empty()
						           ? slot.at("owned").get<bool>() && m.value("can_contribute", false)
						           : std::find(slot.at("choices").begin(), slot.at("choices").end(),
						                       Json(c.text.substr(slash + 1))) != slot.at("choices").end();
					}
				return false;
			}
			if (c.action == "away.transport")
			{
				const auto selection = c.text.empty() ? m.at("transport").get<std::string>() : c.text;
				bool craft = false;
				for (const auto &row : view.gameplay.value("craft", Json::array()))
					craft |= "craft:" + row.at("id").get<std::string>() == selection && consumes(*owner, row.at("bay"));
				if (c.text.empty())
					return m.value("transport_owned", false) &&
					       (craft || consumes(*owner, selection) ||
					        (selection == "ship" && m.at("dispatch").get<bool>() &&
					         consumes(*owner, m.value("dispatch_system", std::string{}))));
				return (craft || consumes(*owner, c.text) ||
				        (c.text == "ship" && m.at("dispatch").get<bool>() &&
				         std::any_of(view.systems.begin(), view.systems.end(),
				                     [&](const auto &system)
				                     {
					                     return system.kind == OperatingSystem::Dispatch && consumes(*owner, system.id);
				                     }))) &&
				       std::find(m.at("transports").begin(), m.at("transports").end(), c.text) !=
				           m.at("transports").end();
			}
			if (c.action == "away.assign")
			{
				const auto slash = c.text.find('/');
				if (slash == std::string::npos)
					return false;
				const auto asset = c.text.substr(slash + 1);
				if (asset.starts_with("team:") && !consumes(*owner, asset.substr(5)))
					return false;
				for (const auto &slot : m.at("slots"))
					if (slot.at("id") == c.text.substr(0, slash))
						return std::find(slot.at("choices").begin(), slot.at("choices").end(), asset) !=
						       slot.at("choices").end();
				return false;
			}
			if (c.action == "away.withdraw")
			{
				for (const auto &slot : m.at("slots"))
					if (slot.at("id") == c.text && slot.at("owned").get<bool>())
					{
						const auto asset = slot.at("assigned").get<std::string>();
						return !asset.starts_with("team:") || consumes(*owner, asset.substr(5));
					}
				return false;
			}
			if (!c.text.empty() || !m.at("dispatch").get<bool>() ||
			    (c.action != "away.open" && c.action != "away.send" && c.action != "away.recall" &&
			     c.action != "away.cancel"))
				return false;
			if (m.contains("dispatch_system"))
				return consumes(*owner, m.at("dispatch_system"));
			for (const auto &system : view.systems)
				if (system.kind == OperatingSystem::Dispatch &&
				    (system.id == c.controller || consumes(*owner, system.id)))
					return true;
		}
	return false;
}
bool craft_allowed(const Observation &view, const OperatingCommand &c)
{
	const auto *owner = section(view, c.controller);
	if (!owner || (!owner->value("human", false) && !owner->value("craft_authority", false)) ||
	    owner->at("generation") != c.generation || !c.channel.empty() || c.request_id || !c.arguments.empty())
		return false;
	for (const auto &craft : view.gameplay.value("craft", Json::array()))
		if (craft.at("id") == c.system && craft.value("manual", true) && craft.at("revision") == c.value &&
		    consumes(*owner, craft.at("bay")))
		{
			if (c.action == "craft.recall")
				return c.text.empty();
			if (c.action == "craft.defend")
				return c.text.empty() && craft.at("role") == "interceptor";
			if (c.action == "craft.escort" && craft.at("role") == "interceptor")
			{
				if (c.text == std::to_string(view.own.id))
					return true;
				for (const auto &contact : view.contacts)
					if (c.text == std::to_string(contact.id))
						return true;
			}
			if (c.action == "craft.bomb" && craft.at("role") == "bomber")
				for (const auto &contact : view.contacts)
					if (c.text == std::to_string(contact.id) && contact.disposition == "hostile")
						return true;
		}
	return false;
}
bool logistics_allowed(const Observation &view, const OperatingCommand &c)
{
	const auto *owner = section(view, c.controller);
	const auto board = view.gameplay.value("logistics", Json::object());
	const bool delegate = c.action == "logistics.craft_allow" &&
	                      board.value("cargo_authorisation_controller", std::string{}) == c.controller;
	if (!owner || !owner->value("human", false) || board.empty() || (!board.value("manual", true) && !delegate) ||
	    owner->at("generation") != c.generation || board.at("revision") != c.value || !c.channel.empty() ||
	    c.request_id || !c.arguments.empty())
		return false;
	if (c.action == "logistics.route")
		return board.at("route_controller") == c.controller;
	if (c.action == "logistics.craft_allow" && delegate)
	{
		for (const auto &craft : board.value("cargo_craft", Json::array()))
			if (craft.at("id").dump() == c.system && craft.value("controllable", false))
				return c.text == "0" || c.text == "1";
		return false;
	}
	if (c.action == "logistics.craft_allow")
		for (const auto &craft : view.gameplay.value("craft", Json::array()))
			if (craft.at("id") == c.system && consumes(*owner, craft.at("bay")))
				return c.text == "0" || c.text == "1";
	if (c.action == "logistics.cancel" || c.action == "logistics.cargo_cancel" || c.action == "logistics.deliver")
		return board.at("controller") == c.controller;
	for (const auto &store : board.value("stores", Json::array()))
		if (store.at("id") == c.system && store.at("controller") == c.controller)
			return c.action == "logistics.target" || c.action == "logistics.hold" || c.action == "logistics.release" ||
			       c.action == "logistics.transfer" ||
			       (c.action == "logistics.select" && store.value("single_resource", false) &&
			        std::any_of(store.at("selectable_resources").begin(), store.at("selectable_resources").end(),
			                    [&](const auto &resource)
			                    {
				                    return resource == c.text;
			                    }));
	return false;
}
bool selection_allowed(const Observation &view, const OperatingCommand &c)
{
	if (c.action == "store.select")
	{
		auto command = c;
		command.action = "logistics.select";
		return logistics_allowed(view, command);
	}
	const auto *owner = section(view, c.controller);
	if (!owner || !owner->value("human", false) || owner->at("generation") != c.generation || !c.channel.empty() ||
	    c.request_id || !c.arguments.empty() || !c.speaker_name.empty() || !c.speaker_avatar.empty() ||
	    c.text.empty() || c.text.size() > 256)
		return false;
	if (c.action == "magazine.select")
	{
		for (const auto &row : view.gameplay.value("combat", Json::object()).value("magazines", Json::array()))
			if (row.at("id") == c.system && row.value("manual", false) && row.at("stock_revision") == c.value &&
			    consumes(*owner, c.system))
				for (const auto &resource : row.value("selectable_resources", Json::array()))
					if (resource == c.text)
						return true;
	}
	else if (c.action == "processor.select")
	{
		for (const auto &row : view.gameplay.value("processors", Json::array()))
			if (row.at("id") == c.system && row.value("manual", false) && row.at("selection_revision") == c.value &&
			    consumes(*owner, row.at("equipment")))
				for (const auto &recipe : row.at("recipes"))
					if (recipe.at("id") == c.text)
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
	for (const auto &candidate : controller_rows(view.gameplay))
	{
		if (!candidate.value("human", false))
			continue;
		for (auto it = candidate.at("commands").begin(); it != candidate.at("commands").end(); ++it)
			if (it.value().at("action") == action && it.value().at("semantics") == "setting" &&
			    overlaps(*anchor, it.value()))
			{
				if (!result.empty())
					return {};
				result = binding(candidate.at("id"), it.key());
			}
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
	if (command.action.starts_with("team."))
		return team_allowed(view, command) ? std::optional<EligibleConsoleCommand>{{command, {}}} : std::nullopt;
	if (command.action.starts_with("personnel."))
		return personnel_allowed(view, command) ? std::optional<EligibleConsoleCommand>{{command, {}}} : std::nullopt;
	if (command.action.starts_with("away."))
		return away_allowed(view, command) ? std::optional<EligibleConsoleCommand>{{command, {}}} : std::nullopt;
	if (command.action.starts_with("craft."))
		return craft_allowed(view, command) ? std::optional<EligibleConsoleCommand>{{command, {}}} : std::nullopt;
	if (command.action.starts_with("logistics."))
		return logistics_allowed(view, command) ? std::optional<EligibleConsoleCommand>{{command, {}}} : std::nullopt;
	if (command.action == "magazine.select" || command.action == "processor.select" || command.action == "store.select")
		return selection_allowed(view, command) ? std::optional<EligibleConsoleCommand>{{command, {}}} : std::nullopt;
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
				const bool power = command.action == "generator_output" || command.action == "group_allocation" ||
				                   command.action == "cooling_allocation" || command.action == "charge_limit" ||
				                   (command.action == "intent_integer" && c.contains("ui_system"));
				if (power)
					for (const auto &control : view.gameplay.value("controls", Json::array()))
						if (control.value("controller", "") == command.controller &&
						    control.value("channel", "") == command.channel &&
						    control.value("action", "") == command.action && control.value("enabled", true))
							projected = true;
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
