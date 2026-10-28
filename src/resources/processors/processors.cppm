module;
#include <nlohmann/json.hpp>
#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <vector>

export module phoenix.resources.processors;
export import phoenix.resources.stocks;

export namespace phoenix
{
inline constexpr ResourceAmount processor_ratio_period_limit = 4096;
struct RecipeDefinition
{
	std::string id, label;
	std::set<std::string> tags;
	std::map<std::string, ResourceAmount> inputs, outputs;
	ResourceAmount work = 1;
};
struct ProcessorDefinition
{
	std::string id, equipment, initial_recipe;
	std::vector<RecipeDefinition> recipes;
	std::map<std::string, std::string> inputs, outputs;
	ResourceAmount work_per_second = 20, energy_per_second = 0;
	bool continuous = false;
};
struct ProcessorState
{
	std::string recipe;
	std::uint64_t selection_revision = 1;
	ResourceAmount progress = 0, prepared_work = 0, rate_remainder = 0, funding_remainder = 0;
	ResourceAmount committed_batches = 0;
	std::map<std::string, ResourceAmount> input_paid;
	bool active = false;
};
using ProcessorStores = std::map<std::string, StockStore *>;
class ResourceProcessor
{
public:
	explicit ResourceProcessor(ProcessorDefinition definition);
	const ProcessorDefinition &definition() const;
	const ProcessorState &state() const;
	// Prepare commits entry ingredients and receiving claims for a batch, or
	// reserves the next exact continuous slice. No operating cost is paid here.
	ResourceAmount prepare(const ProcessorStores &, ResourceAmount maximum_work);
	void advance(const ProcessorStores &, ResourceAmount funded_work);
	bool select(const ProcessorStores &, const std::string &recipe);
	bool can_select(const ProcessorStores &, const std::string &recipe) const;
	ResourceAmount work_budget(ResourceAmount effective_rate);
	ResourceAmount milli_work_budget(ResourceAmount effective_milli_rate);
	ResourceAmount quoted_funded_work(ResourceAmount supply_permille) const;
	ResourceAmount funded_work(ResourceAmount supply_permille, ResourceAmount maximum_work = resource_limit);
	ResourceAmount operating_rate(ResourceAmount maximum_work) const;
	Json save() const;
	void restore(const Json &, const ProcessorStores &);
	std::string input_claim(const std::string &) const;
	std::string output_claim(const std::string &) const;

private:
	ProcessorDefinition definition_;
	ProcessorState state_;
	const RecipeDefinition &recipe() const;
};

} // namespace phoenix
