#pragma once
// Uses the cargo test's synthetic fixture and ordinary Dispatch commands.
void supply_cases()
{
	for (const auto trade : {false, true})
	{
		auto source = fixture();
		auto provider = source["entities"][0];
		provider["id"] = "test.provider";
		provider["logistics"]["materials"].push_back({{"id", "tokens"}});
		provider["logistics"]["stores"][0]["accepts"].push_back("tokens");
		provider["logistics"]["stores"][0]["initial"] = {{"parts", 4}, {"tokens", 0}};
		for (auto &entity : source["entities"])
		{
			entity["logistics"]["materials"].push_back({{"id", "tokens"}});
			entity["logistics"]["stores"][0]["accepts"].push_back("tokens");
			if (!entity.contains("craft"))
			{
				entity["logistics"]["stores"][0]["initial"] = {{"parts", 0}, {"tokens", 4}};
				entity["logistics"]["stores"][0]["targets"] = {{"parts", 3}};
			}
		}
		source["entities"].push_back(provider);
		auto &map = source["maps"][0];
		map.erase("scenario");
		map["entities"][0]["definition"] = "test.provider";
		auto &mission = map["away_missions"][0];
		mission["slots"] = Json::array();
		mission["craft_slots"] =
		    Json::array({{{"id", "transport"}, {"role", "transport"}, {"craft_role", "shuttle"}, {"required", true}}});
		mission["supply"] = {{"provider_store", "hold"}, {"home_store", "hold"}, {"material", "parts"}, {"limit", 3}};
		if (trade)
		{
			mission["supply"]["payment"] = "tokens";
			mission["supply"]["pay"] = 1;
		}
		auto &layout = source["entities"][0]["control_layout"];
		layout["station_complexity"] = true;
		layout["profiles"][0]["stars"] = 3;
		auto assisted = layout["profiles"][0];
		assisted["id"] = "assisted";
		assisted["stars"] = 1;
		const auto assisted_index = layout["profiles"].size();
		layout["profiles"].push_back(assisted);
		const auto catalog = load(source);
		World world;
		world.load(catalog, "test.map", {{"crew", "test.away"}});
		require(world.claim(101, 2, "drive") && world.claim(102, 2, "observer"), "Supply crew claims");
		ticks(world, 5);
		auto away = [&](World &w, const std::string &verb, const std::string &text = {})
		{
			const auto o = *w.observe(101);
			const auto &m = o.gameplay.at("away_missions")[0];
			OperatingCommand c{2, "away." + verb, m.at("revision"), "job"};
			c.controller = verb == "transport" ? "bay" : "dispatch";
			c.text = text;
			for (const auto &controller : o.gameplay.at("controllers"))
				if (controller.at("id") == c.controller)
					c.generation = controller.at("generation");
			return w.submit(101, c);
		};
		require(away(world, "open"), "Supply Dispatch opens craft-only mission");
		ticks(world, 1);
		const auto craft = world.observe(101)->gameplay.at("craft")[0].at("id").get<std::string>();
		require(away(world, "transport", "craft:" + craft), "Supply craft contribution assigned");
		ticks(world, 1);
		require(away(world, "quantity", "1"), "Three-star Engineering edits a valid manifest");
		ticks(world, 1);
		require(world.submit(101, {2, "preset", static_cast<std::int64_t>(assisted_index), {}}),
		        "Downgrade manifest responsibility");
		ticks(world, 1);
		require(world.observe(101)->gameplay.at("away_missions")[0].at("supply").at("quantity") == 3,
		        "Assisted manifest recomputes from stock targets instead of retaining a manual quantity");
		require(!away(world, "quantity", "1"), "One-star Engineering cannot edit manifest quantities");
		require(world.submit(101, {2, "preset", 0, {}}), "Restore full manifest workload");
		ticks(world, 1);
		require(away(world, "quantity", "0"), "Restore automatic quantity selection");
		ticks(world, 1);
		const auto prepared = world.save();
		auto shortage = [&](EntityId id, const std::string &material, std::int64_t quantity, const std::string &reason)
		{
			auto state = prepared;
			for (auto &body : state["bodies"])
				if (body.at("body").at("id") == id)
					body["inventory"]["stores"][0]["quantities"][material] = quantity;
			World blocked;
			blocked.restore(catalog, state);
			require(!away(blocked, "send") && blocked.save().at("logistics").at("cargo").empty(), reason.c_str());
		};
		shortage(1, "parts", 0, "Insufficient provider stock cannot create a cargo job");
		shortage(2, "tokens", 7, "Insufficient home receiving capacity cannot dispatch");
		shortage(static_cast<EntityId>(std::stoull(craft)), "tokens", 7,
		         "Occupied cargo hold cannot accept a supply manifest");
		if (trade)
			shortage(2, "tokens", 0, "Unaffordable barter cannot dispatch");
		const auto projected = network::read_observation(network::observation_json(*world.observe(101)), catalog);
		require(projected.gameplay.at("away_missions") == world.observe(101)->gameplay.at("away_missions"),
		        "Supply manifest and routed permissions survive network projection");
		require(away(world, "send"), "Finite supply manifest dispatched");
		ticks(world, 1);
		{
			auto loss = world.save();
			for (auto &body : loss["bodies"])
				if (body.at("body").at("id") == static_cast<EntityId>(std::stoull(craft)))
				{
					body["destructible"]["destroyed_tick"] = loss.at("tick");
					body["destructible"]["durability"] = 0;
					for (auto &room : body["combat"]["compartments"])
					{
						room["hp"] = 0;
						room["disabled"] = true;
					}
				}
			World destroyed;
			destroyed.restore(catalog, loss);
			ticks(destroyed, 3);
			const auto after = destroyed.save();
			require(after.at("logistics").at("cargo")[0].at("phase") == "lost" &&
			            after.at("logistics").at("cargo")[0].at("reserved") == 0 &&
			            after.at("logistics").at("cargo")[0].at("collected") == 0 &&
			            after.at("logistics").at("cargo")[0].at("paid") == 0,
			        "Destroyed collection transport releases reservations without inventing goods or payment");
			World resumed_loss;
			resumed_loss.restore(catalog, after);
			ticks(destroyed, 3);
			ticks(resumed_loss, 3);
			require(destroyed.save() == resumed_loss.save(),
			        "Collection loss preserves actual transfers across recovery");
		}
		World cancelled;
		cancelled.restore(catalog, world.save());
		require(away(cancelled, "cancel"), "Supply cancellation admitted");
		ticks(cancelled, 1);
		for (int i = 0; i < 1500 && cancelled.save().at("logistics").at("cargo")[0].at("phase") != "complete"; ++i)
			cancelled.tick();
		require(cancelled.save().at("logistics").at("cargo")[0].at("phase") == "complete",
		        "Cancelled collection unloads and recovers");
		std::set<std::string> resumed;
		for (int i = 0; i < 2000; ++i)
		{
			const auto saved = world.save();
			const auto phase = saved.at("logistics").at("cargo")[0].at("phase").get<std::string>();
			if (resumed.insert(phase).second)
			{
				World restored;
				restored.restore(catalog, saved);
				world.tick();
				restored.tick();
				require(world.save() == restored.save(), "Supply phase resumes without duplicated transfers");
			}
			else
				world.tick();
			if (phase == "complete")
				break;
		}
		const auto saved = world.save();
		const auto &job = saved.at("logistics").at("cargo")[0];
		if (job.at("phase") != "complete" || job.at("delivered") != 3)
			std::cerr << "Supply diagnostic " << job.dump() << "\n" << saved.at("craft").dump() << "\n";
		require(job.at("phase") == "complete" && job.at("delivered") == 3, "Supplies physically unloaded at home");
		require(job.at("paid") == (trade ? 3 : 0), "Gift/barter receipt records actual payment");
		require(world.observe(102)->gameplay.at("logistics").at("stores")[0].at("quantities").at("parts") == 3,
		        "Supply delivery changes actual stock");
		require(world.observe(101)->gameplay.at("away_missions")[0].at("result") == "complete:Supplies delivered",
		        "Dispatch completes after unloading");
	}
}
