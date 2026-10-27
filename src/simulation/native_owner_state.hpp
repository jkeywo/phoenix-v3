// Included inside World::Impl. Resolve the existing native owner once; each
// feature selects its own state without introducing another ownership model.
template <typename State>
State *native_owner_state(const CustomPropertyAddress &address, State OfficerState::*officer_state,
                          State CompartmentState::*compartment_state) const
{
	const auto at = entities.find(address.entity);
	if (at == entities.end())
		return nullptr;
	auto entity = at->second;
	if (!address.equipment.empty())
	{
		const auto *index = entity.try_get<EquipmentIndex>();
		if (!index)
			return nullptr;
		const auto child = index->by_id.find(address.equipment);
		return child == index->by_id.end() ? nullptr : child->second.try_get_mut<State>();
	}
	if (!address.officer.empty())
	{
		auto *personnel = entity.try_get_mut<PersonnelState>();
		if (!personnel)
			return nullptr;
		const auto &officers = definition(address.entity).personnel.officers;
		for (std::size_t i = 0; i < officers.size(); ++i)
			if (officers[i].id == address.officer)
				return &(personnel->officers.at(i).*officer_state);
		return nullptr;
	}
	if (address.compartment >= 0)
	{
		auto *structure = entity.try_get_mut<CombatStructure>();
		return !structure || static_cast<std::size_t>(address.compartment) >= structure->compartments.size()
		           ? nullptr
		           : &(structure->compartments[address.compartment].*compartment_state);
	}
	return entity.try_get_mut<State>();
}
