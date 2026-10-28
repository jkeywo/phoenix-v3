module;
#include <algorithm>
#include <cstdint>
#include <map>
#include <string>
#include <utility>
#include <vector>

module phoenix.effects.events;

namespace phoenix
{
bool RuleEventQueue::enqueue(const std::string &rule, const std::string &source, const NativeRuleContext &context,
                             RuleEventPhase phase, std::int64_t due)
{
	if (next_ >= 9007199254740991ULL)
		return false;
	const auto id = next_++;
	if (pending_.size() + draining_ >= rule_pending_limit)
	{
		record(id, "failed: rule queue capacity exhausted");
		return false;
	}
	pending_.push_back({id, due, phase, rule, source, 0, context});
	return true;
}
bool RuleEventQueue::can_schedule_damage(std::size_t count) const
{
	return count <= rule_pending_limit - std::min(rule_pending_limit, pending_.size() + draining_) &&
	       next_ + count < 9007199254740991ULL;
}
void RuleEventQueue::schedule_damage(PendingRuleExecution job)
{
	job.id = next_++;
	pending_.push_back(std::move(job));
}
void RuleEventQueue::drain(RuleEventPhase phase, std::int64_t tick, const RuleEventCallbacks &callbacks)
{
	std::vector<PendingRuleExecution> due;
	std::erase_if(pending_,
	              [&](const auto &job)
	              {
		              if (job.phase != phase || job.due > tick)
			              return false;
		              due.push_back(job);
		              return true;
	              });
	std::sort(due.begin(), due.end(),
	          [](const auto &a, const auto &b)
	          {
		          return a.id < b.id;
	          });
	std::vector<RuleEvaluation> frozen;
	frozen.reserve(due.size());
	for (const auto &job : due)
		frozen.push_back(job.rule.empty() ? RuleEvaluation{} : callbacks.freeze(job));
	draining_ = due.size();
	for (std::size_t i = 0; i < due.size(); ++i)
	{
		--draining_;
		record(due[i].id, callbacks.execute(due[i], frozen[i]));
	}
	callbacks.settled();
}
void RuleEventQueue::record(std::uint64_t id, std::string status)
{
	record_effect_result(results_, id, std::move(status));
}
std::uint64_t RuleEventQueue::next_identity() const
{
	return next_;
}
std::uint64_t RuleEventQueue::claim_identity()
{
	return next_++;
}
const std::vector<PendingRuleExecution> &RuleEventQueue::pending() const
{
	return pending_;
}
const std::map<std::uint64_t, std::string> &RuleEventQueue::results() const
{
	return results_;
}
void RuleEventQueue::restore(std::vector<PendingRuleExecution> pending, std::map<std::uint64_t, std::string> results,
                             std::uint64_t next)
{
	pending_ = std::move(pending);
	results_ = std::move(results);
	next_ = next;
	draining_ = 0;
}
} // namespace phoenix
