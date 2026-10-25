// Manually invoked native UI evidence; never registered with CTest.
#include "../support/assertions.hpp"
#include "../support/away_assignment_fixture.hpp"
#include <raylib.h>
#include <algorithm>
#include <iostream>
import phoenix.presentation;
import phoenix.console;
import phoenix.session.execution;
using namespace phoenix;
using namespace phoenix::test;
int main()
{
	try
	{
		Application app;
		app.can_host = true;
		app.session = make_local_session();
		app.session->add_pack(archive(away_assignment_fixture()));
		app.session->start({"test.map", {{"crew", "test.away"}}, "crew", "drive"});
		ConsoleModel helm, support;
		helm.refresh(app.session);
		require(app.session->claim_console(2, {helm.observation()->own.id, "observer"}), "claim Support");
		support.refresh(app.session, 2);
		require(helm.command("preset", 1) && support.command("preset", 1), "start with automatic personnel");
		app.screen = Screen::console;
		app.console_regions = {{1, 0, 0, 1280, 900}, {2, 1280, 0, 1280, 900}};
		open_window();
		SetWindowSize(2560, 900);
		SetTargetFPS(60);
		auto frame = [&]
		{
			app.session->tick();
			BeginDrawing();
			draw_screen(app);
			EndDrawing();
		};
		auto has = [&](const std::string &key)
		{
			return std::any_of(app.screen_controls.begin(), app.screen_controls.end(),
			                   [&](const auto &row)
			                   {
				                   return row.first == key;
			                   });
		};
		auto click = [&](unsigned route, const std::string &key)
		{
			require(probe_console_tab(route, key), ("pointer invokes " + key).c_str());
			for (int i = 0; i < 3; ++i)
				frame();
		};
		for (int i = 0; i < 1200 && !has("console/2/support-details/team/team"); ++i)
			frame();
		require(!has("console/1/console-page/drive/station.personnel") &&
		            !has("console/2/console-page/observer/station.personnel"),
		        "automatic personnel has no tab on either station");
		require(!has("console/2/console-page/observer/station.away"),
		        "contributor has no Away lifecycle page even when authored");
		click(1, "console-page/drive/station.away");
		click(1, "away/open/job/");
		click(2, "support-details/team/team");
		require(has("console/2/asset-away/assign/job/repair/team:team"), "opened mission appears in team details");
		TakeScreenshot("build/away-team-assignment.png");
		click(2, "asset-away/assign/job/repair/team:team");
		support.refresh(app.session, 2);
		require(support.away_missions()[0]["slots"][0]["assigned"] == "team:team",
		        "team popup assignment reaches the mission");
		click(2, "asset-away/withdraw/job/repair/team:team");
		click(2, "systems-close");
		support.refresh(app.session, 2);
		require(support.command("preset", 0), "enable manual personnel on Support");
		for (int i = 0; i < 4; ++i)
			frame();
		click(2, "console-page/observer/station.personnel");
		if (has("console/2/asset-away/withdraw/job/specialist/officer:chief"))
			click(2, "asset-away/withdraw/job/specialist/officer:chief");
		if (!has("console/2/asset-away/assign/job/specialist/officer:chief"))
		{
			support.refresh(app.session, 2);
			std::cerr << support.away_missions().dump() << "\n";
			for (const auto &control : app.screen_controls)
				if (control.first.starts_with("console/2/"))
					std::cerr << control.first << "\n";
		}
		require(has("console/2/asset-away/assign/job/specialist/officer:chief"),
		        "officer roster offers the selected mission");
		TakeScreenshot("build/away-officer-assignment.png");
		click(2, "asset-away/assign/job/specialist/officer:chief");
		support.refresh(app.session, 2);
		require(support.away_missions()[0]["slots"][1]["assigned"] == "officer:chief",
		        "officer roster assignment reaches the mission");
		click(1, "away/cancel/job/");
		require(!has("console/2/asset-away/withdraw/job/specialist/officer:chief") &&
		            !app.screen_action("console/2/asset-away/withdraw/job/specialist/officer:chief"),
		        "cancel removes assignment options and refuses stale input");
		close_window();
		std::cout
		    << "PASS Dispatch-only Away, automatic Personnel visibility and pointer team/officer mission assignments\n";
	}
	catch (const std::exception &error)
	{
		std::cerr << error.what() << '\n';
		return 1;
	}
}
