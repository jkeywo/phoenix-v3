module;
#include <algorithm>
#include <cstdint>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

module phoenix.workshop_test;
import phoenix.simulation;
import phoenix.content.verification;

namespace phoenix
{
struct GameplayTestHost::Impl
{
	std::unique_ptr<World> world;
	std::optional<WorkshopTestRequest> snapshot;
	bool paused = true;
	std::uint32_t elapsed = 0;
	std::string status;
	std::vector<std::string> diagnostics;
	void start(WorkshopTestRequest request)
	{
		if (request.pack.empty() || request.mission.empty() || request.slot.empty() || request.ship.empty())
			throw std::runtime_error("Disposable test requires a verified pack, mission, slot and ship.");
		if (request.dependencies.size() > 64)
			throw std::runtime_error("Disposable test has more than 64 dependency packs.");
		std::size_t size = request.pack.size();
		for (const auto &dependency : request.dependencies)
			size += dependency.size();
		if (size > 128 * 1024 * 1024)
			throw std::runtime_error("Disposable test archives exceed 128 MiB.");
		std::vector<Pack> packs;
		for (const auto &dependency : request.dependencies)
			packs.push_back(read_pack(dependency));
		packs.push_back(read_pack(request.pack));
		auto catalog = resolve_packs(packs);
		if (!catalog.maps.contains(request.mission))
			throw std::runtime_error("Disposable test mission is unresolved.");
		if (!catalog.entities.contains(request.ship))
			throw std::runtime_error("Disposable test ship is unresolved.");
		bool bound = false;
		for (const auto &slot : catalog.maps.at(request.mission).at("ship_slots"))
			if (slot.at("id") == request.slot)
			{
				bound = true;
				if (std::find(slot.at("allowed_ships").begin(), slot.at("allowed_ships").end(), Json(request.ship)) ==
				    slot.at("allowed_ships").end())
					throw std::runtime_error("Selected ship is not permitted in the disposable mission slot.");
			}
		if (!bound)
			throw std::runtime_error("Disposable test slot is unresolved.");
		auto candidate = std::make_unique<World>();
		candidate->load(catalog, request.mission, {{request.slot, request.ship}});
		world = std::move(candidate);
		request.action = WorkshopTestAction::Start;
		snapshot = std::move(request);
		paused = false;
		elapsed = 0;
		diagnostics.clear();
		status = "Disposable test started.";
	}
};
GameplayTestHost::GameplayTestHost() : impl_(std::make_unique<Impl>())
{
}
GameplayTestHost::~GameplayTestHost() = default;
bool GameplayTestHost::submit(WorkshopTestRequest request)
{
	try
	{
		switch (request.action)
		{
		case WorkshopTestAction::Start:
			impl_->start(std::move(request));
			break;
		case WorkshopTestAction::Restart:
			if (!impl_->snapshot)
				throw std::runtime_error("No disposable test snapshot to restart.");
			impl_->start(*impl_->snapshot);
			impl_->status = "Disposable test restarted from its exported snapshot.";
			break;
		case WorkshopTestAction::Pause:
			if (!impl_->world)
				throw std::runtime_error("No disposable test is running.");
			impl_->paused = !impl_->paused;
			impl_->elapsed = 0;
			impl_->status = impl_->paused ? "Disposable test paused." : "Disposable test resumed.";
			break;
		case WorkshopTestAction::Step:
			if (!impl_->world)
				throw std::runtime_error("No disposable test is running.");
			impl_->paused = true;
			impl_->elapsed = 0;
			impl_->world->tick();
			impl_->status = "Disposable test advanced one tick.";
			break;
		case WorkshopTestAction::Stop:
			impl_->world.reset();
			impl_->snapshot.reset();
			impl_->paused = true;
			impl_->elapsed = 0;
			impl_->diagnostics.clear();
			impl_->status = "Disposable test stopped.";
			break;
		default:
			throw std::runtime_error("Unsupported disposable test operation.");
		}
		return true;
	}
	catch (const std::exception &error)
	{
		impl_->status = error.what();
		impl_->diagnostics.push_back(error.what());
		if (impl_->diagnostics.size() > 32)
			impl_->diagnostics.erase(impl_->diagnostics.begin());
		return false;
	}
}
void GameplayTestHost::advance(std::uint32_t elapsed_ms)
{
	if (!impl_->world || impl_->paused)
		return;
	impl_->elapsed += std::min(elapsed_ms, 250u);
	try
	{
		while (impl_->elapsed >= 50)
		{
			impl_->world->tick();
			impl_->elapsed -= 50;
		}
	}
	catch (const std::exception &error)
	{
		impl_->paused = true;
		impl_->elapsed = 0;
		impl_->status = error.what();
		impl_->diagnostics.push_back(error.what());
		if (impl_->diagnostics.size() > 32)
			impl_->diagnostics.erase(impl_->diagnostics.begin());
	}
}
Json GameplayTestHost::feedback() const
{
	Json result = impl_->world ? impl_->world->debug() : Json::object();
	result["running"] = impl_->world != nullptr;
	result["paused"] = impl_->paused;
	result["status"] = impl_->status;
	result["host_diagnostics"] = impl_->diagnostics;
	if (impl_->snapshot)
	{
		result["mission"] = impl_->snapshot->mission;
		result["slot"] = impl_->snapshot->slot;
		result["ship"] = impl_->snapshot->ship;
		if (impl_->world)
			if (const auto ship = impl_->world->ship_for_slot(impl_->snapshot->slot))
			{
				result["model_snapshots"] = Json::array();
				for (const auto &row : impl_->world->controller_ai(*ship))
					if (row.at("eligible").get<bool>() && !row.at("human").get<bool>() &&
					    result["model_snapshots"].size() < 4)
						result["model_snapshots"].push_back(impl_->world->controller_decision_snapshot(
						    {*ship, row.at("console"), row.at("controller")}));
			}
	}
	return result;
}
} // namespace phoenix
