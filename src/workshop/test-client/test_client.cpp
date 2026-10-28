module;
#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>
#include <cstdint>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

module phoenix.workshop.test_client;
import phoenix.content.documents;
import phoenix.workshop.authoring;

namespace phoenix
{
struct WorkshopTestClient::State : AuthoringControls
{
	std::uint64_t probe_tick = 0;
	Json feedback = Json::object();
	std::optional<GameplayTestRequest> test_request;
	std::string test_ship, test_slot, test_map, inspected_controller;
	void disposable_test(WorkshopDocument &document, ToolMessage &status);
};

void WorkshopTestClient::State::disposable_test(WorkshopDocument &document, ToolMessage &status)
{
	heading("Disposable production test");
	text("Run an isolated simulation of the verified draft. Pause, step and restart apply to this test.");
	Json selection{{"Mission", test_map}, {"Ship", test_ship}, {"Slot", test_slot}};
	if (choice(selection, "Mission", document.definition_choices(WorkshopDefinitionKind::Map)))
		test_map = selection.at("Mission").get<std::string>();
	if (choice(selection, "Ship", document.definition_choices(WorkshopDefinitionKind::Entity)))
		test_ship = selection.at("Ship").get<std::string>();
	std::vector<std::string> slots;
	if (!test_map.empty())
	{
		try
		{
			const auto map = document.preview_definition(WorkshopDefinitionKind::Map, test_map);
			if (map.contains("ship_slots"))
				slots = ids(map.at("ship_slots"));
		}
		catch (const std::exception &error)
		{
			ImGui::TextWrapped("%s", error.what());
		}
	}
	if (choice(selection, "Slot", slots))
		test_slot = selection.at("Slot").get<std::string>();
	for (const auto &[title, action] :
	     std::vector<std::pair<const char *, GameplayTestAction>>{{"Start test", GameplayTestAction::Start},
	                                                              {"Pause test", GameplayTestAction::Pause},
	                                                              {"Step test", GameplayTestAction::Step},
	                                                              {"Restart test", GameplayTestAction::Restart},
	                                                              {"Stop test", GameplayTestAction::Stop}})
	{
		if (button(title))
		{
			if (action == GameplayTestAction::Start && (test_map.empty() || test_slot.empty() || test_ship.empty()))
				status = "Choose a mission, slot and ship before starting a test.";
			else
			{
				GameplayTestRequest request;
				request.action = action;
				request.mission = test_map;
				request.slot = test_slot;
				request.ship = test_ship;
				if (action == GameplayTestAction::Start)
				{
					request.pack = document.export_pack();
					request.dependencies = document.dependency_archives();
				}
				test_request = std::move(request);
				status = "Disposable test request queued.";
			}
		}
		ImGui::SameLine();
	}
	ImGui::NewLine();
	heading("Disposable runtime inspection");
	if (feedback.contains("controllers"))
	{
		Json selection{{"Inspected controller", inspected_controller}};
		if (choice(selection, "Inspected controller", ids(feedback.at("controllers")), true))
			inspected_controller = selection.value("Inspected controller", std::string{});
		for (auto &controller : feedback["controllers"])
			if (identity(controller) == inspected_controller)
				structure(controller, false);
	}
	if (feedback.empty())
		text("No disposable test feedback has been received.");
	else
		structure(feedback, false);
}

WorkshopTestClient::WorkshopTestClient() : state_(std::make_unique<State>())
{
}
WorkshopTestClient::~WorkshopTestClient() = default;
void WorkshopTestClient::draw(WorkshopDocument &document, ToolMessage &status, bool pseudo)
{
	state_->pseudo = pseudo;
	state_->disposable_test(document, status);
}
void WorkshopTestClient::set_feedback(const Json &feedback)
{
	state_->feedback = feedback;
}
const Json &WorkshopTestClient::feedback() const
{
	return state_->feedback;
}
const std::string &WorkshopTestClient::inspected_controller() const
{
	return state_->inspected_controller;
}
std::optional<GameplayTestRequest> WorkshopTestClient::take_request()
{
	return std::exchange(state_->test_request, std::nullopt);
}
bool WorkshopTestClient::probing() const
{
	return state_->controller_probe;
}
bool WorkshopTestClient::probe(WorkshopDocument &document, unsigned stage)
{
	auto &editor = *state_;
	if (stage == 0)
	{
		editor.controller_probe = true;
		editor.test_map = "test.map";
		editor.test_slot = "test.slot";
		editor.test_ship = "test.controllable";
		return document.definitions()
		           .at("entities")
		           .at(0)
		           .at("control_layout")
		           .at("controllers")
		           .at(0)
		           .at("commands")
		           .at(0)
		           .at("initial") == 3000;
	}
	if (!editor.probe_action.empty())
		return false;
	if (stage == 1)
	{
		editor.probe_action = "Start test";
		return true;
	}
	const auto &feedback = editor.feedback;
	if (!feedback.value("running", false) || feedback.value("tick", 0) < 1)
		return false;
	if (stage == 2)
	{
		bool command = false;
		for (const auto &owner : feedback.value("controllers", Json::array()))
			for (const auto &field : owner.value("commands", Json::array()))
				if (field.value("channel", "") == "speed" && field.value("value", 0) == 3000 &&
				    field.at("revision") == field.at("consumed"))
					command = true;
		if (!command)
			return false;
		editor.probe_action = "Pause test";
	}
	if (stage == 3)
	{
		if (!feedback.value("paused", false))
			return false;
		editor.probe_tick = feedback.at("tick").get<std::uint64_t>();
		editor.probe_action = "Step test";
	}
	if (stage == 4)
	{
		if (!feedback.value("paused", false) || feedback.at("tick") != editor.probe_tick + 1)
			return false;
		editor.probe_action = "Restart test";
	}
	return true;
}
} // namespace phoenix
