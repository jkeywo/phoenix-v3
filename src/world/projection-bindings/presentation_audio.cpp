module;
#include "native_headers.hpp"

module phoenix.simulation;
import :projection_bindings;

namespace phoenix::world_detail
{
void PresentationAudioState::emit(EntityId ship, SceneAudio cue, std::uint64_t tick)
{
	if (cue.clip.asset.empty())
		return;
	auto &events = events_[ship];
	if (events.size() >= 256)
		events.erase(events.begin());
	cue.event = next_event_++;
	cue.tick = tick;
	events.push_back(std::move(cue));
}
void PresentationAudioState::retire(std::uint64_t now)
{
	for (auto &[id, events] : events_)
		std::erase_if(events,
		              [&](const auto &cue)
		              {
			              return now > cue.tick + cue.retention_ticks;
		              });
	std::erase_if(events_,
	              [](const auto &entry)
	              {
		              return entry.second.empty();
	              });
}
auto Runtime::emit_audio(flecs::entity ship, SceneAudio cue) -> void
{
	if (cue.clip.asset.empty())
		return;
	audio.emit(ship.get<StableId>().id, std::move(cue), ecs.get<Clock>().tick);
}
auto Runtime::retire_audio() -> void
{
	audio.retire(ecs.get<Clock>().tick);
}
auto Runtime::emit_arrival_audio(EntityId id) -> void
{
	const auto &d = definition(id);
	// Reinforcement spawns with a fitted passage drive represent FTL arrivals;
	// carried craft launching from bays do not pass through this hook.
	if (!d.ftl.empty())
		emit_audio(entities.at(id), {SceneAudioKind::Shot, "ftl:arrival", d.audio.ftl_arrival});
}

auto Runtime::refresh_presentation_audio(const ConversationDeliveries &delivered, bool baseline) -> void
{
	// Use actual delivered receipts, never queued/in-transit conversation text.
	const auto tick = ecs.get<Clock>().tick;
	const std::set<ConversationId> no_messages;
	for (const auto &[id, ship] : entities)
	{
		if (departed(id) || combat_dead(ship))
			continue;
		const auto &d = definition(id);
		if (d.audio.comms_attention.asset.empty() && d.audio.critical_warning.asset.empty() &&
		    !ship.has<AbilityRuntime>())
			continue;
		auto &state = audio.status(id);
		auto notify = [&](const std::string &source, const AudioClip &clip)
		{
			SceneAudio cue{SceneAudioKind::Shot, source, clip};
			cue.own_only = true; // Internal work/status must not reveal another ship's state.
			emit_audio(ship, std::move(cue));
		};
		const auto at = delivered.find(id);
		const auto &messages = at == delivered.end() ? no_messages : at->second;
		const bool incoming = std::any_of(messages.begin(), messages.end(),
		                                  [&](auto message)
		                                  {
			                                  return !state.delivered.contains(message);
		                                  });
		if (!baseline && incoming && tick >= state.next_comms)
		{
			notify("comms:attention", d.audio.comms_attention);
			state.next_comms = tick + rate;
		}
		state.delivered = messages;

		bool critical = false, shortage = false;
		if (const auto *power = ship.try_get<ShipResources>())
		{
			for (const auto &store : power->network.stores())
				critical |= store.thermal_stop;
			const auto starved = [](const SystemResourceGrant &grant)
			{
				return grant.work_funded < grant.work_requested || grant.paid < grant.requested ||
				       (grant.blocked && (grant.requested > 0 || grant.work_requested > 0 || grant.discrete > 0));
			};
			for (const auto &grant : power->grants)
				shortage |= starved(grant);
			for (const auto &[key, grant] : power->named_grants)
				shortage |= starved(grant);
		}
		state.power_shortage_ticks =
		    shortage ? (baseline ? rate : std::min<unsigned>(rate, state.power_shortage_ticks + 1)) : 0;
		critical |= state.power_shortage_ticks >= rate;
		if (const auto *structure = ship.try_get<CombatStructure>())
		{
			std::int64_t hp = 0, maximum = 0;
			for (std::size_t i = 0; i < structure->compartments.size(); ++i)
			{
				hp += structure->compartments[i].hp;
				maximum += structure->definition.compartments[i].maximum;
			}
			critical |= maximum > 0 && hp * 4 <= maximum;
		}
		if (!baseline && critical && !state.critical && tick >= state.next_warning)
		{
			notify("status:critical", d.audio.critical_warning);
			state.next_warning = tick + 10 * rate;
		}
		state.critical = critical;
		if (const auto *runtime = ship.try_get<AbilityRuntime>())
			for (const auto &[key, grant] : runtime->grants)
			{
				if (baseline)
					state.abilities[key] = grant.activation;
				else if (grant.phase == "active" && grant.output_permille > 0 &&
				         grant.activation > state.abilities[key])
				{
					state.abilities[key] = grant.activation;
					notify("ability:" + key, ability_definition(id, grant).activation_audio);
				}
			}
	}
}
} // namespace phoenix::world_detail
