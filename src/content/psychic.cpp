module;
#include <nlohmann/json.hpp>
#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <stdexcept>
#include <vector>
module phoenix.content;
namespace phoenix
{
PsychicDefinition compile_psychic(const Catalog &catalog, const std::string &entity)
{
	PsychicDefinition out;
	if (catalog.presentation)
		return out;
	const auto &hull = catalog.entities.at(entity);
	const auto fail = [&](bool ok, const std::string &why)
	{
		if (!ok)
			throw std::runtime_error(entity + ".psychic: " + why);
	};
	const auto number = [&](const Json &j, const char *key, std::int64_t initial, std::int64_t lo, std::int64_t hi)
	{
		const auto v = j.value(key, Json(initial));
		fail(v.is_number_integer() && v >= lo && v <= hi, std::string("invalid ") + key);
		return v.get<std::int64_t>();
	};
	const auto profiles = hull.value("psychic_powers", Json::object());
	fail(profiles.is_object() && profiles.size() <= 32, "expected at most 32 power profiles");
	for (const auto &[id, j] : profiles.items())
	{
		fail(valid_identifier(id) && j.is_object(), "invalid power profile");
		PsychicPowerDefinition p;
		p.id = id;
		p.kind = j.at("kind");
		p.label = j.at("label");
		fail((p.kind == "cloak" || p.kind == "scramble" || p.kind == "ward") && !p.label.empty() &&
		         p.label.size() <= 128,
		     "unknown power or invalid label");
		p.strength = number(j, "strength_permille", 700, 1, 1000);
		p.energy = number(j, "energy_per_second", 100, 1, 1000000000);
		p.recovery_ticks = number(j, "recovery_ticks", 100, 1, 36000);
		p.ghosts = number(j, "ghost_count", 3, 1, 16);
		p.offset = number(j, "offset_mm", 20000, 1000, 10000000);
		p.spacing = number(j, "spacing_mm", 8000, 1000, 1000000);
		p.speed = number(j, "speed_mm_s", 10000, 1, 1000000);
		p.acceleration = number(j, "acceleration_mm_s2", 2000, 1, 1000000);
		p.turn_rate = number(j, "turn_mdeg_s", 10000, 1, 360000);
		p.radius = number(j, "radius_mm", 2000, 1, 1000000);
		p.scan_ticks = number(j, "scan_ticks", 100, 1, 36000);
		p.fade_ticks = number(j, "fade_ticks", 20, 1, 200);
		p.apparent_class = j.value("apparent_class", std::string{"Unidentified contact"});
		fail(!p.apparent_class.empty() && p.apparent_class.size() <= 128, "invalid apparent class");
		out.powers.emplace(id, p);
	}
	const auto personnel = compile_personnel(catalog, entity);
	const auto layout = compile_controllable_layout(catalog, entity);
	std::set<std::string> duties;
	std::set<std::pair<std::size_t, std::size_t>> bound;
	for (const auto &[id, j] : hull.at("systems").items())
	{
		if (j.value("kind", id) != "psychic")
			continue;
		ChoirDefinition c;
		c.id = id;
		c.compartment = j.at("compartment");
		c.planning_controller = j.value("planning_controller", std::string{});
		c.range = number(j, "range_mm", 500000, 1, 1000000000);
		c.arc = number(j, "arc_mdeg", 90000, 1, 360000);
		fail(j.contains("power_store") && catalog.resource_definitions.contains(entity),
		     "choir requires power network and store");
		fail(std::find(personnel.compartments.begin(), personnel.compartments.end(), c.compartment) !=
		         personnel.compartments.end(),
		     "unknown choir compartment");
		c.powers = j.at("powers").get<std::set<std::string>>();
		fail(!c.powers.empty() && c.powers.size() <= 32, "choir needs compatible powers");
		for (const auto &p : c.powers)
			fail(out.powers.contains(p), "unknown compatible power");
		const auto &seats = j.at("seats");
		fail(seats.is_array() && !seats.empty() && seats.size() <= 16, "choir needs 1–16 seats");
		for (const auto &s : seats)
		{
			ChoirSeatDefinition seat;
			seat.duty = s.at("duty");
			fail(personnel.duty_indices.contains(seat.duty) && duties.insert(seat.duty).second,
			     "unknown or duplicate choir seat duty");
			const auto &d = personnel.duties.at(personnel.duty_indices.at(seat.duty));
			fail(d.choir == id && d.compartment == c.compartment,
			     "choir seat duty must bind its apparatus and compartment");
			const auto ci = std::find_if(layout.controllers.begin(), layout.controllers.end(),
			                             [&](const auto &v)
			                             {
				                             return v.id == s.at("controller").get<std::string>() &&
				                                    v.console == s.at("console").get<std::string>();
			                             });
			fail(ci != layout.controllers.end(), "unknown choir Controller");
			seat.controller = ci - layout.controllers.begin();
			const auto dep = std::find_if(personnel.departments.begin(), personnel.departments.end(),
			                              [&](const auto &v)
			                              {
				                              return v.id == d.department;
			                              });
			const auto consoles = resolve_consoles(catalog, entity);
			const auto console = std::find_if(consoles.begin(), consoles.end(),
			                                  [&](const auto &v)
			                                  {
				                                  return v.id == ci->console;
			                                  });
			fail(console != consoles.end() && dep != personnel.departments.end() && console->station == dep->station,
			     "choir Controller belongs to another Station");
			const auto channel = [&](const char *key, OperatingAction action)
			{
				const auto it = std::find_if(ci->commands.begin(), ci->commands.end(),
				                             [&](const auto &v)
				                             {
					                             return v.id == s.at(key).get<std::string>();
				                             });
				fail(it != ci->commands.end() && it->action == action && it->minimum >= 0 && it->initial == 0,
				     "choir seat requires zero-initialised intent channels");
				const auto index = static_cast<std::size_t>(it - ci->commands.begin());
				fail(bound.emplace(seat.controller, index).second, "choir channel has multiple consumers");
				return index;
			};
			seat.enabled = channel("enabled", OperatingAction::IntentBoolean);
			seat.target = channel("target", OperatingAction::IntentInteger);
			fail(ci->commands[seat.target].maximum <= 1000000000, "psychic target must be a physical ship identity");
			if (!c.planning_controller.empty())
			{
				const auto planner = std::find_if(layout.controllers.begin(), layout.controllers.end(),
				                                  [&](const auto &v)
				                                  {
					                                  return v.id == c.planning_controller && v.console == ci->console;
				                                  });
				fail(planner != layout.controllers.end(), "unknown same-Console planning Controller");
				for (auto index : {seat.enabled, seat.target})
				{
					const auto &command = ci->commands[index];
					const auto ch = std::find_if(planner->commands.begin(), planner->commands.end(),
					                             [&](const auto &v)
					                             {
						                             return v.id == command.id && v.action == command.action &&
						                                    v.minimum == command.minimum &&
						                                    v.maximum == command.maximum && v.initial == 0;
					                             });
					fail(ch != planner->commands.end(), "planning channels must match bound choir intent");
				}
			}
			c.seats.push_back(seat);
		}
		out.choirs.emplace(id, c);
	}
	fail(out.choirs.size() <= 8, "too many choirs");
	for (const auto &d : personnel.duties)
		if (!d.choir.empty())
			fail(duties.contains(d.id), "unbound choir duty");
	for (const auto &o : personnel.officers)
	{
		fail(o.power.empty() || out.powers.contains(o.power), "unknown officer power");
		if (!o.duty.empty())
		{
			const auto &d = personnel.duties.at(personnel.duty_indices.at(o.duty));
			if (!d.choir.empty())
				fail(!o.power.empty() && out.choirs.at(d.choir).powers.contains(o.power),
				     "starting officer has no compatible power");
		}
	}
	return out;
}
} // namespace phoenix
