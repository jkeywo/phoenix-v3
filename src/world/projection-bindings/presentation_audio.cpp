module;
#include "native_headers.hpp"

module phoenix.simulation;
import :projection_bindings;

namespace phoenix::world_detail
{
auto Runtime::emit_arrival_audio(EntityId id) -> void
{
	const auto &d = definition(id);
	// Reinforcement spawns with a fitted passage drive represent FTL arrivals;
	// carried craft launching from bays do not pass through this hook.
	if (!d.ftl.empty())
		emit_audio(entities.at(id), {SceneAudioKind::Shot, "ftl:arrival", d.audio.ftl_arrival});
}

auto Runtime::refresh_presentation_audio(bool baseline) -> void
{
	// Use actual delivered receipts, never queued/in-transit conversation text.
	std::map<EntityId, std::set<std::uint64_t>> delivered;
	for (const auto &[id, conversation] : conversations.conversations)
		for (const auto &message : conversation.messages)
			for (const auto &recipient : message.delivered)
				if (const auto to = conversations.participants.find(recipient); to != conversations.participants.end())
				{
					const auto from = conversations.participants.find(message.sender);
					if (from == conversations.participants.end() || from->second.host != to->second.host)
						delivered[to->second.host].insert(message.id);
				}
	const auto tick = ecs.get<Clock>().tick;
	for (const auto &[id, ship] : entities)
	{
		if (departed(id) || combat_dead(ship))
			continue;
		const auto &d = definition(id);
		if (d.audio.comms_attention.asset.empty() && d.audio.critical_warning.asset.empty() &&
		    !ship.has<AbilityRuntime>())
			continue;
		auto &state = audio_status[id];
		auto notify = [&](const std::string &source, const AudioClip &clip)
		{
			SceneAudio cue{SceneAudioKind::Shot, source, clip};
			cue.own_only = true; // Internal work/status must not reveal another ship's state.
			emit_audio(ship, std::move(cue));
		};
		const auto &messages = delivered[id];
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
