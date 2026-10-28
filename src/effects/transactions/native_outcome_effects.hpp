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
	NativeOutcomePreparationPort port;
	port.target = [&](EntityId id) -> NativeOutcomeTarget
	{
		auto ship = watch_effect_target(quote, frame, id, false);
		return {combat_dead(ship) || departed(id), ship.try_get<CombatStructure>()};
	};
	port.structure = [&](EntityId id) -> NativeStructureOutcome &
	{
		return prepare_structure_outcome(quote, id);
	};
	port.officer = [&](EntityId id, const std::string &officer) -> std::optional<std::size_t>
	{
		const auto *personnel = entities.at(id).try_get<PersonnelState>();
		const auto &officers = definition(id).personnel.officers;
		const auto found = std::find_if(officers.begin(), officers.end(),
		                                [&](const auto &entry)
		                                {
			                                return entry.id == officer;
		                                });
		if (!personnel || found == officers.end())
			return {};
		return static_cast<std::size_t>(found - officers.begin());
	};
	port.patient = [&](EntityId id, std::size_t patient) -> NativePatientOutcome &
	{
		return prepare_patient_outcome(quote, id, patient);
	};
	port.team = [&](EntityId id, const std::string &equipment)
	{
		const auto fitted = instance(entities.at(id), equipment);
		const auto *mechanism = fitted ? fitted->entity.try_get<CombatMechanism>() : nullptr;
		return mechanism && mechanism->definition.kind == OperatingSystem::Team;
	};
	port.clinical_team = [&](EntityId id, const std::string &team) -> NativeTeamOutcome &
	{
		return prepare_team_outcome(quote, id, team);
	};
	phoenix::prepare_native_outcome(frame, operation, port, rate);
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
