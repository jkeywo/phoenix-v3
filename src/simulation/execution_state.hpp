#pragma once
namespace phoenix
{
// Both abilities and processes publish one frozen receipt after native work.
// Output is normalized activity; work is measured in the declared native unit.
struct ExecutionWorkReceipt
{
	std::string source, owner, family, definition;
	std::uint64_t activation = 0;
	std::int64_t output = 0;
	std::string execution;
	std::int64_t work = 0;
	std::string work_unit = "permille";
	std::uint64_t generation = 0;
	bool operator==(const ExecutionWorkReceipt &) const = default;
};
NLOHMANN_DEFINE_TYPE_NON_INTRUSIVE(ExecutionWorkReceipt, source, owner, family, definition, activation, output,
                                   execution, work, work_unit, generation)
} // namespace phoenix
