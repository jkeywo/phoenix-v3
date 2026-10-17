#include "../../src/session/fleet_runtime.hpp"
#include <phoenix/wire.hpp>
#include <filesystem>
#include <chrono>
#include <thread>
#include <iostream>
using namespace phoenix;
void require(bool value, const char *message)
{
	if (!value)
		throw std::runtime_error(message);
}
struct Temporary
{
	std::filesystem::path root = std::filesystem::temp_directory_path() / ("phoenix-server-" + random_token());
	~Temporary()
	{
		std::error_code ignored;
		std::filesystem::remove_all(root, ignored);
	}
};
void drive(FleetRuntime &runtime, std::shared_ptr<HostAccess> &world)
{
	runtime.poll();
	if (runtime.failed())
		throw std::runtime_error(runtime.status());
	if (const auto *prepared = runtime.prepared())
		world = prepared->world;
	for (const auto &entry : runtime.take_committed())
	{
		if (entry.value.at("kind") == "fleet-presence")
			world->authoritative_world().release(1);
		if (entry.value.at("kind") != "fleet-frame")
			continue;
		const auto &value = entry.value.at("value");
		const auto target = *world->authoritative_world().ship_for_slot(world->setup().local_slot);
		const std::string kind = value.at("kind");
		if (kind == "claim")
			require(world->authoritative_world().claim(1, target), "Committed station claim failed");
		if (kind == "command")
			require(world->authoritative_world().submit(1, {target, "speed_mm_s", 1000}) &&
			            world->authoritative_world().submit(1, {target, "heading_mdeg", 90000}),
			        "Committed operating request failed");
		if (kind == "tick")
			world->tick();
	}
}
template <class Predicate> void until(Predicate done, const std::function<void()> &poll)
{
	const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
	while (!done())
	{
		if (std::chrono::steady_clock::now() > deadline)
			throw std::runtime_error("Durable server runtime integration timed out");
		poll();
		std::this_thread::sleep_for(std::chrono::milliseconds(3));
	}
}
int main()
{
	try
	{
		Temporary storage;
		Pack pack;
		pack.manifest = {{"schema", 1}, {"id", "test-core"}};
		pack.files["definitions.json"] = read_file(std::string(PHOENIX_FIXTURE_DIR) + "/definitions.json");
		pack.files["assets/pixel.png"] = read_file(std::string(PHOENIX_FIXTURE_DIR) + "/pixel.png");
		auto source = make_host_world();
		source->add_pack(write_pack(pack));
		const auto bundle = export_fleet_setup(*source, {"test.map", {{"test.slot", "test.ship"}}, "test.slot"},
		                                       "runtime-" + random_token());
		auto initial = read_fleet_setup(bundle);
		NetworkOptions options;
		options.link.url = "ws://127.0.0.1:9080/phoenix";
		options.link.room = initial.plan.at("room");
		options.link.self = initial.self;
		options.data_location = storage.root.string();
		const std::string session = initial.plan.at("session");
		std::shared_ptr<HostAccess> world;
		auto runtime = std::make_unique<FleetRuntime>(std::move(initial), options);
		require(!runtime->begun() && !runtime->available(),
		        "A verified bundle must not start or vote before explicit start");
		runtime->start();
		until(
		    [&]
		    {
			    return runtime->available();
		    },
		    [&]
		    {
			    drive(*runtime, world);
		    });
		const auto original = world->authoritative_world().inspect().back();
		bool accepted = false;
		until(
		    [&]
		    {
			    return accepted;
		    },
		    [&]
		    {
			    drive(*runtime, world);
			    accepted = runtime->propose({{"kind", "claim"}});
		    });
		until(
		    [&]
		    {
			    return world->authoritative_world().observe(1).has_value();
		    },
		    [&]
		    {
			    drive(*runtime, world);
		    });
		accepted = false;
		until(
		    [&]
		    {
			    return accepted;
		    },
		    [&]
		    {
			    drive(*runtime, world);
			    accepted = runtime->propose({{"kind", "command"}});
		    });
		for (unsigned tick = 1; tick <= 12; ++tick)
			until(
			    [&]
			    {
				    return world->ticks() >= tick;
			    },
			    [&]
			    {
				    drive(*runtime, world);
				    if (world->ticks() < tick)
					    runtime->propose_tick({{"kind", "tick"}});
			    });
		const auto saved = world->authoritative_world().save();
		const auto moving = world->authoritative_world().observe(1)->own;
		require(moving.x != original.x && moving.heading != original.heading,
		        "Durably committed requests must execute real motion");
		auto duplicate = std::make_unique<FleetRuntime>(read_fleet_setup(bundle), options);
		duplicate->start();
		until(
		    [&]
		    {
			    return duplicate->failed();
		    },
		    [&]
		    {
			    duplicate->poll();
		    });
		require(!duplicate->available(), "A second owner must not acquire the same server profile");
		duplicate.reset();
		runtime.reset();
		world.reset();
		runtime = std::make_unique<FleetRuntime>(options);
		until(
		    [&]
		    {
			    return world && world->ticks() == 12;
		    },
		    [&]
		    {
			    drive(*runtime, world);
		    });
		require(
		    world->authoritative_world().save() == saved,
		    "The exact initial bundle and committed journal must reconstruct the running world before re-admission");
		until(
		    [&]
		    {
			    return runtime->available();
		    },
		    [&]
		    {
			    drive(*runtime, world);
		    });
		require(!world->authoritative_world().observe(1),
		        "Returning server admission must invalidate its prior station control");
		runtime.reset();
		world.reset();
		std::filesystem::remove(storage.root / "fleet" / session / "ship1.journal");
		runtime = std::make_unique<FleetRuntime>(options);
		until(
		    [&]
		    {
			    return runtime->failed();
		    },
		    [&]
		    {
			    runtime->poll();
		    });
		require(!runtime->available(), "A sealed server identity must fail closed if its original journal is lost");
		std::cout << "PASS durable server identity, exclusive ownership, exact-world reconstruction and "
		             "missing-journal refusal\n";
	}
	catch (const std::exception &error)
	{
		std::cerr << "FAIL " << error.what() << '\n';
		return 1;
	}
}
