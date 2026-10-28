// Resource identities are projected once, so shared reserves are never counted per fitting.
Json power_board(const ConsoleAddress &address) const
{
	Json out{{"reactors", Json::array()},
	         {"reserves", Json::array()},
	         {"coolant", Json::array()},
	         {"groups", Json::array()},
	         {"systems", Json::array()}};
	out["limits"] = Json::object();
	const auto ship = entities.at(address.ship);
	const auto *power = ship.try_get<ShipResources>();
	if (!power)
		return out;
	const auto &network = power->definition.network;
	const auto &feeds = seat(address.ship, address.console)->feed_systems;
	std::vector<const SystemResourceBinding *> visible;
	std::set<ResourceIndex> stores, groups, coolant;
	auto in_group = [&](ResourceIndex store, ResourceIndex group)
	{
		for (auto at = network.stores.at(store).group; at != no_resource; at = network.groups.at(at).parent)
			if (at == group)
				return true;
		return false;
	};
	for (const auto &binding : power->definition.systems)
		if (std::find(feeds.begin(), feeds.end(), binding.id) != feeds.end())
		{
			visible.push_back(&binding);
			auto &limits = out["limits"][binding.id] = Json::object();
			if (binding.store != no_resource)
				limits["charge_limit"] = network.stores.at(binding.store).charge_rate;
			if (binding.group != no_resource)
				limits["group_allocation"] = network.groups.at(binding.group).budget_rate;
			if (binding.cooling != no_resource)
				limits["cooling_allocation"] = network.cooling_links.at(binding.cooling).capacity_rate;
			if (binding.generator != no_resource)
				limits["generator_output"] = network.generators.at(binding.generator).maximum_rate;
			if (binding.store != no_resource)
				stores.insert(binding.store);
			if (binding.group != no_resource)
			{
				groups.insert(binding.group);
				for (ResourceIndex i = 0; i < network.stores.size(); ++i)
					if (in_group(i, binding.group))
						stores.insert(i);
			}
			if (binding.conduit != no_resource)
			{
				stores.insert(network.links.at(binding.conduit).from);
				stores.insert(network.links.at(binding.conduit).to);
			}
			if (binding.cooling != no_resource)
			{
				stores.insert(network.cooling_links.at(binding.cooling).store);
				coolant.insert(network.cooling_links.at(binding.cooling).source);
			}
			if (binding.generator != no_resource)
			{
				const auto index = binding.generator;
				stores.insert(network.generators.at(index).store);
				out["reactors"].push_back({{"id", binding.id},
				                           {"output", power->network.generators().at(index).actual_rate},
				                           {"maximum", network.generators.at(index).maximum_rate}});
			}
		}
	for (auto index : coolant)
		out["coolant"].push_back({{"id", network.coolant_sources.at(index).id},
		                          {"remaining", power->network.coolant_sources().at(index).remaining()},
		                          {"capacity", network.coolant_sources.at(index).capacity}});
	for (auto index : stores)
	{
		const auto &definition = network.stores.at(index);
		Json row{{"id", definition.id},
		         {"group", definition.group == no_resource ? std::string{} : network.groups.at(definition.group).id},
		         {"allocation", power->network.stores().at(index).charge_limit},
		         {"energy", power->network.stores().at(index).energy()},
		         {"capacity", definition.capacity},
		         {"power_systems", Json::array()},
		         {"coolant_systems", Json::array()}};
		out["reserves"].push_back(
		    {{"id", definition.id}, {"energy", row.at("energy")}, {"capacity", definition.capacity}});
		std::vector<std::pair<int, std::string>> supplies;
		for (const auto *binding : visible)
		{
			if (binding->store == index || (binding->group != no_resource && in_group(index, binding->group)) ||
			    (binding->conduit != no_resource && network.links.at(binding->conduit).to == index))
				supplies.emplace_back(binding->conduit != no_resource && network.links.at(binding->conduit).to == index
				                          ? 0
				                      : binding->group != no_resource && binding->group == definition.group ? 1
				                      : binding->store == index                                             ? 2
				                                                                                            : 3,
				                      binding->id);
			if (binding->cooling != no_resource && network.cooling_links.at(binding->cooling).store == index)
				row["coolant_systems"].push_back(binding->id);
		}
		std::stable_sort(supplies.begin(), supplies.end());
		for (const auto &[priority, system] : supplies)
			row["power_systems"].push_back(system);
		out["systems"].push_back(std::move(row));
	}
	for (auto index : groups)
	{
		Json row{{"id", network.groups.at(index).id},
		         {"parent", network.groups.at(index).parent == no_resource
		                        ? std::string{}
		                        : network.groups.at(network.groups.at(index).parent).id},
		         {"allocation", power->network.groups().at(index).budget_rate},
		         {"energy", 0},
		         {"capacity", 0},
		         {"power_systems", Json::array()},
		         {"coolant_systems", Json::array()}};
		for (auto store : stores)
			if (in_group(store, index))
			{
				row["energy"] = row.at("energy").get<std::int64_t>() + power->network.stores().at(store).energy();
				row["capacity"] = row.at("capacity").get<std::int64_t>() + network.stores.at(store).capacity;
			}
		for (const auto *binding : visible)
		{
			if (binding->group == index)
				row["power_systems"].push_back(binding->id);
			if (binding->cooling != no_resource && in_group(network.cooling_links.at(binding->cooling).store, index))
				row["coolant_systems"].push_back(binding->id);
		}
		out["groups"].push_back(std::move(row));
	}
	return out;
}
