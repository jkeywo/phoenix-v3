#pragma once
#include "away_assignment_fixture.hpp"
namespace phoenix::test
{
inline void away_assignment_cases()
{
	auto source = away_assignment_fixture();
	auto session = make_local_session();
	session->add_pack(archive(source));
	session->start({"test.map", {{"crew", "test.away"}}, "crew", "drive"});
	for (int i = 0; i < 4; ++i)
		session->tick();
	ConsoleModel helm, support;
	helm.refresh(session);
	auto unrevealed = *helm.observation();
	unrevealed.gameplay["away_missions"] = Json::array();
	unrevealed.gameplay["profile_stars"][unrevealed.gameplay.at("profile").get<std::string>()] = 1;
	require(console_ui_availability(unrevealed).features.contains("away"),
	        "one-star Dispatch retains its lifecycle component before missions are revealed");
	require(session->claim_console(2, {helm.observation()->own.id, "observer"}), "claim contributing station");
	support.refresh(session, 2);
	require(helm.ui_availability().features.contains("away") && !support.ui_availability().features.contains("away"),
	        "only Dispatch station exposes mission lifecycle page");
	require(support.ui_availability().features.contains("personnel"), "manual personnel has a Personnel page");
	require(support.asset_away_controls("team:team").empty(), "unselected mission is absent from team assignments");
	require(helm.away_action("open", "job"), "Helm opens mission assignments");
	session->tick();
	helm.refresh(session);
	support.refresh(session, 2);
	const auto team_options = support.asset_away_controls("team:team");
	const auto officer_options = support.asset_away_controls("officer:chief");
	require(team_options.size() == 1 && team_options[0].second.find("Repair job") != std::string::npos &&
	            officer_options.size() == 1,
	        "opened mission is offered on eligible owned team and officer interfaces");
	require(helm.asset_away_controls("team:team").empty() && !helm.asset_away_action(team_options[0].first),
	        "Helm cannot contribute another department's team");
	require(support.asset_away_action(officer_options[0].first), "officer mission assignment uses normal admission");
	session->tick();
	support.refresh(session, 2);
	const auto withdraw_officer = support.asset_away_controls("officer:chief");
	require(withdraw_officer.size() == 1 && withdraw_officer[0].first.starts_with("asset-away/withdraw/") &&
	            support.asset_away_action(withdraw_officer[0].first),
	        "officer assignment can be withdrawn through the same interface");
	session->tick();
	support.refresh(session, 2);
	require(support.command("preset", 1), "select automatic personnel workload");
	session->tick();
	support.refresh(session, 2);
	require(!support.ui_availability().features.contains("personnel") &&
	            support.asset_away_controls("officer:chief").empty(),
	        "automated personnel hides the tab and individual officer mission choices");
	const auto assisted_team = support.asset_away_controls("team:team");
	require(assisted_team.size() == 1 && support.asset_away_action(assisted_team[0].first),
	        "department can authorise its team at one star through contribution authority");
	session->tick();
	support.refresh(session, 2);
	require(support.away_missions()[0]["slots"][0]["assigned"] == "team:team",
	        "team assignment reaches authoritative mission state");
	const auto withdraw_team = support.asset_away_controls("team:team");
	require(withdraw_team.size() == 1 && withdraw_team[0].first.starts_with("asset-away/withdraw/"),
	        "assigned team offers withdrawal rather than a second dispatch");
	helm.refresh(session);
	require(helm.away_action("cancel", "job"), "Helm cancels selected mission");
	session->tick();
	support.refresh(session, 2);
	require(support.asset_away_controls("team:team").empty() && !support.asset_away_action(assisted_team[0].first) &&
	            !support.asset_away_action(withdraw_team[0].first),
	        "cancelling mission removes asset choices and rejects stale commands");
}
} // namespace phoenix::test
