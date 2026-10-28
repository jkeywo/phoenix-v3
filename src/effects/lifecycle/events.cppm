module;
#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

export module phoenix.effects.events;
export import phoenix.effects.rule_state;
import phoenix.effects.rules;

export namespace phoenix
{
// Native adapters freeze facts, execute physical outcomes and reconcile lost owners.
// The queue owns scheduling/identity/capacity; adapters never mutate its journal directly.
struct RuleEventCallbacks
{
	std::function<RuleEvaluation(const PendingRuleExecution &)> freeze;
	std::function<std::string(const PendingRuleExecution &, const RuleEvaluation &)> execute;
	std::function<void()> settled;
};
class RuleEventQueue
{
public:
	bool enqueue(const std::string &rule, const std::string &source, const NativeRuleContext &, RuleEventPhase,
	             std::int64_t due);
	bool can_schedule_damage(std::size_t count) const;
	void schedule_damage(PendingRuleExecution);
	void drain(RuleEventPhase, std::int64_t tick, const RuleEventCallbacks &);
	void record(std::uint64_t id, std::string status);
	std::uint64_t next_identity() const;
	std::uint64_t claim_identity();
	const std::vector<PendingRuleExecution> &pending() const;
	const std::map<std::uint64_t, std::string> &results() const;
	void restore(std::vector<PendingRuleExecution>, std::map<std::uint64_t, std::string>, std::uint64_t next);

private:
	std::vector<PendingRuleExecution> pending_;
	std::map<std::uint64_t, std::string> results_;
	std::uint64_t next_ = 1;
	std::size_t draining_ = 0;
};
} // namespace phoenix
