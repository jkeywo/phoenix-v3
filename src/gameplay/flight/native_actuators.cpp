module;
#include <flecs.h>
#include <algorithm>
#include <cstdint>
#include <cstddef>
#include <cstdlib>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>
module phoenix.gameplay.flight.native;
import phoenix.effects.properties;
namespace phoenix
{
namespace
{
constexpr std::int64_t rate = simulation_tick_hz;
static_assert(simulation_tick_hz == resource_tick_hz);
} // namespace
struct NativeFlightActuators::Impl
{
	struct Phase;
	flecs::world &ecs;
	NativeFlightPorts ports;
	flecs::query<const Equipment, Steering> steering_query;
	flecs::query<const Equipment, Engine> engine_query;
	flecs::query<const Equipment, FlightRuntime> flight_query;
	flecs::query<const Equipment> advanced_equipment_query;
	Impl(flecs::world &world, NativeFlightPorts callbacks) : ecs(world), ports(std::move(callbacks))
	{
		steering_query = ecs.query_builder<const Equipment, Steering>()
		                     .with<BasicEquipment>()
		                     .with(flecs::ChildOf)
		                     .second("$ship")
		                     .cached()
		                     .build();
		engine_query = ecs.query_builder<const Equipment, Engine>()
		                   .with<BasicEquipment>()
		                   .with(flecs::ChildOf)
		                   .second("$ship")
		                   .cached()
		                   .build();
		flight_query =
		    ecs.query_builder<const Equipment, FlightRuntime>().with(flecs::ChildOf).second("$ship").cached().build();
		advanced_equipment_query = ecs.query_builder<const Equipment>()
		                               .with<AdvancedEquipment>()
		                               .with(flecs::ChildOf)
		                               .second("$ship")
		                               .cached()
		                               .build();
	}
	template <typename... Components>
	std::vector<flecs::entity> ordered_equipment(const flecs::query<Components...> &query,
	                                             flecs::entity ship = {}) const
	{
		std::vector<flecs::entity> rows;
		auto iterator = query.iter();
		if (ship)
			iterator.set_var("ship", ship);
		iterator.run(
		    [&](flecs::iter &it)
		    {
			    while (it.next())
				    for (auto row : it)
				    {
					    auto entity = it.entity(row);
					    rows.push_back(entity);
				    }
		    });
		std::sort(rows.begin(), rows.end(),
		          [](flecs::entity a, flecs::entity b)
		          {
			          const auto &x = a.get<Equipment>();
			          const auto &y = b.get<Equipment>();
			          return std::pair{a.parent().get<StableId>().id, x.ordinal} <
			                 std::pair{b.parent().get<StableId>().id, y.ordinal};
		          });
		return rows;
	}
};
// One synchronous phase borrows its book; the query owner retains no book reference.
struct NativeFlightActuators::Impl::Phase
{
	Impl &owner;
	ProcessMachineryBook &process_machinery;
	NativeFlightPorts &ports;
	const flecs::query<const Equipment, Steering> &steering_query;
	const flecs::query<const Equipment, Engine> &engine_query;
	const flecs::query<const Equipment, FlightRuntime> &flight_query;
	const flecs::query<const Equipment> &advanced_equipment_query;
	Phase(Impl &queries, ProcessMachineryBook &book)
	    : owner(queries), process_machinery(book), ports(queries.ports), steering_query(queries.steering_query),
	      engine_query(queries.engine_query), flight_query(queries.flight_query),
	      advanced_equipment_query(queries.advanced_equipment_query)
	{
	}
	bool structurally_disabled(flecs::entity ship, const std::string &equipment) const
	{
		return ports.structurally_disabled(ship.get<StableId>().id, equipment);
	}
	bool dock_attached(EntityId ship) const
	{
		return ports.docked(ship);
	}
	std::int64_t effective_property(flecs::entity ship, const std::string &equipment, const std::string &property,
	                                std::int64_t base, std::int64_t extra = 0) const
	{
		return ports.property(ship.get<StableId>().id, equipment, property_key(property), base, extra);
	}
	std::int64_t native_process_rate(EntityId actor, std::string_view kind, const std::string &equipment) const
	{
		if (const auto quote = process_machinery.rate(actor, kind, equipment))
			return *quote;
		return ports.propulsion_rate(actor, equipment);
	}
	std::int64_t native_process_paid(EntityId actor, std::string_view kind, const std::string &equipment,
	                                 std::int64_t paid) const
	{
		return process_machinery.paid(actor, kind, equipment, paid);
	}
	std::int64_t native_process_work_limit(EntityId actor, std::string_view kind, const std::string &equipment) const
	{
		return process_machinery.work_limit(actor, kind, equipment);
	}
	ProcessMachineryQuote *native_machinery_quote(EntityId actor, std::string_view kind, const std::string &equipment)
	{
		return process_machinery.find(actor, kind, equipment);
	}
	void record_native_process_work(EntityId actor, std::string_view kind, const std::string &equipment,
	                                std::int64_t available, std::int64_t completed)
	{
		process_machinery.record(actor, kind, equipment, available, completed);
	}
	Steering effective_basic_steering(flecs::entity child) const
	{
		auto effective = child.get<Steering>();
		const auto ship = child.parent();
		const auto &equipment = child.get<Equipment>().id;
		effective.rate = effective_property(ship, equipment, "turn", effective.rate);
		effective.rate = apply_property_delta(
		    effective.rate, native_process_rate(ship.get<StableId>().id, "propulsion", equipment) - 1000,
		    resource_limit);
		return effective;
	}
	Engine effective_basic_engine(flecs::entity child) const
	{
		auto effective = child.get<Engine>();
		const auto ship = child.parent();
		const auto &equipment = child.get<Equipment>().id;
		effective.enabled &= !structurally_disabled(ship, equipment);
		effective.acceleration = effective_property(ship, equipment, "propulsion", effective.acceleration);
		effective.max_speed = effective_property(ship, equipment, "speed", effective.max_speed);
		effective.speed = effective.speed == child.get<Engine>().max_speed
		                      ? effective.max_speed
		                      : std::min(effective.speed, effective.max_speed);
		effective.acceleration = apply_property_delta(
		    effective.acceleration, native_process_rate(ship.get<StableId>().id, "propulsion", equipment) - 1000,
		    resource_limit);
		return effective;
	}
	void run_basic_steering()
	{
		for (auto child : owner.ordered_equipment(steering_query))
			if (auto ship = child.parent(); ship.has<Movable>() && !dock_attached(ship.get<StableId>().id) &&
			                                !structurally_disabled(ship, child.get<Equipment>().id))
			{
				auto &native = child.get_mut<Steering>();
				const auto *quote =
				    native_machinery_quote(ship.get<StableId>().id, "propulsion", child.get<Equipment>().id);
				auto effective =
				    quote && quote->basic_steering ? *quote->basic_steering : effective_basic_steering(child);
				const auto paid =
				    native_process_paid(ship.get<StableId>().id, "propulsion", child.get<Equipment>().id, 1000);
				const bool active = native.heading != ship.get<Pose>().heading;
				step_basic_steering(ship.get_mut<Pose>(), effective, paid);
				native.remainder = effective.remainder;
				record_native_process_work(ship.get<StableId>().id, "propulsion", child.get<Equipment>().id,
				                           active ? 1000 : 0, active ? paid : 0);
			}
	}
	void run_basic_engines()
	{
		for (auto child : owner.ordered_equipment(engine_query))
			if (auto ship = child.parent(); ship.has<Movable>() && !dock_attached(ship.get<StableId>().id))
			{
				auto &native = child.get_mut<Engine>();
				const auto *quote =
				    native_machinery_quote(ship.get<StableId>().id, "propulsion", child.get<Equipment>().id);
				auto effective = quote && quote->basic_engine ? *quote->basic_engine : effective_basic_engine(child);
				const auto &equipment = child.get<Equipment>().id;
				step_basic_engine(ship.get<Pose>(), ship.get_mut<Motion>(), effective,
				                  native_process_paid(ship.get<StableId>().id, "propulsion", equipment, 1000));
				native.remainder = effective.remainder;
				native.damping_remainder = effective.damping_remainder;
				native.forward_output = effective.forward_output;
				record_native_process_work(ship.get<StableId>().id, "propulsion", equipment, 1000,
				                           effective.forward_output);
			}
	}
	void cancel_ship_impulse(flecs::entity ship)
	{
		for (auto child : owner.ordered_equipment(flight_query, ship))
		{
			auto &state = child.get_mut<FlightRuntime>().state;
			if (state.impulse != ImpulsePhase::Idle)
				cancel_impulse(state, 5);
			state.external_impulse = false;
		}
	}
	FlightRestrictions flight_restrictions(flecs::entity ship)
	{
		FlightRestrictions result;
		for (auto child : owner.ordered_equipment(flight_query, ship))
		{
			const auto &flight = child.get<FlightRuntime>();
			if (flight.state.impulse != ImpulsePhase::Idle)
			{
				result.impulse = true;
				if (flight.state.impulse == ImpulsePhase::Active)
					result.steering_limit =
					    std::min(result.steering_limit, static_cast<int>(flight.definition.impulse_steering_permille));
			}
		}
		return result;
	}
	FlightDefinition effective_flight_definition(flecs::entity child, FlightRestrictions restrictions,
	                                             std::int64_t speed = 0, std::int64_t acceleration = 0,
	                                             std::int64_t turn = 0, std::int64_t paid = 1000) const
	{
		const auto &flight = child.get<FlightRuntime>();
		auto definition = flight.definition;
		const auto ship = child.parent();
		const auto &equipment = child.get<Equipment>().id;
		// Legacy self-contained boost/impulse processes keep their own funding and
		// controls, but their output joins the same sum as external target bindings.
		if (definition.targets.empty())
		{
			if (flight.state.impulse == ImpulsePhase::Active)
			{
				speed += (definition.impulse_speed_multiplier - 1000) * paid / 1000;
				acceleration += (definition.impulse_acceleration_multiplier - 1000) * paid / 1000;
			}
			else if (flight.state.boost && !restrictions.impulse)
			{
				speed += (definition.boost_speed_multiplier - 1000) * paid / 1000;
				acceleration += (definition.boost_acceleration_multiplier - 1000) * paid / 1000;
			}
		}
		definition.boost_speed_multiplier = definition.boost_acceleration_multiplier = 1000;
		definition.impulse_speed_multiplier = definition.impulse_acceleration_multiplier = 1000;
		definition.maximum_speed = effective_property(ship, equipment, "speed", definition.maximum_speed, speed);
		definition.acceleration =
		    effective_property(ship, equipment, "propulsion", definition.acceleration, acceleration);
		definition.lateral_acceleration =
		    effective_property(ship, equipment, "propulsion", definition.lateral_acceleration, acceleration);
		definition.turn_rate = effective_property(ship, equipment, "turn", definition.turn_rate, turn);
		const auto process_rate = native_process_rate(ship.get<StableId>().id, "propulsion", equipment);
		definition.acceleration = apply_property_delta(definition.acceleration, process_rate - 1000, resource_limit);
		definition.lateral_acceleration =
		    apply_property_delta(definition.lateral_acceleration, process_rate - 1000, resource_limit);
		definition.turn_rate = apply_property_delta(definition.turn_rate, process_rate - 1000, resource_limit);
		return definition;
	}
	FlightState effective_flight_state(flecs::entity child, const FlightDefinition &definition,
	                                   FlightRestrictions restrictions) const
	{
		const auto &flight = child.get<FlightRuntime>();
		auto state = flight.state;
		state.steering_limit_permille = restrictions.steering_limit;
		if (structurally_disabled(child.parent(), child.get<Equipment>().id))
			state.enabled = false;
		if (restrictions.impulse)
			state.boost = false;
		state.desired_speed = state.desired_speed == flight.definition.maximum_speed
		                          ? definition.maximum_speed
		                          : std::min(state.desired_speed, definition.maximum_speed);
		return state;
	}
	std::int64_t lateral_velocity_intent(flecs::entity ship) const
	{
		std::int64_t desired = 0;
		for (const auto child : ship.get<EquipmentIndex>().values)
			if (const auto *side = child.try_get<FlightRuntime>(); side && side->definition.kind == FlightKind::Lateral)
			{
				const auto effort = side->state.lateral;
				if (side->definition.lateral_direction && effort * side->definition.lateral_direction < 0)
					continue;
				const auto value = side->definition.lateral_speed * effort / 1000;
				if (std::abs(value) > std::abs(desired))
					desired = value;
			}
		return desired;
	}
	void include_vertical_demand(flecs::entity child, bool enabled)
	{
		auto &flight = child.get_mut<FlightRuntime>();
		const auto ship = child.parent();
		if (const auto *vertical = ship.try_get<VerticalMotion>();
		    vertical && enabled && flight.definition.kind == FlightKind::Engine &&
		    (vertical->desired != vertical->altitude || vertical->velocity) &&
		    native_process_rate(ship.get<StableId>().id, "propulsion", child.get<Equipment>().id))
		{
			flight.demand.power_rate = std::max(flight.demand.power_rate, flight.definition.power_rate);
			flight.demand.effort_permille = 1000;
		}
	}
	void request_flight_resources(flecs::entity ship, FlightRestrictions restrictions)
	{
		const auto id = ship.get<StableId>().id;
		const auto tick = ports.tick();
		const auto &pose = ship.get<Pose>();
		const auto &motion = ship.get<Motion>();
		const auto lateral = lateral_velocity_intent(ship);
		for (auto child : owner.ordered_equipment(flight_query, ship))
		{
			auto &flight = child.get_mut<FlightRuntime>();
			flight.state.steering_limit_permille = restrictions.steering_limit;
			// Actuator guidance consumes the same frozen, permitted contact feed as its Console.
			const auto contacts = ports.contacts(id, child.get<Equipment>().id);
			std::optional<FlightContactObservation> observed;
			for (const auto &contact : contacts)
				if (contact.id == flight.state.selected_contact)
				{
					observed = FlightContactObservation{contact.id,
					                                    fixed::length({contact.vx, contact.vy}),
					                                    tick,
					                                    0,
					                                    {contact.x, contact.y},
					                                    {contact.vx, contact.vy},
					                                    contact.heading};
					break;
				}
			observe_flight(flight.definition, flight.state, tick, observed);
			guide_follow(flight.definition, flight.state, pose, motion, observed);
			const auto effective = effective_flight_definition(child, restrictions);
			const auto requested = effective_flight_state(child, effective, restrictions);
			flight.demand = flight_demand(effective, requested, pose, motion, lateral);
			if (!native_process_rate(id, "propulsion", child.get<Equipment>().id))
				flight.demand = {};
			include_vertical_demand(child, requested.enabled);
			if (auto power = ship.try_get_mut<ShipResources>())
			{
				const bool charging = flight.state.impulse == ImpulsePhase::Charging &&
				                      flight.definition.impulse_charge_energy &&
				                      native_process_rate(id, "propulsion", child.get<Equipment>().id);
				const auto energy = charging ? quote_flight_charge(flight.definition, flight.state).energy : 0;
				request_system_resource(*power, child.get<ResourceBinding>().slot, 0, energy > 0,
				                        (restrictions.impulse && flight.definition.kind == FlightKind::Boost)
				                            ? 0
				                            : flight.demand.power_rate,
				                        energy);
			}
		}
	}
	void reserve_paid_modifiers(flecs::entity ship, FlightRestrictions restrictions)
	{
		const auto id = ship.get<StableId>().id;
		const auto fittings = owner.ordered_equipment(flight_query, ship);
		auto *power = ship.try_get_mut<ShipResources>();
		// Reserve full target duty while a modifier is requested. The preview and
		// committed allocation use identical requests, even if a paid bonus changes
		// guidance effort. Only the committed tick debits energy or advances cooling.
		for (auto source : fittings)
		{
			const auto &flight = source.get<FlightRuntime>();
			if (!flight.state.enabled || !native_process_rate(id, "propulsion", source.get<Equipment>().id) ||
			    !(flight.state.impulse == ImpulsePhase::Active || (flight.state.boost && !restrictions.impulse)))
				continue;
			for (const auto &binding : flight.definition.targets)
			{
				auto target = ship.get<EquipmentIndex>().values.at(binding.index);
				if (power && target.has<FlightRuntime>() && target.get<FlightRuntime>().state.enabled)
				{
					const auto slot = target.get<ResourceBinding>().slot;
					reserve_system_resource_rate(*power, slot, target.get<FlightRuntime>().definition.power_rate);
				}
			}
		}
	}
	void prepare_paid_modifiers(flecs::entity ship, FlightRestrictions restrictions,
	                            const ShipResourceAllocation *allocation)
	{
		const auto id = ship.get<StableId>().id;
		const auto fittings = owner.ordered_equipment(flight_query, ship);
		for (auto child : fittings)
		{
			auto &flight = child.get_mut<FlightRuntime>();
			flight.state.external_impulse = false;
			flight.state.effective_boost = 0;
			if (flight.state.impulse == ImpulsePhase::Active)
			{
				if (!flight.state.enabled)
					cancel_impulse(flight.state, 4);
				else if (allocation && allocation->grants()[child.get<ResourceBinding>().slot].supply_permille < 1000)
					cancel_impulse(flight.state,
					               allocation->grants()[child.get<ResourceBinding>().slot].blocked ? 4 : 3);
				for (const auto &binding : flight.definition.targets)
				{
					const auto target = ship.get<EquipmentIndex>().values.at(binding.index);
					if (!target.get<FlightRuntime>().state.enabled)
						cancel_impulse(flight.state, 4);
					else if (allocation &&
					         allocation->grants()[target.get<ResourceBinding>().slot].supply_permille < 1000)
						cancel_impulse(flight.state, 3);
				}
			}
		}
		restrictions = flight_restrictions(ship);
		const auto lateral = lateral_velocity_intent(ship);
		for (auto target : fittings)
		{
			auto &flight = target.get_mut<FlightRuntime>();
			auto definition = flight.definition;
			std::int64_t speed = 0, acceleration = 0, turn = 0;
			for (auto source : fittings)
			{
				auto &modifier = source.get_mut<FlightRuntime>();
				const bool impulse = modifier.state.impulse == ImpulsePhase::Active;
				if (!modifier.state.enabled || structurally_disabled(ship, source.get<Equipment>().id) ||
				    !(impulse || (modifier.state.boost && !restrictions.impulse)))
					continue;
				const auto paid = native_process_paid(
				    id, "propulsion", source.get<Equipment>().id,
				    allocation ? allocation->grants()[source.get<ResourceBinding>().slot].supply_permille : 1000);
				for (const auto &binding : modifier.definition.targets)
					if (binding.index == target.get<Equipment>().ordinal)
					{
						const auto bonus = (binding.multiplier - 1000) * paid / 1000;
						auto &value = binding.property == FlightProperty::Speed          ? speed
						              : binding.property == FlightProperty::Acceleration ? acceleration
						                                                                 : turn;
						value += bonus;
						if (!impulse)
							modifier.state.effective_boost = paid;
						if (impulse && definition.kind == FlightKind::Engine)
							flight.state.external_impulse = true;
					}
			}
			const auto paid = native_process_paid(
			    id, "propulsion", target.get<Equipment>().id,
			    allocation ? allocation->grants()[target.get<ResourceBinding>().slot].supply_permille : 1000);
			definition = effective_flight_definition(target, restrictions, speed, acceleration, turn, paid);
			const auto state = effective_flight_state(target, definition, restrictions);
			flight.demand = flight_demand(definition, state, ship.get<Pose>(), ship.get<Motion>(), lateral);
			include_vertical_demand(target, state.enabled);
		}
	}
	void actuate_flight(flecs::entity child, FlightRestrictions restrictions)
	{
		auto &flight = child.get_mut<FlightRuntime>();
		auto ship = child.parent();
		auto power = ship.try_get_mut<ShipResources>();
		const auto slot = power ? child.get<ResourceBinding>().slot : 0;
		int paid =
		    static_cast<int>(native_process_paid(ship.get<StableId>().id, "propulsion", child.get<Equipment>().id,
		                                         power ? power->grants[slot].supply_permille : 1000));
		const auto charge_before = flight.state.charge_milliticks;
		if (flight.state.impulse == ImpulsePhase::Charging && flight.definition.impulse_charge_energy)
		{
			const auto &grant = power->grants[slot];
			const auto due = quote_flight_charge(flight.definition, flight.state).energy;
			if ((!due || grant.discrete_paid) && !grant.blocked &&
			    native_process_rate(ship.get<StableId>().id, "propulsion", child.get<Equipment>().id) &&
			    native_process_work_limit(ship.get<StableId>().id, "propulsion", child.get<Equipment>().id) >= 1000)
			{
				flight.state.charge_spent += due;
				flight.state.charge_milliticks += 1000;
				flight.state.impulse_reason = 0;
				if (flight.state.charge_milliticks == flight.definition.impulse_charge_ticks * 1000)
					flight.state.impulse = ImpulsePhase::Active;
			}
			else
				flight.state.impulse_reason = 2;
			paid = (!due || grant.discrete_paid) && !grant.blocked &&
			               native_process_rate(ship.get<StableId>().id, "propulsion", child.get<Equipment>().id) &&
			               native_process_work_limit(ship.get<StableId>().id, "propulsion",
			                                         child.get<Equipment>().id) >= 1000
			           ? 1000
			           : 0;
		}
		if (restrictions.impulse && flight.definition.kind == FlightKind::Boost)
			paid = 0;
		const bool structural_stop =
		    dock_attached(ship.get<StableId>().id) || structurally_disabled(ship, child.get<Equipment>().id);
		const bool accepted_enabled = flight.state.enabled;
		if (structural_stop)
			flight.state.enabled = false;
		const bool requested_boost = flight.state.boost;
		if (restrictions.impulse)
			flight.state.boost = false;
		const auto execution =
		    step_flight(flight.definition, flight.state, ship.get_mut<Pose>(), ship.get_mut<Motion>(), flight.demand,
		                paid, power ? power->grants[slot].overdrive_permille : 1000,
		                power ? power->grants[slot].overdrive_heat_permille : 1000);
		if (power && execution.heat_rate)
		{
			const auto store = power->definition.systems[slot].store;
			if (store != no_resource)
			{
				auto &remainder = child.get_mut<OperatingHeat>().remainder;
				remainder += execution.heat_rate;
				power->network.apply_operating_heat(store, remainder / rate);
				remainder %= rate;
			}
		}
		flight.state.enabled = accepted_enabled;
		flight.state.boost = requested_boost;
		record_native_process_work(
		    ship.get<StableId>().id, "propulsion", child.get<Equipment>().id, flight.demand.effort_permille,
		    execution.effort_milliticks + std::max<std::int64_t>(0, flight.state.charge_milliticks - charge_before));
		if (auto engine = child.try_get_mut<Engine>())
		{
			engine->speed = flight.state.desired_speed;
			engine->enabled = flight.state.enabled;
			engine->forward_output = std::max(0, execution.forward_permille);
		}
		if (auto steering = child.try_get_mut<Steering>())
			steering->heading = flight.state.desired_heading;
	}
	void run_equipment_actuation(flecs::entity ship, FlightRestrictions restrictions)
	{
		const auto id = ship.get<StableId>().id;
		// Merge the flight and telemetry work in authored order. Sampling immediately
		// after each actuator preserves telemetry when several fittings heat one store.
		const auto actuators = owner.ordered_equipment(flight_query, ship);
		std::size_t next = 0;
		for (auto child : owner.ordered_equipment(advanced_equipment_query, ship))
		{
			if (next < actuators.size() && actuators[next] == child)
			{
				actuate_flight(child, restrictions);
				++next;
			}
			const auto &fit = child.get<Equipment>();
			ports.telemetry(id, fit.id, fit.ordinal);
		}
	}
	void quote_basic_process_work()
	{
		for (auto &quote : process_machinery.rows())
		{
			quote.basic_available = quote.basic_full = 0;
			quote.basic_engine.reset();
			quote.basic_steering.reset();
		}
		BasicFlightWorkBook candidates;
		for (auto child : owner.ordered_equipment(steering_query))
			if (auto ship = child.parent(); ship.has<Movable>() && !dock_attached(ship.get<StableId>().id) &&
			                                !structurally_disabled(ship, child.get<Equipment>().id))
			{
				auto effective = effective_basic_steering(child);
				const auto id = ship.get<StableId>().id;
				const auto &equipment = child.get<Equipment>().id;
				const auto paid = native_process_paid(id, "propulsion", equipment, 1000);
				const auto work = candidates.steering(id, ship.get<Pose>(), effective, paid);
				if (auto *quote = native_machinery_quote(id, "propulsion", equipment))
				{
					quote->basic_full = work.full;
					quote->basic_available = work.available;
					quote->basic_steering = work.frozen;
				}
			}
		for (auto child : owner.ordered_equipment(engine_query))
			if (auto ship = child.parent(); ship.has<Movable>() && !dock_attached(ship.get<StableId>().id))
			{
				auto full_engine = effective_basic_engine(child);
				auto actual_engine = effective_basic_engine(child);
				const auto id = ship.get<StableId>().id;
				const auto &equipment = child.get<Equipment>().id;
				const auto paid = native_process_paid(id, "propulsion", equipment, 1000);
				const auto work =
				    candidates.engine(id, ship.get<Pose>(), ship.get<Motion>(), full_engine, actual_engine, paid);
				if (auto *quote = native_machinery_quote(id, "propulsion", equipment))
				{
					quote->basic_engine = work.frozen;
					quote->basic_full = std::max<std::int64_t>(quote->basic_full, work.full);
					quote->basic_available = std::max<std::int64_t>(quote->basic_available, work.available);
				}
			}
	}
};
NativeFlightActuators::NativeFlightActuators(flecs::world &world, NativeFlightPorts ports)
    : impl_(std::make_unique<Impl>(world, std::move(ports)))
{
}
NativeFlightActuators::~NativeFlightActuators() = default;
void NativeFlightActuators::run_basic_steering(ProcessMachineryBook &book)
{
	Impl::Phase phase(*impl_, book);
	phase.run_basic_steering();
}
void NativeFlightActuators::run_basic_engines(ProcessMachineryBook &book)
{
	Impl::Phase phase(*impl_, book);
	phase.run_basic_engines();
}
void NativeFlightActuators::quote_basic_process_work(ProcessMachineryBook &book)
{
	Impl::Phase phase(*impl_, book);
	phase.quote_basic_process_work();
}
void NativeFlightActuators::cancel_ship_impulse(ProcessMachineryBook &book, flecs::entity ship)
{
	Impl::Phase phase(*impl_, book);
	phase.cancel_ship_impulse(ship);
}
FlightRestrictions NativeFlightActuators::flight_restrictions(ProcessMachineryBook &book, flecs::entity ship)
{
	Impl::Phase phase(*impl_, book);
	return phase.flight_restrictions(ship);
}
void NativeFlightActuators::request_flight_resources(ProcessMachineryBook &book, flecs::entity ship,
                                                     FlightRestrictions restrictions)
{
	Impl::Phase phase(*impl_, book);
	phase.request_flight_resources(ship, restrictions);
}
void NativeFlightActuators::reserve_paid_modifiers(ProcessMachineryBook &book, flecs::entity ship,
                                                   FlightRestrictions restrictions)
{
	Impl::Phase phase(*impl_, book);
	phase.reserve_paid_modifiers(ship, restrictions);
}
void NativeFlightActuators::run_equipment_actuation(ProcessMachineryBook &book, flecs::entity ship,
                                                    FlightRestrictions restrictions)
{
	Impl::Phase phase(*impl_, book);
	phase.run_equipment_actuation(ship, restrictions);
}
void NativeFlightActuators::prepare_paid_modifiers(ProcessMachineryBook &book, flecs::entity ship,
                                                   FlightRestrictions restrictions,
                                                   const ShipResourceAllocation *allocation)
{
	Impl::Phase phase(*impl_, book);
	phase.prepare_paid_modifiers(ship, restrictions, allocation);
}
NativeFlightSnapshot NativeFlightActuators::snapshot(std::span<const flecs::entity> ships) const
{
	NativeFlightSnapshot result;
	for (auto ship : ships)
		for (auto child : impl_->ordered_equipment(impl_->flight_query, ship))
		{
			const auto &flight = child.get<FlightRuntime>();
			result.rows.emplace_back(child, flight.state, flight.demand);
		}
	return result;
}
void NativeFlightActuators::restore(NativeFlightSnapshot &snapshot)
{
	for (auto &[child, state, demand] : snapshot.rows)
	{
		auto &flight = child.get_mut<FlightRuntime>();
		flight.state = state;
		flight.demand = demand;
	}
}
} // namespace phoenix
