module;
#include "native_headers.hpp"

module phoenix.simulation;
import :runtime;
import phoenix.content.assets;
import phoenix.content.combat;
import phoenix.content.controllers;
import phoenix.content.effects;
import phoenix.content.equipment;
import phoenix.content.knowledge;
import phoenix.content.logistics;
import phoenix.content.operations;
import phoenix.content.resources;
import phoenix.content.scenarios;
import phoenix.content.sensing;
import phoenix.content.ui;
import phoenix.content.verification;
import phoenix.resources;
import phoenix.conversations;
import phoenix.gameplay_vocabulary;
import phoenix.gameplay_scenario;
import phoenix.knowledge.consoles;
import phoenix.knowledge.projections;
import phoenix.knowledge.read_models;
import phoenix.control.controllers;
import phoenix.control.views;
import phoenix.control.templates;
import phoenix.control.coordination;
import phoenix.control.protocol;
import phoenix.control.coordination_facts;
import phoenix.control.execution;
import phoenix.control.providers;
import phoenix.control.ownership;
import phoenix.control.handover;
import phoenix.effects.contracts;
import phoenix.effects.abilities;
import phoenix.effects.processes;
import phoenix.effects.rules;
import phoenix.effects.properties;
import phoenix.effects.rule_state;
import phoenix.world.state;
import phoenix.world.persistence;
import phoenix.world.assembly;
import phoenix.world.spatial;
import phoenix.gameplay.flight;
import phoenix.gameplay.flight.native;
import phoenix.gameplay.fitting;
import phoenix.gameplay.combat.faults;
import phoenix.gameplay.docking;
import phoenix.gameplay.craft;
import phoenix.gameplay.logistics;
import phoenix.gameplay.ftl;
import phoenix.gameplay.ftl.coordinator;
import phoenix.gameplay.combat.state;
import phoenix.gameplay.directed;
import phoenix.gameplay.ordnance;
import phoenix.gameplay.ordnance.contacts;
import phoenix.gameplay.repairs;
import phoenix.gameplay.damage;
import phoenix.gameplay.combat.continuation;
import phoenix.gameplay.weapons;
import phoenix.gameplay.personnel.state;
import phoenix.gameplay.personnel.clinical;
import phoenix.gameplay.personnel.treatment;
import phoenix.gameplay.personnel.runtime;
import phoenix.gameplay.teams.routes;
import phoenix.resources.equipment;
import phoenix.effects.events;
import phoenix.effects.grants;
import phoenix.effects.transactions;
import phoenix.effects.native;
import phoenix.effects.clocks;
import phoenix.resources.processing;
import phoenix.gameplay.machinery;
import phoenix.gameplay.flight.work;
import phoenix.effects.machinery;
import phoenix.effects.finite_work;
import phoenix.effects.process_validation;
import phoenix.effects.continuation_validation;
import phoenix.effects.ability_validation;
import phoenix.effects.bindings;
import phoenix.effects.funding;
import phoenix.effects.preparation;
import phoenix.gameplay.combat.outcomes;
import phoenix.gameplay.combat.shields;
import phoenix.gameplay.personnel.outcomes;
import phoenix.gameplay.away.state;
import phoenix.gameplay.away.operations;
import phoenix.gameplay.away.crew;
import phoenix.gameplay.boarding.runtime;
import phoenix.scenarios.objectives;
import phoenix.scenarios.environment.streaming;
import phoenix.gameplay.psychic.state;
import phoenix.scenario.interdictions;
import phoenix.gameplay.psychic.emission;
import phoenix.gameplay.psychic.interference;
import phoenix.gameplay.psychic.contributions;
import phoenix.gameplay.psychic.receptions;
import phoenix.gameplay.psychic.projection;
import phoenix.gameplay.sensors.state;
import phoenix.gameplay.sensors.envelopes;
import phoenix.gameplay.sensors.acquisition;
import phoenix.gameplay.sensors.findings;
import phoenix.gameplay.sensors.validation;
import phoenix.gameplay.sensors.projection;
import phoenix.gameplay.sensors.coverage;
import phoenix.gameplay.sensors.evidence;
import phoenix.gameplay.away.runtime;
import phoenix.world.schedule.profiling;
import phoenix.effects.properties.runtime;
import phoenix.effects.active_rules;

namespace phoenix
{
using namespace world_detail;
unsigned long long World::ticks() const
{
	return impl_->ecs.get<Clock>().tick;
}

std::vector<BodyView> World::inspect() const
{
	std::vector<BodyView> result;
	result.reserve(impl_->entities.size());
	impl_->bodies.each(
	    [&](const StableId &id, const Definition &definition, const Pose &pose, const Motion &motion)
	    {
		    result.push_back(view(id.id, definition, pose, motion));
	    });
	std::sort(result.begin(), result.end(),
	          [](const auto &a, const auto &b)
	          {
		          return a.id < b.id;
	          });
	return result;
}

std::optional<EntityId> World::ship_for_slot(const std::string &slot) const
{
	auto found = impl_->slot_entities.find(slot);
	if (found == impl_->slot_entities.end())
		return {};
	return found->second;
}
} // namespace phoenix
