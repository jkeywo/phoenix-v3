#pragma once
namespace phoenix::test
{
// Synthetic authoring DSL only. Fixtures emit ordinary shared definitions before
// their ZIP is read by the production loader; gameplay never accepts this DSL.
inline void materialise_fixture_widgets(Json &source)
{
	if (!source.contains("control_widgets"))
		source["control_widgets"] = Json::array();
	std::map<std::string, Json> entities;
	for (const auto &e : source.value("entities", Json::array()))
		entities[e.at("id")] = e;
	std::set<std::string> resolving;
	std::function<Json(std::string)> resolved = [&](std::string id)
	{
		if (!resolving.insert(id).second)
			return entities.at(id);
		auto e = entities.at(id);
		if (e.contains("extends") && entities.contains(e.at("extends").get<std::string>()))
		{
			auto parent = resolved(e.at("extends"));
			parent.merge_patch(e);
			e = parent;
		}
		resolving.erase(id);
		return e;
	};
	for (auto &authored : source["entities"])
	{
		if (authored.contains("extends") && !authored.contains("control_layout") && !authored.contains("stations"))
			continue;
		auto entity = resolved(authored.at("id"));
		if (authored.contains("stations") && !authored.contains("control_layout"))
			entity.erase("control_layout");
		const auto fitted = entity.value("systems", Json::object());
		if (fitted.empty() && !entity.contains("console") && !entity.contains("stations"))
			continue;
		bool inertial = entity.contains("resources") || entity.contains("processors");
		for (const auto &[name, spec] : fitted.items())
		{
			const auto kind = spec.value("kind", name);
			inertial = inertial || (kind != "engine" && kind != "steering" && kind != "radar");
			for (const auto *field : {"reverse_speed_mm_s", "boost", "impulse", "power_rate", "heat_rate",
			                          "energy_per_second", "discrete_energy"})
				inertial = inertial || spec.contains(field);
		}
		if (entity.contains("motion_model"))
			inertial = entity.at("motion_model") == "inertial";

		if (entity.contains("control_layout") && entity.at("control_layout").value("schema", 0) == 2)
			continue;
		auto layout = entity.value("control_layout", Json::object());
		if (layout.contains("controllers") &&
		    std::any_of(layout.at("controllers").begin(), layout.at("controllers").end(),
		                [](const auto &row)
		                {
			                return row.contains("widget");
		                }))
			continue;
		Json consoles = Json::array();
		for (const auto &station : entity.value("stations", Json::array()))
			for (const auto &seat : station.at("consoles"))
				consoles.push_back(seat);
		if (!entity.contains("stations"))
		{
			Json systems = Json::array();
			for (const auto &[id, s] : fitted.items())
				systems.push_back(id);
			consoles.push_back({{"id", "main"}, {"systems", systems}});
		}
		if (!layout.contains("controllers"))
		{
			layout["controllers"] = Json::array();
			layout["profiles"] = Json::array({{{"id", "direct"}, {"stars", 3}, {"consoles", Json::object()}}});
			layout["default_profile"] = "direct";
			for (const auto &seat : consoles)
				for (const auto &equipment : seat.value("systems", Json::array()))
				{
					const std::string id = equipment;
					if (!entity.value("systems", Json::object()).contains(id))
						continue;
					const auto &s = entity.at("systems").at(id);
					const auto kind = s.value("kind", id);
					Json commands = Json::array();
					auto add = [&](std::string action, Json initial = 0)
					{
						Json c{{"id", action}, {"action", action}, {"consumers", {id}}};
						if (action != "heading_mdeg")
							c["initial"] = initial;
						if (const auto *contract = operating_action(action))
						{
							Json arguments = Json::array();
							for (const auto &argument : operating_arguments(contract->action))
								arguments.push_back(std::max<std::int64_t>(0, argument.minimum));
							if (!arguments.empty())
								c["arguments"] = arguments;
						}
						commands.push_back(c);
					};
					const bool flight = kind == "engine" || kind == "steering" || kind == "lateral" ||
					                    kind == "boost" || kind == "impulse";
					const bool advanced = inertial;
					if (kind == "engine")
					{
						add("speed_mm_s");
						add("altitude_mm");
					}
					if (kind == "steering" || s.value("turn_mdeg_s", 0) > 0)
						add("heading_mdeg");
					if (flight)
					{
						if (advanced || kind == "engine")
							add("engine_enabled", s.value("enabled", true) ? 1 : 0);
						if (advanced)
						{
							add("assisted", 1);
							add("selected_contact");
							if (kind == "engine" || kind == "steering")
							{
								add("destination");
								add("follow_contact");
							}
							if (kind == "engine")
							{
								add("longitudinal_thrust");
								add("match_speed");
							}
							if (s.value("turn_mdeg_s", 0) > 0)
								add("manual_steering");
							if (kind == "lateral" || s.value("lateral_acceleration_mm_s2", 0) > 0)
								add("lateral_thrust");
							if (kind == "boost" || s.contains("boost"))
								add("boost");
							if (kind == "impulse" || s.contains("impulse"))
								add("impulse");
						}
					}
					else if (kind == "radar")
					{
						add("radar_enabled", s.value("enabled", true) ? 1 : 0);
						if (!s.contains("directed"))
							add("sensor_target");
					}
					else
						add("system_enabled", s.value("enabled", true) ? 1 : 0);
					if (s.contains("directed"))
					{
						const auto &d = s.at("directed");
						add("directed_target");
						for (const auto &[key, action] : std::map<std::string, std::string>{
						         {"modes", "directed_operating_mode"}, {"targeting", "directed_targeting_mode"}})
							if (d.contains(key) && !d.at(key).empty())
							{
								std::int64_t initial = 0;
								const auto selected =
								    d.value(key == "modes" ? "initial_mode" : "initial_targeting", Json());
								for (std::size_t i = 0; i < d.at(key).size(); ++i)
									if (d.at(key)[i] == selected)
										initial = i;
								add(action, initial);
							}
					}
					if (kind == "weapon")
					{
						add("magazine_select");
						if (!s.contains("directed"))
						{
							add("weapon_target");
							add("weapon_mode");
						}
					}
					if (kind == "shield")
						add("shield_discharge", false);
					if (kind == "team" || kind == "repair_team")
					{
						add("repair_compartment");
						add("repair_system");
					}
					if (s.contains("generator"))
					{
						std::int64_t output = 0;
						for (const auto &generator :
						     entity.value("resources", Json::object()).value("generators", Json::array()))
							if (generator.at("id") == s.at("generator"))
								output = generator.value("initial_output", std::int64_t(0));
						add("generator_output", output);
					}
					if (s.contains("power_store") && kind == "storage")
					{
						const auto network = entity.value("resources", Json::object());
						for (const auto &store : network.value("stores", Json::array()))
							if (store.at("id") == s.at("power_store"))
							{
								add("charge_limit", store.value("charge_rate", std::int64_t(0)));
								add("discharge_limit", store.value("discharge_rate", std::int64_t(0)));
								add("overdrive");
								if (store.value("allows_override", false))
									add("thermal_override");
							}
					}
					if (s.contains("power_group"))
						add("group_allocation");
					if (s.contains("conduit"))
						add("link_isolated");
					if (s.contains("cooling_link"))
					{
						std::int64_t rate = 0;
						for (const auto &link :
						     entity.value("resources", Json::object()).value("cooling_links", Json::array()))
							if (link.at("id") == s.at("cooling_link"))
								rate = link.value("allocation_rate", std::int64_t(0));
						add("cooling_allocation", rate);
					}
					layout["controllers"].push_back(
					    {{"id", id}, {"console", seat.at("id")}, {"fallback", "hold"}, {"commands", commands}});
					layout["profiles"][0]["consoles"][seat.at("id").get<std::string>()]["human"].push_back(id);
				}
		}
		if (!entity.contains("control_layout"))
		{
			authored["motion_model"] =
			    entity.value("motion_model", inertial ? std::string("inertial") : std::string("vector"));
			for (auto &controller : layout["controllers"])
			{
				const std::string key = controller.at("id");
				if (entity.contains("controllers") && entity.at("controllers").contains(key))
				{
					controller["tree"] = entity.at("controllers").at(key);
					controller.erase("fallback");
				}
			}
			authored.erase("controllers");
			authored.erase("controller");
			authored.erase("presets");
			authored.erase("default_preset");
		}

		for (const auto &seat : consoles)
		{
			const std::string console = seat.at("id");
			bool owns = false;
			for (const auto &row : layout["controllers"])
				owns = owns || row.at("console") == console;
			if (owns)
				continue;
			bool bridge = entity.value("bridge", Json::object()).value("alert_console", std::string{}) == console;
			for (const auto &definition : source.value("consoles", Json::array()))
				if (definition.at("id") == seat.value("definition", Json()))
					bridge = bridge || definition.value("command", false) || definition.value("objectives", false) ||
					         !definition.value("stances", Json::array()).empty();
			if (!bridge)
				continue;
			layout["controllers"].push_back({{"id", "bridge"},
			                                 {"console", console},
			                                 {"commands", Json::array()},
			                                 {"fallback", "hold"},
			                                 {"fixture_intrinsic", true}});
			for (auto &profile : layout["profiles"])
				profile["consoles"][console]["human"].push_back("bridge");
		}

		std::set<int> used;
		for (auto &profile : layout["profiles"])
			if (profile.value("stars", 0) > 0)
				used.insert(profile.at("stars").get<int>());
		for (auto &profile : layout["profiles"])
			if (profile.value("stars", 0) == 0)
			{
				bool any = false;
				for (const auto &seat : profile.at("consoles"))
					any = any || !seat.value("human", Json::array()).empty();
				int stars = any ? 3 : 1;
				while (used.contains(stars) && stars > 1)
					--stars;
				profile["stars"] = stars;
				used.insert(stars);
			}

		std::map<std::pair<std::string, std::string>, std::string> member_owners;
		for (const auto &controller : layout["controllers"])
			for (const auto &command : controller.at("commands"))
				for (const auto &member : command.at("consumers"))
					member_owners.try_emplace(
					    std::pair{controller.at("console").get<std::string>(), member.get<std::string>()},
					    controller.at("id").get<std::string>());
		Json instances = Json::array();
		std::set<std::string> operations_owned;
		for (const auto &row : layout["controllers"])
		{
			const std::string console = row.at("console"), id = row.at("id"),
			                  widget =
			                      "fixture.widget." + entity.at("id").get<std::string>() + "." + console + "." + id;
			Json definition{{"id", widget},
			                {"schema", 1},
			                {"commands", row.at("commands")},
			                {"equipment", Json::object()},
			                {"operations", Json::object()},
			                {"levels", Json::array({{{"id", "automated"}, {"human", Json::array()}}})}};
			Json equipment = Json::object();
			std::map<std::string, int> thresholds;
			for (auto &command : definition["commands"])
			{
				thresholds[command.at("id")] = command.value("individual_stars", 1);
				command.erase("individual_stars");
				for (auto &consumer : command["consumers"])
				{
					const std::string member = consumer;
					definition["equipment"][member] = Json::object();
					equipment[member] = member;
					consumer = "$" + member;
				}
			}
			if (row.contains("tree"))
				definition["tree"] = row.at("tree");
			if (row.contains("policy"))
			{
				definition["policy_template"] = row.at("policy");
				if (row.contains("ai"))
					definition["ai"] = row.at("ai");
			}
			std::map<int, Json> human;
			for (int stars = 1; stars <= 3; ++stars)
				human[stars] = Json::array();
			for (const auto &profile : layout["profiles"])
			{
				const auto stars = profile.at("stars").get<int>();
				const auto seats = profile.at("consoles");
				const auto selected = seats.value(console, Json::object()).value("human", Json::array());
				if (std::find(selected.begin(), selected.end(), Json(id)) != selected.end())
					for (const auto &c : definition["commands"])
						if (stars >= thresholds[c.at("id")])
							human[stars].push_back(c.at("id"));
			}

			const bool primary = operations_owned.insert(console).second;
			bool communications = false;
			for (const auto &[member, value] : equipment.items())
				communications = communications || fitted.at(member).value("kind", member) == "comms";
			Json attached = Json::array();
			for (const auto &[member, value] : equipment.items())
				if (member_owners.at({console, member}) == id)
					attached.push_back("$" + member);
			for (const auto *op :
			     {"team_assignment", "team_work", "medical",        "personnel",        "craft",
			      "craft_return",    "transport", "team_selection", "logistics",        "cargo_authorisation",
			      "conversation",    "mission",   "mission_send",   "mission_quantity", "mission_staffing",
			      "contribution",    "departure", "docking",        "coordination",     "alert",
			      "stance",          "objectives"})
			{
				const std::string name = op;
				if (row.value("fixture_intrinsic", false) && name != "alert" && name != "stance" &&
				    name != "objectives")
					continue;
				const bool global = name == "personnel" || name == "mission_staffing" || name == "contribution" ||
				                    name == "alert" || name == "stance" || name == "objectives";
				if (global && !primary)
					continue;
				const bool logical = name == "logistics" &&
				                     entity.value("logistics", Json::object()).value("controller", std::string{}) == id;
				if (!global && attached.empty() && name != "coordination" && !logical)
					continue;
				definition["operations"][op] = global || name == "coordination" ? Json::array() : attached;
				if (name == "contribution")
					for (const auto &candidate : layout["controllers"])
						if (candidate.at("console") == console)
							for (const auto &command : candidate.at("commands"))
								for (const auto &member : command.at("consumers"))
								{
									const std::string key = member;
									const auto kind = fitted.at(key).value("kind", key);
									if (kind == "team" || kind == "repair_team" || kind == "shuttle_bay")
									{
										definition["equipment"][key] = Json::object();
										equipment[key] = key;
										auto &members = definition["operations"][op];
										if (std::find(members.begin(), members.end(), Json("$" + key)) == members.end())
											members.push_back("$" + key);
									}
								}
				const auto workloads = layout.value("manual_workloads", Json::object()).value(console, Json::object());
				int minimum = workloads.value(
				    op, (name == "personnel" || name == "mission_staffing" || name == "contribution" ||
				         name == "mission_quantity" ||
				         (name == "transport" && layout.value("station_complexity", Json(false)) != Json(false)))
				            ? 3
				            : 1);
				if (name == "mission_send" && layout.value("station_complexity", Json(false)) != Json(false))
					minimum = 2;
				if (name == "team_work")
					minimum = workloads.contains("team_assignment") ? 3 : 1;
				for (const auto &profile : layout["profiles"])
				{
					const int stars = profile.at("stars");
					const auto allocated =
					    profile.at("consoles").value(console, Json::object()).value("human", Json::array());
					if (stars >= minimum &&
					    ((global && !allocated.empty()) ||
					     std::find(allocated.begin(), allocated.end(), Json(id)) != allocated.end() ||
					     workloads.contains(name) ||
					     (name == "conversation" && communications &&
					      layout.value("station_complexity", Json(false)) != Json(false) && !allocated.empty())))
						human[stars].push_back(op);
				}
			}

			Json coordinated = Json::array();
			for (const auto &command : row.at("commands"))
				if (command.contains("coordinated_group"))
					for (const auto &member : command.at("consumers"))
						coordinated.push_back("$" + member.get<std::string>());
			if (!coordinated.empty())
			{
				definition["operations"]["coordinated_impulse"] = coordinated;
				for (const auto &profile : layout["profiles"])
				{
					const int stars = profile.at("stars");
					const auto allocated =
					    profile.at("consoles").value(console, Json::object()).value("human", Json::array());
					if (std::find(allocated.begin(), allocated.end(), Json(id)) != allocated.end())
						human[stars].push_back("coordinated_impulse");
				}
			}
			std::vector<Json> levels;
			for (const auto &[stars, items] : human)
				if (!items.empty() && std::find(levels.begin(), levels.end(), items) == levels.end())
					levels.push_back(items);
			std::sort(levels.begin(), levels.end(),
			          [](const auto &a, const auto &b)
			          {
				          return a.size() < b.size();
			          });
			Json previous = Json::array();
			for (std::size_t n = 0; n < levels.size(); ++n)
			{
				Json level{{"id", "level" + std::to_string(n)}, {"human", levels[n]}};
				for (const auto &old : previous)
					if (std::find(levels[n].begin(), levels[n].end(), old) == levels[n].end())
						level["replaces"][old.get<std::string>()] = levels[n];
				definition["levels"].push_back(level);
				previous = levels[n];
			}
			Json mapping = Json::object();
			for (int stars = 1; stars <= 3; ++stars)
			{
				std::string level = "automated";
				for (std::size_t n = 0; n < levels.size(); ++n)
					if (human[stars] == levels[n])
						level = "level" + std::to_string(n);
				mapping[std::to_string(stars)] = level;
			}
			std::erase_if(source["control_widgets"].get_ref<Json::array_t &>(),
			              [&](const auto &item)
			              {
				              return item.at("id") == widget;
			              });
			source["control_widgets"].push_back(definition);
			auto instance = row;
			for (const auto *key : {"commands", "tree", "fallback", "policy", "ai", "fixture_intrinsic"})
				instance.erase(key);
			instance["widget"] = widget;
			instance["equipment"] = equipment;
			instance["star_levels"] = mapping;
			instances.push_back(instance);
		}
		for (auto &profile : layout["profiles"])
			profile.erase("consoles");
		layout["schema"] = 2;
		layout["controllers"] = instances;
		layout.erase("manual_workloads");
		authored["control_layout"] = layout;
	}
}
} // namespace phoenix::test
