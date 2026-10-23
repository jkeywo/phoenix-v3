// Included inside World::Impl. Prepare only native component copies; callers commit
// them with their own receipts/payment after the complete operation succeeds.
enum class EffectApplicationKind
{
	Shield,
	Modifier
};
enum class EffectApplicationOperation
{
	Apply,
	Refresh,
	Remove
};
enum class EffectSourceKind
{
	Scenario,
	Reaction
};
struct EffectApplication
{
	EffectApplicationKind kind;
	EffectApplicationOperation operation;
	std::string source, equipment, definition;
};
static void validate_effect_identity(const std::string &source, const std::string &equipment, bool requires_equipment)
{
	if (source.empty() || source.size() > 512 || equipment.size() > 256 || (requires_equipment && equipment.empty()))
		throw std::runtime_error("Invalid effect application identity");
}
static std::string effect_source(EffectSourceKind kind, std::uint64_t owner, const std::string &grant,
                                 const std::string &parent = {})
{
	if (grant.empty() || (kind == EffectSourceKind::Reaction && parent.empty()))
		throw std::runtime_error("Invalid effect source identity");
	auto source = std::string(kind == EffectSourceKind::Scenario ? "scenario." : "reaction.") + std::to_string(owner) +
	              "." + (parent.empty() ? std::string{} : parent + ".") + grant;
	validate_effect_identity(source, {}, false);
	return source;
}
static std::int64_t effect_expiry(std::int64_t tick, std::int64_t duration)
{
	if (tick < 0 || tick > 9007199254740991LL || duration < 0 || duration > 1000000 ||
	    (duration > 0 && tick > 9007199254740991LL - duration))
		throw std::runtime_error("Effect expiry outside supported bounds");
	return duration > 0 ? tick + duration : -1;
}
static std::int64_t refresh_effect_expiry(std::int64_t previous, std::int64_t requested)
{
	return previous < 0 || requested < 0 ? -1 : std::max(previous, requested);
}
void prepare_effect_application(flecs::entity ship, const EffectApplication &application, ShieldComponent *shields,
                                PropertyEffects *properties, std::int64_t tick)
{
	validate_effect_identity(application.source, application.equipment,
	                         application.kind == EffectApplicationKind::Modifier);
	if (combat_dead(ship))
		throw std::runtime_error("Effect target unavailable");
	if (application.kind == EffectApplicationKind::Modifier)
	{
		if (!properties || !ship.has<PropertyEffects>())
			throw std::runtime_error("Incompatible modifier owner");
		if (application.operation == EffectApplicationOperation::Remove)
		{
			remove_modifier_grant(*properties, application.source, application.equipment);
			return;
		}
		auto definition = application.definition;
		if (application.operation == EffectApplicationOperation::Refresh)
		{
			const auto found =
			    std::find_if(properties->contributions.begin(), properties->contributions.end(),
			                 [&](const auto &c)
			                 {
				                 return c.source == application.source && c.equipment == application.equipment;
			                 });
			if (found == properties->contributions.end())
				throw std::runtime_error("Modifier grant unavailable for refresh");
			definition = found->definition;
		}
		const auto &compiled = modifier_templates.at(definition);
		if (!modifier_target_compatible(ship, application.equipment, compiled))
			throw std::runtime_error("Incompatible modifier target");
		install_modifier_grant(*properties, compiled, application.source, application.equipment, tick);
		return;
	}
	if (!shields || !ship.has<ShieldComponent>() || !application.equipment.empty())
		throw std::runtime_error("Incompatible shield grant owner");
	const auto matches = [&](const auto &c)
	{
		return c.equipment.empty() && c.source == application.source;
	};
	const auto found = std::find_if(shields->contributions.begin(), shields->contributions.end(), matches);
	if (application.operation == EffectApplicationOperation::Remove)
	{
		std::erase_if(shields->contributions, matches);
		return;
	}
	if (application.operation == EffectApplicationOperation::Refresh)
	{
		if (found == shields->contributions.end())
			throw std::runtime_error("Shield grant unavailable for refresh");
		const auto expiry = effect_expiry(tick, shield_templates.at(found->definition_id).duration_ticks);
		for (auto &c : shields->contributions)
			if (matches(c))
				c.expiry = refresh_effect_expiry(c.expiry, expiry);
		return;
	}
	// A shield reserve is an intentional instance: a repeated apply must never
	// recreate or refill it. Modifier applies deliberately refresh their handle.
	if (found != shields->contributions.end())
		throw std::runtime_error("Shield grant name already active; refresh it explicitly");
	const auto &compiled = shield_templates.at(application.definition);
	install_shield_grant(*shields, compiled, application.source, {}, application.definition,
	                     effect_expiry(tick, compiled.duration_ticks));
}
