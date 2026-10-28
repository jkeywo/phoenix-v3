module;
#include <cstddef>
#include <cstdint>
#include <nlohmann/json.hpp>
#include <string>

export module phoenix.gameplay.ftl;
export import phoenix.resources.equipment;

export namespace phoenix
{
struct FtlReserve
{
	FtlDefinition definition;
	std::int64_t discharge_remainder = 0;
};
// Frozen crew evidence at departure, never a live scenario truth projection.
struct MissionDeparture
{
	Json report;
};
struct FtlEquipmentFacts
{
	bool enabled = false, structurally_disabled = false, destroyed = false;
	std::int64_t fault_effectiveness = 0, process_rate = 0;
};
void prepare_ftl_reserve(const FtlReserve &, ShipResources &, std::size_t resource_slot, FtlEquipmentFacts);
void actuate_ftl_reserve(FtlReserve &, ResourceNetwork &, std::int64_t ticks_per_second);
std::string ftl_reserve_blocker(const FtlReserve &, const ShipResources &, std::size_t resource_slot, FtlEquipmentFacts,
                                bool displacement_transfer_active);
bool ftl_reserve_ready(const FtlReserve &, const ResourceNetwork &);
} // namespace phoenix
