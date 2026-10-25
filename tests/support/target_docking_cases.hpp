#pragma once
#include "docking_fixture.hpp"
namespace phoenix::test
{
inline void target_docking_cases()
{
	auto session = make_local_session();
	session->add_pack(archive(docking_fixture()));
	session->start({"test.map", {{"crew", "test.away"}}, "crew", "drive"});
	for (int i = 0; i < 5; ++i)
		session->tick();
	ConsoleModel helm, support;
	helm.refresh(session);
	require(!helm.target_docking_control(0) && !helm.target_docking_control(999) &&
	            !helm.target_docking_control(helm.observation()->own.id),
	        "unknown, unselected and own-ship contacts have no Dock button");
	require(session->claim_console(2, {helm.observation()->own.id, "observer"}),
	        "claim station without docking authority");
	support.refresh(session, 2);
	require(!support.target_docking_control(1), "non-owning station has no docking controls");
	const auto dock = helm.target_docking_control(1);
	require(dock && dock->label == "Dock" && dock->action == "assist" && helm.target_docking_action(1, dock->id),
	        "one contextual Dock starts admitted guidance");
	session->tick();
	helm.refresh(session);
	const auto cancel = helm.target_docking_control(1);
	require(cancel && cancel->label == "Cancel docking" && !helm.target_docking_action(1, dock->id),
	        "approach replaces Dock and refuses stale initiation");
	require(helm.target_docking_action(1, cancel->id), "contextual approach cancellation admitted");
	session->tick();
	helm.refresh(session);
	const auto restart = helm.target_docking_control(1);
	require(restart && restart->label == "Dock" && helm.target_docking_action(1, restart->id),
	        "cancel restores the single Dock action");
	for (int i = 0; i < 3000; ++i)
	{
		session->tick();
		helm.refresh(session);
		if (helm.docking()["ports"][0]["phase"] == "docked")
			break;
	}
	require(helm.docking()["ports"][0]["phase"] == "docked", "contextual approach reaches physical docking");
	const auto undock = helm.target_docking_control(1);
	require(undock && undock->label == "Undock" && helm.target_docking_action(1, undock->id),
	        "connected contact offers one normal Undock");
	session->tick();
	helm.refresh(session);
	require(!helm.target_docking_control(1) && !helm.target_docking_action(1, undock->id),
	        "separating hides duplicate commands and refuses stale Undock");
	for (int i = 0; i < 5; ++i)
		session->tick();
	helm.refresh(session);
	require(helm.target_docking_control(1)->label == "Dock", "separation returns the contact to dockable state");
	for (const bool closed : {false, true})
	{
		auto blocked_source = docking_fixture();
		auto remote = blocked_source["entities"][0];
		remote["id"] = "test.blocked";
		for (auto &port : remote["docking"]["ports"])
			if (closed)
				port["auto_accept_allies"] = false;
			else
				port["compatibility"] = "different";
		blocked_source["entities"].push_back(remote);
		blocked_source["maps"][0]["entities"][0]["definition"] = "test.blocked";
		auto other = make_local_session();
		other->add_pack(archive(blocked_source));
		other->start({"test.map", {{"crew", "test.away"}}, "crew", "drive"});
		for (int i = 0; i < 5; ++i)
			other->tick();
		ConsoleModel blocked;
		blocked.refresh(other);
		require(!blocked.target_docking_control(1),
		        closed ? "closed port has no Dock action" : "incompatible known contact has no Dock action");
	}
}
} // namespace phoenix::test
