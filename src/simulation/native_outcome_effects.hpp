// Included inside World::Impl. Native work and direct outcomes share scalar
// snapshot capture and completion helpers; preparation never mutates live state.
NativeStructureOutcome &prepare_structure_outcome(PreparedEffectGroup &quote, EntityId target)
{
	if (const auto prepared = quote.structures.find(target); prepared != quote.structures.end())
		return prepared->second;
	auto original = quote.original_structures.find(target);
	if (original == quote.original_structures.end())
		original = quote.original_structures
		               .emplace(target, native_structure_outcome(entities.at(target).get<CombatStructure>()))
		               .first;
	return quote.structures.emplace(target, original->second).first->second;
}
NativePatientOutcome &prepare_patient_outcome(PreparedEffectGroup &quote, EntityId target, std::size_t patient)
{
	const auto key = std::pair{target, patient};
	if (const auto prepared = quote.patients.find(key); prepared != quote.patients.end())
		return prepared->second;
	auto original = quote.original_patients.find(key);
	if (original == quote.original_patients.end())
		original =
		    quote.original_patients
		        .emplace(key, native_patient_outcome(entities.at(target).get<PersonnelState>().officers.at(patient)))
		        .first;
	return quote.patients.emplace(key, original->second).first->second;
}
NativeTeamOutcome &prepare_team_outcome(PreparedEffectGroup &quote, EntityId target, const std::string &team)
{
	const auto key = std::pair{target, team};
	if (const auto prepared = quote.clinical_teams.find(key); prepared != quote.clinical_teams.end())
		return prepared->second;
	auto original = quote.original_clinical_teams.find(key);
	if (original == quote.original_clinical_teams.end())
		original =
		    quote.original_clinical_teams
		        .emplace(key, native_team_outcome(away_equipment(target, team).get<CombatMechanism>().get<TeamState>()))
		        .first;
	return quote.clinical_teams.emplace(key, original->second).first->second;
}
void prepare_native_outcome(PreparedEffectGroup &quote, const EffectGroupInvocation &frame,
                            const EffectOperation &operation)
{
	const auto actor = effect_group_actor(frame, operation.target);
	if (actor.ordnance || actor.kind == PayloadOutcomeKind::Shield)
		throw std::runtime_error("Native repair/clinical outcome requires an ordinary native owner");
	auto ship = watch_effect_target(quote, frame, actor.entity, false);
	if (combat_dead(ship) || departed(actor.entity))
		throw std::runtime_error("Native repair/clinical target is terminal");
	if (operation.kind == EffectOperationKind::StructuralRepair || operation.kind == EffectOperationKind::FaultRepair)
	{
		const auto *structure = ship.try_get<CombatStructure>();
		if (!structure)
			throw std::runtime_error("Native repair target has no structure");
		auto &prepared = prepare_structure_outcome(quote, actor.entity);
		if (operation.kind == EffectOperationKind::StructuralRepair)
		{
			std::optional<std::size_t> compartment;
			if (!operation.compartment.empty())
			{
				const auto found = structure->definition.compartment_ids.find(operation.compartment);
				if (found == structure->definition.compartment_ids.end())
					throw std::runtime_error("Unknown structural repair compartment");
				compartment = found->second;
			}
			else if (actor.kind == PayloadOutcomeKind::Compartment || actor.kind == PayloadOutcomeKind::System)
				compartment = actor.compartment;
			if (!compartment || *compartment >= prepared.compartments.size() || operation.amount < 0 ||
			    operation.amount > resource_limit)
				throw std::runtime_error("Structural repair requires a bounded amount and explicit compartment");
			native_structural_repair(prepared.compartments.at(*compartment),
			                         structure->definition.compartments.at(*compartment),
			                         operation.amount * rate * 1000, rate * 1000);
		}
		else
		{
			const auto equipment = operation.equipment.empty() ? actor.equipment : operation.equipment;
			const auto found = structure->definition.faults.find(equipment);
			if (found == structure->definition.faults.end())
				throw std::runtime_error("Fault repair requires an explicit fitted system");
			const auto fault = std::find_if(found->second.begin(), found->second.end(),
			                                [&](const auto &definition)
			                                {
				                                return definition.id == operation.fault;
			                                });
			if (fault == found->second.end())
				throw std::runtime_error("Unknown repair fault identity");
			const auto index = static_cast<std::size_t>(fault - found->second.begin());
			if (!native_finish_fault_repair(prepared.faults.at(equipment).at(index), *fault))
				throw std::runtime_error("Fault is inactive or permanently destroys its system");
		}
		return;
	}
	const auto officer = !operation.officer.empty()    ? operation.officer
	                     : operation.equipment.empty() ? actor.officer
	                                                   : std::string{};
	const auto equipment = operation.equipment.empty() ? actor.equipment : operation.equipment;
	if (!officer.empty())
	{
		if (!operation.equipment.empty())
			throw std::runtime_error("Clinical outcome selects one patient");
		const auto *personnel = ship.try_get<PersonnelState>();
		const auto &definitions = definition(actor.entity).personnel.officers;
		const auto found = std::find_if(definitions.begin(), definitions.end(),
		                                [&](const auto &definition)
		                                {
			                                return definition.id == officer;
		                                });
		if (!personnel || found == definitions.end())
			throw std::runtime_error("Unknown clinical officer");
		const auto index = static_cast<std::size_t>(found - definitions.begin());
		auto &prepared = prepare_patient_outcome(quote, actor.entity, index);
		const auto changed = operation.kind == EffectOperationKind::Treat       ? native_treat_patient(prepared)
		                     : operation.kind == EffectOperationKind::Stabilise ? native_stabilise_patient(prepared)
		                                                                        : native_recover_patient(prepared);
		if (!changed)
			throw std::runtime_error("Invalid clinical transition");
		return;
	}
	if (operation.kind != EffectOperationKind::Treat || equipment.empty())
		throw std::runtime_error("Clinical outcome requires an explicit officer or treatment team");
	const auto fitted = instance(ship, equipment);
	const auto *mechanism = fitted ? fitted->entity.try_get<CombatMechanism>() : nullptr;
	if (!mechanism || mechanism->definition.kind != OperatingSystem::Team)
		throw std::runtime_error("Treatment equipment is not a native team");
	auto &prepared = prepare_team_outcome(quote, actor.entity, equipment);
	if (!native_treat_team(prepared))
		throw std::runtime_error("Treatment team is not injured");
}
bool native_outcome_quote_stale(const PreparedEffectGroup &quote) const
{
	for (const auto &[id, original] : quote.original_structures)
	{
		const auto ship = entities.find(id);
		if (ship == entities.end() || !ship->second.has<CombatStructure>() || combat_dead(ship->second) ||
		    departed(id) || native_structure_outcome(ship->second.get<CombatStructure>()) != original)
			return true;
	}
	for (const auto &[key, original] : quote.original_patients)
	{
		const auto ship = entities.find(key.first);
		const auto *personnel = ship == entities.end() ? nullptr : ship->second.try_get<PersonnelState>();
		if (!personnel || key.second >= personnel->officers.size() || combat_dead(ship->second) ||
		    departed(key.first) || native_patient_outcome(personnel->officers[key.second]) != original)
			return true;
	}
	for (const auto &[key, original] : quote.original_clinical_teams)
	{
		const auto ship = entities.find(key.first);
		if (ship == entities.end() || combat_dead(ship->second) || departed(key.first))
			return true;
		const auto fitted = instance(ship->second, key.second);
		const auto *mechanism = fitted ? fitted->entity.try_get<CombatMechanism>() : nullptr;
		if (!mechanism || mechanism->definition.kind != OperatingSystem::Team ||
		    native_team_outcome(mechanism->get<TeamState>()) != original)
			return true;
	}
	return false;
}
void commit_native_outcomes(PreparedEffectGroup &quote)
{
	for (const auto &[id, structure] : quote.structures)
		apply_native_structure_outcome(entities.at(id).get_mut<CombatStructure>(), structure);
	std::set<EntityId> changed;
	for (const auto &[key, patient] : quote.patients)
	{
		const auto &original = quote.original_patients.at(key);
		apply_native_patient_outcome(entities.at(key.first).get_mut<PersonnelState>().officers.at(key.second), patient);
		// Native work advances patient-owned progress without changing the
		// personnel revision. Only a clinical outcome changes roster facts.
		if (patient.condition != original.condition || patient.stabilised != original.stabilised ||
		    patient.facility != original.facility)
			changed.insert(key.first);
	}
	for (const auto id : changed)
		personnel_changed(id);
	for (const auto &[key, team] : quote.clinical_teams)
		apply_native_team_outcome(away_equipment(key.first, key.second).get_mut<CombatMechanism>().get<TeamState>(),
		                          team);
}
