module;
#include <algorithm>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

module phoenix.effects.machinery;

namespace phoenix
{
std::string_view native_process_kind_name(NativeProcessKind kind)
{
	switch (kind)
	{
	case NativeProcessKind::Generation:
		return "generation";
	case NativeProcessKind::Propulsion:
		return "propulsion";
	case NativeProcessKind::Cooling:
		return "cooling";
	case NativeProcessKind::Processor:
		return "processor";
	case NativeProcessKind::Repair:
		return "repair";
	case NativeProcessKind::Medical:
		return "medical";
	case NativeProcessKind::Recovery:
		return "recovery";
	case NativeProcessKind::ServicingEnergy:
		return "servicing_energy";
	case NativeProcessKind::ServicingAmmunition:
		return "servicing_ammunition";
	case NativeProcessKind::Ftl:
		return "ftl";
	default:
		return "effects";
	}
}
std::string process_native_identity(const CustomPropertyAddress &address, const ProcessState &state,
                                    const ProcessDefinition &definition)
{
	if (definition.native == NativeProcessKind::Effects)
		return {};
	const auto native = state.native_id.empty() ? address.equipment : state.native_id;
	return std::to_string(address.entity) + ":" + std::string(native_process_kind_name(definition.native)) + ":" +
	       native;
}
std::int64_t native_process_price(const ProcessMachineryQuote &quote)
{
	if (quote.limit == resource_limit)
		return 1000;
	return quote.full_work ? std::min<std::int64_t>(1000, quote.limit * 1000 / quote.full_work) : 0;
}
void consider_native_process(NativeProcessView &result, const CustomPropertyAddress &address,
                             const ProcessRuntime &runtime, const ProcessCatalog &catalog, std::string_view kind,
                             const std::string &native)
{
	for (const auto &[id, state] : runtime.processes)
	{
		const auto &definition = catalog.at(state.definition);
		const auto native_id = state.native_id.empty() ? address.equipment : state.native_id;
		if (native_process_kind_name(definition.native) != kind || native_id != native || !state.admitted ||
		    !state.requested || !state.blocker.empty())
			continue;
		if (result.state)
			throw std::runtime_error("Duplicate native process driver");
		result = {address, &state};
	}
}
void ProcessMachineryBook::begin()
{
	quotes_.clear();
	rejected_inputs_.clear();
}
void ProcessMachineryBook::add_owner(const CustomPropertyAddress &address, const ProcessRuntime &runtime,
                                     const ProcessCatalog &catalog)
{
	for (const auto &[id, state] : runtime.processes)
		if (state.admitted && state.requested && state.blocker.empty())
		{
			const auto &definition = catalog.at(state.definition);
			if (definition.native == NativeProcessKind::Repair || definition.native == NativeProcessKind::Medical ||
			    definition.native == NativeProcessKind::Recovery ||
			    definition.native == NativeProcessKind::ServicingEnergy ||
			    definition.native == NativeProcessKind::ServicingAmmunition)
				continue;
			const auto native = state.native_id.empty() ? address.equipment : state.native_id;
			quotes_.push_back({address, state, definition.native, native});
		}
}
std::span<ProcessMachineryQuote> ProcessMachineryBook::rows()
{
	return quotes_;
}
std::span<const ProcessMachineryQuote> ProcessMachineryBook::rows() const
{
	return quotes_;
}
ProcessMachineryQuote *ProcessMachineryBook::find(EntityId entity, std::string_view kind, const std::string &native)
{
	for (auto &quote : quotes_)
		if (quote.owner.entity == entity && native_process_kind_name(quote.kind) == kind && quote.native == native)
			return &quote;
	return nullptr;
}
const ProcessMachineryQuote *ProcessMachineryBook::find(EntityId entity, std::string_view kind,
                                                        const std::string &native) const
{
	for (const auto &quote : quotes_)
		if (quote.owner.entity == entity && native_process_kind_name(quote.kind) == kind && quote.native == native)
			return &quote;
	return nullptr;
}
std::optional<std::int64_t> ProcessMachineryBook::rate(EntityId entity, std::string_view kind,
                                                       const std::string &native) const
{
	const auto *quote = find(entity, kind, native);
	return quote ? std::optional<std::int64_t>{quote->limit ? quote->frozen.rate_permille : 0} : std::nullopt;
}
std::int64_t ProcessMachineryBook::paid(EntityId entity, std::string_view kind, const std::string &native,
                                        std::int64_t paid) const
{
	const auto *quote = find(entity, kind, native);
	return quote ? paid * native_process_price(*quote) / 1000 : 0;
}
std::int64_t ProcessMachineryBook::work_limit(EntityId entity, std::string_view kind, const std::string &native) const
{
	const auto *quote = find(entity, kind, native);
	return quote ? quote->limit : 0;
}
void ProcessMachineryBook::record(EntityId entity, std::string_view kind, const std::string &native,
                                  std::int64_t available, std::int64_t completed)
{
	if (auto *quote = find(entity, kind, native))
	{
		quote->work = quote->actual_recorded ? std::max(quote->work, completed) : completed;
		quote->actual_recorded = true;
		if (!quote->full_work)
			quote->full_work = available;
		quote->activity = quote->full_work ? std::min<std::int64_t>(1000, quote->work * 1000 / quote->full_work) : 0;
	}
}
bool ProcessMachineryBook::capture(ProcessMachineryQuote &quote, std::int64_t available, std::int64_t full, bool stage)
{
	if (!quote.full_work)
		quote.full_work = full;
	available = std::min(available, quote.limit);
	quote.work = available;
	quote.activity = quote.full_work ? std::min<std::int64_t>(1000, available * 1000 / quote.full_work) : 0;
	if (stage && quote.quoted_work != available)
	{
		quote.quoted_work = available;
		return true;
	}
	return false;
}
void ProcessMachineryBook::reset_caps(const std::map<std::string, RuleUpkeepPlan> &plans)
{
	for (auto &quote : quotes_)
	{
		const auto permanent = [&](bool work)
		{
			const auto found = plans.find(process_funding_key(quote.owner, quote.frozen, work));
			return found != plans.end() && found->second.rejected && !found->second.provisional_shortage &&
			       !found->second.dependency_suspended;
		};
		quote.limit = permanent(false) || permanent(true) ? 0 : resource_limit;
		quote.quoted_work = -1;
	}
}
bool ProcessMachineryBook::cap(const std::map<std::string, RuleUpkeepPlan> &plans, const MachineryProcessRead &read,
                               bool producers_only)
{
	bool admission_changed = false;
	for (const auto &[key, plan] : plans)
		if (plan.rejected && !plan.provisional_shortage && !plan.dependency_suspended &&
		    rejected_inputs_.insert(key).second)
			admission_changed = true;
	if (admission_changed)
	{
		reset_caps(plans);
		return true;
	}
	bool changed = false;
	for (auto &quote : quotes_)
	{
		if (producers_only && quote.kind != NativeProcessKind::Generation)
			continue;
		const auto output = [&](bool work)
		{
			const auto *state = read(quote.owner, quote.frozen.id);
			if (!state)
				return std::int64_t{0};
			const auto found = plans.find(process_funding_key(quote.owner, *state, work));
			return found == plans.end() || found->second.rejected ? std::int64_t{0} : found->second.payment.output;
		};
		const auto idle = output(false), work = output(true);
		auto maximum = quote.limit;
		if (idle < 1000)
			maximum = std::min(maximum, quote.full_work * idle / 1000);
		if (work < 1000)
			maximum = std::min(maximum, std::max<std::int64_t>(0, quote.quoted_work) * work / 1000);
		if (maximum != quote.limit)
		{
			quote.limit = maximum;
			changed = true;
		}
	}
	return changed;
}

} // namespace phoenix
