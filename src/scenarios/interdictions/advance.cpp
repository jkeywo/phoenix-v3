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
void reset_interdiction_attempt(InterdictionState &s, InterdictionNative &native)
{
	s.progress = 0;
	native.expire_pleas(s.conversation, s.plea_messages);
	s.plea_messages.clear();
}
bool open_interdiction_conversation(InterdictionState &s, const InterdictionDefinition &d, EntityId player,
                                    InterdictionNative &native)
{
	if (!s.conversation && s.boarder)
	{
		const auto pirate = "ship:" + std::to_string(s.boarder);
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
				reset_interdiction_attempt(s, native);
		}
		if (!live.empty() && std::find(live.begin(), live.end(), s.boarder) == live.end())
		{
			s.boarder = live.front();
			reset_interdiction_attempt(s, native);
		}
		facts = native.observe(d, s);
		// These observations stay frozen while admitted outputs perform native actions.
		const bool lost = facts.target_lost, claimed = facts.claimed, player_active = facts.player_active;
		const auto &answer = facts.answer;
		const bool close_threat =
		    s.boarder && player_active && s.posture == "hostile" &&
		    fixed::length(facts.player_position - facts.boarder_position) <= (live.size() == 1 ? d.engagement
		                                                                      : s.blocked      ? d.clearance
		                                                                                       : d.threat);
		const bool ready = !lost && !claimed && !close_threat && s.boarder && facts.boarder_alive && facts.dock_ready &&
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
				const auto fit = native.observe(d, s).player_definition;
				const auto &ships = d.variants.at(d.variants.contains(fit) ? fit : "default");
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
				reset_interdiction_attempt(s, native);
			}
			else if (name == "takeover" && !s.captured && !lost && ready && s.boarder && native.take_over(d, s.boarder))
			{
				s.captured = true;
				s.progress = d.takeover_ticks;
				native.publish({InterdictionReceiptKind::Taken, player, d.target, d.id, "taken_by_pirates"});
			}
		}
		if (s.posture == "hostile" || s.posture == "truce")
		{
			// Geometry and physical docking are re-read; lost/claimed/player_active/live remain frozen.
			const auto physical = native.observe(d, s);
			if (!s.boarder || lost || claimed || !physical.boarder_alive)
				continue;
			const auto separation = fixed::length(physical.player_position - physical.boarder_position);
			const bool threat = player_active && s.posture == "hostile" &&
			                    separation <= (live.size() == 1 ? d.engagement
			                                   : s.blocked      ? d.clearance
			                                                    : d.threat);
			s.blocked = threat;
			if (threat || s.captured)
			{
				if (physical.boarder_docked)
					native.dock(s.boarder, InterdictionDockOrder::Undock);
				if (!s.captured)
					reset_interdiction_attempt(s, native);
			}
			else if (physical.dock_ready)
			{
				s.progress = std::min(d.takeover_ticks, s.progress + 1);
				for (const auto &[tick, line] : d.pleas)
					if (s.progress == tick && open_interdiction_conversation(s, d, player, native))
						if (const auto message = native.report_dialogue(s.conversation, line))
							s.plea_messages.push_back(*message);
			}
			else
			{
				reset_interdiction_attempt(s, native);
				if (!physical.boarder_docked)
					native.dock(s.boarder, InterdictionDockOrder::Assist, d.target);
			}
		}
	}
}
} // namespace phoenix
