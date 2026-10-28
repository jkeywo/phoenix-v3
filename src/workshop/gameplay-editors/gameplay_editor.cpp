module;
#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>
#include <algorithm>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <stdexcept>
#include <set>
#include <string>
#include <utility>
#include <vector>

module phoenix.workshop.gameplay;
import phoenix.content.documents;
import phoenix.content.operations;
import phoenix.content.sensing;
import phoenix.workshop.authoring;
import phoenix.workshop.tree;
import phoenix.workshop.test_client;

namespace phoenix
{
#include "state.hpp"

GameplayEditors::GameplayEditors() : state_(std::make_unique<State>())
{
}
GameplayEditors::~GameplayEditors() = default;
void GameplayEditors::areas(WorkshopDocument &document, bool pseudo, ToolMessage &status, std::string &selection)
{
	if (!state_->tree_editor.has_vocabulary())
		set_vocabulary(gameplay_tree_vocabulary());
	state_->areas(document, pseudo, status, selection);
}
void GameplayEditors::properties(WorkshopDocument &document, bool pseudo, ToolMessage &status,
                                 const std::string &selection, const std::string &entity)
{
	if (!state_->tree_editor.has_vocabulary())
		set_vocabulary(gameplay_tree_vocabulary());
	state_->properties(document, pseudo, status, selection, entity);
}
void GameplayEditors::console_properties(WorkshopDocument &document, bool pseudo, Json &console)
{
	state_->console_properties(document, pseudo, console);
}
void GameplayEditors::set_feedback(const Json &feedback)
{
	state_->test_client.set_feedback(feedback);
}
std::optional<GameplayTestRequest> GameplayEditors::take_request()
{
	return state_->test_client.take_request();
}
bool GameplayEditors::probe_test(WorkshopDocument &document, unsigned stage)
{
	return state_->test_client.probe(document, stage);
}
void GameplayEditors::set_vocabulary(const Json &vocabulary)
{
	if (vocabulary.contains("controller") || vocabulary.contains("system") || vocabulary.contains("scenario"))
	{
		state_->tree_editor.set_vocabulary(vocabulary);
		return;
	}
	Json contexts = Json::object();
	for (const auto &[context, mask] :
	     std::vector<std::pair<std::string, unsigned>>{{"system", 1}, {"scenario", 2}, {"controller", 4}})
	{
		contexts[context] = {{"actions", Json::array()}, {"observations", Json::array()}, {"intents", Json::array()}};
		for (const char *kind : {"actions", "observations", "intents"})
			if (vocabulary.contains(kind))
				for (auto descriptor : vocabulary.at(kind))
					if ((descriptor.value("contexts", 3u) & mask) != 0)
					{
						descriptor["id"] = descriptor.at("name");
						contexts[context][kind].push_back(std::move(descriptor));
					}
	}
	const auto dialogue = conversation_registry();
	contexts["conversation"] = {
	    {"actions", Json::array()}, {"observations", Json::array()}, {"intents", Json::array()}};
	for (const auto &f : dialogue.observations)
		contexts["conversation"]["observations"].push_back(
		    {{"id", f.name},
		     {"name", f.name},
		     {"type", f.type == state_tree::Type::Text      ? "text"
		              : f.type == state_tree::Type::Boolean ? "boolean"
		                                                    : "integer"}});
	for (const auto &a : dialogue.actions)
		contexts["conversation"]["actions"].push_back(
		    {{"id", a.name},
		     {"name", a.name},
		     {"arguments", Json::array({{{"name", "definition"}, {"type", "text"}}})},
		     {"effect", true}});
	const auto teams = team_registry();
	contexts["team"] = {{"observations", Json::array()}, {"actions", Json::array()}};
	for (const auto &f : teams.observations)
		contexts["team"]["observations"].push_back(
		    {{"id", f.name}, {"type", f.type == state_tree::Type::Boolean ? "boolean" : "integer"}});
	for (const auto &a : teams.actions)
		contexts["team"]["actions"].push_back({{"id", a.name}, {"arguments", Json::array()}});
	const auto away = away_registry(Json{{"boarding", true}});
	contexts["away"] = {{"observations", Json::array()}, {"actions", Json::array()}};
	for (const auto &f : away.observations)
		contexts["away"]["observations"].push_back({{"id", f.name},
		                                            {"name", f.name},
		                                            {"type", f.type == state_tree::Type::Text      ? "text"
		                                                     : f.type == state_tree::Type::Boolean ? "boolean"
		                                                                                           : "integer"}});
	for (const auto &a : away.actions)
		contexts["away"]["actions"].push_back(
		    {{"id", a.name},
		     {"name", a.name},
		     {"arguments", Json::array({{{"name", "value"}, {"type", a.name == "work" ? "integer" : "text"}}})},
		     {"effect", true}});
	const auto encounter = interdiction_registry();
	contexts["interdiction"] = {{"observations", Json::array()}, {"actions", Json::array()}};
	for (const auto &f : encounter.observations)
		contexts["interdiction"]["observations"].push_back(
		    {{"id", f.name}, {"name", f.name}, {"type", f.type == state_tree::Type::Text ? "text" : "boolean"}});
	for (const auto &a : encounter.actions)
		contexts["interdiction"]["actions"].push_back(
		    {{"id", a.name},
		     {"name", a.name},
		     {"arguments", Json::array({{{"name", "value"}, {"type", "text"}}})},
		     {"effect", true}});
	state_->tree_editor.set_vocabulary(contexts);
}
} // namespace phoenix
