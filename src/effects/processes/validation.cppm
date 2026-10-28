module;
#include <algorithm>
#include <cstdint>
#include <functional>
#include <map>
#include <set>
#include <span>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

export module phoenix.effects.process_validation;
export import phoenix.effects.processes;
export import phoenix.effects.continuation_validation;

export namespace phoenix
{
struct ProcessValidationDefinitions
{
	const ProcessCatalog &processes;
	const std::map<std::string, ProcessGrantDefinition> &grants;
};
struct ProcessValidationPorts
{
	std::function<ProcessRuntime()> initial;
	std::function<std::string(const ProcessGrantDefinition &)> native_binding;
};
void validate_process_runtime(const CustomPropertyAddress &, const ProcessRuntime &, ProcessValidationDefinitions,
                              const ProcessValidationPorts &, const RuleContinuationValidator &, std::int64_t tick);
} // namespace phoenix
