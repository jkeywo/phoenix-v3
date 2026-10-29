module;
#include "native_headers.hpp"

module phoenix.simulation;
import :projection_bindings;

namespace phoenix
{
using namespace world_detail;
std::optional<Observation> World::observe(std::uint64_t console) const
{
	const auto snapshot = observe_shared(console);
	return snapshot ? std::optional<Observation>{*snapshot} : std::nullopt;
}

std::shared_ptr<const Observation> World::observe_shared(std::uint64_t console) const
{
	if (const auto cached = impl_->presentation.find(console))
		return cached;
	const auto found = impl_->ownership.claims().find(console);
	if (found == impl_->ownership.claims().end())
		return {};
	auto result = observe_at(console, found->second);
	for (const auto &seat : impl_->definition(found->second.ship).consoles)
	{
		const ConsoleAddress address{found->second.ship, seat.id};
		if (address != found->second && seat.human_seeking && impl_->console_operator(address) == console &&
		    impl_->bridge_visible(address))
			result.additional_consoles.push_back(observe_at(console, address));
	}
	auto snapshot = std::make_shared<const Observation>(std::move(result));
	impl_->presentation.remember(console, snapshot);
	return snapshot;
}

Observation World::observe_at(std::uint64_t console, const ConsoleAddress &address) const
{
	auto entity = impl_->entities.at(address.ship);
	const auto &seat = *impl_->seat(address.ship, address.console);
	Observation result = impl_->capture_console_observation(
	    address, impl_->knowledge.project_json(address), Impl::ObservationDetail::Presentation,
	    &impl_->presentation.telemetry(), &impl_->presentation.contacts());
	result.gameplay["alert"] = impl_->alert_board(address.ship);
	result.gameplay["navigation"] = impl_->navigation_board(address.ship);
	result.gameplay["stance"] = impl_->stance_board(address);
	if (impl_->definition(address.ship).console_bridge.at(address.console).objectives)
		result.gameplay["objective_board"] = impl_->objectives_board(address.ship);
	result.gameplay["personnel"] = impl_->personnel_board(address);
	result.gameplay["psychic"] = impl_->psychic_board(address);
	result.gameplay["abilities"] = impl_->ability_board(address);
	if (auto rules = impl_->rules_board(address); !rules.empty())
		result.gameplay["rules"] = std::move(rules);
	result.gameplay["teams"] = impl_->team_board(address);
	result.gameplay["away_missions"] = impl_->away_board(address);
	result.gameplay["craft"] = impl_->craft_board(address);
	result.gameplay["ftl"] = impl_->ftl_board(address);
	result.gameplay["processes"] = impl_->process_board(address, &result);
	result.gameplay["mission_report"] =
	    entity.has<MissionDeparture>() ? entity.get<MissionDeparture>().report : Json(nullptr);
	if (impl_->comms_owner(address.ship) == address)
		result.gameplay["conversations"] = impl_->conversations.project(address.ship);
	if (entity.has<EntityControls>())
		result.gameplay["controllers"] = impl_->project_controllers(address, result);

	auto &buttons = result.gameplay["controls"] = Json::array();
	if (impl_->departed(address.ship))
		return result;
	for (auto child : entity.get<EquipmentIndex>().values)
		if (child.has<FtlReserve>() && impl_->ftl_owner(address.ship, child.get<Equipment>().id) == address)
			for (const auto &ctrl : entity.get<EntityControls>().controllers)
				if (controller_operation_human(ctrl, "departure") && ctrl.definition.console == address.console)
					for (const auto &channel : ctrl.definition.commands)
						if ((!ctrl.definition.widget.empty() &&
						     controller_operation_owns(ctrl, "departure", child.get<Equipment>().id)) ||
						    (ctrl.definition.widget.empty() &&
						     std::find(channel.consumers.begin(), channel.consumers.end(),
						               child.get<Equipment>().ordinal) != channel.consumers.end()))
						{
							OperatingCommand command{
							    address.ship, "ftl.depart",   0, child.get<Equipment>().id, ctrl.definition.id,
							    {},           ctrl.generation};
							auto descriptor = encode_operating_command(command);
							descriptor["id"] = "ftl.depart";
							descriptor["label"] = "Leave mission";
							descriptor["enabled"] = impl_->ftl_blocker(address.ship, child).empty();
							buttons.push_back(std::move(descriptor));
							break;
						}
	const auto button = [&](const std::string &id, const std::string &label, const std::string &action,
	                        const std::string &system, std::int64_t value)
	{
		if (std::none_of(buttons.begin(), buttons.end(),
		                 [&](const auto &b)
		                 {
			                 return b.at("id") == id;
		                 }))
			buttons.push_back({{"id", id}, {"label", label}, {"action", action}, {"system", system}, {"value", value}});
	};
	for (const auto &kind : impl_->camera_widgets.at(seat.definition))
	{
		if (kind == "camera_mode" || kind == "camera_direction")
			for (int value = 0; value < (kind == "camera_mode" ? 2 : 8); ++value)
				button(kind + "." + std::to_string(value),
				       kind == "camera_mode" ? (value ? "Third person" : "First person") : camera_directions[value],
				       kind, {}, value);
	}
	const bool away = impl_->ownership.is_away(address);
	impl_->bridge_controls(address, buttons);
	button("away", away ? "Resume control" : "Away", "away", {}, away ? 0 : 1);
	if (entity.has<EntityControls>())
	{
		const auto &controls = entity.get<EntityControls>();
		Impl::ConsoleContacts ability_contacts;
		ability_contacts.emplace(address, result);
		if (!impl_->definition(address.ship).console_bridge.at(seat.id).command)
		{
			result.gameplay["profile"] = controls.layout.profiles.at(controls.profiles.at(seat.id)).id;
			result.gameplay["profile_stars"] = Json::object();
			for (std::size_t i = 0; i < controls.layout.profiles.size(); ++i)
			{
				if (controls.layout.fixed_profiles.contains(seat.id) && controls.layout.fixed_profiles.at(seat.id) != i)
					continue;
				if (controls.layout.profiles[i].stars)
					result.gameplay["profile_stars"][controls.layout.profiles[i].id] =
					    controls.layout.profiles[i].stars;
				button("profile." + controls.layout.profiles[i].id, controls.layout.profiles[i].id, "preset", {}, i);
			}
		}
		for (const auto &controller : controls.controllers)
			if (controller.definition.console == seat.id && controller.human)
				for (std::size_t i = 0; i < controller.commands.size(); ++i)
				{
					if (!controller_command_human(controller, i))
						continue;
					const auto &channel = controller.definition.commands[i];
					const auto &state = controller.commands[i];
					if (!channel.coordinated_group.empty())
					{
						const auto key = "flight.impulse." + channel.coordinated_group;
						if (std::none_of(buttons.begin(), buttons.end(),
						                 [&](const auto &b)
						                 {
							                 return b.at("id") == key + ".1";
						                 }))
						{
							std::uint64_t next = 1;
							for (const auto &peer : controls.controllers)
								if (peer.definition.console == address.console)
									for (std::size_t j = 0; j < peer.commands.size(); ++j)
										if (peer.definition.commands[j].coordinated_group == channel.coordinated_group)
											next = std::max(next, peer.commands[j].request_id + 1);
							for (const auto value : {0, 1})
							{
								OperatingCommand c{address.ship, "flight.impulse", value, channel.coordinated_group};
								c.generation = impl_->coordinated_generation(address, channel.coordinated_group);
								c.request_id = next;
								auto descriptor = encode_operating_command(c);
								descriptor["id"] = key + "." + std::to_string(value);
								descriptor["label_key"] = value ? "Start impulse" : "Cancel impulse";
								descriptor["label"] = value ? "Start impulse" : "Cancel impulse";
								buttons.push_back(std::move(descriptor));
							}
						}
					}
					if (impl_->station_stars(address) < channel.individual_stars)
						continue;
					if (impl_->process_controls(address, controller, i, buttons))
						continue;
					if (impl_->ability_controls(address, controller, i, buttons, ability_contacts))
						continue;
					const auto scale = channel.action == OperatingAction::GroupAllocation
					                       ? std::max(state.value, channel.initial)
					                       : channel.maximum - channel.minimum;
					const auto step = std::max<std::int64_t>(1, scale / 20);
					std::set<std::int64_t> values;
					if (channel.action == OperatingAction::DirectedOperatingMode ||
					    channel.action == OperatingAction::DirectedTargetingMode)
					{
						if (channel.consumers.empty())
							continue;
						const auto child = entity.get<EquipmentIndex>().values.at(channel.consumers.front());
						const auto projection = impl_->directed_projection(address.ship, child.get<Equipment>().id);
						const auto key = channel.action == OperatingAction::DirectedOperatingMode ? "operating_modes"
						                                                                          : "targeting_modes";
						if (!projection.contains(key))
							continue;
						for (const auto &choice : projection.at(key))
						{
							if (!choice.at("compatible").get<bool>())
								continue;
							OperatingCommand command{address.ship,
							                         std::string(operating_action(channel.action).name),
							                         choice.at("value").get<std::int64_t>(),
							                         {},
							                         controller.definition.id,
							                         channel.id,
							                         controller.generation};
							auto descriptor = encode_operating_command(command);
							descriptor["id"] = "directed." + controller.definition.id + "." + channel.id + "." +
							                   std::to_string(command.value);
							descriptor["label"] = choice.at("label");
							descriptor["label_prefix"] = controller.definition.id + ": ";
							descriptor["selected"] = state.value == command.value;
							buttons.push_back(std::move(descriptor));
						}
						continue;
					}
					if (channel.action == OperatingAction::MagazineSelect ||
					    channel.action == OperatingAction::WeaponMode ||
					    channel.action == OperatingAction::RepairSystem)
					{
						if (channel.consumers.empty())
							continue;
						const auto child = entity.get<EquipmentIndex>().values.at(channel.consumers.front());
						const auto *mechanism = child.try_get<CombatMechanism>();
						if (!mechanism)
							continue;
						std::vector<std::string> labels;
						if (channel.action == OperatingAction::MagazineSelect)
							labels = mechanism->definition.get<WeaponDefinition>().magazines;
						if (channel.action == OperatingAction::WeaponMode &&
						    mechanism->definition.get<WeaponDefinition>().point_defence)
							labels = {"Off", "Auto", "Manual"};
						if (channel.action == OperatingAction::RepairSystem)
						{
							labels.push_back("Automatic repairs");
							for (const auto &[equipment, compartment] :
							     entity.get<CombatStructure>().definition.equipment_compartments)
								labels.push_back(equipment);
							if (channel.hull_priority)
								labels.push_back("Compartment hull");
						}
						for (std::size_t choice = 0; choice < labels.size(); ++choice)
						{
							OperatingCommand command{address.ship,
							                         std::string(operating_action(channel.action).name),
							                         static_cast<std::int64_t>(choice),
							                         {},
							                         controller.definition.id,
							                         channel.id,
							                         controller.generation};
							auto descriptor = encode_operating_command(command);
							descriptor["id"] =
							    "combat." + controller.definition.id + "." + channel.id + "." + std::to_string(choice);
							descriptor["label"] = labels[choice];
							descriptor["label_prefix"] = controller.definition.id + ": ";
							descriptor["selected"] = state.value == static_cast<std::int64_t>(choice);
							buttons.push_back(std::move(descriptor));
						}
						continue;
					}
					if (channel.action == OperatingAction::WeaponTarget ||
					    channel.action == OperatingAction::DirectedTarget ||
					    channel.action == OperatingAction::SensorTarget ||
					    channel.action == OperatingAction::RepairCompartment)
					{
						std::set<std::int64_t> choices{0};
						if (channel.action == OperatingAction::WeaponTarget ||
						    channel.action == OperatingAction::DirectedTarget ||
						    channel.action == OperatingAction::SensorTarget)
						{
							for (const auto &contact : result.contacts)
								if (contact.id <= 2000000000ULL)
									choices.insert(static_cast<std::int64_t>(contact.id));
							for (const auto &contact : result.bearing_contacts)
								if (contact.id <= 1000000000ULL)
									choices.insert(static_cast<std::int64_t>(contact.id));
						}
						else if (const auto *structure = entity.try_get<CombatStructure>())
							for (std::size_t c = 0; c < structure->compartments.size(); ++c)
								choices.insert(static_cast<std::int64_t>(c + 1));
						if (channel.action == OperatingAction::SensorTarget)
							for (const auto &contact : result.gameplay.at("sensors").at("contacts"))
								if (contact.at("id").get<EntityId>() <= 2000000000ULL)
									choices.insert(contact.at("id").get<std::int64_t>());
						for (const auto choice : choices)
						{
							OperatingCommand command{address.ship,
							                         std::string(operating_action(channel.action).name),
							                         choice,
							                         {},
							                         controller.definition.id,
							                         channel.id,
							                         controller.generation};
							auto descriptor = encode_operating_command(command);
							descriptor["id"] =
							    "combat." + controller.definition.id + "." + channel.id + "." + std::to_string(choice);
							descriptor["label"] =
							    choice ? controller.definition.id : "Cancel " + controller.definition.id;
							descriptor["combat_choice"] = (channel.action == OperatingAction::WeaponTarget ||
							                               channel.action == OperatingAction::DirectedTarget)
							                                  ? "weapon"
							                              : channel.action == OperatingAction::SensorTarget ? "sensor"
							                                                                                : "repair";
							if (channel.action == OperatingAction::SensorTarget)
							{
								for (const auto &other : entity.get<EntityControls>().controllers)
									for (const auto &target : other.definition.commands)
										if (other.definition.console == address.console &&
										    target.shared_target_controller == controller.definition.id &&
										    target.shared_target_channel == channel.id)
										{
											if (target.action == OperatingAction::WeaponTarget)
											{
												descriptor["shared_combat_target"] = true;
												descriptor["combat_choice"] = "weapon";
											}
											else if (target.action == OperatingAction::SensorTarget)
												descriptor["shared_scan_target"] = true;
										}
								descriptor["label"] = channel.label_key.empty()
								                          ? (choice ? "Scan target" : "Clear scan target")
								                          : (choice ? channel.label_key : "Clear " + channel.label_key);
								descriptor["label_prefix"] = controller.definition.id + ": ";
							}
							buttons.push_back(std::move(descriptor));
						}
						continue;
					}

					if (!channel.choices.empty())
						for (const auto &[value, label] : channel.choices)
							values.insert(value);
					else if (channel.action == OperatingAction::Lateral)
						values = {-1000, 0, 1000};
					else if (channel.type == KnowledgeType::Boolean)
						values = {0, 1};
					else
						values = {std::clamp(state.value - step, channel.minimum, channel.maximum),
						          std::clamp(state.value + step, channel.minimum, channel.maximum)};
					if (channel.action != OperatingAction::Lateral && channel.choices.empty() && channel.minimum <= 0 &&
					    channel.maximum >= 0)
						values.insert(0);
					const bool ftl_toggle =
					    channel.action == OperatingAction::SystemEnabled && channel.consumers.size() == 1 &&
					    entity.get<EquipmentIndex>().values.at(channel.consumers.front()).has<FtlReserve>();
					if (ftl_toggle)
						values = {state.value ? 0 : 1};
					for (const auto value : values)
					{
						if (state.request_id == std::numeric_limits<std::uint64_t>::max())
							continue;
						OperatingCommand command{address.ship,
						                         std::string(operating_action(channel.action).name),
						                         value,
						                         {},
						                         controller.definition.id,
						                         channel.id,
						                         controller.generation,
						                         channel.semantics == CommandSemantics::Request ? state.request_id + 1
						                                                                        : 0};
						command.arguments = state.arguments;
						if (!operating_arguments_valid(channel.action, value, command.arguments))
							continue;
						auto descriptor = encode_operating_command(command);
						descriptor["id"] =
						    "controller." + controller.definition.id + "." + channel.id + "." + std::to_string(value);
						descriptor["label"] = controller.definition.id + " " + channel.id + " " + std::to_string(value);
						if (channel.action == OperatingAction::Lateral || channel.action == OperatingAction::Boost ||
						    channel.action == OperatingAction::Impulse)
						{
							descriptor["label_key"] = channel.action == OperatingAction::Lateral
							                              ? (value < 0   ? "Thrust port"
							                                 : value > 0 ? "Thrust starboard"
							                                             : "Release flight controls")
							                          : channel.action == OperatingAction::Boost
							                              ? (value ? "Hold boost" : "Release boost")
							                              : (value ? "Start impulse" : "Cancel impulse");
							descriptor["momentary"] = channel.action != OperatingAction::Impulse && value != 0;
						}
						if (channel.choices.contains(value))
							descriptor["label_key"] = channel.choices.at(value);
						else if (!channel.label_key.empty())
						{
							descriptor["label_key"] = channel.label_key;
							descriptor["label_value"] = true;
						}
						if (ftl_toggle)
						{
							descriptor["label_key"] = value ? "Turn FTL battery on" : "Turn FTL battery off";
							descriptor["label_value"] = false;
						}
						buttons.push_back(std::move(descriptor));
					}
					const auto fields = operating_arguments(channel.action);
					for (std::size_t argument = 0; argument < fields.size(); ++argument)
					{
						std::set<std::int64_t> choices;
						if (fields[argument].name == "contact")
						{
							for (const auto &contact : result.contacts)
								if (contact.id <= static_cast<EntityId>(fields[argument].maximum))
									choices.insert(contact.id);
						}
						else
							for (const auto delta : {-1000, 1000})
								choices.insert(std::clamp(state.arguments[argument] + delta, fields[argument].minimum,
								                          fields[argument].maximum));
						for (const auto value : choices)
						{
							OperatingCommand command{address.ship,
							                         std::string(operating_action(channel.action).name),
							                         state.value,
							                         {},
							                         controller.definition.id,
							                         channel.id,
							                         controller.generation};
							command.arguments = state.arguments;
							command.arguments[argument] = value;
							auto descriptor = encode_operating_command(command);
							descriptor["id"] = "controller." + controller.definition.id + "." + channel.id + "." +
							                   std::string(fields[argument].name) + "." + std::to_string(value);
							descriptor["label"] = controller.definition.id + " " + channel.id + " " +
							                      std::string(fields[argument].name) + " " + std::to_string(value);
							buttons.push_back(std::move(descriptor));
						}
					}
				}
	}
	if (entity.has<EntityControls>())
		for (const auto &controller : entity.get<EntityControls>().controllers)
			if (controller.definition.console == seat.id && controller.human)
				for (const auto &name : impl_->definition(address.ship).message_templates)
				{
					OperatingCommand command{address.ship, "message." + name,    0, {}, controller.definition.id,
					                         {},           controller.generation};
					command.console = address.console;
					if (!impl_->meta(console, command, false))
						continue;
					auto descriptor = encode_operating_command(command);
					descriptor["id"] = "message." + controller.definition.id + "." + name;
					descriptor["label"] = "Send " + name;
					buttons.push_back(std::move(descriptor));
				}
	const auto &spec = impl_->definition(address.ship);
	for (const auto &name : spec.publications)
	{
		OperatingCommand command{address.ship, "publish." + name, 0, {}};
		command.console = address.console;
		if (impl_->meta(console, command, false))
			button("publish." + name, "Publish " + name, "publish." + name, {}, 0);
	}
	for (auto &button : buttons)
		button["console"] = address.console;
	return result;
}
} // namespace phoenix
