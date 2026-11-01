module;
#include <algorithm>
#include <cstdint>
#include <limits>
#include <map>
#include <set>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>
#include <nlohmann/json.hpp>
module phoenix.scenario.interdictions;
namespace phoenix
{
InterdictionNative::~InterdictionNative() = default;
namespace
{
std::string attacker_participant(const InterdictionState &s, const InterdictionDefinition &d)
{
	return "ship:" + std::to_string(s.boarder) + (d.attacker_character.empty() ? "" : "/" + d.attacker_character);
}
void pause_interdiction_work(InterdictionState &s, InterdictionNative &native)
{
	s.working = false;
	native.expire_pleas(s.conversation, s.plea_messages);
	s.plea_messages.clear();
}
bool open_interdiction_conversation(InterdictionState &s, const InterdictionDefinition &d, EntityId player,
                                    InterdictionNative &native)
{
	if (!s.conversation && s.boarder)
	{
		const auto pirate = attacker_participant(s, d);
		const auto engineer =
		    "ship:" + std::to_string(d.target) + (d.target_character.empty() ? "" : "/" + d.target_character);
		const auto self = "ship:" + std::to_string(player);
		s.conversation = native.open_dialogue(
		    {d.dialogue, {{"pirate", pirate}, {"engineer", engineer}, {"ship", self}}, {pirate, engineer, self}});
	}
	return s.conversation != 0;
}
} // namespace
void InterdictionBook::advance(InterdictionNative &native)
{
	using state_tree::Value;
	for (std::size_t i = 0; i < states_.size(); ++i)
	{
		auto &s = states_[i];
		const auto &d = definitions_[i];
		if (s.finished)
			continue;
		auto facts = native.observe(d, s);
		const auto player = facts.player;
		// Freeze the pre-output live roster; ships spawned later this tick are excluded.
		const auto live = facts.live_ships;
		if (live.empty() && s.boarder)
		{
			s.boarder = 0;
			if (!s.captured)
				pause_interdiction_work(s, native);
		}
		if (!live.empty() && std::find(live.begin(), live.end(), s.boarder) == live.end())
		{
			s.boarder = live.front();
			pause_interdiction_work(s, native);
		}
		if (s.conversation && s.boarder)
			native.bind_dialogue_attacker(s.conversation, attacker_participant(s, d));
		facts = native.observe(d, s);
		// These observations stay frozen while admitted outputs perform native actions.
		const bool lost = facts.target_lost, claimed = facts.claimed, player_active = facts.player_active;
		const auto &answer = facts.answer;
		const bool ready = !lost && !claimed && s.boarder && facts.boarder_alive && facts.dock_ready &&
		                   s.progress + 1 >= d.takeover_ticks;
		const bool start = facts.start;
		std::vector<Value> observations{
		    Value::boolean(start),
		    Value::boolean(s.attacked),
		    Value::boolean(s.reinforced),
		    Value::boolean(s.captured),
		    Value::boolean(lost),
		    Value::boolean(claimed),
		    Value::boolean(ready),
		    Value::string(answer),
		    Value::integer(player_active && !lost ? fixed::length(facts.player_position - facts.target_position)
		                                          : std::numeric_limits<std::int64_t>::max())};
		const auto outputs = state_tree::step(d.program, s.continuation, observations);
		for (const auto &o : outputs.outputs)
		{
			if (!admit_effect(s.effects, o))
				continue;
			const auto name = d.program.registry.actions.at(o.action).name;
			const auto value = o.arguments.at(0).text;
			if (name == "spawn" && s.ships.empty())
			{
				const auto &ships = d.variants.at(d.fleet_variant);
				for (const auto &spawn : ships)
					s.ships.push_back(native.spawn_ship(spawn));
				s.boarder = s.ships.front();
			}
			else if (name == "posture")
			{
				s.posture = value;
				if (value != "watch" && s.conversation)
					native.clear_prompt(s.conversation);
			}
			else if (name == "say" && open_interdiction_conversation(s, d, player, native))
				native.report_dialogue(s.conversation, value);
			else if (name == "offer" && open_interdiction_conversation(s, d, player, native))
				native.offer_dialogue(s.conversation, value);
			else if (name == "log" && s.effects.insert("log/" + value).second)
				native.publish({InterdictionReceiptKind::Log, player, d.target, d.id, value});
			else if (name == "emit")
				native.publish({InterdictionReceiptKind::Emit, player, d.target, d.id, value});
			else if (name == "finish")
			{
				s.finished = true;
				pause_interdiction_work(s, native);
			}
			else if (name == "takeover" && !s.captured && !lost && ready && s.boarder && native.take_over(d, s.boarder))
			{
				s.captured = true;
				s.progress = d.takeover_ticks;
				native.publish({InterdictionReceiptKind::Taken, player, d.target, d.id, "taken_by_pirates"});
			}
		}
		s.working = !s.finished && !s.captured && !lost && !claimed && s.boarder && facts.boarder_alive &&
		            (s.posture == "hostile" || s.posture == "truce") && native.observe(d, s).dock_ready;
		if (s.working)
		{
			s.progress = std::min(d.takeover_ticks, s.progress + 1);
			for (const auto &[tick, line] : d.pleas)
				if (s.progress == tick && open_interdiction_conversation(s, d, player, native))
					if (const auto message = native.report_dialogue(s.conversation, line))
						s.plea_messages.push_back(*message);
		}
		else if (!s.captured)
			pause_interdiction_work(s, native);
	}
}
} // namespace phoenix
