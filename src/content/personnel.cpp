module;
#include <nlohmann/json.hpp>
#include <algorithm>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
module phoenix.content;
namespace phoenix
{
PersonnelDefinition compile_personnel(const Catalog &catalog, const std::string &entity)
{
	PersonnelDefinition out;
	const auto &hull = catalog.entities.at(entity);
	if (catalog.presentation)
		return out;
	if (!hull.contains("personnel") || hull.at("personnel").is_null())
	{
		for (const auto &[key, s] : hull.at("systems").items())
			if (s.value("kind", key) == "displacement")
				throw std::runtime_error(entity + ": displacement requires personnel and a Navigator duty");
		return out;
	}
	const auto fail = [&](bool ok, const std::string &why)
	{
		if (!ok)
			throw std::runtime_error(entity + ".personnel: " + why);
	};
	const auto &data = hull.at("personnel");
	auto identifier = [&](const Json &v)
	{
		auto s = v.get<std::string>();
		fail(!s.empty() && s.size() <= 64 &&
		         s.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_.-") ==
		             std::string::npos,
		     "invalid identifier");
		return s;
	};
	auto text = [&](const Json &v)
	{
		auto s = v.get<std::string>();
		fail(!s.empty() && s.size() <= 128, "invalid label");
		return s;
	};
	const auto seats = resolve_consoles(catalog, entity);
	const auto layout = compile_controllable_layout(catalog, entity);
	const auto systems = resolve_systems(catalog, entity);
	std::map<std::string, OperatingSystem> kinds;
	for (const auto &system : systems)
		kinds.emplace(system.id, system.kind);
	std::set<std::string> departments, stations, duties, officers, assigned, controllers, teams;
	for (const auto &seat : seats)
		stations.insert(seat.station);
	for (const auto &v : data.at("departments"))
	{
		PersonnelDepartment d{identifier(v.at("id")), text(v.at("label")), identifier(v.at("station"))};
		fail(departments.insert(d.id).second && stations.contains(d.station),
		     "duplicate department or unknown Station");
		out.departments.push_back(d);
	}
	fail(out.departments.size() <= 32, "too many departments");
	const auto combat = compile_combat(catalog, entity);
	if (combat)
	{
		for (const auto &c : combat->compartments)
			out.compartments.push_back(c.id);
		for (const auto &c : combat->connections)
			out.links.push_back({c.from, c.to, c.ticks});
	}
	else
	{
		for (const auto &c : data.at("compartments"))
			out.compartments.push_back(identifier(c));
		for (const auto &v : data.at("connections"))
		{
			auto from = std::find(out.compartments.begin(), out.compartments.end(), v.at("from").get<std::string>());
			auto to = std::find(out.compartments.begin(), out.compartments.end(), v.at("to").get<std::string>());
			auto ticks = v.at("ticks").get<std::int64_t>();
			fail(from != out.compartments.end() && to != out.compartments.end() && from != to && ticks > 0 &&
			         ticks <= 36000,
			     "invalid travel link");
			out.links.push_back({static_cast<std::size_t>(from - out.compartments.begin()),
			                     static_cast<std::size_t>(to - out.compartments.begin()), ticks});
		}
	}
	fail(!out.compartments.empty() && out.compartments.size() <= 256 &&
	         std::set<std::string>(out.compartments.begin(), out.compartments.end()).size() == out.compartments.size(),
	     "invalid compartments");
	for (const auto &link : out.links)
		fail(link.ticks > 0 && link.ticks <= 36000, "personnel travel link outside supported duration");
	const std::map<std::string, OperatingSystem> slots{
	    {"propulsion", OperatingSystem::Engine}, {"sensors", OperatingSystem::Radar},
	    {"cycling", OperatingSystem::Weapon},    {"traverse", OperatingSystem::Turret},
	    {"reload", OperatingSystem::Magazine},   {"recharge", OperatingSystem::Shield},
	    {"repair", OperatingSystem::RepairTeam}, {"comms", OperatingSystem::Comms}};
	for (const auto &v : data.at("traits"))
	{
		OfficerTrait t{identifier(v.at("id")), text(v.at("label")), {}};
		for (const auto &[slot, bonus] : v.at("bonuses").items())
		{
			auto b = bonus.get<std::int64_t>();
			fail(slots.contains(slot) && b >= -10000 && b <= 10000, "unsupported modifier slot or magnitude");
			t.bonuses.emplace(slot, b);
		}
		fail(out.traits.emplace(t.id, t).second, "duplicate trait");
	}
	fail(out.traits.size() <= 128, "too many traits");
	for (const auto &v : data.at("duties"))
	{
		PersonnelDuty d;
		d.id = identifier(v.at("id"));
		d.label = text(v.at("label"));
		d.department = identifier(v.at("department"));
		d.compartment = identifier(v.at("compartment"));
		d.controller = v.value("controller", std::string{});
		d.console = v.value("console", std::string{});
		d.required_trait = v.value("required_trait", std::string{});
		fail(d.required_trait.empty() || out.traits.contains(d.required_trait), "unknown required trait");
		d.team = v.value("team", std::string{});
		fail(duties.insert(d.id).second && departments.contains(d.department) &&
		         std::find(out.compartments.begin(), out.compartments.end(), d.compartment) != out.compartments.end(),
		     "invalid duty identity, department or compartment");
		fail(d.controller.empty() != d.team.empty(), "duty requires exactly one Controller or team");
		if (!d.controller.empty())
		{
			const auto c =
			    std::find_if(layout.controllers.begin(), layout.controllers.end(),
			                 [&](const auto &c)
			                 {
				                 return c.id == d.controller && (d.console.empty() || c.console == d.console);
			                 });
			fail(c != layout.controllers.end(), "unknown Controller duty");
			if (d.console.empty())
			{
				fail(std::count_if(layout.controllers.begin(), layout.controllers.end(),
				                   [&](const auto &x)
				                   {
					                   return x.id == d.controller;
				                   }) == 1,
				     "ambiguous Controller duty: supply console");
				d.console = c->console;
			}
			fail(controllers.insert(d.console + "/" + d.controller).second, "unknown or duplicate Controller duty");
			const auto seat = std::find_if(seats.begin(), seats.end(),
			                               [&](const auto &s)
			                               {
				                               return s.id == c->console;
			                               });
			const auto dept = std::find_if(out.departments.begin(), out.departments.end(),
			                               [&](const auto &x)
			                               {
				                               return x.id == d.department;
			                               });
			fail(seat != seats.end() && seat->station == dept->station,
			     "Controller department belongs to another Station");
		}
		else
			fail(kinds.contains(d.team) && kinds.at(d.team) == OperatingSystem::RepairTeam &&
			         teams.insert(d.team).second,
			     "unknown or duplicate team duty");
		if (!d.team.empty())
		{
			fail(combat.has_value(), "team requires combat compartments");
			const auto team = std::find_if(combat->systems.begin(), combat->systems.end(),
			                               [&](const auto &s)
			                               {
				                               return s.id == d.team;
			                               });
			fail(team != combat->systems.end() && team->compartment == d.compartment,
			     "team duty must start in its physical compartment");
		}
		const auto effects = v.value("effects", Json::object());
		fail(effects.is_object() && effects.size() <= 256, "too many effect targets");
		for (const auto &[system, values] : effects.items())
		{
			fail(kinds.contains(system) && (d.team.empty() || system == d.team), "invalid effect target " + system);
			std::set<std::string> unique;
			for (const auto &slot : values)
			{
				auto key = slot.get<std::string>();
				fail(slots.contains(key) && slots.at(key) == kinds.at(system) && unique.insert(key).second,
				     "incompatible or duplicate effect slot");
				d.effects[system].push_back(key);
			}
		}
		out.duty_indices.emplace(d.id, out.duties.size());
		out.duties.push_back(d);
	}
	fail(out.duties.size() <= 128 && controllers.size() == layout.controllers.size(),
	     "every Controller requires one duty (maximum 128)");
	for (const auto &[id, kind] : kinds)
		if (kind == OperatingSystem::RepairTeam)
			fail(teams.contains(id), "every team requires a duty");
	for (const auto &v : data.at("officers"))
	{
		OfficerDefinition o;
		o.id = identifier(v.at("id"));
		o.name = text(v.at("name"));
		fail(o.name.size() <= 64, "officer name too long");
		o.department = identifier(v.at("department"));
		o.duty = v.value("duty", std::string{});
		o.compartment = identifier(v.at("compartment"));
		o.portrait = v.value("portrait", std::string{});
		fail(officers.insert(o.id).second && departments.contains(o.department) &&
		         std::find(out.compartments.begin(), out.compartments.end(), o.compartment) != out.compartments.end(),
		     "invalid officer");
		if (!o.duty.empty())
		{
			const auto d = std::find_if(out.duties.begin(), out.duties.end(),
			                            [&](const auto &d)
			                            {
				                            return d.id == o.duty;
			                            });
			fail(d != out.duties.end() && d->department == o.department && d->compartment == o.compartment &&
			         assigned.insert(o.duty).second,
			     "invalid starting assignment");
		}
		if (!o.portrait.empty())
		{
			fail(catalog.assets.contains(o.portrait), "missing portrait");
			const auto &bytes = catalog.assets.at(o.portrait);
			fail(bytes.size() <= 256 * 1024, "portrait exceeds 256 KiB");
			(void)decode_asset_image(bytes);
			o.portrait = "console-assets:assets/comms/" + sha256(bytes) + ".png";
		}
		std::set<std::string> unique;
		for (const auto &t : v.at("traits"))
		{
			auto id = t.get<std::string>();
			fail(out.traits.contains(id) && unique.insert(id).second, "unknown or duplicate trait");
			o.traits.push_back(id);
			for (const auto &[slot, bonus] : out.traits.at(id).bonuses)
				o.bonuses[slot] += bonus;
		}
		fail(o.traits.size() <= 16, "too many officer traits");
		out.officers.push_back(o);
	}
	fail(out.officers.size() <= 128, "too many officers");
	for (const auto &o : out.officers)
		if (!o.duty.empty())
		{
			const auto &d = out.duties.at(out.duty_indices.at(o.duty));
			fail(d.required_trait.empty() ||
			         std::find(o.traits.begin(), o.traits.end(), d.required_trait) != o.traits.end(),
			     "starting officer lacks duty qualification");
		}
	for (const auto &[key, s] : hull.at("systems").items())
		if (s.value("kind", key) == "displacement")
		{
			fail(std::find(out.compartments.begin(), out.compartments.end(), s.at("compartment").get<std::string>()) !=
			         out.compartments.end(),
			     "unknown displacement compartment");
			bool qualified = false;
			for (const auto &c : layout.controllers)
				for (const auto &command : c.commands)
					for (auto consumer : command.consumers)
						if (systems.at(consumer).id == key)
							for (const auto &d : out.duties)
								if (d.console == c.console && d.controller == c.id && !d.required_trait.empty())
									qualified = true;
			fail(qualified, "displacement requires a qualified Navigator duty");
		}
	// All authored duties/reserves must be mutually reachable.
	std::set<std::size_t> reached{0};
	for (std::size_t n = 0; n < out.compartments.size(); ++n)
		for (const auto &l : out.links)
		{
			if (reached.contains(l.from))
				reached.insert(l.to);
			if (reached.contains(l.to))
				reached.insert(l.from);
		}
	fail(reached.size() == out.compartments.size(), "disconnected personnel routes");
	return out;
}
} // namespace phoenix
