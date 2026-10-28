module;
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

export module phoenix.effects.clocks;
export import phoenix.effects.rule_state;

export namespace phoenix
{
struct RuleClockAddress
{
	EntityId ship = 0;
	std::uint64_t active = 0;
};
struct RuleClockStorage
{
	std::function<RuleRuntime *(EntityId, bool create)> runtime;
	std::function<ActiveRuleEffect *(std::uint64_t)> active;
};
struct RuleClockReadStorage
{
	std::function<const RuleRuntime *(EntityId)> runtime;
	std::function<const ActiveRuleEffect *(std::uint64_t)> active;
};
struct RuleClockSources
{
	std::function<void(const std::function<void(const RuleRuntime &)> &)> runtimes;
	std::function<void(const std::function<void(const ActiveRuleEffect &)> &)> active;
};
// A derived index only. Native RuleRuntime and ActiveRuleEffect records retain
// their authoritative clocks; addresses never hold ECS handles or pointers.
class RuleClockStore
{
public:
	void begin_rebuild();
	void index_runtime(EntityId owner, const RuleRuntime &);
	void index_active(const ActiveRuleEffect &);
	void finish_rebuild() const;
	std::size_t size() const;
	const std::map<std::string, RuleClockAddress> &addresses() const;
	std::optional<RuleClockAddress> address(const std::string &) const;
	RuleExecutionState *find(const std::string &, const RuleClockStorage &);
	const RuleExecutionState *read(const std::string &, const RuleClockReadStorage &) const;
	RuleExecutionState *ensure(const NativeRuleSource &, const RuleBinding &, const NativeRuleContext &, bool create,
	                           const RulesCatalog &, const std::map<std::string, PeriodicEffectDefinition> &,
	                           const RuleClockStorage &);
	void erase(const std::string &, const RuleClockStorage &);
	void forget(const std::string &);
	void invalidate_capacity();
	std::size_t capacity(const RuleClockSources &, const std::vector<ActiveRuleEffect> *prepared,
	                     const std::map<std::string, PeriodicEffectDefinition> &) const;

private:
	std::map<std::string, RuleClockAddress> addresses_;
	mutable std::optional<std::size_t> capacity_;
};
} // namespace phoenix
