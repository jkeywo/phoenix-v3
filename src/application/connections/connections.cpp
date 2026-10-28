module;
#include <array>
#include <cstdint>
#include <unordered_map>
#include <charconv>
#include <cmath>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <utility>
#include <functional>
#include <memory>
#include <string>
#include <vector>

module phoenix.application.connections;

namespace phoenix
{
bool ai_configuration_available(const Application &app)
{
	return app.can_host && app.session && app.session->inference_state().value("configured", false);
}

std::vector<std::pair<std::string, std::string>> inference_controls(const Application &app)
{
	std::vector<std::pair<std::string, std::string>> controls;
	if (!app.can_host || !app.session)
		return controls;
	controls.emplace_back("ai.pair", "Pair inference bridge");
	const auto state = app.session->inference_state();
	for (const auto &row : state.value("controllers", Json::array()))
	{
		if (!row.at("eligible").get<bool>())
			continue;
		auto providers = state.value("providers", Json::array());
		providers.push_back("tree");
		for (const auto &provider : providers)
		{
			if (provider != "tree" && !row.at("eligible").get<bool>())
				continue;
			const auto console = row.at("console").get<std::string>(),
			           controller = row.at("controller").get<std::string>(), p = provider.get<std::string>();
			controls.emplace_back("ai.select/" + console + "/" + controller + "/" + p,
			                      console + " / " + controller + ": " + (p == "tree" ? "State tree" : p) +
			                          (row.at("selected") == p ? " (selected)" : ""));
		}
	}
	return controls;
}

std::vector<std::pair<std::string, std::string>> inference_readouts(const Application &app)
{
	std::vector<std::pair<std::string, std::string>> rows;
	if (!app.can_host || !app.session)
		return rows;
	const auto state = app.session->inference_state();
	rows.emplace_back("ai.health",
	                  state.value("online", false) ? "Inference bridge connected" : "Inference bridge offline");
	rows.emplace_back("ai.latency",
	                  "Last inference: " + std::to_string(state.value("latency_ms", std::uint64_t{0})) + " ms");
	if (state.contains("pair") && !state.value("paired", false))
		rows.emplace_back("ai.pair-code", "Pairing token: " + state.at("pair").get<std::string>());
	for (const auto &row : state.value("controllers", Json::array()))
		rows.emplace_back(
		    "ai.status/" + row.at("console").get<std::string>() + "/" + row.at("controller").get<std::string>(),
		    row.at("controller").get<std::string>() + ": " +
		        (row.value("human", false) ? std::string("Human operator") : row.at("effective").get<std::string>()) +
		        "; " + row.at("reason").get<std::string>() +
		        (row.at("eligible").get<bool>() ? "" : "; no model candidates authored"));
	return rows;
}
} // namespace phoenix
