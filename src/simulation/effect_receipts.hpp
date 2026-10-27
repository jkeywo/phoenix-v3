#pragma once
#include <set>
#include <stdexcept>
#include <string>
namespace phoenix
{
inline constexpr std::size_t effect_receipt_limit = 65536;
// Stable identity within one tree instance, including reference-call activations.
// Scope separates independently restarted actions that share a Controller journal.
inline std::string tree_effect_identity(const state_tree::Output &output, const std::string &scope = {})
{
	std::string receipt = scope + std::to_string(output.activation) + "/" + std::to_string(output.task);
	for (std::size_t i = 0; i < output.call_path.size(); ++i)
		receipt += "/" + std::to_string(output.call_path[i]) + ":" + std::to_string(output.call_activations.at(i));
	return receipt;
}
inline bool admit_effect(std::set<std::string> &receipts, const state_tree::Output &output,
                         const std::string &scope = {})
{
	auto receipt = tree_effect_identity(output, scope);
	if (receipts.contains(receipt))
		return false;
	if (receipts.size() >= effect_receipt_limit)
		throw std::runtime_error("Tree effect journal exhausted");
	receipts.insert(std::move(receipt));
	return true;
}
} // namespace phoenix
