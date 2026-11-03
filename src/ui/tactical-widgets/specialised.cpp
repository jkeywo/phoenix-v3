module;
#include <RmlUi/Core.h>
#include <RmlUi/Core/StyleSheetContainer.h>
#include <RmlUi/Core/StreamMemory.h>
#include <RmlUi/Core/ElementText.h>
#include <RmlUi/Core/Elements/ElementFormControl.h>
#include <RmlUi/Core/StringUtilities.h>
#include <raylib.h>
#include <raymath.h>
#include <rlgl.h>
#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iomanip>
#include <iterator>
#include <limits>
#include <list>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>
#include <nlohmann/json.hpp>
module phoenix.ui.tactical_widgets;
import phoenix.ui.engineering_widgets;
import phoenix.application.preferences;
import phoenix.content.controllers;
namespace phoenix
{
SpecialisedPanel TacticalWidgets::render_specialised(const std::string &component, ConsoleModel &model,
                                                     const Catalog &catalog,
                                                     std::vector<std::pair<ConsoleModel *, std::string>> sources,
                                                     const std::string &prefix)
{

	SpecialisedPanel result{prefix, component};
	auto &markup = result.markup;
	auto &buttons = result.buttons;
	auto &task_actions_ = result.actions;
	const auto id = [&](const std::string &name)
	{
		return result.id(name);
	};
	const auto esc = [](const std::string &s)
	{
		return Rml::StringUtilities::EncodeRml(s);
	};
	const auto text = [&](const std::string &s, const std::string &compact = std::string{})
	{
		return result.text(s, compact);
	};
	const auto button = [&](const std::string &action, const std::string &label, std::optional<bool> selected = {},
	                        const std::string &visible = std::string{}, bool enabled = true)
	{
		return result.button(action, label, selected, visible, enabled);
	};
	const auto &primary = *model.observation();
	const auto profile = primary.gameplay.value("profile", std::string{});
	const int stars = primary.gameplay.value("profile_stars", Json::object()).value(profile, 0);
	const auto control = [&](ConsoleModel &source, const std::string &source_prefix, const std::string &action,
	                         const std::string &equipment, std::int64_t value)
	{
		return source.observation()
		           ? permitted_control_action(*source.observation(), source_prefix, action, equipment, value)
		           : std::string{};
	};
	if (component == "target-info")
	{
		const auto selected = console().empty() ? model.sensor_contact() : this->model().sensor_contact();
		if (selected)
			markup += button("task-detail/target", localise("Contact details"), {}, "i");
		for (const auto &[key, value] : this->target_readouts(model, catalog, false))
			markup += text(value);
	}
	else if (component == "target-actions")
	{
		markup = "<div class=\"target-actions\">";
		const auto target = this->console().empty() ? model.sensor_contact() : this->model().sensor_contact();
		const bool positional = target && !model.manual_active() &&
		                        std::any_of(primary.contacts.begin(), primary.contacts.end(),
		                                    [&](const auto &contact)
		                                    {
			                                    return contact.id == target;
		                                    });
		markup += button("contact-navigation/follow_contact", localise("Follow"), {}, {}, positional);
		markup += button("contact-navigation/match_speed", localise("Match speed"), {}, {}, positional);
		const auto dock = model.target_docking_control(target);
		markup += button(dock ? dock->id : "unavailable-dock", dock ? dock->label : localise("Dock"), {}, {},
		                 dock.has_value());
		markup += "</div>";
	}
	else if (component == "weapons" || component == "missiles")
	{
		markup =
		    "<h2>" + localise(component == "weapons" ? "Weapons" : "Missiles") + "</h2><div class=\"courier-weapons\">";
		std::set<std::string> shown, salvos;

		std::reverse(sources.begin(), sources.end());
		for (auto &[source, prefix] : sources)
			if (source->observation())
			{
				const auto &view = *source->observation();
				const auto combat = view.gameplay.value("combat", Json::object());
				const auto widgets = console_control_widgets(catalog, view);
				auto main_data = [&](const std::string &equipment, const std::string &binding)
				{
					for (const auto &widget : widgets)
						for (const auto &datum : widget.data)
							if (datum.equipment == equipment && datum.binding == binding &&
							    datum.destination == WidgetDataDestination::Main)
								return true;
					return false;
				};
				auto meter = [&](const std::string &equipment, const std::string &binding)
				{
					for (const auto &widget : widgets)
						for (const auto &datum : widget.data)
							if (datum.equipment == equipment && datum.binding == binding && !datum.meter.empty() &&
							    datum.destination == WidgetDataDestination::Main)
							{
								std::optional<float> fraction;
								if (datum.quantity && datum.capacity)
									fraction = std::clamp(static_cast<float>(*datum.quantity) /
									                          static_cast<float>(*datum.capacity),
									                      0.f, 1.f);
								const auto display = fraction && datum.meter != "ammo"
								                         ? std::to_string(static_cast<int>(*fraction * 100.f + .5f)) + "%"
								                         : datum.text;
								return result.meter(datum.meter, datum.label + ": " + display, fraction);
							}
					return std::string{};
				};
				for (const auto &w : combat.value("weapons", Json::array()))
				{
					const auto identity = w.at("id").get<std::string>();
					const bool missile = w.value("load_ticks", std::int64_t{}) > 0 && !w.value("point_defence", false);
					if (missile != (component == "missiles") || !shown.insert(identity).second)
						continue;
					const auto system = std::find_if(view.systems.begin(), view.systems.end(),
					                                 [&](const auto &s)
					                                 {
						                                 return s.id == identity;
					                                 });
					if (system == view.systems.end())
						continue;
					const ControlWidgetView *details = nullptr;
					for (const auto &widget : widgets)
						if (std::any_of(widget.data.begin(), widget.data.end(),
						                [&](const auto &datum)
						                {
							                return datum.equipment == identity;
						                }) &&
						    (!details || widget.data.size() > details->data.size()))
							details = &widget;
					markup += "<div class=\"courier-weapon\"><div class=\"weapon-heading\">";
					markup += "<div class=\"weapon-switch\">";
					const auto toggle = control(*source, prefix, "system_enabled", identity, system->enabled ? 0 : 1);
					const auto weapon_label =
					    localise(w.value("label", std::string{}).empty() ? engineering_name(identity)
					                                                     : w.at("label").get<std::string>());
					markup += missile && toggle.empty()
					              ? "<h3>" + esc(weapon_label) + "</h3>"
					              : button(toggle, weapon_label + (toggle.empty() ? " " + localise("Auto") : ""),
					                       system->enabled);
					markup += "</div>";
					if (details)
						markup += "<div class=\"weapon-details\">" +
						          button("control-details/" + view.console + "/" + details->id,
						                 localise("Details") + ": " + weapon_label, {}, "i") +
						          "</div>";
					markup += "</div>";
					if (main_data(identity, "system.status"))
					{
						const auto status =
						    system->status.empty() ? (system->enabled ? "Operational" : "Off") : system->status;
						markup += "<div class=\"weapon-status " +
						          std::string(status == "Operational" ? "ready" : status == "Off" ? "off" : "warning") +
						          "\" role=\"img\" aria-label=\"" + esc(localise(status)) + "\">" +
						          text(localise(status)) + "</div>";
					}
					const auto mags = w.value("magazines", std::vector<std::string>{});
					const auto selected = w.value("selected", std::size_t{});
					if (!mags.empty() && selected < mags.size())
					{
						const auto magazine = std::find_if(view.systems.begin(), view.systems.end(),
						                                   [&](const auto &s)
						                                   {
							                                   return s.id == mags[selected];
						                                   });
						const auto ammo = magazine == view.systems.end()         ? 0
						                  : magazine->telemetry.contains("ammo") ? magazine->telemetry.at("ammo")
						                                                         : 0;
						auto magazine_name = engineering_name(mags[selected]);
						if (magazine_name.ends_with(" mag"))
							magazine_name.replace(magazine_name.size() - 4, 4, " magazine");
						std::string type = localise(magazine_name);
						for (const auto &mag : combat.value("magazines", Json::array()))
							if (mag.at("id") == mags[selected])
							{
								const auto label = mag.value("resource_labels", Json::object())
								                       .value(mag.value("selected_resource", std::string{}), type);
								if (label != mag.value("selected_resource", std::string{}))
									type = localise(label);
							}
						if (missile)
						{
							if (main_data(mags[selected], "system.ammo"))
								markup += text(type + ": " + std::to_string(ammo));
							const bool show_type = main_data(identity, "weapon.loaded_resource"),
							           show_quantity = main_data(identity, "weapon.loaded_quantity");
							if (show_type || show_quantity)
							{
								const auto loaded = w.value("loaded", std::int64_t{-1});
								std::string loaded_type = loaded >= 0 && static_cast<std::size_t>(loaded) < mags.size()
								                              ? localise(engineering_name(mags[loaded]))
								                              : localise("Empty");
								if (loaded >= 0 && static_cast<std::size_t>(loaded) < mags.size())
									for (const auto &mag : combat.value("magazines", Json::array()))
										if (mag.at("id") == mags[loaded])
										{
											const auto resource = w.value("loaded_resource", std::string{});
											const auto label = mag.value("resource_labels", Json::object())
											                       .value(resource, loaded_type);
											if (label != resource)
												loaded_type = localise(label);
										}
								markup += text(localise("Loaded") + ": " + (show_type ? loaded_type : "") +
								               (show_type && show_quantity ? " / " : "") +
								               (show_quantity ? w.at("loaded_quantity").dump() : ""));
							}
							markup += text(localise(w.value("unloading", 0) > 0         ? "Unloading"
							                        : w.value("loading", 0) > 0         ? "Loading"
							                        : w.value("loaded_quantity", 0) > 0 ? "Loaded"
							                                                            : "Empty"));
							if (stars >= 2)
								for (std::size_t i = 0; i < mags.size(); ++i)
									markup += button(control(*source, prefix, "magazine_select", identity, i),
									                 localise(engineering_name(mags[i])), selected == i);
						}
						else if (main_data(mags[selected], "system.ammo"))
						{
							auto bar = meter(mags[selected], "system.ammo");
							markup += bar.empty() ? text(localise("Ammo") + ": " + std::to_string(ammo),
							                             localise("Ammo") + " " + std::to_string(ammo))
							                      : bar;
						}
					}
					if (missile)
						for (const auto &owner : view.gameplay.value("controllers", Json::array()))
							for (const auto &[channel, state] : owner.at("commands").items())
								if (state.at("action") == "weapon_fire" && state.value("request_id", std::uint64_t{}) &&
								    !state.value("pending", false) && !state.value("accepted", false) &&
								    std::find(state.at("consumers").begin(), state.at("consumers").end(),
								              Json(identity)) != state.at("consumers").end())
									markup += text(localise("Not fired: tube not ready or target unavailable"));
					const auto battery = missile ? std::string{} : meter(identity, "system.stored_energy");
					markup += battery;
					if (battery.empty() && !missile && main_data(identity, "system.stored_energy") &&
					    system->telemetry.contains("stored_energy"))
					{
						const auto stored = system->telemetry.at("stored_energy");
						const auto capacity =
						    system->telemetry.contains("energy_capacity") ? system->telemetry.at("energy_capacity") : 0;
						markup += text(
						    localise("Battery") + ": " +
						        (capacity > 0 ? std::to_string(stored * 100 / capacity) + "%" : std::to_string(stored)),
						    capacity > 0 ? std::to_string(stored * 100 / capacity) + "%" : std::to_string(stored));
					}
					if (stars >= 3)
					{
						const auto target = source->sensor_contact();
						if (target)
							markup +=
							    button(control(*source, prefix, "weapon_target", identity, target), localise("Target"));
						if (w.value("point_defence", false))
							for (int i = 0; i < 3; ++i)
								markup += button(control(*source, prefix, "weapon_mode", identity, i),
								                 localise(i == 0   ? "Hold fire"
								                          : i == 1 ? "Intercept"
								                                   : "Selected target"),
								                 w.value("mode", 0) == i);
					}
					std::set<std::pair<std::string, std::string>> extra;
					for (const auto &widget : widgets)
						for (const auto &datum : widget.data)
							if (datum.equipment == identity)
							{
								if (datum.destination == WidgetDataDestination::Main &&
								    datum.binding != "system.status" &&
								    (missile || datum.binding != "system.stored_energy") &&
								    (!missile || (datum.binding != "weapon.loaded_quantity" &&
								                  datum.binding != "weapon.loaded_resource")) &&
								    extra.emplace(datum.equipment, datum.binding).second)
								{
									const auto bar = meter(identity, datum.binding);
									markup += bar.empty() ? text(datum.label + ": " + datum.text) : bar;
								}
							}
					markup += "</div>";
				}
				if (component == "missiles")
					for (const auto &d : view.gameplay.value("controls", Json::array()))
						if (d.value("action", "") == "weapon.salvo" &&
						    salvos.insert(view.console + "/" + d.value("system", "")).second)
							markup += button(prefix + descriptor_action(d.at("id")), localise("Fire"));
			}
		markup += "</div>";
	}
	return result;
}
} // namespace phoenix
