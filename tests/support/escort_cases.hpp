#pragma once
void escort_cases()
{
	auto source = armed_craft_fixture();
	auto &layout = source["entities"][0]["control_layout"];
	layout["station_complexity"] = true;
	layout["profiles"][0]["stars"] = 2;
	layout["manual_workloads"] = {{"drive", {{"craft", 2}, {"transport", 2}}}};
	source["maps"][0]["away_missions"][0]["craft_slots"] =
	    Json::array({{{"id", "primary"}, {"role", "transport"}, {"craft_role", "shuttle"}, {"required", true}},
	                 {{"id", "escort"}, {"role", "escort"}, {"craft_role", "interceptor"}, {"required", true}}});
	const auto catalog = load(source);
	World world;
	world.load(catalog, "test.map", {{"crew", "test.away"}});
	const auto ship = *world.ship_for_slot("crew");
	require(world.claim(101, ship, "drive") && world.claim(102, ship, "observer"), "Escort crew claims");
	ticks(world, 5);
	std::string primary, escort;
	const auto view = *world.observe(101);
	for (const auto &craft : view.gameplay.at("craft"))
	{
		if (craft.at("role") == "shuttle" && primary.empty())
			primary = craft.at("id");
		if (craft.at("role") == "interceptor")
			escort = craft.at("id");
	}
	assign(world, "craft:" + primary);
	require(!world.submit(101, command(world, "away", "send", "job")),
	        "Required escort blocks departure until assigned");
	auto c = command(world, "away", "escort", "job", "escort/craft:" + escort);
	c.controller = "bay";
	const auto prepared = *world.observe(101);
	for (const auto &ctrl : prepared.gameplay.at("controllers"))
		if (ctrl.at("id") == "bay")
			c.generation = ctrl.at("generation");
	require(world.submit(101, c), "Two-star owning watch assigns an interceptor escort");
	world.tick();
	require(!world.submit(101, command(world, "craft", "defend", escort)),
	        "Preparing escort cannot take a conflicting sortie");
	require(world.submit(101, command(world, "away", "send", "job")), "Mission dispatches with its authorized escort");
	world.tick();
	World restored;
	restored.restore(catalog, world.save());
	world.tick();
	restored.tick();
	require(world.save() == restored.save(), "Escort/primary launch reservations resume exactly");
	for (int i = 0; i < 500 && missions(world)[0].at("phase") != "deployed"; ++i)
		world.tick();
	require(missions(world)[0].at("phase") == "deployed", "Escorted transport delivers its real party");
	require(world.submit(101, command(world, "craft", "recall", escort)), "Two-star individual escort recall admitted");
	ticks(world, 3);
	require(missions(world)[0].at("phase") == "deployed" &&
	            missions(world)[0].at("craft_slots")[0].at("assigned") == "",
	        "Escort recall withdraws its contribution without requesting passenger pickup");
}
