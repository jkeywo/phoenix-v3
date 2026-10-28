module;
#include <flecs.h>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

export module phoenix.effects.native;
export import phoenix.effects.contracts;
export import phoenix.effects.processes;
export import phoenix.gameplay.fitting;
export import phoenix.gameplay.personnel.state;
export import phoenix.gameplay.logistics.state;

export namespace phoenix
{
enum class NamedStockKind
{
	Physical,
	Electrical,
	Coolant
};
struct NamedStock
{
	StockStore &stock;
	std::string canonical;
	NamedStockKind kind;
	ResourceIndex index = no_resource;
};
NamedStock named_stock(flecs::entity, const LogisticsDefinition &, const std::string &name,
                       ResourceNetwork *prepared_network = nullptr);
CustomPropertyState *custom_property_state(flecs::entity, const CustomPropertyAddress &, const PersonnelDefinition &);
ProcessRuntime *process_runtime(flecs::entity, const CustomPropertyAddress &, const PersonnelDefinition &);
std::vector<CustomPropertyAddress> custom_property_addresses(flecs::entity, const PersonnelDefinition &);
bool effect_group_equipment_available(flecs::entity, const std::string &equipment, bool unavailable);
} // namespace phoenix
