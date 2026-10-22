// Compartment access is shared by people, teams and physical deliveries.
std::string compartment_edge_key(std::size_t a, std::size_t b) const
{
	return std::to_string(std::min(a, b)) + ":" + std::to_string(std::max(a, b));
}
std::uint64_t logistics_revision(flecs::entity e) const
{
	if (const auto *inventory = e.try_get<MaterialInventory>())
		return inventory->revision;
	if (const auto *structure = e.try_get<CombatStructure>())
		return structure->access_revision;
	return 1;
}
bool physical_edge_open(EntityId id, std::size_t from, std::size_t to) const
{
	const auto *structure = entities.at(id).try_get<CombatStructure>();
	return !structure || !structure->closed_routes.contains(compartment_edge_key(from, to));
}
template <class Links>
std::pair<std::int64_t, std::int64_t> physical_route(EntityId id, std::size_t count, const Links &links,
                                                     std::size_t from, std::size_t to, std::int64_t *total = nullptr,
                                                     bool avoid_disabled = false) const
{
	const auto *structure = entities.at(id).try_get<CombatStructure>();
	Links available;
	for (const auto &edge : links)
		if (physical_edge_open(id, edge.from, edge.to) &&
		    (!avoid_disabled || !structure ||
		     (!structure->compartments.at(edge.from).disabled && !structure->compartments.at(edge.to).disabled)))
			available.push_back(edge);
	return compartment_route(count, available, from, to, total);
}
bool compartment_access_command(const ConsoleAddress &a, const OperatingCommand &c, bool execute, bool ai)
{
	const auto e = entities.at(a.ship);
	const auto *structure = e.try_get<CombatStructure>();
	if (!structure || structure->definition.access_controller.empty() ||
	    !material_authority(a, c, structure->definition.access_controller, ai))
		return false;
	const auto fields = material_fields(c.text);
	const auto &compartments = structure->definition.compartment_ids;
	if (fields.size() != 2 || !compartments.contains(c.system) || !compartments.contains(fields[0]) ||
	    (fields[1] != "open" && fields[1] != "close"))
		return false;
	const auto from = compartments.at(c.system), to = compartments.at(fields[0]);
	bool exists = false;
	for (const auto &edge : structure->definition.connections)
		exists |= (edge.from == from && edge.to == to) || (edge.from == to && edge.to == from);
	if (!exists)
		return false;
	if (execute)
	{
		auto &state = e.get_mut<CombatStructure>();
		const auto key = compartment_edge_key(from, to);
		if (fields[1] == "close")
			state.closed_routes.insert(key);
		else
			state.closed_routes.erase(key);
		if (e.has<MaterialInventory>())
			++e.get_mut<MaterialInventory>().revision;
		else
			++state.access_revision;
	}
	return true;
}
