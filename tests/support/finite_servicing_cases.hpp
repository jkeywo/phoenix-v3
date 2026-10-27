// Included after the craft system fixture's production command helpers.
std::int64_t finite_serviced_ammunition(const Json &saved, EntityId home)
{
	std::set<EntityId> craft;
	for (const auto &entry : saved.at("craft").at("entities"))
		if (entry.at("state").at("home") == home)
			craft.insert(entry.at("id").get<EntityId>());
	std::int64_t total = 0;
	for (const auto &body : saved.at("bodies"))
		if (craft.contains(body.at("body").at("id").get<EntityId>()) && body.contains("combat") &&
		    body.at("combat").at("systems").contains("magazine"))
			total += saved_ammunition(body, "magazine");
	return total;
}
std::int64_t finite_service_property(const Json &saved, EntityId home, const std::string &id)
{
	for (const auto &owner : saved.at("custom_properties"))
		if (owner.at("entity") == home && owner.at("equipment") == "bay")
			return owner.at("values").at(id).get<std::int64_t>();
	throw std::runtime_error("Missing finite servicing property");
}
void finite_energy_servicing_cases()
{
	auto source = craft_fixture();
	for (auto &entity : source["entities"])
		if (entity.contains("craft"))
		{
			entity["properties"]["test.energy-eligible"] = true;
			for (auto &store : entity["resources"]["stores"])
				store["initial"] = store.at("capacity");
		}
	auto &bay = source["entities"][0]["systems"]["bay"];
	bay["properties"] = {{"test.energy-credit", 10}, {"test.energy-starts", 0}, {"test.energy-completions", 0}};
	bay["properties"]["test.energy-rate"] = 1000;
	bay["processes"] = {{"energy", {{"process", "test.finite-energy"}}}};
	source["properties"] = Json::array();
	for (const auto *id : {"test.energy-credit", "test.energy-starts", "test.energy-completions"})
		source["properties"].push_back(
		    {{"id", id}, {"kind", "quantity"}, {"minimum", 0}, {"maximum", 1000}, {"default", 0}});
	source["properties"].push_back({{"id", "test.energy-rate"},
	                                {"kind", "attribute"},
	                                {"unit", "permille"},
	                                {"minimum", 0},
	                                {"maximum", 1000000},
	                                {"default", 1000}});
	source["properties"].push_back({{"id", "test.energy-eligible"}, {"kind", "flag"}, {"default", true}});
	source["effects"] = Json::array(
	    {{{"id", "test.energy-start"},
	      {"kind", "operations"},
	      {"operations",
	       Json::array(
	           {{{"kind", "property_add"}, {"target", "owner"}, {"property", "test.energy-starts"}, {"amount", 1}}})}},
	     {{"id", "test.energy-complete"},
	      {"kind", "operations"},
	      {"operations", Json::array({{{"kind", "property_add"},
	                                   {"target", "owner"},
	                                   {"property", "test.energy-completions"},
	                                   {"amount", 1}}})}},
	     {{"id", "test.finite-energy"},
	      {"kind", "process"},
	      {"native", "servicing_energy"},
	      {"start_effect", "test.energy-start"},
	      {"completion_effect", "test.energy-complete"},
	      {"entry_costs", Json::array({{{"payer", "owner"}, {"property", "test.energy-credit"}, {"amount", 1}}})}}});
	for (auto &effect : source["effects"])
		if (effect.at("id") == "test.finite-energy")
		{
			effect["rate"] = {{"fact", "owner.property"}, {"property", "test.energy-rate"}};
			effect["requirements"] = {
			    {"op", "eq"},
			    {"args", Json::array({{{"fact", "target.property"}, {"property", "test.energy-eligible"}}, true})}};
		}
	for (const auto *role : {"target.property", "subject.property"})
	{
		auto invalid = source;
		for (auto &effect : invalid["effects"])
			if (effect.at("id") == "test.finite-energy")
				effect["rate"] = {{"fact", role}, {"property", "test.energy-rate"}};
		rejects(
		    [&]
		    {
			    load(invalid);
		    },
		    "Shared bay rate rejects recipient-dependent facts before runtime");
	}
	const auto catalog = load(source);
	World world;
	world.load(catalog, "test.map", {{"crew", "test.away"}});
	const auto home = *world.ship_for_slot("crew");
	require(world.claim(101, home, "drive"), "Claim full-energy servicing fixture");
	ticks(world, 30);
	const auto full = world.save();
	require(finite_service_property(full, home, "test.energy-credit") == 10 &&
	            finite_service_property(full, home, "test.energy-starts") == 0 &&
	            finite_service_property(full, home, "test.energy-completions") == 0,
	        "Already-full craft create no servicing episode, entry charge or completion event");
	auto demand = full;
	EntityId craft = 0;
	for (const auto &entry : demand.at("craft").at("entities"))
		if (entry.at("state").at("home") == home)
		{
			craft = entry.at("id").get<EntityId>();
			break;
		}
	require(craft != 0, "Select the tested carrier's actual docked craft");
	for (auto &body : demand["bodies"])
		if (body.at("body").at("id") == craft)
		{
			auto &quantities = body["resources"]["network"]["stores"][0]["stock"]["quantities"];
			quantities.begin().value() = quantities.begin().value().get<std::int64_t>() - 10;
		}
	world.restore(catalog, demand);
	ticks(world, 30);
	const auto completed = world.save();
	require(finite_service_property(completed, home, "test.energy-credit") == 9 &&
	            finite_service_property(completed, home, "test.energy-starts") == 1 &&
	            finite_service_property(completed, home, "test.energy-completions") == 1,
	        "A genuine new energy deficit starts and completes exactly one servicing episode");
	ticks(world, 30);
	const auto maintained = world.save();
	require(finite_service_property(maintained, home, "test.energy-credit") == 9 &&
	            finite_service_property(maintained, home, "test.energy-starts") == 1 &&
	            finite_service_property(maintained, home, "test.energy-completions") == 1,
	        "Full serviced craft do not rearm or pay entry again on later ticks");
}
void finite_servicing_cases()
{
	finite_energy_servicing_cases();
	auto source = armed_craft_fixture();
	for (auto &entity : source["entities"])
		if (entity["systems"].contains("magazine"))
			entity["systems"]["magazine"]["initial"] = 0;
	auto &carrier = source["entities"][0];
	carrier["systems"]["bay"]["ammunition"][0]["rounds_per_second"] = 200;
	carrier["systems"]["bay"]["properties"] = {
	    {"test.service-credit", 2}, {"test.service-starts", 0}, {"test.service-completions", 0}};
	carrier["systems"]["bay"]["processes"] = {
	    {"ammo", {{"process", "test.finite-service"}, {"native_id", "bay.ammo-0"}}}};
	carrier["logistics"]["units_per_second"] = 1000;
	carrier["logistics"]["batch_size"] = 3;
	source["properties"] = Json::array();
	for (const auto *id : {"test.service-credit", "test.service-starts", "test.service-completions"})
		source["properties"].push_back(
		    {{"id", id}, {"kind", "quantity"}, {"minimum", 0}, {"maximum", 1000}, {"default", 0}});
	source["effects"] = Json::array(
	    {{{"id", "test.service-start"},
	      {"kind", "operations"},
	      {"operations",
	       Json::array(
	           {{{"kind", "property_add"}, {"target", "owner"}, {"property", "test.service-starts"}, {"amount", 1}}})}},
	     {{"id", "test.service-complete"},
	      {"kind", "operations"},
	      {"operations", Json::array({{{"kind", "property_add"},
	                                   {"target", "owner"},
	                                   {"property", "test.service-completions"},
	                                   {"amount", 1}}})}},
	     {{"id", "test.finite-service"},
	      {"kind", "process"},
	      {"native", "servicing_ammunition"},
	      {"shortage", "proportional"},
	      {"start_effect", "test.service-start"},
	      {"completion_effect", "test.service-complete"},
	      {"work_costs", Json::array({{{"payer", "owner"}, {"property", "test.service-credit"}, {"per_work", 1}}})}}});
	const auto catalog = load(source);
	World world;
	world.load(catalog, "test.map", {{"crew", "test.away"}});
	const auto home = *world.ship_for_slot("crew");
	require(world.claim(101, home, "drive"), "Claim finite servicing fixture");
	ticks(world, 20);
	const auto partial = world.save();
	if (finite_serviced_ammunition(partial, home) != 2 ||
	    finite_service_property(partial, home, "test.service-credit") != 0)
		throw std::runtime_error(
		    "Proportional servicing receives only the rounds actually funded: ammunition=" +
		    std::to_string(finite_serviced_ammunition(partial, home)) +
		    " credit=" + std::to_string(finite_service_property(partial, home, "test.service-credit")) +
		    " logistics=" + partial.at("logistics").dump() + " processes=" + partial.at("processes").dump());
	bool retained = false;
	for (const auto &batch : partial.at("logistics").at("batches"))
		retained |= batch.at("source") == home && !batch.at("service").at("bay").get<std::string>().empty() &&
		            batch.at("quantity") == 1;
	require(retained, "Partially delivered servicing batch keeps its exact unreceived remainder");
	require(finite_service_property(partial, home, "test.service-starts") == 2 &&
	            finite_service_property(partial, home, "test.service-completions") == 0,
	        "Interleaved craft retain their own admission without repeated entry or premature completion");
	World restored;
	restored.restore(catalog, partial);
	ticks(world, 3);
	ticks(restored, 3);
	require(world.save() == restored.save(), "Partial servicing reservations and target episodes restore exactly");
	auto supplied = world.save();
	for (auto &owner : supplied["custom_properties"])
		if (owner.at("entity") == home && owner.at("equipment") == "bay")
			owner["values"]["test.service-credit"] = 4;
	world.restore(catalog, supplied);
	restored.restore(catalog, supplied);
	ticks(world, 40);
	ticks(restored, 40);
	const auto completed = world.save();
	require(completed == restored.save() && finite_serviced_ammunition(completed, home) == 6 &&
	            finite_service_property(completed, home, "test.service-credit") == 0,
	        "Refilled process costs finish the same physical supplies without double charging");
	require(finite_service_property(completed, home, "test.service-starts") == 2 &&
	            finite_service_property(completed, home, "test.service-completions") == 2,
	        "Each serviced craft completes once and preserves its original entry episode");
}
