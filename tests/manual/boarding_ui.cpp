// Explicit graphical boarding journey. Not part of automatic CTest.
#include <raylib.h>
#include <algorithm>
#include <iostream>
#include <string>
#include <nlohmann/json.hpp>
import phoenix.application;
import phoenix.presentation;
import phoenix.session.execution;
using namespace phoenix;
int main(int argc, char **argv)
{
	Application app;
	bool opened = false;
	try
	{
		if (argc < 2)
			throw std::runtime_error("Usage: phoenix_boarding_smoke pack-directory");
		app.can_host = true;
		app.native_menu = false;
		app.session = make_local_session();
		auto draft = read_pack(read_file(std::string(argv[1]) + "/base.zip")).draft();
		auto definitions = parse_json(draft.files.at("definitions.json"), "boarding UI variant");
		// A deterministic smoke variant exercises the success UI without claiming balance acceptance.
		for (auto &e : definitions["entities"])
			if (e["id"] == "base.boarding-target")
				for (auto &encounter : e["boarding_missions"][0]["encounters"])
				{
					encounter["success_permille"] = 1000;
					encounter["defender_penalty_permille"] = 0;
					encounter["loss_permille"] = 0;
				}
		draft.files["definitions.json"] = json_bytes(definitions);
		app.session->add_pack(write_pack(draft));
		app.session->start({"base.boarding-action", {{"courier", "base.rescue-courier"}}, "courier", "helm"});
		const auto ship = app.session->observation()->own.id;
		if (!app.session->claim_console(2, {ship, "support"}))
			throw std::runtime_error("Boarding Console claims failed");
		app.screen = Screen::console;
		open_window();
		opened = true;
		SetWindowSize(2560, 900);
		app.console_regions = {{1, 0, 0, 1280, 900}, {2, 1280, 0, 1280, 900}};
		auto frame = [&]
		{
			BeginDrawing();
			ClearBackground(BLACK);
			draw_screen(app);
			EndDrawing();
		};
		for (int i = 0; i < 600; ++i)
		{
			frame();
			if (std::any_of(app.screen_controls.begin(), app.screen_controls.end(),
			                [](const auto &c)
			                {
				                return c.first == "console/2/personnel";
			                }))
				break;
		}
		auto advance = [&](int n)
		{
			for (int i = 0; i < n; ++i)
			{
				app.session->tick();
				if (i == n - 1)
					frame();
			}
		};
		auto action = [&](const std::string &id)
		{
			if (app.focus_control)
				app.focus_control(id);
			frame();
			frame();
			for (const auto &control : app.screen_controls)
				if (control.first == id)
				{
					auto copy = id;
					if (app.screen_action(copy))
						return;
				}
			TakeScreenshot("boarding-ui-failure.png");
			for (const auto &control : app.screen_controls)
				if (control.first.find("away") != std::string::npos)
					std::cerr << "Control: " << control.first << '\n';
			const auto view = app.session->console_observation(1);
			std::cerr << "Boarding missions: " << view->gameplay.value("away_missions", Json::array()).dump()
			          << " Flight operations: " << view->gameplay.value("craft", Json::array()).dump() << '\n';
			throw std::runtime_error("Missing/refused boarding UI action: " + id);
		};
		advance(20);
		const auto mission = "board." + std::to_string(ship) + ".1.prize";
		action("console/1/away");
		advance(2);
		action("console/1/away/open/" + mission + "/");
		advance(2);
		action("console/1/local-settings.close");
		advance(1);
		action("console/1/game.console/tactical");
		advance(2);
		action("console/1/away");
		action("console/2/away");
		advance(2);
		action("console/1/away/assign/" + mission + "/security/team:security-team");
		advance(2);
		action("console/2/away/assign/" + mission + "/medical/team:medical-team");
		advance(2);
		action("console/1/away/transport/" + mission + "/boarding-torpedo");
		advance(160);
		TakeScreenshot("boarding-manifest.png");
		action("console/1/local-settings.close");
		advance(1);
		action("console/1/game.console/helm");
		advance(2);
		action("console/1/away");
		advance(2);
		action("console/1/away/send/" + mission + "/");
		advance(2);
		auto deployed = [&]
		{
			const auto view = app.session->console_observation(1);
			for (const auto &m : view->gameplay.at("away_missions"))
				if (m["id"] == mission)
					return m["phase"] == "deployed";
			return false;
		};
		for (int i = 0; i < 200 && !deployed(); ++i)
			advance(10);
		if (!deployed())
		{
			const auto container = read_pack(app.session->save()).draft();
			const auto state = parse_json(container.files.at("session.json"), "boarding diagnosis").at("state");
			std::cerr << "Craft: " << state.at("craft").dump() << " Missions: " << state.at("away_missions").dump()
			          << '\n';
			for (const auto &body : state.at("bodies"))
				std::cerr << "Body: " << body.at("body").dump() << '\n';
			throw std::runtime_error("Physical boarding delivery did not finish");
		}
		action("console/2/local-settings.close");
		advance(1);
		action("console/2/game.console/comms");
		advance(3);
		for (const auto &c : app.screen_controls)
			if (c.first.starts_with("console/2/comms/select/"))
			{
				auto id = c.first;
				action(id);
				break;
			}
		advance(2);
		TakeScreenshot("boarding-choice.png");
		action("console/2/comms/answer/bridge");
		advance(20);
		auto assessed = [&]
		{
			const auto view = app.session->console_observation(1);
			for (const auto &m : view->gameplay.at("away_missions"))
				if (m["id"] == mission)
					return m.contains("boarding_check");
			return false;
		};
		for (int i = 0; i < 100 && !assessed(); ++i)
			advance(5);
		if (!assessed())
			throw std::runtime_error("Delivered assessment not visible in Dispatch");
		TakeScreenshot("boarding-assessment.png");
		advance(200);
		if (!app.session->claim_console(3, {1, "helm"}))
			throw std::runtime_error("Prize Console was not made available to captor");
		app.console_regions[1].console = 3;
		for (int i = 0; i < 3; ++i)
			frame();
		TakeScreenshot("boarding-prize.png");
		close_window();
		opened = false;
		std::cout << "PASS boarding manifest, physical torpedo delivery, Comms objective, assessment and prize Console "
		             "UI smoke\n";
	}
	catch (const std::exception &e)
	{
		std::cerr << e.what() << '\n';
		if (opened)
			close_window();
		return 1;
	}
}
