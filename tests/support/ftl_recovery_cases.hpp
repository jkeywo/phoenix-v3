#pragma once
#include "away_scored_fixture.hpp"
void ftl_replacement_recovery_case()
{
	auto source = craft_fixture();
	fit_ftl(source, "helm", "drive");
	append_scored_away_policy(source, "test.recovery.mission");
	Json preparation;
	for (const auto &tree : source["trees"])
		if (tree.at("id") == "test.recovery.mission")
			preparation = tree;
	for (auto &tree : source["trees"])
		if (tree.at("id") == "test.bay.policy")
		{
			tree["away_preparation_observations"] = true;
			tree["request_observations"] = true;
			for (auto binding : preparation.at("action_bindings"))
			{
				binding["id"] = "mission-" + binding.at("id").get<std::string>();
				tree["action_bindings"].push_back(binding);
			}
			for (auto binding : preparation.at("objective_bindings"))
			{
				binding["id"] = "mission-" + binding.at("id").get<std::string>();
				binding["action"] = "mission-" + binding.at("action").get<std::string>();
				tree["objective_bindings"].push_back(binding);
			}
		}
	Json consumable;
	for (const auto &entity : source["entities"])
		if (entity.at("id") == "test.shuttle")
			consumable = entity;
	consumable["id"] = "test.consumable";
	consumable["craft"]["role"] = "boarding_torpedo";
	source["entities"].push_back(consumable);
	auto &ship = source["entities"][0];
	ship["systems"]["bay"]["initial"] = {"test.consumable", "test.shuttle"};
	ship["standing_directives"].push_back({{"id", "known-recovery"},
	                                       {"behavior", "away"},
	                                       {"tier", "scripted_low"},
	                                       {"title_key", "Recover known party"}});
	auto &layout = ship["control_layout"];
	layout["station_complexity"] = true;
	layout["profiles"][0]["stars"] = 3;
	auto delegated = layout["profiles"][0];
	delegated["id"] = "recovery";
	delegated["stars"] = 1;
	for (const auto *id : {"bay", "dispatch"})
	{
		auto &human = delegated["consoles"]["drive"]["human"];
		human.erase(std::find(human.begin(), human.end(), Json(id)));
		delegated["consoles"]["drive"]["ai"].push_back(id);
	}
	layout["profiles"].push_back(delegated);
	for (auto &controller : layout["controllers"])
		if (controller.at("id") == "dispatch")
		{
			controller.erase("fallback");
			controller["tree"] = "test.recovery.mission";
		}
	for (auto &store : ship["resources"]["stores"])
		if (store.at("id") == "jump-cell")
			store["capacity"] = 20;
	const auto catalog = load(source);
	World world;
	world.load(catalog, "test.map", {{"crew", "test.away"}});
	const auto home = *world.ship_for_slot("crew");
	require(world.claim(101, home, "drive") && world.claim(102, home, "observer"),
	        "Observe replacement recovery watches");
	ticks(world, 4);
	std::string consumed;
	const auto fleet_view = world.observe(101);
	for (const auto &craft : fleet_view->gameplay.at("craft"))
		if (craft.at("role") == "boarding_torpedo")
			consumed = craft.at("id");
	require(!consumed.empty(), "Observe explicit consumable transport identity");
	assign(world, "craft:" + consumed);
	require(world.submit(101, command(world, "away", "send", "job")), "Admit consumable insertion");
	for (int tick = 0; tick < 40 && missions(world)[0].at("phase") != "deployed"; ++tick)
		world.tick();
	require(missions(world)[0].at("phase") == "deployed" &&
	            world.save().at("away_missions")[0].at("transport_released"),
	        "Physical insertion consumes the original delivery craft");
	require(world.submit(101, {home, "preset", 1}), "Delegate recovery choices through ordinary handover");
	world.tick();
	const auto view = world.observe(101);
	Observation navigator;
	for (const auto &extra : view->additional_consoles)
		if (extra.console == "navigator")
			navigator = extra;
	OperatingCommand charge{home, "system_enabled", 1};
	charge.console = "navigator";
	charge.controller = "jump";
	charge.channel = "enabled";
	for (const auto &controller : navigator.gameplay.at("controllers"))
		if (controller.at("id") == "jump")
			charge.generation = controller.at("generation");
	require(world.submit(101, charge), "Admit departure preparation for the deployed party");
	world.tick();
	World resumed;
	resumed.restore(catalog, world.save());
	bool transport = false, recall = false;
	for (int tick = 0; tick < 100 && missions(world)[0].at("phase") != "recovered"; ++tick)
	{
		world.tick();
		resumed.tick();
		const auto observed = world.observe(101);
		for (const auto &controller : observed->gameplay.at("controllers"))
			if (controller.at("action").at("status") == "succeeded")
			{
				transport |= controller.at("action").at("id") == "mission-transport";
				recall |= controller.at("action").at("id") == "recall";
			}
	}
	require(transport && recall, "Replacement transport and party recall use scored ordinary Controller actions");
	require(missions(world)[0].at("transport") != "craft:" + consumed && missions(world)[0].at("phase") == "recovered",
	        "Another reported craft physically recovers the party during departure preparation");
	require(world.save() == resumed.save(), "Replacement reservation and physical recovery continue deterministically");
}
