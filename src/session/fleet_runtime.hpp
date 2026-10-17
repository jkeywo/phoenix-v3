#pragma once
#include "fleet_setup.hpp"
#include <phoenix/fleet.hpp>
namespace phoenix
{
// Private server composition: an exact initial bundle plus its durable committed log.
// Identity records are sealed before the fleet driver can advertise or vote.
class FleetRuntime
{
public:
	FleetRuntime(PreparedFleet prepared, NetworkOptions options);
	explicit FleetRuntime(NetworkOptions recovery);
	~FleetRuntime();
	void start();
	void poll();
	const PreparedFleet *prepared() const;
	bool begun() const;
	bool failed() const;
	bool available() const;
	bool leader() const;
	bool started() const;
	bool propose(const Json &value);
	bool propose_tick(const Json &value);
	std::vector<CommittedEntry> take_committed();
	std::string status() const;

private:
	struct Impl;
	std::unique_ptr<Impl> impl_;
};
} // namespace phoenix
