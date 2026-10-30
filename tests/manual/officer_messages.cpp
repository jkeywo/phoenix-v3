// Explicit graphical journey through production message routing and Console UI.
#include "../support/assertions.hpp"
#include "../support/controller_fixture.hpp"
#include <RmlUi/Core.h>
#include <raylib.h>
#include <algorithm>
#include <iostream>
#include <set>
import phoenix.ui.console_surface;
import phoenix.presentation;
import phoenix.session.local;
using namespace phoenix;
using phoenix::test::require;
int main()
{
	try
	{
		auto source = phoenix::test::controller_fixture();
		auto &ship = source["entities"][0];
		ship["systems"]["steering"] = {{"kind", "steering"}, {"turn_mdeg_s", 90000}};
		ship["control_layout"]["controllers"][0].erase("fallback");
		ship["control_layout"]["controllers"][0]["tree"] = "test.report";
		ship["message_templates"] = {"test.arrival", "test.urgent"};
		source["knowledge"] = Json::parse(R"({"roles":["crew"],"schemas":[
 {"id":"report","producers":["crew"],"readers":["crew"],"fields":[{"id":"amount","type":"integer"}]}]})");
		for (auto &console : ship["stations"][0]["consoles"])
			console["knowledge_roles"] = {"crew"};
		source["messages"] = Json::parse(R"([
 {"id":"test.arrival","schema":"report","destination":{"kind":"console","id":"observer"},
 "title_key":"Engineering report","body_key":"The assigned repair work is complete. Power is available again.","fields":{"amount":1}},
 {"id":"test.urgent","schema":"report","destination":{"kind":"console","id":"observer"},"urgency":2,
 "title_key":"Engineering report","body_key":"The cooling supply cannot sustain the requested operation. I am keeping the equipment within its operating limits.","fields":{"amount":2}}])");
		source["trees"].push_back(Json::parse(R"({"id":"test.report","context":"controller","initial":"wait",
 "message_templates":["test.arrival","test.urgent"],"command_bindings":[{"channel":"speed"}],
 "states":[{"id":"wait","transitions":[{"target":"report","conditions":[{"left":{"elapsed":true},"op":"ge","right":20}]}]},
 {"id":"report","entry":[{"action":"send.test.arrival","arguments":[1,true,""]},{"action":"send.test.urgent","arguments":[2,true,""]}]}]})"));
		source["maps"][0]["ship_slots"] = Json::array({{{"id", "test.slot"},
		                                                {"allowed_ships", {"test.controllable"}},
		                                                {"x_mm", 100000},
		                                                {"y_mm", 0},
		                                                {"heading_mdeg", 0}}});
		Application app;
		app.can_host = true;
		app.session = make_local_session();
		app.session->add_pack(write_pack(phoenix::test::fixture_pack(json_bytes(source), "test-core")));
		app.session->start({"test.map", {{"test.slot", "test.controllable"}}, "test.slot", "observer"});
		app.screen = Screen::console;
		unsigned cues = 0;
		app.message_cue = [&](ConsoleId)
		{
			++cues;
		};
		SetConfigFlags(FLAG_WINDOW_HIDDEN);
		open_window();
		SetWindowSize(1280, 720);
		SetTargetFPS(120);
		UiRenderer renderer;
		{
			ConsoleSurface surface(1, "officer-message-smoke-", true, &renderer);
			ConsoleRegion region{1, 0, 0, 1280, 720};
			const auto frame = [&]
			{
				app.session->tick();
				app.screen_controls.clear();
				app.screen_readouts.clear();
				app.control_placements.clear();
				surface.prepare(app, region, {});
				std::set<std::string> controls;
				for (const auto &[id, label] : app.screen_controls)
					require(controls.insert(id).second, "message overlays publish each semantic control once");
				BeginDrawing();
				ClearBackground(BLACK);
				surface.render_ui(renderer, region);
				EndDrawing();
			};
			frame();
			auto *document = surface.context().GetDocument(0);
			require(document && !document->GetElementById("messages-popup")->IsVisible(true), "history starts closed");
			for (unsigned tick = 0; tick < 60; ++tick)
				frame();
			require(document->GetElementById("message-arrival")->IsVisible(true),
			        "AI arrivals appear over the active tab");
			require(cues == 1, "simultaneous arrivals produce one urgent cue and no ordinary cue");
			TakeScreenshot("officer-messages-arrival.png");
			require(surface.action("console/1/messages"), "Messages button opens history");
			frame();
			frame();
			require(document->GetElementById("messages-popup")->IsVisible(true), "history popup is visible");
			require(document->GetElementById("message-history")->GetNumChildren() == 2,
			        "popup retains both delivered messages");
			TakeScreenshot("officer-messages-wide.png");
			region.width = 390;
			region.height = 780;
			SetWindowSize(390, 780);
			surface.preview_text_scale(150);
			frame();
			frame();
			auto *history = document->GetElementById("message-history");
			require(history->GetClientWidth() > 200 && history->GetClientHeight() > 200,
			        "narrow enlarged-text popup remains readable and scrollable");
			TakeScreenshot("officer-messages-narrow.png");
			require(surface.controller_back(), "Back closes the popup");
			frame();
			require(!document->GetElementById("messages-popup")->IsVisible(true), "popup closes without stopping play");
			require(surface.context().GetFocusElement() &&
			            surface.context().GetFocusElement()->GetId() == "console/1/messages",
			        "closing returns focus to Messages");
		}
		close_window();
		std::cout << "Officer message popup smoke passed\n";
		return 0;
	}
	catch (const std::exception &error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
