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
import phoenix.workshop.authoring_core;
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
void GameplayEditors::open_ship(const std::string &id)
{
	state_->graph_entity = id;
	state_->graph_node.clear();
	state_->ship_graph_open = true;
	state_->ship_graph_focus = true;
}
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
	state_->tree_editor.set_vocabulary(workshop_prepare_vocabulary(vocabulary));
}
} // namespace phoenix
