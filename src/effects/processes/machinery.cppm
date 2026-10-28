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

export module phoenix.effects.machinery;
export import phoenix.effects.funding;
export import phoenix.world.state;

export namespace phoenix
{
std::string_view native_process_kind_name(NativeProcessKind);
std::string process_native_identity(const CustomPropertyAddress &, const ProcessState &, const ProcessDefinition &);
struct NativeProcessView
{
	CustomPropertyAddress owner;
	const ProcessState *state = nullptr;
};
void consider_native_process(NativeProcessView &, const CustomPropertyAddress &, const ProcessRuntime &,
                             const ProcessCatalog &, std::string_view kind, const std::string &native);
struct ProcessMachineryQuote
{
	CustomPropertyAddress owner;
	ProcessState frozen;
	NativeProcessKind kind = NativeProcessKind::Effects;
	std::string native;
	std::int64_t full_work = 0, quoted_work = -1, work = 0, limit = resource_limit, activity = 0;
	std::int64_t power_rate = 0, applied_power_rate = 0;
	std::int64_t basic_available = 0, basic_full = 0;
	std::optional<Engine> basic_engine;
	std::optional<Steering> basic_steering;
	bool actual_recorded = false;
	bool discrete_request = false;
};
std::int64_t native_process_price(const ProcessMachineryQuote &);
using MachineryProcessRead = std::function<const ProcessState *(const CustomPropertyAddress &, const std::string &)>;
class ProcessMachineryBook
{
public:
	void begin();
	void add_owner(const CustomPropertyAddress &, const ProcessRuntime &, const ProcessCatalog &);
	std::span<ProcessMachineryQuote> rows();
	std::span<const ProcessMachineryQuote> rows() const;
	ProcessMachineryQuote *find(EntityId, std::string_view kind, const std::string &native);
	const ProcessMachineryQuote *find(EntityId, std::string_view kind, const std::string &native) const;
	std::optional<std::int64_t> rate(EntityId, std::string_view kind, const std::string &native) const;
	std::int64_t paid(EntityId, std::string_view kind, const std::string &native, std::int64_t) const;
	std::int64_t work_limit(EntityId, std::string_view kind, const std::string &native) const;
	void record(EntityId, std::string_view kind, const std::string &native, std::int64_t available,
	            std::int64_t completed);
	bool capture(ProcessMachineryQuote &, std::int64_t available, std::int64_t full, bool stage);
	void reset_caps(const std::map<std::string, RuleUpkeepPlan> &);
	bool cap(const std::map<std::string, RuleUpkeepPlan> &, const MachineryProcessRead &, bool producers_only = false);

private:
	std::vector<ProcessMachineryQuote> quotes_;
	std::set<std::string> rejected_inputs_;
};
} // namespace phoenix
