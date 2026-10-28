module;
#include "native_headers.hpp"

module phoenix.simulation;
import :schedule;
import phoenix.world.schedule.profiling;

namespace phoenix
{
using namespace world_detail;
void World::tick()
{
	impl_->clear_presentation_snapshots();
	TickProfile profile;
	impl_->retire_audio();
	if (!impl_->audio_baselined)
	{
		impl_->refresh_presentation_audio(true);
		impl_->audio_baselined = true;
	}
	// Keep committed frame numbering live after the race, but freeze its world.
	if (impl_->scenario && impl_->scenario->runtime().mission_complete())
	{
		// Presentation settings remain usable after physical mission execution stops.
		for (const auto &[claim, command] : impl_->meta_pending)
			if (command.action == "camera_mode" || command.action == "camera_direction")
				impl_->meta(claim, command, true);
		impl_->meta_pending.clear();
		impl_->controller_pending.clear();
		++impl_->ecs.get_mut<Clock>().tick;
		return;
	}
	impl_->stream_fields();
	impl_->index_contacts.run();
	// Departure is revalidated after this tick's admitted Controller changes are consumed.
	std::vector<std::pair<std::uint64_t, OperatingCommand>> departures;
	for (const auto &[claim, command] : impl_->meta_pending)
		if (command.action == "ftl.depart")
			departures.emplace_back(claim, command);
		else
			impl_->meta(claim, command, true);
	impl_->meta_pending.clear();
	impl_->prune_displays();
	for (const auto &[id, entity] : impl_->entities)
		if (!impl_->departed(id) && entity.has<PersonnelState>())
			impl_->advance_personnel(id);
	for (const auto &proposal : impl_->controller_pending)
		if (!impl_->departed(proposal.address.entity))
		{
			const auto &ctrl =
			    impl_->entities.at(proposal.address.entity).get<EntityControls>().controllers.at(proposal.controller);
			if (impl_->commit_controller(proposal))
				impl_->cancel_docking_assist(
				    proposal.address.entity, proposal.address.controller,
				    std::string(operating_action(ctrl.definition.commands.at(proposal.channel).action).name),
				    proposal.value);
		}
	impl_->controller_pending.clear();
	impl_->step_shield_effects();
	impl_->step_property_effects();
	profile.mark("admission_personnel_effects");
	if (impl_->scenario)
	{
		auto bodies = inspect();
		std::erase_if(bodies,
		              [](const auto &body)
		              {
			              return body.id >= Impl::field_identity_base;
		              });
		auto active_slots = impl_->slot_entities;
		std::erase_if(active_slots,
		              [&](const auto &entry)
		              {
			              return impl_->departed(entry.second);
		              });
		impl_->scenario->begin_tick(ticks(), std::move(active_slots), bodies);
		std::vector<ScenarioKnowledgeSnapshot> snapshots;
		for (const auto &reader : impl_->scenario->knowledge_readers())
		{
			const auto board = impl_->knowledge.project({reader.ship, reader.console});
			snapshots.push_back({reader, board.records, board.shared});
		}
		impl_->scenario->supply_knowledge(snapshots);
		impl_->scenario->step();
		impl_->scenario_spawns();
		impl_->scenario_effects();
		impl_->scenario_publications();
	}
	impl_->decision_frame.clear();
	profile.mark("scenario");
	impl_->prepare_craft();
	impl_->prepare_docking();
	profile.mark("prepare_craft_docking");
	for (const auto &[id, entity] : impl_->entities)
		if (entity.has<EntityControls>())
		{
			impl_->reconcile_objective_priority(id);
			ControllerCommandSections snapshot;
			for (const auto &controller : entity.get<EntityControls>().controllers)
			{
				auto &section = snapshot.emplace_back();
				for (const auto &command : controller.commands)
					section.push_back(command);
			}
			impl_->decision_frame.freeze_commands(id, std::move(snapshot));
		}
	profile.mark("command_snapshots");
	Impl::TelemetrySnapshot decision_telemetry;
	Impl::ContactSnapshot decision_contacts;
	for (const auto &[id, entity] : impl_->entities)
		for (const auto &console : impl_->definition(id).consoles)
		{
			const ConsoleAddress address{id, console.id};
			impl_->decision_frame.freeze_observation(
			    address, impl_->capture_decision_observation(address, &decision_telemetry, &decision_contacts));
		}
	profile.mark("console_observation_snapshots");
	for (const auto &[id, entity] : impl_->entities)
		if (entity.has<EntityControls>())
			impl_->refresh_navigation_blackboard(id);
	profile.mark("navigation_blackboards");
	for (const auto &[id, entity] : impl_->entities)
		if (entity.has<EntityControls>())
			for (const auto &controller : entity.get<EntityControls>().controllers)
			{
				const ConsoleAddress address{id, controller.definition.console};
				if (!controller.human)
				{
					const ControllerAddress target{id, controller.definition.console, controller.definition.id};
					impl_->decision_frame.freeze_inbox(target, impl_->knowledge.peek_controller(target));
				}
				if (!impl_->decision_frame.knowledge().contains(address))
					impl_->decision_frame.freeze_knowledge(address, impl_->knowledge.project(address));
			}
	impl_->step_starts.clear();
	profile.mark("knowledge_snapshots");
	impl_->bodies.each(
	    [&](const StableId &id, const Definition &, const Pose &pose, const Motion &)
	    {
		    impl_->step_starts.emplace(id.id, pose.position);
	    });
	// Explicit fixed-tick phases. Each pre-collision row operates only on itself,
	// so archetype/table ordering cannot affect gameplay. No wall-clock progress.
	for (const auto &[id, entity] : impl_->entities)
		if (!impl_->departed(id) && !impl_->craft_dormant(entity) && entity.has<EntityControls>())
			impl_->run_console_controllers(entity);
	for (const auto &[claim, command] : departures)
		impl_->meta(claim, command, true);
	impl_->apply_interdiction_orders();
	profile.mark("controllers");
	impl_->commit_departures();
	impl_->apply_structural_disablement();
	impl_->initialise_rule_membership();
	impl_->prepare_rule_sources();
	impl_->step_rule_events(RuleEventPhase::Source);
	impl_->capture_dock_propulsion();
	for (const auto &[id, entity] : impl_->entities)
		if (entity.has<EntityControls>() && !entity.has<GameplayShip>())
			for (auto child : entity.get<EquipmentIndex>().values)
			{
				const auto &fit = child.get<Equipment>();
				impl_->knowledge.ingest_telemetry(id, fit.id, impl_->telemetry(id, fit.ordinal), ticks());
			}
	impl_->run_advanced_equipment();
	impl_->refresh_rule_eligibility();
	profile.mark("equipment");
	impl_->adjust_dock_propulsion();
	impl_->step_vertical();
	impl_->service_craft();
	impl_->step_cargo();
	impl_->step_logistics();
	// Commands may change velocity this tick. Activate swept collision partners
	// after propulsion, including their starting poses, before any integration.
	impl_->stream_fields();
	impl_->bodies.each(
	    [&](const StableId &id, const Definition &, const Pose &pose, const Motion &)
	    {
		    impl_->step_starts.try_emplace(id.id, pose.position);
	    });
	impl_->integrate.run();
	impl_->sync_docks();
	impl_->resolve_collisions.run();
	profile.mark("logistics_motion_collision");
	impl_->step_docking();
	impl_->step_region_rules();
	impl_->refresh_rule_eligibility();
	impl_->step_rule_clocks();
	impl_->step_rule_events(RuleEventPhase::Periodic);
	impl_->step_rule_events(RuleEventPhase::Payload);
	impl_->step_rule_events(RuleEventPhase::Shield);
	impl_->step_ordnance();
	impl_->resolve_destructions();
	impl_->refresh_rule_eligibility();
	impl_->resolve_lost_parties();
	impl_->step_teams();
	impl_->step_repairs();
	impl_->step_medical_facilities();
	impl_->index_contacts.run();
	impl_->waypoint_arrivals();
	profile.mark("combat_teams_repairs");
	if (impl_->scenario)
	{
		std::vector<ScenarioMotionSegment> motion;
		impl_->bodies.each(
		    [&](const StableId &id, const Definition &, const Pose &pose, const Motion &)
		    {
			    if (id.id >= Impl::field_identity_base)
				    return;
			    const auto from = impl_->step_starts.at(id.id);
			    motion.push_back({id.id, {from.x, from.y}, {pose.position.x, pose.position.y}});
		    });
		impl_->scenario->evaluate(motion);
		impl_->scenario_publications();
	}
	++impl_->ecs.get_mut<Clock>().tick;
	++impl_->environment_tick;
	impl_->knowledge.tick(ticks());
	profile.mark("scenario_evaluation_knowledge");
	for (const auto &[id, entity] : impl_->entities)
		if (entity.has<BridgeBlackboard>())
			impl_->reconcile_objective_priority(id);
	impl_->refresh_comms();
	impl_->refresh_boarding_templates();
	impl_->update_craft_reports();
	profile.mark("refresh_comms_boarding_craft");
	// Physical receipt and deadlines settle before queued station traffic can be delivered.
	impl_->update_mission_facts();
	impl_->conversation_scenarios();
	impl_->conversations.advance(ticks());
	impl_->advance_away();
	profile.mark("missions_conversations_away");
	impl_->advance_interdictions();
	impl_->advance_psychic();
	impl_->advance_sensors();
	impl_->update_mission_facts();
	impl_->finalise_destroyed_missions();
	impl_->prune_property_targets();
	impl_->refresh_rule_eligibility();
	profile.mark("missions_comms_away_sensors");
	impl_->finalise_finite_native_jobs();
	impl_->refresh_presentation_audio();
	impl_->decision_frame.clear();
}
} // namespace phoenix
