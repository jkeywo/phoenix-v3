module;
#include <flecs.h>
#include <functional>
#include <chrono>
#include <charconv>
#include <iterator>
#include <type_traits>
#include <string_view>
#include <cstdlib>
#include <iostream>
#include <algorithm>
#include <array>
#include <variant>
#include <set>
#include <stdexcept>
#include <cstdint>
#include <map>
#include <limits>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <tuple>
#include <vector>

module phoenix.simulation;
import :bindings;

namespace phoenix::world_detail
{
auto Runtime::refresh_ability_work(bool advance) -> void
{
	for (const auto &[ship, entity] : entities)
	{
		auto *runtime = entity.try_get_mut<AbilityRuntime>();
		if (!runtime)
			continue;
		if (advance)
		{
			runtime->work.clear();
			runtime->work_tick = ecs.get<Clock>().tick;
		}
		std::vector<std::string> completed;
		for (auto &[key, g] : runtime->grants)
		{
			g.output_permille = 0;
			if (advance)
				g.funding_permille = 0;
			g.fault_permille = 0;
			if (!ability_running(g) || !ability_apparatus_blocker(ship, g).empty())
				continue;
			const auto &d = definition(ship).abilities.catalog->definitions.at(g.definition);
			const auto preparing = g.phase == "preparing" || g.resume_phase == "preparing";
			const auto funding = advance ? ability_upkeep_output(ship, key) : g.funding_permille;
			std::int64_t faults = 1000;
			if (!g.apparatus.empty())
				faults = fault_effectiveness(entity, g.apparatus);
			if ((g.funding_blocked || funding < 1000) && d.shortage != "proportional")
			{
				if (advance && d.shortage == "stop")
					stop_ability(ship, *runtime, g);
				else if (advance && g.phase != "paused")
				{
					g.resume_phase = g.phase;
					g.phase = "paused";
				}
				g.blocker = "Insufficient ability funding";
				continue;
			}
			if (!advance && g.phase == "paused")
				continue;
			if (advance && g.phase == "paused")
			{
				g.phase = g.resume_phase;
				g.resume_phase.clear();
			}
			if (preparing)
			{
				if (advance)
				{
					g.progress = std::min(d.preparation_ticks * 1000, g.progress + funding);
					if (g.progress == d.preparation_ticks * 1000)
						completed.push_back(key);
				}
			}
			else
			{
				g.funding_permille = funding;
				g.fault_permille = faults;
				g.output_permille = funding * faults / 1000;
				if (advance && g.output_permille)
					runtime->work.push_back({g.source, g.owner, d.family, g.definition, g.activation, g.output_permille,
					                         g.id, g.output_permille, "permille", g.generation});
			}
		}
		for (const auto &key : completed)
		{
			auto *g = ability_execution(ship, key);
			if (!g)
				continue;
			activate_ability(ship, key);
		}
	}
}
} // namespace phoenix::world_detail
