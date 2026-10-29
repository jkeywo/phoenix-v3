module;
#include <cstddef>
#include <functional>
#include <cstdint>
#include <map>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>
export module phoenix.scenario.interdictions;
export import phoenix.content.sensing.types;
export import phoenix.world.state;
export import phoenix.effects.contracts;
export namespace phoenix
{
struct InterdictionState
{
	std::vector<EntityId> ships;
	EntityId boarder = 0;
	std::string posture = "watch";
	std::uint64_t conversation = 0;
	std::int64_t progress = 0;
	bool attacked = false, reinforced = false, captured = false, blocked = false, finished = false;
	std::set<std::string> effects;
	std::vector<std::uint64_t> plea_messages;
	state_tree::Instance continuation;
};
struct InterdictionConversationReceipt
{
	std::string program;
	std::set<std::uint64_t> messages;
};
struct InterdictionRestoreFacts
{
	std::set<EntityId> entities, captured_targets;
	std::map<std::uint64_t, InterdictionConversationReceipt> conversations;
	std::map<std::string, std::string> legacy_signatures;
};
struct InterdictionShipRestore
{
	EntityId id = 0;
	std::string definition;
};
struct InterdictionOrderFacts
{
	EntityId player = 0, target_owner = 0;
	std::string player_definition;
	fixed::Vec player_position, target_position;
	bool target_captured = false, target_available = true;
	std::vector<EntityId> live_ships;
};
struct InterdictionShipFacts
{
	EntityId ship = 0;
	fixed::Vec position;
	std::optional<fixed::Vec> permitted_player_contact;
};
struct EncounterMovement
{
	fixed::Vec destination;
	std::int64_t stop_radius = 0, cruise_speed = 20000;
};
struct EncounterMotionDemand
{
	std::int64_t heading = 0, speed = 0;
};
struct InterdictionShipOrder
{
	EntityId ship = 0, weapon_target = 0;
	bool hold_if_docked = false, attack = false;
};
struct InterdictionDepartureOrder
{
	EntityId leader = 0, prize = 0;
	bool ready = false;
	std::set<EntityId> group;
	EncounterMovement movement;
};
EncounterMotionDemand encounter_motion_demand(const Pose &, const EncounterMovement &);
bool encounter_manages_action(OperatingAction);
struct InterdictionTickFacts
{
	EntityId player = 0;
	std::string player_definition, answer;
	std::vector<EntityId> live_ships;
	fixed::Vec player_position, target_position, boarder_position;
	bool target_lost = false, claimed = false, player_active = false, boarder_alive = false;
	bool dock_ready = false, boarder_docked = false, start = false;
};
struct InterdictionDialogue
{
	std::string program;
	std::map<std::string, std::string> roles;
	std::vector<std::string> participants;
};
enum class InterdictionReceiptKind
{
	Log,
	Emit,
	Taken
};
struct InterdictionReceipt
{
	InterdictionReceiptKind kind = InterdictionReceiptKind::Log;
	EntityId player = 0, target = 0;
	std::string encounter, text;
};
enum class InterdictionDockOrder
{
	Undock,
	Assist
};
// Logical encounter operations only. Implementations retain ordinary native
// Controller/docking/conversation/ownership paths; no World handle is exposed.
class InterdictionNative
{
public:
	virtual ~InterdictionNative();
	virtual InterdictionTickFacts observe(const InterdictionDefinition &, const InterdictionState &) const = 0;
	virtual EntityId spawn_ship(const InterdictionShip &) = 0;
	virtual std::uint64_t open_dialogue(const InterdictionDialogue &) = 0;
	virtual std::optional<std::uint64_t> report_dialogue(std::uint64_t, const std::string &) = 0;
	virtual void offer_dialogue(std::uint64_t, const std::string &) = 0;
	virtual void clear_prompt(std::uint64_t) = 0;
	virtual void expire_pleas(std::uint64_t, std::span<const std::uint64_t>) = 0;
	virtual bool take_over(const InterdictionDefinition &, EntityId boarder) = 0;
	virtual void publish(const InterdictionReceipt &) = 0;
	virtual void dock(EntityId, InterdictionDockOrder, EntityId target = 0) = 0;
};
class InterdictionBook
{
	std::vector<InterdictionDefinition> definitions_;
	std::vector<InterdictionState> states_;
	bool departure_members_allowed(std::size_t, EntityId leader, EntityId member, EntityId target_owner) const;
	bool obsolete_departure(std::size_t, EntityId leader, const std::set<EntityId> &group, EntityId target_owner) const;

public:
	void configure(std::vector<InterdictionDefinition>);
	std::size_t size() const;
	const InterdictionDefinition &definition(std::size_t) const;
	const InterdictionState &state(std::size_t) const;
	void advance(InterdictionNative &);
	bool docking_permission(EntityId ship, EntityId target) const;
	bool participant(std::size_t, EntityId) const;
	bool capture_receipt(EntityId target, EntityId owner) const;
	bool permits_departure_member(EntityId leader, EntityId member,
	                              const std::function<EntityId(EntityId)> &owner) const;
	bool obsolete_departure_group(EntityId leader, const std::set<EntityId> &group,
	                              const std::function<EntityId(EntityId)> &owner) const;
	void record_attack(std::size_t, EntityId source_owner, EntityId player_owner, EntityId target);
	void record_insertion(std::size_t, EntityId ship, EntityId player, EntityId target);
	Json save() const;
	void restore(const Json &, const InterdictionRestoreFacts &);
	std::vector<InterdictionShipRestore> restore_ships(const Json &rows, const Json &bodies,
	                                                   const std::map<std::string, std::string> &slot_fittings,
	                                                   const std::set<EntityId> &existing) const;
	InterdictionShipOrder ship_order(std::size_t encounter, const InterdictionOrderFacts &,
	                                 const InterdictionShipFacts &) const;
	EncounterMovement ship_movement(std::size_t encounter, std::size_t fleet_index, const InterdictionOrderFacts &,
	                                const InterdictionShipFacts &, const InterdictionShipOrder &) const;
	std::optional<InterdictionDepartureOrder> departure_order(std::size_t, const InterdictionOrderFacts &) const;
};
} // namespace phoenix
