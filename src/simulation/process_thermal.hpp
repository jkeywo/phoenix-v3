// Included inside World::Impl. Thermal effects stage the real native network;
// heat is never represented by a stock resource or a second thermal counter.
struct ThermalEffectTarget
{
	EntityId entity = 0;
	ResourceIndex store = no_resource;
};
ThermalEffectTarget thermal_effect_target(PreparedEffectGroup &quote, const EffectGroupInvocation &frame,
                                          const EffectOperation &operation)
{
	const auto actor = effect_group_actor(frame, operation.target);
	if (actor.ordnance)
		throw std::runtime_error("Ordnance has no native thermal owner");
	auto ship = watch_effect_target(quote, frame, actor.entity, false);
	const auto *power = ship.try_get<ShipResources>();
	if (!power)
		throw std::runtime_error("Thermal target has no resource network");
	auto &network = quote.payments.try_emplace(actor.entity, power->network).first->second.network;
	ResourceIndex selected = no_resource;
	if (!operation.store.empty())
	{
		const auto stock = named_stock(ship, operation.store, &network);
		if (stock.kind != NamedStockKind::Electrical)
			throw std::runtime_error("Thermal store must identify a native thermal reserve");
		selected = stock.index;
	}
	const auto equipment =
	    operation.equipment.empty() ? actor.equipment : effect_group_equipment(frame, operation.equipment);
	if (!equipment.empty())
	{
		const auto fitted = instance(ship, equipment);
		if (!fitted || !fitted->entity.has<ResourceBinding>())
			throw std::runtime_error("Thermal equipment unavailable");
		const auto store = power->definition.systems.at(fitted->entity.get<ResourceBinding>().slot).store;
		if (store == no_resource || (selected != no_resource && selected != store))
			throw std::runtime_error("Thermal equipment and store disagree");
		selected = store;
	}
	if (selected == no_resource || selected >= network.stores().size())
		throw std::runtime_error("Thermal effect requires an explicit native equipment or store");
	return {actor.entity, selected};
}
void prepare_thermal_networks(PreparedEffectGroup &quote)
{
	// Establish network copies before any stock alias is staged, including when
	// a preceding operation changes stock and a later operation changes heat.
	for (std::size_t i = 0; i < quote.invocations.size(); ++i)
	{
		quote.failed_invocation = i;
		const auto &frame = quote.invocations[i];
		for (const auto &operation : frame.execution.operations)
			if (operation.kind == EffectOperationKind::HeatAdd || operation.kind == EffectOperationKind::HeatRemove)
				(void)thermal_effect_target(quote, frame, operation);
	}
}
void prepare_thermal_operation(PreparedEffectGroup &quote, const EffectGroupInvocation &frame,
                               const EffectOperation &operation)
{
	if (operation.amount < 0 || operation.amount > resource_limit)
		throw std::runtime_error("Thermal amount outside supported bounds");
	const auto target = thermal_effect_target(quote, frame, operation);
	auto &network = quote.payments.at(target.entity).network;
	if (operation.kind == EffectOperationKind::HeatAdd)
		(void)network.add_thermal(target.store, operation.amount);
	else
		(void)network.remove_thermal(target.store, operation.amount);
}
