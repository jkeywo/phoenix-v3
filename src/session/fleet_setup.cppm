module;
#include <cstdint>
#include <map>
#include <memory>
#include <nlohmann/json.hpp>
#include <optional>
#include <span>
#include <string>
#include <vector>

export module phoenix.fleet_setup;
export import phoenix.session.local;
export import phoenix.fleet;

export namespace phoenix
{
struct PreparedFleet
{
	Json plan;
	std::string self;
	std::shared_ptr<HostAccess> world;
	Bytes bundle;
};
// A private setup bundle contains the exact packs and the fixed member credential.
// Only its inner gameplay archives may be advertised to player/GM clients.
Bytes export_fleet_setup(const HostAccess &source, const GameSetup &selection, const std::string &room);
PreparedFleet read_fleet_setup(const Bytes &archive, const std::string &member = {});
Bytes save_fleet_recovery(const HostAccess &source, const Json &plan);
} // namespace phoenix

export namespace phoenix
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
