module;
#include <flecs.h>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <span>
#include <string>
#include <vector>

export module phoenix.effects.active_rules;
export import phoenix.effects.clocks;

export namespace phoenix
{
using RuleTargetResolver = std::function<flecs::entity(const PayloadHitContext &)>;
using RuleTargetValidity = std::function<bool(const PayloadHitContext &, flecs::entity)>;
// Native property-rate bookkeeping and physical grant withdrawal are the only
// mutation notifications. Query membership and condition clocks stay effects-owned.
struct ActiveRuleMutationObserver
{
	std::function<void(const ActiveRuleEffect &)> before_removal;
};
struct ActiveRuleRetirementObserver
{
	std::function<void(RuleExecutionState &)> withdraw;
	std::function<void()> transaction_changed;
};
class ActiveRuleStore
{
public:
	void initialise(flecs::world &);
	const std::vector<flecs::entity> &rows() const;
	std::vector<ActiveRuleEffect> snapshot() const;
	flecs::entity find(std::uint64_t) const;
	std::size_t size() const;
	std::uint64_t next_identity() const;
	void set_next_identity(std::uint64_t);
	void erase(flecs::entity, RuleClockStore &, const ActiveRuleMutationObserver &);
	std::size_t erase_if(const std::function<bool(const ActiveRuleEffect &)> &, RuleClockStore &,
	                     const ActiveRuleMutationObserver &);
	void commit(flecs::world &, std::span<const ActiveRuleEffect>, const RuleTargetResolver &, RuleClockStore &,
	            const ActiveRuleMutationObserver &);
	bool exists(const std::vector<ActiveRuleEffect> *prepared, const std::string &source, EntityId target) const;
	bool target_available(const ActiveRuleEffect &, const RuleTargetValidity &) const;
	void retire_expired(std::int64_t now, const RuleTargetValidity &, RuleClockStore &,
	                    const ActiveRuleMutationObserver &, const ActiveRuleRetirementObserver &);

private:
	flecs::query<const ActiveRuleEffect> query_;
	std::map<std::uint64_t, flecs::entity> index_;
	mutable std::vector<flecs::entity> order_;
	mutable bool order_dirty_ = true;
	std::uint64_t next_ = 1;
};
} // namespace phoenix
