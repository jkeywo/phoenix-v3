module;
#include <nlohmann/json.hpp>
#include <algorithm>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
module phoenix.content;
namespace phoenix
{
namespace
{
void craft_require(bool ok, const std::string &why)
{
	if (!ok)
		throw std::runtime_error("carried craft: " + why);
}
std::int64_t craft_number(const Json &j, const char *key, std::int64_t initial, std::int64_t lo, std::int64_t hi)
{
	const auto v = j.value(key, Json(initial));
	craft_require(v.is_number_integer() && !v.is_boolean() && v >= lo && v <= hi, std::string("invalid ") + key);
	return v.get<std::int64_t>();
}
void craft_key(const std::string &key)
{
	craft_require(!key.empty() && key.size() <= 64 &&
	                  key.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789._-") ==
	                      std::string::npos,
	              "invalid capability");
}
} // namespace
// Compatibility is confined to authoring: legacy fittings compile to ordinary craft and bays.
void expand_transport_fittings(Catalog &catalog)
{
	std::vector<std::pair<std::string, Json>> additions;
	for (auto &[id, entity] : catalog.entities)
		for (auto &[key, fitting] : entity.at("systems").items())
		{
			const auto kind = fitting.value("kind", key);
			const auto mode = fitting.value("mode", std::string{"displacement"});
			if ((kind != "transport" && kind != "displacement") || mode == "displacement")
				continue;
			craft_require(mode == "shuttle" || mode == "boarding_torpedo", id + ": unknown transport mode");
			const auto original = fitting.at("craft").get<std::string>();
			craft_require(catalog.entities.contains(original), id + ": missing transport craft");
			auto craft = catalog.entities.at(original);
			for (const auto &[child, spec] : craft.at("systems").items())
			{
				const auto type = spec.value("kind", child);
				craft_require(type != "shuttle_bay" && type != "transport" && type != "displacement",
				              id + "." + key + ": nested carried transports are unsupported");
			}
			const auto name = id + ".transport." + key;
			craft_require(!catalog.entities.contains(name), "generated transport identity overlaps " + name);
			craft_require(craft.value("motion_model", std::string{}) == "inertial",
			              name + ": inertial flight required");
			craft["id"] = name;
			craft["craft"] = {{"role", mode}, {"cruise_altitude_mm", 0}, {"return_reserve_permille", 0}};
			craft["transport"]["capacity"] = fitting.at("capacity");
			bool guidance = false;
			for (auto &controller : craft.at("control_layout").at("controllers"))
			{
				auto &commands = controller.at("commands");
				for (std::size_t i = 0; i < commands.size(); ++i)
					if (commands[i].value("action", std::string{}) == "follow_contact")
					{
						craft_require(!guidance, name + ": needs one dedicated follow-contact Controller");
						if (controller.contains("tree"))
						{
							const auto tree = controller.at("tree").get<std::string>();
							const auto bindings = compile_controller_bindings(catalog.trees.at(tree), catalog);
							const auto program =
							    state_tree::compile(catalog.trees.at(tree), bindings.registry, catalog.trees);
							craft_require(program.states.size() == 1 && program.states[0].entry.empty() &&
							                  program.states[0].running.empty() && program.states[0].exit.empty() &&
							                  program.states[0].transitions.empty(),
							              name + ": dedicated idle guidance required");
						}
						const auto consumers = commands[i].at("consumers");
						std::string engine, steering;
						for (const auto &consumer : consumers)
						{
							const auto system = consumer.get<std::string>();
							const auto type = craft.at("systems").at(system).value("kind", system);
							if (type == "engine")
								engine = system;
							if (type == "steering")
								steering = system;
						}
						craft_require(!engine.empty() && !steering.empty(),
						              name + ": guidance needs engine and steering");
						const auto follow = commands[i].at("id").get<std::string>();
						commands.push_back({{"id", "transport_altitude"},
						                    {"action", "altitude_mm"},
						                    {"initial", 0},
						                    {"consumers", {engine}}});
						Json bindings =
						    Json::array({{{"channel", follow}, {"action", "follow_contact"}, {"type", "boolean"}},
						                 {{"channel", "transport_altitude"}, {"action", "altitude_mm"}}});
						Json work = Json::array(
						    {{{"action", "write." + follow},
						      {"arguments",
						       Json::array({true,
						                    {{"observation", "craft.target"}},
						                    std::max<std::int64_t>(
						                        1, fitting.at("arrival_distance_mm").get<std::int64_t>() / 2),
						                    fitting.at("cruise_speed_mm_s")})}},
						     {{"action", "write.transport_altitude"},
						      {"arguments", Json::array({{{"observation", "craft.altitude"}}})}}});
						const auto tree = name + ".guidance";
						craft_require(!catalog.trees.contains(tree), "generated guidance identity overlaps " + tree);
						catalog.trees[tree] = {{"id", tree},
						                       {"context", "controller"},
						                       {"command_bindings", bindings},
						                       {"initial", "fly"},
						                       {"states", Json::array({{{"id", "fly"}, {"running", work}}})}};
						controller["tree"] = tree;
						controller.erase("fallback");
						guidance = true;
						break;
					}
			}
			craft_require(guidance, name + ": dedicated follow-contact Controller required");
			additions.emplace_back(name, std::move(craft));
			fitting["kind"] = "shuttle_bay";
			fitting["legacy_transport"] = true;
			fitting["slots"] = fitting.at("count");
			const auto count = craft_number(fitting, "count", 1, 1, 16);
			fitting["initial"] = std::vector<std::string>(count, name);
			const auto height = [](const Json &e)
			{
				return e.value("half_height_mm",
				               e.value("combat", Json::object()).value("half_height_mm", std::int64_t{5000}));
			};
			const auto clearance = height(entity) + height(catalog.entities.at(original)) + 1000;
			fitting["docking_range_mm"] =
			    std::max(craft_number(fitting, "arrival_distance_mm", 5000, 1, 1000000000), clearance * 2);
			fitting["docking_speed_mm_s"] = fitting.at("arrival_speed_mm_s");
		}
	for (auto &[id, entity] : additions)
		catalog.entities.emplace(id, std::move(entity));
}
TransportDefinition compile_transport(const Catalog &catalog, const std::string &id)
{
	const auto &e = catalog.entities.at(id);
	const auto j = e.value("transport", Json::object());
	craft_require(j.is_object(), "transport must be an object");
	TransportDefinition d;
	d.capacity = craft_number(j, "capacity", 64, 1, 256);
	d.compartment = j.value("compartment", std::string{});
	if (e.contains("personnel"))
	{
		const auto p = compile_personnel(catalog, id);
		if (d.compartment.empty() && !p.compartments.empty())
			d.compartment = p.compartments.front();
		craft_require(d.compartment.empty() || std::find(p.compartments.begin(), p.compartments.end(), d.compartment) !=
		                                           p.compartments.end(),
		              "unknown transport compartment");
	}
	const auto tags = j.value("capabilities", Json::array());
	craft_require(tags.is_array() && tags.size() <= 64, "invalid transport capabilities");
	std::set<std::string> seen;
	for (const auto &v : tags)
	{
		const auto tag = v.get<std::string>();
		craft_key(tag);
		craft_require(seen.insert(tag).second, "duplicate transport capability");
		d.capabilities.push_back(tag);
	}
	const auto bindings = j.value("capability_systems", Json::object());
	craft_require(bindings.is_object() && bindings.size() <= 64, "invalid capability bindings");
	for (const auto &[tag, systems] : bindings.items())
	{
		craft_key(tag);
		craft_require(seen.contains(tag) && systems.is_array() && systems.size() <= 32,
		              "capability binding needs a declared tag and systems");
		for (const auto &s : systems)
		{
			const auto key = s.get<std::string>();
			craft_require(e.at("systems").contains(key), "unknown capability System");
			d.capability_systems[tag].push_back(key);
		}
	}
	return d;
}
std::optional<CraftDefinition> compile_craft(const Catalog &catalog, const std::string &id)
{
	const auto &e = catalog.entities.at(id);
	if (!e.contains("craft"))
		return {};
	const auto &j = e.at("craft");
	craft_require(j.is_object(), "craft must be an object");
	CraftDefinition d;
	d.role = j.value("role", std::string{"shuttle"});
	craft_require(d.role == "shuttle" || d.role == "interceptor" || d.role == "bomber" || d.role == "boarding_torpedo",
	              "unknown craft role");
	d.vertical_speed = craft_number(j, "vertical_speed_mm_s", 10000, 1, 1000000);
	d.vertical_acceleration = craft_number(j, "vertical_acceleration_mm_s2", 5000, 1, 1000000);
	d.cruise_altitude = craft_number(j, "cruise_altitude_mm", 15000, -1000000, 1000000);
	d.engagement_range = craft_number(j, "engagement_range_mm", 200000, 1, 1000000000);
	d.return_reserve = craft_number(j, "return_reserve_permille", 200, 0, 1000);
	if (!catalog.presentation)
	{
		const auto systems = resolve_systems(catalog, id);
		for (auto kind : {OperatingSystem::Engine, OperatingSystem::Steering})
			craft_require(std::any_of(systems.begin(), systems.end(),
			                          [kind](const auto &s)
			                          {
				                          return s.kind == kind;
			                          }),
			              "craft needs propulsion and steering");
		craft_require(e.contains("control_layout"), "craft needs ordinary Controllers");
	}
	return d;
}
std::map<std::string, CraftBayDefinition> compile_craft_bays(const Catalog &catalog, const std::string &id)
{
	std::map<std::string, CraftBayDefinition> out;
	if (catalog.presentation)
		return out;
	const auto &e = catalog.entities.at(id);
	for (const auto &[key, j] : e.at("systems").items())
	{
		if (j.value("kind", key) != "shuttle_bay")
			continue;
		CraftBayDefinition d;
		d.compartment = j.at("compartment");
		d.legacy_transport = j.value("legacy_transport", false);
		if (d.legacy_transport)
		{
			d.transport_range = craft_number(j, "range_mm", 0, 1, 1000000000);
			d.transport_capacity = craft_number(j, "capacity", 0, 1, 64);
			d.arrival_distance = craft_number(j, "arrival_distance_mm", 5000, 1, 1000000000);
			d.arrival_speed = craft_number(j, "arrival_speed_mm_s", 1000, 1, 1000000000);
			d.cruise_speed = craft_number(j, "cruise_speed_mm_s", 10000, 1, 1000000000);
		}
		d.slots = craft_number(j, "slots", 0, 1, 32);
		d.transfer_ticks = craft_number(j, "transfer_ticks", 20, 1, 36000);
		d.docking_range = craft_number(j, "docking_range_mm", 10000, 1, d.legacy_transport ? 1000000000 : 1000000);
		d.docking_speed = craft_number(j, "docking_speed_mm_s", 5000, 1, d.legacy_transport ? 1000000000 : 1000000);
		d.flight_spacing = craft_number(j, "flight_spacing_mm", 0, 0, 1000000);
		d.charge_rate = craft_number(j, "charge_per_second", 0, 0, 1000000000);
		d.power_store = j.value("power_store", std::string{});
		const auto initial = j.value("initial", Json::array());
		craft_require(initial.is_array() && initial.size() <= static_cast<std::size_t>(d.slots), "bay exceeds slots");
		if (!catalog.presentation)
		{
			const auto combat = compile_combat(catalog, id);
			craft_require(combat && combat->compartment_ids.contains(d.compartment),
			              "bay requires a physical compartment");
			if (d.charge_rate)
				craft_require(catalog.resource_definitions.contains(id) &&
				                  catalog.resource_definitions.at(id).store_ids.contains(d.power_store),
				              "bay charger requires a carrier power store");
		}
		for (const auto &v : initial)
		{
			const auto name = v.get<std::string>();
			craft_require(catalog.entities.contains(name) && compile_craft(catalog, name).has_value(),
			              "bay initial entry must be a craft entity");
			for (const auto &[child, spec] : catalog.entities.at(name).at("systems").items())
				craft_require(spec.value("kind", child) != "shuttle_bay", "nested carried craft are unsupported");
			d.initial.push_back(name);
		}
		const auto supplies = j.value("ammunition", Json::array());
		craft_require(supplies.is_array() && supplies.size() <= 32, "invalid ammunition connections");
		for (const auto &s : supplies)
		{
			CraftSupplyDefinition a{s.at("source"), s.at("destination"),
			                        craft_number(s, "rounds_per_second", 1, 1, 1000000)};
			const auto &source = e.at("systems").at(a.source);
			craft_require(source.value("kind", a.source) == "magazine", "ammunition source must be a magazine");
			bool recipient = false;
			for (const auto &name : d.initial)
			{
				const auto &systems = catalog.entities.at(name).at("systems");
				if (!systems.contains(a.destination))
					continue;
				const auto &target = systems.at(a.destination);
				recipient = true;
				craft_require(target.value("kind", a.destination) == "magazine",
				              "ammunition destination must be a magazine");
				craft_require(!source.value("ammunition_type", std::string{}).empty() &&
				                  source.at("ammunition_type") == target.value("ammunition_type", Json()),
				              "incompatible ammunition connection");
			}
			craft_require(recipient, "ammunition connection has no receiving magazine");
			d.ammunition.push_back(a);
		}
		out.emplace(key, std::move(d));
	}
	std::int64_t slots = 0;
	for (const auto &[key, bay] : out)
		slots += bay.slots;
	craft_require(slots <= 128, "ship exceeds 128 carried slots");
	return out;
}
} // namespace phoenix
