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
};
struct PeriodicEffectInstance
{
	std::uint64_t id = 0;
	EntityId original_source = 0;
	std::string source, definition, group_source, original_equipment;
	PayloadHitContext context;
	std::int64_t expiry = 0, next = 0;
};
// Mutable periodic execution state; identity, lifetime and resolved target are
// shared ECS components. PeriodicEffectInstance is the transaction/save record.
struct PeriodicSchedule
{
	std::int64_t next = 0;
	EntityId original_source = 0;
	std::string group_source, original_equipment;
	PayloadHitContext context;
};
struct PendingPayload
{
	std::uint64_t id = 0;
	EntityId original_source = 0;
	std::string source, payload, binding, original_equipment;
	PayloadHitContext context;
	std::int64_t due = 0;
};
} // namespace phoenix
