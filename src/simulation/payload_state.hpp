#pragma once
namespace phoenix
{
// Captured native outcome. Source identity is diagnostic/lifetime identity, never authority.
struct PayloadHitContext
{
	EntityId target = 0;
	PayloadOutcomeKind kind = PayloadOutcomeKind::Entity;
	std::int64_t compartment = -1;
	std::string equipment;
	std::uint64_t contribution = 0;
	std::string contribution_name;
	bool ordnance = false;
	bool operator==(const PayloadHitContext &) const = default;
};
} // namespace phoenix
