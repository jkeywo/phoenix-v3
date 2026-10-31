#pragma once
#include "away_fixture.hpp"
namespace phoenix::test
{
inline void append_scored_away_policy(Json &source, const std::string &root)
{
	const auto obs = [](const char *name)
	{
		return Json{{"observation", name}};
	};
	const auto equal = [&](const char *name, Json value)
	{
		return Json{{"left", obs(name)}, {"op", "eq"}, {"right", value}};
	};
	const auto action = [&](const std::string &id, Json inputs, const std::string &verb, Json arguments)
	{
		Json operation{{"action", verb}, {"arguments", arguments}};
		Json success{{"action", "action.succeed"}, {"arguments", {"admitted"}}};
		Json failure{{"action", "action.fail"}, {"arguments", {"refused"}}};
		source["trees"].push_back(
		    {{"id", id},
		     {"context", "controller"},
		     {"controller_action", {{"label", "Prepare known mission"}, {"inputs", inputs}}},
		     {"initial", "act"},
		     {"states",
		      Json::array(
		          {{{"id", "act"},
		            {"running", Json::array({{{"attempt", Json::array({{{"tasks", Json::array({operation, success})}},
		                                                               {{"tasks", Json::array({failure})}}})}}})}}})}});
	};
	Json bindings = Json::array(), objectives = Json::array();
	for (const auto *verb : {"open", "contribute", "assign", "transport", "escort", "send", "recall"})
	{
		const auto id = root + "." + verb;
		action(id,
		       Json::array({{{"name", "mission"}, {"type", "text"}},
		                    {{"name", "selection"}, {"type", "text"}},
		                    {{"name", "revision"}, {"type", "integer"}}}),
		       "away." + std::string(verb),
		       Json::array({obs("input.mission"), obs("input.selection"), obs("input.revision")}));
		bindings.push_back({{"id", verb}, {"tree", id}});
		Json available = Json::array({equal("away_candidate.operation", verb), equal("away_candidate.eligible", true)});
		if (std::string(verb) == "open" || std::string(verb) == "send")
			available.push_back(equal("away_candidate.offered", true));
		if (std::string(verb) == "contribute")
			available.push_back(equal("away_candidate.requested", true));
		const int progress = std::string(verb) == "send"        ? 100
		                     : std::string(verb) == "transport" ? 90
		                     : std::string(verb) == "assign"    ? 80
		                                                        : 50;
		objectives.push_back({{"id", verb},
		                      {"behavior", "away"},
		                      {"candidate_source", "away"},
		                      {"action", verb},
		                      {"retry_ticks", 2},
		                      {"target", obs("away_candidate.target")},
		                      {"available", available},
		                      {"arguments", Json::array({obs("away_candidate.mission"), obs("away_candidate.selection"),
		                                                 obs("away_candidate.revision")})},
		                      {"factors", {{"progress", progress}}}});
	}
	action(root + ".request",
	       Json::array({{{"name", "console"}, {"type", "text"}},
	                    {{"name", "controller"}, {"type", "text"}},
	                    {{"name", "mission"}, {"type", "text"}}}),
	       "coordination.request_manifest",
	       Json::array({obs("input.console"), obs("input.controller"), obs("input.mission"), 1, 1, 600}));
	bindings.push_back({{"id", "request"}, {"tree", root + ".request"}});
	objectives.push_back(
	    {{"id", "request"},
	     {"behavior", "away"},
	     {"candidate_source", "away"},
	     {"action", "request"},
	     {"target", obs("away_candidate.target")},
	     {"available", Json::array({equal("away_candidate.operation", "request"),
	                                equal("away_candidate.eligible", true),
	                                equal("away_candidate.offered", true),
	                                {{"left", obs("away_candidate.retry_age")}, {"op", "ge"}, {"right", 20}}})},
	     {"arguments", Json::array({obs("away_candidate.peer_console"), obs("away_candidate.peer_controller"),
	                                obs("away_candidate.mission")})},
	     {"factors", {{"progress", 70}}}});
	for (const auto *status : {"accepted", "completed"})
	{
		const auto id = root + "." + status;
		action(id, Json::array({{{"name", "request"}, {"type", "integer"}}}), "coordination.respond_to",
		       Json::array({obs("input.request"), status, 1, "known_manifest"}));
		bindings.push_back({{"id", status}, {"tree", id}});
		Json available =
		    Json::array({equal("request.operation", "manifest"), equal("request.feasible", true),
		                 equal("request.status", std::string(status) == "accepted" ? "requested" : "accepted")});
		if (std::string(status) == "completed")
			available.push_back(equal("request.done", true));
		objectives.push_back({{"id", status},
		                      {"behavior", "away"},
		                      {"candidate_source", "requests"},
		                      {"action", status},
		                      {"target", obs("request.id")},
		                      {"available", available},
		                      {"arguments", Json::array({obs("request.id")})},
		                      {"factors", {{"progress", 100}}}});
	}
	Json optional = Json::array();
	for (auto &binding : objectives)
		if (binding.at("id") == "assign" || binding.at("id") == "escort")
		{
			auto secondary = binding;
			secondary["id"] = binding.at("id").get<std::string>() + "-optional";
			secondary["available"].push_back(equal("away_candidate.required", false));
			secondary["factors"]["progress"] = 10;
			optional.push_back(secondary);
			binding["available"].push_back(equal("away_candidate.required", true));
		}
	for (const auto &binding : optional)
		objectives.push_back(binding);
	source["trees"].push_back({{"id", root},
	                           {"context", "controller"},
	                           {"away_preparation_observations", true},
	                           {"request_observations", true},
	                           {"action_bindings", bindings},
	                           {"objective_bindings", objectives},
	                           {"initial", "observe"},
	                           {"states", Json::array({{{"id", "observe"}}})}});
}
inline Json scored_away_fixture()
{
	auto source = away_fixture();
	append_scored_away_policy(source, "test.scored-away");
	auto &ship = source["entities"][0];
	ship["standing_directives"] = Json::array({{{"id", "known-missions"},
	                                            {"behavior", "away"},
	                                            {"tier", "scripted_low"},
	                                            {"title_key", "Attend known mission"}}});
	auto &layout = ship["control_layout"];
	layout["station_complexity"] = true;
	layout["profiles"][0]["stars"] = 3;
	auto automatic = layout["profiles"][0];
	automatic["id"] = "scored";
	automatic["stars"] = 1;
	for (const auto *console : {"drive", "observer"})
	{
		auto &seat = automatic["consoles"][console];
		seat["ai"] = seat["human"];
		seat["human"] = Json::array();
	}
	layout["profiles"].push_back(automatic);
	layout["default_profile"] = "scored";
	for (auto &controller : layout["controllers"])
	{
		controller.erase("fallback");
		controller["tree"] = "test.scored-away";
	}
	source["maps"][0]["away_missions"][0]["ai_dispatch"] = true;
	source["maps"][0]["away_missions"][0]["slots"].insert(
	    source["maps"][0]["away_missions"][0]["slots"].begin(),
	    Json{{"id", "aaa-optional"}, {"kind", "team"}, {"required", false}});
	return source;
}
} // namespace phoenix::test
