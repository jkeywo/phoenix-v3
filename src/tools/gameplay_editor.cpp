module;
#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>
#include <algorithm>
#include <cstdint>
#include <functional>
#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <exception>
#include <set>
#include <string>
#include <utility>
#include <vector>

module phoenix.tools;
import phoenix.content;
import phoenix.files;
import phoenix.presentation;
import phoenix.session.api;

namespace phoenix
{
namespace
{
#include "editor_controls.hpp"
struct ScopedTree
{
	bool open;
	explicit ScopedTree(const char *label) : open(ImGui::TreeNode(label))
	{
	}
	explicit operator bool() const
	{
		return open;
	}
	~ScopedTree()
	{
		if (open)
			ImGui::TreePop();
	}
	ScopedTree(const ScopedTree &) = delete;
};
struct ScopedCombo
{
	bool open;
	ScopedCombo(const char *label, const char *preview) : open(ImGui::BeginCombo(label, preview))
	{
	}
	explicit operator bool() const
	{
		return open;
	}
	~ScopedCombo()
	{
		if (open)
			ImGui::EndCombo();
	}
	ScopedCombo(const ScopedCombo &) = delete;
};
struct ScopedChild
{
	ScopedChild(const char *name, ImVec2 size, ImGuiWindowFlags flags)
	{
		ImGui::BeginChild(name, size, ImGuiChildFlags_Borders, flags);
	}
	~ScopedChild()
	{
		ImGui::EndChild();
	}
	ScopedChild(const ScopedChild &) = delete;
};
struct GameplayEditor
{

	bool controller_probe = false;
	mutable std::string probe_action;
	std::uint64_t probe_tick = 0;
	Json feedback = Json::object();
	std::optional<GameplayTestRequest> test_request;
	std::string entity, tree, test_ship, test_slot, test_map;
	std::string inspected_controller, selected_system, system_entity, message_tree;
	Json vocabulary = Json::object();
	Json composition = Json::object();
	Catalog composition_catalog;
	int selected_state = -1;
	bool pseudo = false;
	std::string label(const char *text) const
	{
		return translate(text, pseudo) + "###" + text;
	}
	void text(const char *message) const
	{
		ImGui::TextWrapped("%s", translate(message, pseudo).c_str());
	}
	bool button(const char *message) const
	{
		const bool pressed = ImGui::Button(label(message).c_str());
		if (controller_probe && probe_action == message)
		{
			probe_action.clear();
			return true;
		}
		return pressed;
	}
	void heading(const char *message) const
	{
		ImGui::SeparatorText(translate(message, pseudo).c_str());
	}
	bool string(Json &value, const char *key, const char *title = nullptr)
	{
		return edit_field(label(title ? title : key).c_str(), value, key, std::string{});
	}
	void audio_fields(Json &owner, const char *key, bool shield = false)
	{
		ScopedId identity(key);
		ScopedTree panel(key);
		if (!panel)
			return;
		if (!owner.contains(key) && button("Add audio recording"))
			owner[key] = {{"asset", ""}, {"gain", shield ? .06 : 1.0}};
		if (!owner.contains(key))
			return;
		auto &clip = owner[key];
		string(clip, "asset");
		edit_field(label("gain").c_str(), clip, "gain", 1.0);
		if (shield)
		{
			edit_field(label("peak_gain").c_str(), clip, "peak_gain", .8);
			edit_field(label("decay_per_second").c_str(), clip, "decay_per_second", 1.5);
			integer(clip, "damage_threshold", 1);
			integer(clip, "damage_full_spike", 30);
		}
		if (button("Remove audio recording"))
			owner.erase(key);
	}
	void optional_string(Json &value, const char *key)
	{
		ScopedId identity(key);
		string(value, key);
		if (value.contains(key))
		{
			ImGui::SameLine();
			if (button("Use inherited or default value"))
				value.erase(key);
		}
	}
	bool integer(Json &value, const char *key, std::int64_t fallback = 0)
	{
		return edit_field(label(key).c_str(), value, key, fallback);
	}
	bool boolean(Json &value, const char *key, bool fallback = false)
	{
		return edit_field(label(key).c_str(), value, key, fallback);
	}
	bool decimal(Json &value, const char *key, double fallback)
	{
		return edit_field(label(key).c_str(), value, key, fallback);
	}
	void gameplay_panel(Json &owner)
	{
		ScopedId identity("gameplay-panel");
		if (ScopedTree section(label("Gameplay panel layout").c_str()); section)
		{
			if (owner.contains("gameplay_panel"))
			{
				auto &panel = owner["gameplay_panel"];
				decimal(panel, "x", 0.0);
				decimal(panel, "y", 0.0);
				decimal(panel, "width", 1.0);
				decimal(panel, "height", 1.0);
				text("Coordinates are relative to the Console, from zero to one. The positive rectangle must fit "
				     "inside it.");
				if (button("Use default gameplay panel layout"))
					owner.erase("gameplay_panel");
			}
			else if (button("Set gameplay panel layout"))
				owner["gameplay_panel"] = {{"x", 0.0}, {"y", 0.0}, {"width", 1.0}, {"height", 1.0}};
		}
	}
	bool choice(Json &value, const char *key, const std::vector<std::string> &options, bool optional = false)
	{
		const auto current = value.value(key, std::string{});
		bool changed = false;
		if (ScopedCombo scope(label(key).c_str(),
		                      current.empty() ? translate("None", pseudo).c_str() : current.c_str());
		    scope)
		{
			if (optional && ImGui::Selectable(label("None").c_str(), current.empty()))
			{
				value.erase(key);
				changed = true;
			}
			for (const auto &option : options)
				if (ImGui::Selectable(option.c_str(), option == current))
				{
					value[key] = option;
					changed = true;
				}
		}
		return changed;
	}
	std::string identity(const Json &row) const
	{
		if (!row.is_object() || !row.contains("id"))
			return {};
		const auto &value = row.at("id");
		if (value.is_string())
			return value.get<std::string>();
		if (value.is_number_integer() || value.is_number_unsigned())
			return value.dump();
		return {};
	}
	std::vector<std::string> ids(const Json &array) const
	{
		std::vector<std::string> result;
		if (array.is_array())
		{
			for (const auto &row : array)
				if (const auto name = identity(row); !name.empty())
					result.push_back(name);
		}
		else if (array.is_object())
		{
			for (auto it = array.begin(); it != array.end(); ++it)
				result.push_back(it.key());
		}
		return result;
	}
	std::string fresh(const Json &array, const std::string &prefix) const
	{
		const auto names = ids(array);
		for (unsigned i = 1;; ++i)
		{
			auto name = prefix + std::to_string(i);
			if (std::find(names.begin(), names.end(), name) == names.end())
				return name;
		}
	}
	void names(Json &value, const char *key, const std::vector<std::string> &options)
	{
		ScopedId id_scope_0(key);
		text(key);
		for (const auto &option : options)
		{
			bool checked = value.contains(key) &&
			               std::find(value.at(key).begin(), value.at(key).end(), Json(option)) != value.at(key).end();
			if (ImGui::Checkbox(option.c_str(), &checked))
			{
				if (!value.contains(key))
					value[key] = Json::array();
				auto &array = value[key];
				if (checked)
					array.push_back(option);
				else
					array.erase(std::find(array.begin(), array.end(), Json(option)));
			}
		}
	}
	void strings(Json &value, const char *key)
	{
		ScopedId identity(key);
		if (ScopedTree scope(label(key).c_str()); scope)
		{
			if (value.contains(key) && value.at(key).is_array())
				array_rows(value[key],
				           [&](Json &row, std::size_t)
				           {
					           auto field = row.get<std::string>();
					           if (edit_value(label("Value").c_str(), field))
						           row = field;
					           ImGui::SameLine();
					           return button("Remove");
				           });
			if (button("Add value"))
				append_row(value, key, "");
		}
	}
	void numbers(Json &value, const char *key)
	{
		ScopedId identity(key);
		heading(key);
		if (value.contains(key))
			array_rows(value[key],
			           [&](Json &row, std::size_t)
			           {
				           auto field = row.get<std::int64_t>();
				           if (edit_value(label("Level").c_str(), field))
					           row = field;
				           ImGui::SameLine();
				           return button("Remove level");
			           });
		if (button("Add level"))
			append_row(value, key, 0);
	}
	void records(Json &parent, const char *key, Json prototype, const std::function<void(Json &)> &inspect)
	{
		ScopedId id_scope_3(key);
		heading(key);
		if (button("Add record"))
		{
			if (!parent.contains(key))
				parent[key] = Json::array();
			if (prototype.contains("id"))
				prototype["id"] = fresh(parent.at(key), prototype.at("id").get<std::string>());
			parent[key].push_back(prototype);
		}
		if (parent.contains(key) && parent.at(key).is_array())
			array_rows(parent[key],
			           [&](Json &row, std::size_t i)
			           {
				           const auto title = row.is_object()
				                                  ? row.value("id", row.value("kind", std::to_string(i + 1)))
				                                  : std::to_string(i + 1);
				           if (controller_probe)
					           ImGui::SetNextItemOpen(true, ImGuiCond_Always);
				           if (ScopedTree scope((title + "###record").c_str()); scope)
				           {
					           inspect(row);
					           if (button("Move earlier") && i > 0)
						           std::swap(parent[key][i], parent[key][i - 1]);
					           ImGui::SameLine();
					           if (button("Move later") && i + 1 < parent.at(key).size())
						           std::swap(parent[key][i], parent[key][i + 1]);
					           return button("Remove record");
				           }
				           return false;
			           });
	}
	void scalar_map(Json &parent, const char *key)
	{
		ScopedId id_scope_5(key);
		heading(key);
		if (parent.contains(key) && parent.at(key).is_object())
			object_rows(parent[key],
			            [&](const std::string &name, Json &row)
			            {
				            if (row.is_string())
					            string(parent[key], name.c_str());
				            else if (row.is_boolean())
					            boolean(parent[key], name.c_str());
				            else if (row.is_number_integer())
					            integer(parent[key], name.c_str());
				            else
				            {
					            ImGui::TextUnformatted(name.c_str());
					            text("Structured value is retained; edit its domain inspector.");
				            }
				            ImGui::SameLine();
				            return button("Remove");
			            });
		static std::map<std::string, std::string> pending_names;
		static std::map<std::string, int> pending_types;
		const auto scope = std::to_string(ImGui::GetID(key));
		ImGui::InputText(label("New field name").c_str(), &pending_names[scope]);
		ImGui::Combo(label("New field type").c_str(), &pending_types[scope], "Integer\0Boolean\0Text\0");
		if (button("Add field") && !pending_names[scope].empty())
		{
			if (!parent.contains(key))
				parent[key] = Json::object();
			if (!parent[key].contains(pending_names[scope]))
				parent[key][pending_names[scope]] = pending_types[scope] == 0   ? Json(0)
				                                    : pending_types[scope] == 1 ? Json(false)
				                                                                : Json("");
			pending_names[scope].clear();
		}
	}
	void structure(Json &value, bool writable = true, unsigned depth = 0)
	{
		if (depth > 32)
		{
			text("Inspector nesting limit reached.");
			return;
		}
		if (value.is_object() || value.is_array())
		{
			std::string remove;
			int remove_index = -1;
			for (auto it = value.begin(); it != value.end(); ++it)
			{
				const auto key = value.is_object() ? it.key() : std::to_string(it - value.begin());
				ScopedId identity(key.c_str());
				if (ScopedTree node(key.c_str()); node)
				{
					structure(it.value(), writable, depth + 1);
					if (writable && button("Remove value"))
					{
						if (value.is_object())
							remove = key;
						else
							remove_index = static_cast<int>(it - value.begin());
					}
				}
			}
			if (!remove.empty())
				value.erase(remove);
			if (remove_index >= 0)
				value.erase(value.begin() + remove_index);
			if (writable)
			{
				static std::map<ImGuiID, std::string> names;
				static std::map<ImGuiID, std::string> types;
				const auto identity = ImGui::GetID("structure-add");
				if (value.is_object())
					ImGui::InputText(label("Property name").c_str(), &names[identity]);
				Json selector{{"Value type", types[identity].empty() ? "integer" : types[identity]}};
				if (choice(selector, "Value type", {"integer", "boolean", "text", "object", "array", "null"}))
					types[identity] = selector.at("Value type").get<std::string>();
				if (button("Add value") && (!value.is_object() || !names[identity].empty()))
				{
					const auto type = selector.at("Value type").get<std::string>();
					Json added = type == "boolean"  ? Json(false)
					             : type == "text"   ? Json("")
					             : type == "object" ? Json::object()
					             : type == "array"  ? Json::array()
					             : type == "null"   ? Json()
					                                : Json(0);
					if (value.is_array())
						value.push_back(added);
					else if (!value.contains(names[identity]))
						value[names[identity]] = added;
				}
			}
		}
		else if (writable)
		{
			if (value.is_string())
			{
				auto field = value.get<std::string>();
				if (ImGui::InputText(label("Value").c_str(), &field))
					value = field;
			}
			else if (value.is_boolean())
			{
				auto field = value.get<bool>();
				if (ImGui::Checkbox(label("Value").c_str(), &field))
					value = field;
			}
			else if (value.is_number_integer())
			{
				auto field = value.get<std::int64_t>();
				if (ImGui::InputScalar(label("Value").c_str(), ImGuiDataType_S64, &field))
					value = field;
			}
			else
			{
				ImGui::TextUnformatted(value.dump().c_str());
			}
		}
		else
			ImGui::TextWrapped("%s",
			                   value.is_string() ? value.get_ref<const std::string &>().c_str() : value.dump().c_str());
	}
	void structured_field(Json &parent, const char *key)
	{
		ScopedId identity(key);
		if (ScopedTree section(label(key).c_str()); section)
		{
			if (parent.contains(key))
				structure(parent[key]);
			else if (button("Create local override"))
				parent[key] = Json::object();
		}
	}
	Json *select_entity(WorkshopDocument &document)
	{
		if (!document.definitions().contains("entities"))
		{
			text("Create an entity definition first.");
			return nullptr;
		}
		auto &entities = document.definitions()["entities"];
		for (auto &row : entities)
			if (row.at("id") == entity)
				return &row;
		text("Select a local definition to edit. Inherited library definitions remain read-only until overridden.");
		return nullptr;
	}
	std::vector<std::string> fitted_ids(const WorkshopDocument &document, const Json &ship)
	{
		composition = Json::object();
		composition_catalog = Catalog{};
		auto fallback = ship.contains("systems") ? ids(ship.at("systems")) : std::vector<std::string>{};
		try
		{
			composition_catalog = document.preview_equipment();
			const auto &expanded = composition_catalog.entities.at(ship.at("id").get<std::string>());
			composition = expanded;
			if (expanded.contains("systems"))
				return ids(expanded.at("systems"));
		}
		catch (const std::exception &error)
		{
			ImGui::TextWrapped("%s", error.what());
		}
		return fallback;
	}
	void destruction_fields(Json &d)
	{
		strings(d, "payloads");
		optional_string(d, "visual");
		optional_string(d, "sound");
		integer(d, "chance_permille", 1000);
		integer(d, "damage");
		integer(d, "radius_mm");
		integer(d, "shield_bypass_permille");
		integer(d, "growth_ticks", 1);
		integer(d, "fade_ticks", 10);
		integer(d, "size_mm", 10000);
		boolean(d, "affects_missiles", true);
	}
	void projectile_fields(Json &p)
	{
		strings(p, "payloads");
		string(p, "visual");
		for (const auto *key : {"speed_mm_s", "lifetime_ticks", "penetration", "damage", "damage_per_m", "spread_mdeg",
		                        "shield_bypass_permille", "durability", "arc_height_mm", "chaos_mdeg",
		                        "blast_radius_mm", "fuse_radius_mm"})
			integer(p, key);
		integer(p, "radius_mm", 100);
		integer(p, "resistance_per_m", 1);
		integer(p, "turn_mdeg_s", 90000);
		integer(p, "seeker_range_mm", 1000000);
		boolean(p, "tracer");
		choice(p, "guidance", {"none", "homing", "chaotic"});
		optional_string(p, "trail");
		if (p.contains("destruction"))
		{
			ScopedId scope("projectile-destruction");
			destruction_fields(p["destruction"]);
			if (button("Remove destruction effect"))
				p.erase("destruction");
		}
		else if (button("Add destruction effect"))
			p["destruction"] = {{"chance_permille", 1000}, {"affects_missiles", false}};
	}
	void system_details(Json &spec, const std::string &fallback_kind = "engine")
	{
		const auto kind = spec.value("kind", fallback_kind);
		for (const char *key : {"power_store", "generator", "power_group", "conduit", "cooling_link", "coolant_source"})
			optional_string(spec, key);
		if (kind != "weapon")
		{
			integer(spec, "energy_per_second");
			integer(spec, "discrete_energy");
		}
		boolean(spec, "powerless_start");
		records(spec, "reporters", {{"id", "reporter"}, {"field", "heat"}, {"above", 0}, {"message", ""}},
		        [&](Json &report)
		        {
			        string(report, "id");
			        string(report, "field");
			        integer(report, "above");
			        string(report, "message");
			        optional_string(report, "recovery_message");
			        integer(report, "debounce_ticks");
			        integer(report, "recovery_ticks");
			        integer(report, "minimum_gap_ticks");
			        text("Physical reporters emit measured facts under both human and AI operation.");
		        });
		if (kind == "weapon" || kind == "radar")
		{
			if (spec.contains("directed"))
			{
				ScopedId scope("directed-capability");
				heading("Directed capability");
				auto &directed = spec["directed"];
				strings(directed, "modes");
				strings(directed, "targeting");
				optional_string(directed, "initial_mode");
				optional_string(directed, "initial_targeting");
				text("One operating mode and one targeting policy are selected independently. Concurrent operations "
				     "use separate capabilities and mounts.");
				if (button("Remove directed capability"))
					spec.erase("directed");
			}
			else if (button("Add directed capability"))
				spec["directed"] = {{"modes", Json::array()}, {"targeting", Json::array()}};
		}
		if (kind == "ftl")
		{
			boolean(spec, "enabled");
			integer(spec, "charge_per_second", 100);
			integer(spec, "charging_loss_per_second", 20);
			integer(spec, "ready_loss_per_second", 50);
			integer(spec, "off_discharge_per_second", 80);
			text("FTL uses a dedicated empty reserve. Charge and loss rates compete for actual reactor flow.");
		}
		if (kind == "displacement" || kind == "transport")
		{
			integer(spec, "range_mm", 100000);
			integer(spec, "capacity", 8);
			integer(spec, "transfer_ticks", 40);
			choice(spec, "mode", {"displacement", "shuttle", "boarding_torpedo"});
			if (spec.value("mode", std::string{"displacement"}) != "displacement")
			{
				string(spec, "craft");
				integer(spec, "count", 1);
				integer(spec, "arrival_distance_mm", 5000);
				integer(spec, "arrival_speed_mm_s", 1000);
				integer(spec, "cruise_speed_mm_s", 10000);
			}
		}
		if (kind == "psychic")
		{
			integer(spec, "range_mm", 500000);
			integer(spec, "arc_mdeg", 90000);
			optional_string(spec, "planning_controller");
			strings(spec, "supported_abilities");
			records(spec, "seats", {{"duty", ""}, {"console", ""}, {"controller", ""}, {"enabled", ""}, {"target", ""}},
			        [&](Json &s)
			        {
				        for (const auto *k : {"duty", "console", "controller", "enabled", "target"})
					        string(s, k);
				        optional_string(s, "ability");
			        });
		}
		if (kind == "shuttle_bay")
		{
			integer(spec, "slots", 4);
			integer(spec, "transfer_ticks", 20);
			integer(spec, "docking_range_mm", 10000);
			integer(spec, "docking_speed_mm_s", 5000);
			integer(spec, "flight_spacing_mm", 0);
			integer(spec, "charge_per_second", 0);
			strings(spec, "initial");
			records(spec, "ammunition", {{"source", ""}, {"destination", ""}, {"rounds_per_second", 1}},
			        [&](Json &v)
			        {
				        string(v, "source");
				        string(v, "destination");
				        integer(v, "rounds_per_second", 1);
			        });
		}

		if (kind == "comms")
			integer(spec, "range_mm", 1000000);
		optional_string(spec, "compartment");
		strings(spec, "abilities");
		if (kind == "weapon" || kind == "turret" || kind == "shield")
		{
			integer(spec, "arc_mdeg", 360000);
			integer(spec, "bearing_mdeg");
			integer(spec, "x_mm");
			integer(spec, "y_mm");
		}
		records(spec, "faults",
		        {{"id", "fault"},
		         {"symptom", "Erratic equipment response"},
		         {"diagnosis", "Damaged mechanism requires repair"},
		         {"chance_permille", 100},
		         {"repair_work", 100}},
		        [&](Json &f)
		        {
			        string(f, "id");
			        string(f, "symptom");
			        string(f, "diagnosis");
			        choice(f, "effect", {"output", "disabled", "feed"});
			        integer(f, "chance_permille", 100);
			        integer(f, "damage_threshold", 1);
			        integer(f, "severity", 1);
			        integer(f, "effectiveness_permille", 500);
			        integer(f, "repair_work", 100);
			        boolean(f, "destroyed");
		        });
		if (kind == "weapon")
		{
			optional_string(spec, "magazine");
			strings(spec, "magazines");
			optional_string(spec, "turret");
			optional_string(spec, "muzzle_marker");
			strings(spec, "intercept_classes");
			text("Weapon timing, costs, range and payloads are authored on its directed operating modes.");
		}
		if (kind == "shield")
		{
			string(spec, "shield_effect");
		}
		if (kind == "turret")
		{
			integer(spec, "traverse_mdeg_s", 45000);
			string(spec, "marker");
			string(spec, "visual");
			integer(spec, "scale_permille", 1000);
			boolean(spec, "flip_y");
		}
		if (kind == "magazine")
		{
			if (!spec.contains("stock"))
				spec["stock"] = {{"capacity_tag", "ammo"}, {"capacity", 10}, {"single_resource", true}};
			{
				ScopedId scope("magazine-stock");
				stock_fields(spec["stock"]);
			}
			if (spec.contains("processor"))
			{
				ScopedId scope("magazine-processor");
				processor_fields(spec["processor"]);
				if (button("Remove magazine processor"))
					spec.erase("processor");
			}
			else if (button("Add magazine processor"))
				spec["processor"] = Json::object();
		}
		if (kind == "repair_team" || kind == "team")
		{
			integer(spec, "hp_per_second", 10);
			integer(spec, "stack_penalty_permille", 500);
			optional_string(spec, "type");
			strings(spec, "capabilities");
			integer(spec, "treatment_ticks", 400);
			integer(spec, "stabilise_ticks", 200);
			integer(spec, "injured_effectiveness_permille", 500);
			records(
			    spec, "orders",
			    {{"id", "order"}, {"label", "Team order"}, {"icon", "team"}, {"target", "compartments"}, {"tree", ""}},
			    [&](Json &o)
			    {
				    string(o, "id");
				    string(o, "label");
				    string(o, "icon");
				    choice(o, "target", {"compartments", "damage", "patients"});
				    optional_string(o, "capability");
				    strings(o, "patient_states");
				    string(o, "tree");
			    });
		}

		if (kind == "engine" || ((kind == "boost" || kind == "impulse") && !spec.contains("targets")))
		{
			integer(spec, "max_speed_mm_s");
			integer(spec, "acceleration_mm_s2");
			integer(spec, "deceleration_mm_s2", spec.value("acceleration_mm_s2", std::int64_t{0}));
		}
		if (kind == "engine" || kind == "steering")
			integer(spec, "turn_mdeg_s");
		if (kind == "radar")
		{
			integer(spec, "range_mm");
			bool long_range = spec.contains("long_range_mm");
			if (ImGui::Checkbox(label("Long-range detection").c_str(), &long_range))
			{
				if (long_range)
					spec["long_range_mm"] = spec.value("range_mm", 100000LL) * 3;
				else
					spec.erase("long_range_mm");
			}
			if (long_range)
				integer(spec, "long_range_mm");
			integer(spec, "arc_mdeg", 360000);
			integer(spec, "bearing_mdeg");
			optional_string(spec, "turret");
			text("Focused acquisition capabilities are authored on the directed scan mode.");
			strings(spec, "shows");
		}
		if (kind == "engine")
		{
			integer(spec, "reverse_speed_mm_s");
			integer(spec, "match_stale_ticks", 100);
		}
		if (kind == "engine" || kind == "lateral")
		{
			integer(spec, "lateral_speed_mm_s");
			integer(spec, "lateral_acceleration_mm_s2");
			boolean(spec, "assist_lateral");
			choice(spec, "lateral_direction", {"both", "port", "starboard"});
		}
		if (kind == "engine" || kind == "steering" || kind == "lateral")
		{
			integer(spec, "power_rate");
			integer(spec, "heat_rate");
		}
		auto mode = [&](Json &definition, bool impulse)
		{
			integer(definition, "acceleration_multiplier_permille", 1000);
			integer(definition, "speed_multiplier_permille", 1000);
			integer(definition, "power_rate");
			integer(definition, "heat_rate");
			if (impulse)
			{
				integer(definition, "charge_ticks", 20);
				integer(definition, "steering_permille");
				integer(definition, "charge_power_rate");
				integer(definition, "charge_energy");
				integer(definition, "minimum_reserve");
			}
		};
		if (kind == "boost" || kind == "impulse")
		{
			if (!spec.contains("targets"))
				mode(spec, kind == "impulse");
			else
			{
				integer(spec, "power_rate");
				integer(spec, "heat_rate");
				if (kind == "impulse")
				{
					integer(spec, "charge_ticks", 20);
					integer(spec, "charge_energy");
					integer(spec, "minimum_reserve");
					integer(spec, "steering_permille");
				}
			}
			if (ScopedTree section(label("Modifier targets").c_str()); section)
			{
				if (spec.contains("targets"))
					structure(spec["targets"], true);
				if (button("Add modifier target"))
				{
					if (!spec.contains("targets"))
						spec["targets"] = Json::array();
					spec["targets"].push_back(
					    {{"system", ""}, {"property", "max_speed_mm_s"}, {"multiplier_permille", 1000}});
				}
			}
		}
		if (kind == "engine")
			for (const char *name : {"boost", "impulse"})
			{
				ScopedId identity(name);
				if (ScopedTree section(label(name).c_str()); section)
				{
					if (spec.contains(name))
					{
						mode(spec[name], std::string(name) == "impulse");
						if (button("Remove mode"))
							spec.erase(name);
					}
					else if (button("Add mode"))
						spec[name] = Json::object();
				}
			}
	}
	void combat(WorkshopDocument &document)
	{
		auto *ship = select_entity(document);
		if (!ship)
			return;
		integer(*ship, "durability");
		rule_bindings_fields(*ship, document);
		integer(*ship, "half_height_mm", 5000);
		if (ship->contains("destruction"))
		{
			ScopedId scope("entity-destruction");
			destruction_fields((*ship)["destruction"]);
			if (button("Remove entity destruction effect"))
				ship->erase("destruction");
		}
		else if (button("Add entity destruction effect"))
			(*ship)["destruction"] = Json::object();
		if (!ship->contains("combat"))
		{
			if (button("Add compartment combat layout"))
				(*ship)["combat"] = {
				    {"decks", Json::array()}, {"connections", Json::array()}, {"destruction", Json::object()}};
			return;
		}
		auto &c = (*ship)["combat"];
		string(c, "repair_console");
		optional_string(c, "access_controller");
		integer(c, "collision_scale", 1);
		integer(c, "collision_threshold_mm_s", 1000);
		records(c, "decks", {{"id", "deck"}, {"weight", 1}, {"compartments", Json::array()}},
		        [&](Json &deck)
		        {
			        string(deck, "id");
			        integer(deck, "weight", 1);
			        records(deck, "compartments",
			                {{"id", "room"},
			                 {"x_mm", 0},
			                 {"y_mm", 0},
			                 {"half_width_mm", 1000},
			                 {"half_length_mm", 1000},
			                 {"hp", 100},
			                 {"resistance_per_m", 10},
			                 {"recovery_hp", 20}},
			                [&](Json &room)
			                {
				                string(room, "id");
				                for (const auto *key : {"x_mm", "y_mm", "half_width_mm", "half_length_mm", "hp",
				                                        "resistance_per_m", "recovery_hp"})
					                integer(room, key);
			                });
			        const auto origin = ImGui::GetCursorScreenPos();
			        const float size = 280;
			        auto *draw = ImGui::GetWindowDrawList();
			        std::int64_t extent = 1;
			        for (const auto &room : deck.value("compartments", Json::array()))
				        extent = std::max({extent,
				                           std::abs(room.value("x_mm", std::int64_t{0})) +
				                               room.value("half_width_mm", std::int64_t{1}),
				                           std::abs(room.value("y_mm", std::int64_t{0})) +
				                               room.value("half_length_mm", std::int64_t{1})});
			        const float scale = size / (extent * 2.f);
			        for (const auto &room : deck.value("compartments", Json::array()))
			        {
				        const float x = room.value("x_mm", 0.f), y = room.value("y_mm", 0.f),
				                    hw = room.value("half_width_mm", 1.f), hl = room.value("half_length_mm", 1.f);
				        const ImVec2 a{origin.x + size / 2 + (y - hl) * scale, origin.y + size / 2 - (x + hw) * scale},
				            b{origin.x + size / 2 + (y + hl) * scale, origin.y + size / 2 - (x - hw) * scale};
				        draw->AddRectFilled(a, b, IM_COL32(20, 65, 65, 255));
				        draw->AddRect(a, b, IM_COL32(210, 200, 160, 255));
				        draw->AddText(a, IM_COL32(240, 230, 190, 255), room.value("id", std::string{}).c_str());
			        }
			        ImGui::Dummy({size, size});
		        });
		records(c, "connections", {{"from", ""}, {"to", ""}, {"travel_ticks", 20}},
		        [&](Json &edge)
		        {
			        string(edge, "from");
			        string(edge, "to");
			        integer(edge, "travel_ticks", 20);
		        });
		if (!c.contains("destruction"))
			c["destruction"] = Json::object();
		integer(c, "half_height_mm", 5000);
		auto &d = c["destruction"];
		destruction_fields(d);
		string(d, "visual");
		string(d, "sound");
		integer(d, "growth_ticks", 10);
		integer(d, "fade_ticks", 20);
		integer(d, "size_mm", 100000);
	}

	void personnel(Json &ship, WorkshopDocument &document)
	{
		ScopedTree panel("Personnel and departments");
		if (!panel)
			return;
		if (!ship.contains("personnel") && button("Add personnel roster"))
			ship["personnel"] = {{"departments", Json::array()},  {"duties", Json::array()},
			                     {"officers", Json::array()},     {"traits", Json::array()},
			                     {"compartments", Json::array()}, {"connections", Json::array()}};
		if (!ship.contains("personnel"))
			return;
		auto &p = ship["personnel"];
		if (!p.contains("casualties"))
			p["casualties"] = Json::object();
		{
			ScopedTree policy("Casualty policy");
			if (policy)
			{
				auto &c = p["casualties"];
				integer(c, "significant_damage", 20);
				integer(c, "heavy_below_permille", 300);
				boolean(c, "lethal", false);
				integer(c, "lethal_damage", 80);
				integer(c, "stun_permille", 150);
				integer(c, "minor_permille", 100);
				integer(c, "severe_permille", 30);
				integer(c, "killed_permille", 10);
			}
		}
		records(p, "facilities", {{"id", "sickbay"}, {"compartment", ""}, {"capacity", 1}, {"recovery_ticks", 1200}},
		        [&](Json &f)
		        {
			        string(f, "id");
			        string(f, "compartment");
			        integer(f, "capacity", 1);
			        integer(f, "recovery_ticks", 1200);
		        });
		records(p, "departments", {{"id", "department"}, {"label", "Department"}, {"station", ""}},
		        [&](Json &d)
		        {
			        string(d, "id");
			        string(d, "label");
			        string(d, "station");
		        });
		strings(p, "compartments");
		records(p, "connections", {{"from", ""}, {"to", ""}, {"ticks", 40}},
		        [&](Json &c)
		        {
			        string(c, "from");
			        string(c, "to");
			        integer(c, "ticks", 40);
		        });
		records(p, "traits", {{"id", "trait"}, {"label", "Expertise"}, {"rules", Json::array()}},
		        [&](Json &t)
		        {
			        string(t, "id");
			        string(t, "label");
			        rule_bindings_fields(t, document);
		        });
		records(p, "duties",
		        {{"id", "duty"},
		         {"label", "Duty"},
		         {"department", ""},
		         {"controller", ""},
		         {"compartment", ""},
		         {"bindings", Json::object()}},
		        [&](Json &d)
		        {
			        string(d, "id");
			        string(d, "label");
			        choice(d, "department", ids(p["departments"]));
			        optional_string(d, "controller");
			        optional_string(d, "console");
			        optional_string(d, "team");
			        optional_string(d, "choir");
			        choice(d, "required_trait", ids(p["traits"]), true);
			        string(d, "compartment");
			        auto &effects = d["bindings"];
			        if (effects.is_null())
				        effects = Json::object();
			        std::string old_target, new_target;
			        for (auto it = effects.begin(); it != effects.end();)
			        {
				        ScopedId identity(it.key().c_str());
				        auto target = it.key();
				        if (edit_value(label("System target").c_str(), target))
				        {
					        old_target = it.key();
					        new_target = target;
				        }
				        strings(effects, it.key().c_str());
				        if (button("Remove target binding"))
					        it = effects.erase(it);
				        else
					        ++it;
			        }
			        if (!old_target.empty() && !new_target.empty() && !effects.contains(new_target) &&
			            effects.contains(old_target))
			        {
				        effects[new_target] = effects.at(old_target);
				        effects.erase(old_target);
			        }
			        if (button("Add target binding"))
				        effects[fresh(effects, "system")] = Json::array();
		        });
		records(p, "officers",
		        {{"id", "officer"},
		         {"name", "New officer"},
		         {"department", ""},
		         {"compartment", ""},
		         {"traits", Json::array()}},
		        [&](Json &o)
		        {
			        string(o, "id");
			        string(o, "name");
			        optional_string(o, "portrait");
			        strings(o, "abilities");
			        rule_bindings_fields(o, document);
			        choice(o, "department", ids(p["departments"]));
			        choice(o, "duty", ids(p["duties"]), true);
			        string(o, "compartment");
			        strings(o, "traits");
			        choice(o, "condition", {"healthy", "minor", "severe"});
		        });
	}
	void fitted(WorkshopDocument &document)
	{
		auto *selected = select_entity(document);
		if (!selected)
			return;
		auto &ship = *selected;
		personnel(ship, document);
		{
			ScopedTree panel("Host audio");
			if (panel)
			{
				if (!ship.contains("audio"))
					ship["audio"] = Json::object();
				audio_fields(ship["audio"], "ambient");
				audio_fields(ship["audio"], "engine");
			}
		}
		{
			ScopedTree panel("Storage and logistics");
			if (panel)
			{
				if (!ship.contains("logistics") && button("Configure material stores"))
					ship["logistics"] = {{"controller", ""}, {"stores", Json::array()}, {"consumers", Json::array()}};
				if (ship.contains("logistics"))
				{
					auto &l = ship["logistics"];
					string(l, "controller");
					integer(l, "units_per_second", 20);
					integer(l, "concurrency", 4);
					integer(l, "batch_size", 10);
					records(l, "stores",
					        {{"id", "hold"},
					         {"compartment", ""},
					         {"stock", {{"capacity_tag", "cargo"}, {"capacity", 100}}},
					         {"targets", Json::object()}},
					        [&](Json &m)
					        {
						        string(m, "id");
						        string(m, "compartment");
						        optional_string(m, "controller");
						        optional_string(m, "system");
						        stock_fields(m["stock"]);
						        boolean(m, "main", false);
						        structured_field(m, "targets");
					        });
					records(l, "consumers", {{"system", ""}, {"store", ""}, {"costs", Json::object()}},
					        [&](Json &m)
					        {
						        string(m, "system");
						        string(m, "store");
						        structured_field(m, "costs");
					        });
				}
			}
		}
		{
			ScopedTree panel("Docking ports");
			if (panel)
			{
				if (!ship.contains("docking") && button("Configure docking ports"))
					ship["docking"] = {{"ports", Json::array()}};
				if (ship.contains("docking"))
					records(ship["docking"], "ports",
					        {{"id", "port"}, {"controller", ""}, {"compartment", ""}, {"compatibility", "standard"}},
					        [&](Json &m)
					        {
						        string(m, "id");
						        string(m, "controller");
						        string(m, "compartment");
						        string(m, "compatibility");
						        integer(m, "x_mm", 0);
						        integer(m, "y_mm", 0);
						        integer(m, "z_mm", 0);
						        integer(m, "heading_mdeg", 0);
						        integer(m, "capture_range_mm", 1000);
						        integer(m, "capture_speed_mm_s", 500);
						        integer(m, "alignment_mdeg", 5000);
						        integer(m, "mating_ticks", 20);
						        integer(m, "separation_ticks", 20);
						        integer(m, "units_per_second", 20);
						        boolean(m, "auto_accept_allies", false);
						        boolean(m, "supply", false);
						        boolean(m, "emergency_release", false);
						        integer(m, "emergency_damage", 0);
					        });
			}
		}

		{
			ScopedTree panel("Ability control bindings");
			if (panel)
			{
				if (!ship.contains("ability_controls"))
					ship["ability_controls"] = Json::object();
				auto &controls = ship["ability_controls"];
				std::string remove;
				for (auto &[id, binding] : controls.items())
				{
					ScopedTree endpoint(id.c_str());
					if (!endpoint)
						continue;
					choice(binding, "source", {"officer", "duty", "equipment"});
					string(binding, "owner");
					choice(binding, "ability", document.definition_choices(WorkshopDefinitionKind::Effect));
					for (const auto *key : {"console", "controller", "enabled"})
						string(binding, key);
					for (const auto *key : {"target", "apparatus", "planner"})
						optional_string(binding, key);
					if (button("Remove ability endpoint"))
						remove = id;
				}
				if (!remove.empty())
					controls.erase(remove);
				if (button("Add ability endpoint"))
					controls[fresh(controls, "ability")] = {{"source", "equipment"}, {"owner", ""},
					                                        {"ability", ""},         {"console", ""},
					                                        {"controller", ""},      {"enabled", ""}};
			}
		}

		{
			ScopedTree panel("Boarding missions");
			if (panel)
				records(
				    ship, "boarding_missions",
				    {{"id", "boarding"},
				     {"title_key", "Board target"},
				     {"entry", ""},
				     {"tree", ""},
				     {"dialogue", ""},
				     {"slots", Json::array()},
				     {"encounters", Json::array()},
				     {"objectives", Json::object()}},
				    [&](Json &m)
				    {
					    string(m, "id");
					    string(m, "title_key");
					    string(m, "entry");
					    choice(m, "tree", document.definition_choices(WorkshopDefinitionKind::Tree));
					    choice(m, "dialogue", document.definition_choices(WorkshopDefinitionKind::Tree));
					    boolean(m, "retain_craft", true);
					    strings(m, "docking_ports");
					    records(m, "slots",
					            {{"id", "security"}, {"kind", "team"}, {"capability", "security"}, {"required", true}},
					            [&](Json &slot)
					            {
						            string(slot, "id");
						            choice(slot, "kind", {"team", "officer"});
						            optional_string(slot, "capability");
						            optional_string(slot, "team_type");
						            optional_string(slot, "trait");
						            boolean(slot, "required", true);
					            });
					    records(m, "encounters", {{"id", "guards"}, {"compartment", ""}, {"defenders", Json::array()}},
					            [&](Json &e)
					            {
						            string(e, "id");
						            string(e, "compartment");
						            strings(e, "defenders");
						            optional_string(e, "report");
						            integer(e, "success_permille", 700);
						            integer(e, "team_bonus_permille", 100);
						            integer(e, "defender_penalty_permille", 150);
						            integer(e, "injury_permille", 250);
						            integer(e, "loss_permille", 50);
						            integer(e, "medical_reduction_permille", 150);
						            integer(e, "officer_injury_permille", 0);
						            integer(e, "officer_loss_permille", 0);
						            integer(e, "assessment_ticks", 20);
						            structured_field(e, "defences");
					            });
					    structured_field(m, "objectives");
				    });
		}
		{
			ScopedTree panel("Craft and transport");
			if (panel)
			{
				if (!ship.contains("craft") && button("Add craft configuration"))
					ship["craft"] = {{"role", "shuttle"}};
				if (ship.contains("craft"))
				{
					auto &c = ship["craft"];
					choice(c, "role", {"shuttle", "interceptor", "bomber", "boarding_torpedo"});
					integer(c, "vertical_speed_mm_s", 10000);
					integer(c, "vertical_acceleration_mm_s2", 5000);
					integer(c, "cruise_altitude_mm", 15000);
					integer(c, "engagement_range_mm", 200000);
					integer(c, "return_reserve_permille", 200);
				}
				if (!ship.contains("transport") && button("Configure transport capabilities"))
					ship["transport"] = {
					    {"capacity", 64}, {"capabilities", Json::array()}, {"capability_systems", Json::object()}};
				if (ship.contains("transport"))
				{
					auto &t = ship["transport"];
					integer(t, "capacity", 64);
					optional_string(t, "compartment");
					strings(t, "capabilities");
					for (const auto &tag : t.value("capabilities", Json::array()))
					{
						if (!t.contains("capability_systems"))
							t["capability_systems"] = Json::object();
						strings(t["capability_systems"], tag.get<std::string>().c_str());
					}
				}
			}
		}
		{
			ScopedTree panel("External Comms");
			if (panel)
			{
				if (!ship.contains("comms") && button("Add Comms directory"))
					ship["comms"] = {{"characters", Json::array()}};
				if (ship.contains("comms"))
				{
					choice(ship["comms"], "default_conversation",
					       document.definition_choices(WorkshopDefinitionKind::Tree), true);
					records(ship["comms"], "characters",
					        {{"id", "character"}, {"name_key", "New character"}, {"public", true}},
					        [&](Json &p)
					        {
						        string(p, "id");
						        string(p, "name_key");
						        optional_string(p, "portrait");
						        boolean(p, "public", true);
						        choice(p, "default_conversation",
						               document.definition_choices(WorkshopDefinitionKind::Tree), true);
					        });
				}
			}
		}
		{
			ScopedTree panel("Alert conditions and Viewscreen");
			if (panel)
			{
				if (!ship.contains("bridge") && button("Add bridge configuration"))
					ship["bridge"] = {{"conditions", Json::array()}};
				if (ship.contains("bridge"))
				{
					auto &bridge = ship["bridge"];
					optional_string(bridge, "alert_console");
					optional_string(bridge, "sidebar_default");
					optional_string(bridge, "initial");
					records(bridge, "conditions",
					        {{"id", "condition"}, {"label", "Condition"}, {"colour", "#e73535"}, {"glow", false}},
					        [&](Json &condition)
					        {
						        string(condition, "id");
						        string(condition, "label");
						        optional_string(condition, "description");
						        string(condition, "colour");
						        boolean(condition, "glow", false);
						        optional_string(condition, "siren");
						        optional_string(condition, "music");
						        edit_field(label("music_gain").c_str(), condition, "music_gain", 1.0);
					        });
				}
			}
		}
		strings(ship, "tags");
		choice(ship, "extends", document.definition_choices(WorkshopDefinitionKind::Entity), true);
		if (system_entity != entity)
		{
			selected_system.clear();
			system_entity = entity;
		}
		heading("Fitted Systems");
		static std::string new_system;
		ImGui::InputText(label("New System identity").c_str(), &new_system);
		if (button("Add System") && !new_system.empty())
		{
			if (!ship.contains("systems"))
				ship["systems"] = Json::object();
			if (!ship["systems"].contains(new_system))
				ship["systems"][new_system] = {
				    {"kind", "engine"}, {"max_speed_mm_s", 10000}, {"acceleration_mm_s2", 1000}};
			selected_system = new_system;
			new_system.clear();
		}
		std::string remove;
		if (ship.contains("systems"))
		{
			for (auto it = ship["systems"].begin(); it != ship["systems"].end(); ++it)
				if (ImGui::Selectable(it.key().c_str(), selected_system == it.key()))
					selected_system = it.key();
			if (ship["systems"].contains(selected_system))
			{
				ScopedId identity(selected_system.c_str());
				auto &spec = ship["systems"][selected_system];
				heading("Selected System");
				ImGui::TextUnformatted(selected_system.c_str());
				choice(spec, "template", document.definition_choices(WorkshopDefinitionKind::Equipment), true);
				choice(spec, "kind",
				       {"engine",    "steering", "radar",       "generator", "distribution", "coolant",
				        "storage",   "lateral",  "boost",       "impulse",   "weapon",       "turret",
				        "magazine",  "team",     "repair_team", "shield",    "comms",        "displacement",
				        "transport", "dispatch", "ftl",         "psychic"},
				       true);
				auto kind = equipment_kind(document, spec);
				if (kind.empty())
					kind = selected_system;
				system_details(spec, kind);
				rule_bindings_fields(spec, document);
				telemetry_records(spec, document);
				scalar_map(spec, "parameters");
				if (button("Remove System"))
					remove = selected_system;
			}
		}
		if (!remove.empty())
			ship["systems"].erase(remove);
		const auto systems = fitted_ids(document, ship);
		if (composition.contains("systems"))
		{
			if (ScopedTree section(label("Inherited and assembled equipment").c_str()); section)
			{
				for (auto it = composition["systems"].begin(); it != composition["systems"].end(); ++it)
					if (!ship.contains("systems") || !ship.at("systems").contains(it.key()))
					{
						ScopedId identity(it.key().c_str());
						ImGui::TextUnformatted(it.key().c_str());
						structure(it.value(), false);
					}
			}
		}
		if (!ship.contains("stations") && composition.contains("stations"))
		{
			text("Stations are inherited. Copy them only when authoring an explicit local ownership override.");
			if (button("Override inherited Stations"))
				ship["stations"] = composition.at("stations");
		}
		records(ship, "stations", {{"id", "station"}, {"consoles", Json::array()}},
		        [&](Json &station)
		        {
			        string(station, "id");
			        records(
			            station, "consoles",
			            {{"id", "console"}, {"definition", ""}, {"systems", Json::array()}, {"feeds", Json::array()}},
			            [&](Json &console)
			            {
				            string(console, "id");
				            choice(console, "definition", document.definition_choices(WorkshopDefinitionKind::Console));
				            names(console, "systems", systems);
				            names(console, "feeds", systems);
				            boolean(console, "shield_feed");
				            names(console, "human_preferences", ids(ship.at("stations")));
				            gameplay_panel(console);
			            });
		        });
	}
	void resources(WorkshopDocument &document)
	{
		auto *selected = select_entity(document);
		if (!selected)
			return;
		if (!selected->contains("resources"))
		{
			text("This definition has no resource network.");
			if (button("Create resource network"))
				(*selected)["resources"] = Json::object();
			return;
		}
		auto &network = (*selected)["resources"];
		network_editor(network);
	}
	void network_editor(Json &network, const std::vector<std::string> &external = {})
	{
		text("Rates are integer units per second. Energy, heat and coolant units are authored. Verification checks "
		     "finite limits and compatible service links.");
		records(network, "groups",
		        {{"id", "group"}, {"budget_rate", 1000}, {"restore_margin_rate", 0}, {"restore_ticks", 1}},
		        [&](Json &row)
		        {
			        string(row, "id");
			        choice(row, "parent",
			               network.contains("groups") ? ids(network.at("groups")) : std::vector<std::string>{}, true);
			        integer(row, "budget_rate");
			        integer(row, "restore_margin_rate");
			        integer(row, "restore_ticks", 1);
		        });
		records(network, "stores",
		        {{"id", "store"},
		         {"capacity", 1000},
		         {"initial", 1000},
		         {"charge_rate", 100},
		         {"discharge_rate", 100},
		         {"stop_heat", 1000},
		         {"recover_heat", 500}},
		        [&](Json &row)
		        {
			        string(row, "id");
			        string(row, "resource");
			        choice(row, "group",
			               network.contains("groups") ? ids(network.at("groups")) : std::vector<std::string>{}, true);
			        for (const char *key :
			             {"capacity", "initial", "charge_rate", "discharge_rate", "loss_empty_ppm", "loss_full_ppm",
			              "initial_heat", "passive_cooling", "stop_heat", "recover_heat", "charge_heat_ppm",
			              "operation_heat_ppm", "depletion_heat_ppm", "stress_rate", "override_stress_rate",
			              "stress_limit"})
				        integer(row, key);
			        integer(row, "overdrive_permille", 1000);
			        integer(row, "overdrive_heat_permille", 1000);
			        boolean(row, "enabled", true);
			        boolean(row, "allows_override");
		        });
		auto stores = network.contains("stores") ? ids(network.at("stores")) : std::vector<std::string>{};
		stores.insert(stores.end(), external.begin(), external.end());
		records(
		    network, "generators",
		    {{"id", "generator"}, {"store", ""}, {"maximum_rate", 100}, {"response_rate", 100}, {"initial_output", 0}},
		    [&](Json &row)
		    {
			    string(row, "id");
			    choice(row, "store", stores);
			    for (const char *key : {"maximum_rate", "response_rate", "initial_output", "heat_ppm"})
				    integer(row, key);
			    boolean(row, "enabled", true);
		    });
		records(network, "links",
		        {{"id", "link"}, {"from", ""}, {"to", ""}, {"capacity_rate", 100}, {"isolated", false}},
		        [&](Json &row)
		        {
			        string(row, "id");
			        choice(row, "from", stores);
			        choice(row, "to", stores);
			        integer(row, "capacity_rate");
			        boolean(row, "isolated");
		        });
		records(network, "coolant_sources",
		        {{"id", "coolant"}, {"initial", 1000}, {"removal_rate", 100}, {"coolant_per_heat_ppm", 1000000}},
		        [&](Json &row)
		        {
			        string(row, "id");
			        string(row, "resource");
			        integer(row, "capacity", 1000);
			        choice(row, "power_store", stores, true);
			        for (const char *key : {"initial", "removal_rate", "coolant_per_heat_ppm"})
				        integer(row, key);
		        });
		auto sources =
		    network.contains("coolant_sources") ? ids(network.at("coolant_sources")) : std::vector<std::string>{};
		sources.insert(sources.end(), external.begin(), external.end());
		records(network, "cooling_links",
		        {{"id", "cooling"}, {"source", ""}, {"store", ""}, {"capacity_rate", 100}, {"allocation_rate", 100}},
		        [&](Json &row)
		        {
			        string(row, "id");
			        choice(row, "source", sources);
			        choice(row, "store", stores);
			        integer(row, "capacity_rate");
			        integer(row, "allocation_rate");
			        boolean(row, "isolated");
		        });
	}
	void knowledge(WorkshopDocument &document)
	{
		auto &definitions = document.definitions();
		if (!definitions.contains("knowledge"))
		{
			text("Author typed records and permitted producers/readers before binding feeds or publication.");
			if (button("Create knowledge contract"))
				definitions["knowledge"] = {
				    {"history_limit", 256}, {"roles", Json::array()}, {"schemas", Json::array()}};
			return;
		}
		auto &contract = definitions["knowledge"];
		const auto visible_contract = document.preview_knowledge();
		integer(contract, "history_limit", 256);
		strings(contract, "roles");
		std::vector<std::string> roles;
		if (visible_contract.contains("roles"))
			for (const auto &role : visible_contract.at("roles"))
				roles.push_back(role.get<std::string>());
		records(contract, "schemas",
		        {{"id", "record"},
		         {"producers", Json::array()},
		         {"readers", Json::array()},
		         {"ship_publishable", false},
		         {"stale_after_ticks", 100},
		         {"fields", Json::array()}},
		        [&](Json &schema)
		        {
			        string(schema, "id");
			        names(schema, "producers", roles);
			        names(schema, "readers", roles);
			        boolean(schema, "ship_publishable");
			        integer(schema, "stale_after_ticks", 100);
			        records(schema, "fields",
			                {{"id", "field"}, {"type", "integer"}, {"required", true}, {"publishable", true}},
			                [&](Json &field)
			                {
				                string(field, "id");
				                choice(field, "type", {"integer", "boolean", "text"});
				                boolean(field, "required", true);
				                boolean(field, "publishable", true);
			                });
		        });
		text("Shipwide publication is explicit. Private Console observations and private AI memory are not "
		     "automatically merged. Rendered visibility does not grant knowledge.");
	}
	void knowledge_bindings(WorkshopDocument &document)
	{
		const auto visible_contract = document.preview_knowledge();
		std::vector<std::string> roles;
		for (const auto &role : visible_contract.at("roles"))
			roles.push_back(role.get<std::string>());
		if (auto *ship = select_entity(document); ship && ship->contains("stations"))
		{
			std::vector<std::string> consoles;
			for (auto &station : (*ship)["stations"])
				for (auto &console : station["consoles"])
				{
					consoles.push_back(console.at("id").get<std::string>());
					ScopedId id_scope_10(console.at("id").get<std::string>().c_str());
					ImGui::TextUnformatted(console.at("id").get<std::string>().c_str());
					names(console, "knowledge_roles", roles);
					names(console, "knowledge_producers", roles);
				}
			const auto schemas = ids(visible_contract.at("schemas"));
			auto inspect_fields = [&](Json &row)
			{
				std::vector<std::string> fields;
				if (visible_contract.contains("schemas"))
					for (const auto &schema : visible_contract.at("schemas"))
						if (schema.at("id") == row.value("schema", std::string{}))
							fields = ids(schema.at("fields"));
				if (fields.empty())
					text("Choose a schema with supported fields first.");
				else
					names(row, "fields", fields);
			};
			records(*ship, "feeds",
			        {{"source_console", ""},
			         {"target_console", ""},
			         {"schema", ""},
			         {"subject", 0},
			         {"fields", Json::array()}},
			        [&](Json &feed)
			        {
				        choice(feed, "source_console", consoles);
				        choice(feed, "target_console", consoles);
				        choice(feed, "schema", schemas);
				        integer(feed, "subject");
				        inspect_fields(feed);
			        });
			records(*ship, "publications",
			        {{"id", "publication"},
			         {"source_console", ""},
			         {"schema", ""},
			         {"subject", 0},
			         {"fields", Json::array()},
			         {"live", false}},
			        [&](Json &publication)
			        {
				        string(publication, "id");
				        choice(publication, "source_console", consoles);
				        choice(publication, "schema", schemas);
				        integer(publication, "subject");
				        inspect_fields(publication);
				        boolean(publication, "live");
			        });
			text("Authored feeds refresh only their declared scope. Publication templates require explicit activation; "
			     "defining one does not share it.");
			if (ship->contains("systems"))
				for (auto &system : (*ship)["systems"].items())
				{
					ScopedId identity(system.key().c_str());
					if (ScopedTree inspector(system.key().c_str()); inspector)
						telemetry_records(system.value(), document);
				}
		}
	}
	void telemetry_records(Json &spec, WorkshopDocument &document)
	{
		const auto knowledge = document.preview_knowledge();
		std::vector<std::string> roles;
		for (const auto &role : knowledge.at("roles"))
			roles.push_back(role.get<std::string>());
		records(spec, "records",
		        {{"schema", ""}, {"producer", ""}, {"subject", 0}, {"kind", "fact"}, {"fields", Json::object()}},
		        [&](Json &record)
		        {
			        choice(record, "schema", ids(knowledge.at("schemas")));
			        choice(record, "producer", roles);
			        integer(record, "subject");
			        choice(record, "kind", {"fact", "claim", "intent", "prediction"});
			        std::vector<std::string> fields;
			        for (const auto &schema : knowledge.at("schemas"))
				        if (schema.at("id") == record.value("schema", std::string{}))
					        fields = ids(schema.at("fields"));
			        if (record.contains("fields"))
				        for (auto it = record["fields"].begin(); it != record["fields"].end(); ++it)
				        {
					        ScopedId field(it.key().c_str());
					        string(record["fields"], it.key().c_str());
				        }
			        static std::map<ImGuiID, std::string> selected_fields;
			        const auto identity = ImGui::GetID("telemetry-field");
			        Json selected{{"Schema field", selected_fields[identity]}};
			        if (choice(selected, "Schema field", fields))
				        selected_fields[identity] = selected.at("Schema field").get<std::string>();
			        if (button("Bind telemetry field") && !selected_fields[identity].empty())
			        {
				        if (!record.contains("fields"))
					        record["fields"] = Json::object();
				        record["fields"][selected_fields[identity]] = "requested";
			        }
			        text("Telemetry source names are verified by the production knowledge compiler. They refer to "
			             "permitted World telemetry, not arbitrary JSON paths.");
			        structured_field(record, "fields");
		        });
	}
	std::vector<std::string> vocabulary_ids(const std::string &context, const char *kind) const
	{
		if (vocabulary.contains(context) && vocabulary.at(context).contains(kind))
			return ids(vocabulary.at(context).at(kind));
		return {};
	}
	std::string authoring_context(const Json &definition) const
	{
		return definition.value("interdiction", false) ? "interdiction"
		                                               : definition.value("context", std::string("system"));
	}
	Json action_signature(const std::string &context, const std::string &name) const
	{
		if (vocabulary.contains(context) && vocabulary.at(context).contains("actions"))
			for (const auto &action : vocabulary.at(context).at("actions"))
				if (action.at("id") == name)
					return action;
		return Json::object();
	}
	void binding(Json &parent, const char *key, const Json &definition,
	             const std::vector<std::string> &allowed = {"integer", "boolean", "text", "reference", "parameter",
	                                                        "observation", "intent", "memory", "elapsed"})
	{
		ScopedId id_scope_11(key);
		text(key);
		const auto current = parent.contains(key) ? parent.at(key) : Json(0);
		std::string mode = "integer";
		if (current.is_boolean())
			mode = "boolean";
		else if (current.is_string())
			mode = "text";
		else if (current.is_object())
		{
			for (const char *source : {"argument", "parameter", "observation", "intent", "memory", "elapsed"})
				if (current.contains(source))
					mode = source;
			if (current.contains("type"))
				mode = "reference";
		}
		Json selector{{"source", mode}};
		if (choice(selector, "source", allowed))
		{
			mode = selector.at("source").get<std::string>();
			if (mode == "integer")
				parent[key] = 0;
			else if (mode == "boolean")
				parent[key] = false;
			else if (mode == "text")
				parent[key] = "";
			else if (mode == "reference")
				parent[key] = {{"type", "entity"}, {"id", 1}};
			else if (mode == "elapsed")
				parent[key] = {{"elapsed", true}};
			else
				parent[key] = {{mode, ""}};
		}
		if (mode == "integer")
			integer(parent, key);
		else if (mode == "boolean")
			boolean(parent, key);
		else if (mode == "text")
			string(parent, key);
		else if (mode == "reference")
		{
			if (parent.contains(key))
			{
				choice(parent[key], "type", {"entity", "system", "console", "objective", "region", "message"});
				integer(parent[key], "id", 1);
			}
		}
		else if (mode != "elapsed")
		{
			std::vector<std::string> options;
			if (mode == "parameter" && definition.contains("parameters"))
				options = ids(definition.at("parameters"));
			if (mode == "memory" && definition.contains("memory"))
				for (const auto &row : definition.at("memory"))
					options.push_back(row.value("name", std::string{}));
			if (mode == "observation")
				options = vocabulary_ids(authoring_context(definition), "observations");
			if (mode == "observation" && definition.contains("record_bindings"))
				for (const auto &field : definition.at("record_bindings"))
					options.push_back(field.at("name").get<std::string>());
			if (mode == "observation" && definition.contains("summary_bindings"))
				for (const auto &field : definition.at("summary_bindings"))
					options.push_back(field.at("name").get<std::string>());
			if (mode == "argument")
				options = {"amount", "flag", "detail"};
			if (mode == "intent")
				options = vocabulary_ids(authoring_context(definition), "intents");
			if (parent.contains(key))
			{
				if (!options.empty())
					choice(parent[key], mode.c_str(), options);
				else
					string(parent[key], mode.c_str());
			}
		}
	}
	void binding_map(Json &parent, const char *key, const Json &definition,
	                 const std::vector<std::string> &suggested = {},
	                 const std::vector<std::string> &allowed = {"integer", "boolean", "text", "reference", "parameter",
	                                                            "observation", "intent", "memory", "elapsed"})
	{
		ScopedId id_scope_12(key);
		heading(key);
		std::string remove;
		if (parent.contains(key) && parent.at(key).is_object())
			for (auto it = parent[key].begin(); it != parent[key].end(); ++it)
			{
				ScopedId id_scope_13(it.key().c_str());
				binding(parent[key], it.key().c_str(), definition, allowed);
				if (button("Remove binding"))
					remove = it.key();
			}
		if (!remove.empty())
			parent[key].erase(remove);
		static std::map<std::string, std::string> names;
		const auto scope = std::to_string(ImGui::GetID(key));
		if (suggested.empty())
			ImGui::InputText(label("Binding name").c_str(), &names[scope]);
		else
		{
			Json pick{{"Binding name", names[scope]}};
			if (choice(pick, "Binding name", suggested))
				names[scope] = pick.at("Binding name").get<std::string>();
		}
		if (button("Add binding") && !names[scope].empty())
		{
			if (!parent.contains(key))
				parent[key] = Json::object();
			if (!parent[key].contains(names[scope]))
				parent[key][names[scope]] = 0;
			names[scope].clear();
		}
	}
	void tasks(Json &state, const char *phase, Json &definition, WorkshopDocument &document)
	{
		ScopedId id_scope_14(phase);
		heading(phase);
		for (const char *kind : {"action", "intent", "memory", "call"})
		{
			ScopedId id_scope_15(kind);
			if (button(kind))
			{
				if (!state.contains(phase))
					state[phase] = Json::array();
				if (std::string(kind) == "action")
					state[phase].push_back({{"action", ""}, {"arguments", Json::array()}, {"once", true}});
				else if (std::string(kind) == "call")
					state[phase].push_back({{"call", ""}, {"parameters", Json::object()}});
				else
					state[phase].push_back({{kind, ""}, {"value", 0}});
			}

			ImGui::SameLine();
		}
		ImGui::NewLine();
		int remove = -1;
		if (state.contains(phase))
			for (std::size_t i = 0; i < state.at(phase).size(); ++i)
			{
				auto &task = state[phase][i];
				ScopedId id_scope_16(static_cast<int>(i));
				ImGui::Text("%zu", i + 1);
				if (task.contains("action"))
				{
					const auto actions = vocabulary_ids(authoring_context(definition), "actions");
					if (!actions.empty())
						choice(task, "action", actions);
					else
					{
						string(task, "action");
						text("Action palette awaits the production context vocabulary. Verification rejects "
						     "unsupported names.");
					}
					boolean(task, "once", true);
					const auto signature =
					    action_signature(authoring_context(definition), task.at("action").get<std::string>());
					if (signature.contains("arguments"))
					{
						text("Expected arguments in order");
						for (const auto &argument : signature.at("arguments"))
							ImGui::Text("%s: %s", argument.value("name", std::string{}).c_str(),
							            argument.value("type", std::string{}).c_str());
						if (button("Use action signature"))
						{
							task["arguments"] = Json::array();
							for (const auto &argument : signature.at("arguments"))
							{
								const auto type = argument.value("type", std::string("integer"));
								task["arguments"].push_back(type == "boolean"   ? Json(false)
								                            : type == "text"    ? Json("")
								                            : type == "integer" ? Json(0)
								                                                : Json{{"type", type}, {"id", 1}});
							}
						}
					}
					int remove_argument = -1;
					for (std::size_t n = 0; n < task.at("arguments").size(); ++n)
					{
						ScopedId id_scope_17(static_cast<int>(n));
						Json wrapper{{"argument", task.at("arguments")[n]}};
						binding(wrapper, "argument", definition);
						if (wrapper.at("argument") != task.at("arguments")[n])
							task["arguments"][n] = wrapper.at("argument");
						if (button("Remove argument"))
							remove_argument = static_cast<int>(n);
					}
					if (remove_argument >= 0)
						task["arguments"].erase(task["arguments"].begin() + remove_argument);
					if (button("Add argument"))
						task["arguments"].push_back(0);
				}
				else if (task.contains("call"))
				{
					choice(task, "call", document.definition_choices(WorkshopDefinitionKind::Tree));
					binding_map(task, "parameters", definition, {},
					            {"integer", "boolean", "text", "reference", "parameter"});
				}
				else if (task.contains("memory"))
				{
					std::vector<std::string> memory;
					if (definition.contains("memory"))
						for (const auto &row : definition.at("memory"))
							memory.push_back(row.value("name", std::string{}));
					choice(task, "memory", memory);
					binding(task, "value", definition);
				}
				else if (task.contains("intent"))
				{
					const auto intents = vocabulary_ids(definition.value("context", std::string("system")), "intents");
					if (!intents.empty())
						choice(task, "intent", intents);
					else
						string(task, "intent");
					binding(task, "value", definition);
				}
				else
					text("Legacy timed output retained. Edit its existing controller inspector or convert the tree "
					     "explicitly.");
				if (button("Earlier") && i > 0)
					std::swap(state[phase][i], state[phase][i - 1]);
				ImGui::SameLine();
				if (button("Later") && i + 1 < state.at(phase).size())
					std::swap(state[phase][i], state[phase][i + 1]);
				ImGui::SameLine();
				if (button("Remove task"))
					remove = static_cast<int>(i);
				ImGui::Separator();
			}
		if (remove >= 0)
			state[phase].erase(state[phase].begin() + remove);
	}
	void tree_graph(const Json &definition, std::string &selection)
	{
		if (!definition.contains("states"))
			return;
		const auto &states = definition.at("states");
		ScopedChild child("state-graph", {0, 290}, ImGuiWindowFlags_HorizontalScrollbar);
		const auto origin = ImGui::GetCursorScreenPos();
		std::map<std::string, ImVec2> positions;
		float width = 500;
		const Json *runtime = &feedback;
		if (feedback.contains("controllers"))
		{
			runtime = nullptr;
			for (const auto &controller : feedback.at("controllers"))
				if (identity(controller) == inspected_controller)
					runtime = &controller;
			if (runtime && !runtime->contains("tree"))
				runtime = nullptr;
		}
		if (runtime && runtime->contains("tree") && runtime->at("tree") != definition.value("id", std::string{}))
			runtime = nullptr;
		for (std::size_t i = 0; i < states.size(); ++i)
		{
			const auto &state = states[i];
			std::size_t depth = 0;
			auto parent = state.value("parent", std::string{});
			for (; !parent.empty() && depth < states.size(); ++depth)
			{
				auto found = std::find_if(states.begin(), states.end(),
				                          [&](const auto &row)
				                          {
					                          return row.value("id", std::string{}) == parent;
				                          });
				if (found == states.end())
					break;
				parent = found->value("parent", std::string{});
			}
			positions[state.value("id", std::to_string(i))] = {
			    origin.x + static_cast<float>(std::min<std::size_t>(depth, 16)) * 190, origin.y + i * 65.f};
			width = std::max(width, static_cast<float>(std::min<std::size_t>(depth, 16)) * 190 + 180);
		}
		auto *draw = ImGui::GetWindowDrawList();
		for (const auto &state : states)
		{
			const auto id = state.value("id", std::string{});
			if (!positions.contains(id))
				continue;
			const auto start = positions.at(id);
			if (state.contains("transitions"))
				for (const auto &edge : state.at("transitions"))
				{
					const auto target = edge.value("target", std::string{});
					if (!positions.contains(target))
						continue;
					const auto end = positions.at(target);
					draw->AddBezierCubic({start.x + 170, start.y + 25}, {start.x + 205, start.y + 25},
					                     {end.x - 35, end.y + 25}, {end.x, end.y + 25},
					                     ImGui::GetColorU32(ImGuiCol_PlotLines), 2);
				}
		}
		for (std::size_t i = 0; i < states.size(); ++i)
		{
			const auto id = states[i].value("id", std::to_string(i));
			ImGui::SetCursorScreenPos(positions.at(id));
			ScopedId id_scope_18(static_cast<int>(i));
			if (runtime && runtime->contains("active_states") && runtime->at("active_states").is_array() &&
			    std::find(runtime->at("active_states").begin(), runtime->at("active_states").end(), Json(id)) !=
			        runtime->at("active_states").end())
			{
				const auto position = positions.at(id);
				draw->AddRect({position.x - 2, position.y - 2}, {position.x + 174, position.y + 48},
				              ImGui::GetColorU32(ImGuiCol_CheckMark), 2, 0, 3);
			}
			if (ImGui::Selectable((id + "###node").c_str(), selected_state == static_cast<int>(i), 0, {170, 45}))
			{
				selected_state = static_cast<int>(i);
				selection = "State trees";
			}
		}
		ImGui::SetCursorScreenPos(origin);
		ImGui::Dummy({width, std::max(260.f, states.size() * 65.f)});
	}
	void state_inspector(Json &state, Json &definition, WorkshopDocument &document, bool patch = false)
	{
		const auto states = definition.contains("states") ? ids(definition.at("states")) : std::vector<std::string>{};
		if (patch)
			choice(state, "state", states);
		else
			string(state, "id");
		choice(state, "parent", states, true);
		choice(state, "initial", states, true);
		for (const char *phase : {"entry", "running", "exit"})
			tasks(state, phase, definition, document);
		records(state, "transitions", {{"target", ""}, {"reason", ""}, {"conditions", Json::array()}},
		        [&](Json &edge)
		        {
			        choice(edge, "target", states);
			        string(edge, "reason");
			        records(edge, "conditions", {{"left", 0}, {"op", "eq"}, {"right", 0}},
			                [&](Json &condition)
			                {
				                binding(condition, "left", definition);
				                if (choice(condition, "op", {"eq", "ne", "lt", "le", "gt", "ge", "present"}) &&
				                    condition.at("op") == "present")
					                condition.erase("right");
				                if (condition.value("op", std::string("eq")) != "present")
					                binding(condition, "right", definition);
			                });
		        });
	}
	void trees(WorkshopDocument &document, bool inspector, std::string &selection)
	{
		auto &defs = document.definitions();
		if (!inspector)
		{
			static std::string derive_base;
			Json derivation{{"Base tree", derive_base}};
			if (choice(derivation, "Base tree", document.definition_choices(WorkshopDefinitionKind::Tree)))
				derive_base = derivation.at("Base tree").get<std::string>();
			if (button("Derive tree from base") && !derive_base.empty())
			{
				if (!defs.contains("trees"))
					defs["trees"] = Json::array();
				tree = fresh(defs.at("trees"), "tree");
				defs["trees"].push_back({{"id", tree}, {"extends", derive_base}});
				selected_state = -1;
				selection = "State trees";
			}
			if (button("New hierarchical tree"))
			{
				selected_state = -1;
				selection = "State trees";
				if (!defs.contains("trees"))
					defs["trees"] = Json::array();
				tree = fresh(defs.at("trees"), "tree");
				defs["trees"].push_back({{"id", tree},
				                         {"context", "controller"},
				                         {"initial", "operating"},
				                         {"states", Json::array({{{"id", "operating"},
				                                                  {"entry", Json::array()},
				                                                  {"running", Json::array()},
				                                                  {"exit", Json::array()},
				                                                  {"transitions", Json::array()}}})}});
			}
			if (!defs.contains("trees"))
				return;
			Json tree_choice{{"Tree", tree}};
			if (choice(tree_choice, "Tree", ids(defs.at("trees"))))
			{
				tree = tree_choice.at("Tree").get<std::string>();
				selected_state = -1;
				selection = "State trees";
			}
			for (const auto &definition : defs["trees"])
				if (definition.at("id") == tree)
				{
					auto resolved = document.preview_tree(definition);
					if (resolved.contains("context"))
					{
						tree_graph(resolved, selection);
					}
				}
			if (button("Inspect selected tree") && !tree.empty())
				selection = "State trees";
			return;
		}
		if (!defs.contains("trees"))
			return;
		for (auto &definition : defs["trees"])
			if (definition.at("id") == tree)
			{
				string(definition, "id");
				tree = definition.at("id").get<std::string>();
				Json resolved = document.preview_tree(definition);

				choice(definition, "extends", document.definition_choices(WorkshopDefinitionKind::Tree), true);
				if (definition.contains("extends"))
				{
					text("Inherited source");
					ImGui::TextUnformatted(definition.at("extends").get<std::string>().c_str());
				}
				Json context{{"context", resolved.at("context")}};
				if (choice(context, "context", {"controller", "scenario", "conversation", "away", "team"}))
					definition["context"] = context.at("context");
				if (context.at("context") == "away")
				{
					boolean(definition, "boarding", false);
					strings(definition, "flags");
					strings(definition, "team_capabilities");
					strings(definition, "events");
				}
				if (context.at("context") == "scenario")
					boolean(definition, "interdiction", false);
				strings(definition, "transport_capabilities");
				if (context.at("context") == "conversation")
				{
					optional_string(definition, "title_key");
					if (!definition.contains("lines") && button("Add dialogue lines"))
						definition["lines"] = {{"greeting",
						                        {{"speaker", "target"},
						                         {"text_key", "Hello"},
						                         {"delay_ticks", 0},
						                         {"incoming_ticks", 20}}}};
					if (!definition.contains("prompts") && button("Add dialogue choices"))
						definition["prompts"] = {{"question",
						                          {{"speaker", "target"},
						                           {"text_key", "Your answer?"},
						                           {"choices", Json::array({{{"id", "yes"}, {"text_key", "Yes"}}})},
						                           {"ai_choice", "yes"}}}};
					structured_field(definition, "lines");
					structured_field(definition, "prompts");
				}
				integer(definition, "budget", resolved.value("budget", 4096));
				Json initial{{"initial", resolved.value("initial", std::string{})}};
				if (choice(initial, "initial", ids(resolved.at("states"))))
					definition["initial"] = initial.at("initial");
				heading("Parameters");
				if (resolved.contains("parameters"))
					for (auto it = resolved["parameters"].begin(); it != resolved["parameters"].end(); ++it)
					{
						ScopedId identity(it.key().c_str());
						ImGui::TextUnformatted(it.key().c_str());
						const bool local =
						    definition.contains("parameters") && definition.at("parameters").contains(it.key());
						if (local)
						{
							auto &parameter = definition["parameters"][it.key()];
							choice(parameter, "type",
							       {"integer", "boolean", "text", "entity", "system", "console", "objective", "region",
							        "message"});
							binding(parameter, "default", resolved, {"integer", "boolean", "text", "reference"});
						}
						else
						{
							structure(it.value(), false);
							if (button("Override parameter"))
							{
								if (!definition.contains("parameters"))
									definition["parameters"] = Json::object();
								definition["parameters"][it.key()] = it.value();
							}
						}
					}
				static std::string parameter_name;
				ImGui::InputText(label("Parameter name").c_str(), &parameter_name);
				if (button("Add parameter") && !parameter_name.empty())
				{
					if (!definition.contains("parameters"))
						definition["parameters"] = Json::object();
					if (!definition["parameters"].contains(parameter_name))
						definition["parameters"][parameter_name] = {{"type", "integer"}, {"default", 0}};
					parameter_name.clear();
				}
				binding_map(definition, "arguments", resolved,
				            resolved.contains("parameters") ? ids(resolved.at("parameters"))
				                                            : std::vector<std::string>{},
				            {"integer", "boolean", "text", "reference"});
				record_bindings(definition, document, resolved);
				if (definition.value("context", resolved.value("context", std::string{})) == "controller")
				{
					controller_bindings(definition, document, resolved);
					resolved = document.preview_tree(definition);
					controller_palette(resolved, document);
				}
				if (resolved.value("context", std::string{}) == "system")
					text("Legacy System tree: migrate bindings and actions to a Console Controller before use.");
				records(definition, "memory", {{"name", "memory"}, {"type", "integer"}, {"initial", 0}},
				        [&](Json &memory)
				        {
					        string(memory, "name");
					        choice(memory, "type",
					               {"integer", "boolean", "text", "entity", "system", "console", "objective", "region",
					                "message"});
					        binding(memory, "initial", resolved,
					                {"integer", "boolean", "text", "reference", "parameter"});
				        });

				if (definition.contains("states"))
				{
					if (button("Add state"))
					{
						definition["states"].push_back({{"id", fresh(definition.at("states"), "state")},
						                                {"entry", Json::array()},
						                                {"running", Json::array()},
						                                {"exit", Json::array()},
						                                {"transitions", Json::array()}});
						selected_state = static_cast<int>(definition.at("states").size() - 1);
					}
					if (selected_state >= 0 && selected_state < static_cast<int>(definition.at("states").size()))
					{
						ScopedId identity("selected-state");
						state_inspector(definition["states"][selected_state], resolved, document);
						if (button("Remove state"))
						{
							definition["states"].erase(definition["states"].begin() + selected_state);
							selected_state = -1;
							selection = "State trees";
						}
					}
				}
				else if (selected_state >= 0 && selected_state < static_cast<int>(resolved.at("states").size()))
				{
					heading("Inherited state");
					structure(resolved["states"][selected_state], false);
					if (button("Override selected state"))
					{
						if (!definition.contains("overrides"))
							definition["overrides"] = Json::array();
						definition["overrides"].push_back({{"state", resolved.at("states")[selected_state].at("id")}});
					}
				}
				records(definition, "overrides", {{"state", ""}},
				        [&](Json &patch)
				        {
					        state_inspector(patch, resolved, document, true);
				        });
				return;
			}
	}
	void record_bindings(Json &tree, WorkshopDocument &document, const Json &resolved)
	{
		const auto knowledge = document.preview_knowledge();
		records(tree, "record_bindings",
		        {{"name", "record"}, {"scope", "console"}, {"schema", ""}, {"field", ""}, {"subject", "self"}},
		        [&](Json &binding)
		        {
			        string(binding, "name");
			        choice(binding, "scope", {"console", "ship"});
			        choice(binding, "schema", ids(knowledge.at("schemas")));
			        std::vector<std::string> fields;
			        for (const auto &schema : knowledge.at("schemas"))
				        if (schema.at("id") == binding.value("schema", std::string{}))
					        fields = ids(schema.at("fields"));
			        choice(binding, "field", fields);
			        const auto subject = binding.contains("subject")             ? binding.at("subject")
			                             : binding.contains("subject_parameter") ? binding.at("subject_parameter")
			                                                                     : Json("self");
			        Json selected{{"Record subject", subject.is_string() ? subject.get<std::string>() : "entity"}};
			        const auto subjects = resolved.value("context", std::string("system")) == "scenario"
			                                  ? std::vector<std::string>{"self", "entity"}
			                              : resolved.value("context", std::string("system")) == "controller"
			                                  ? std::vector<std::string>{"self", "latest", "entity"}
			                                  : std::vector<std::string>{"self", "selected_contact", "entity"};
			        if (choice(selected, "Record subject", subjects))
			        {
				        const auto kind = selected.at("Record subject").get<std::string>();
				        binding.erase("subject_parameter");
				        binding["subject"] = kind == "entity" ? Json(1) : Json(kind);
			        }
			        if (binding.contains("subject") && binding.at("subject").is_number_integer())
				        integer(binding, "subject", 1);
			        text("Record type and read permissions come from the selected schema field. Ship records require "
			             "explicit publication.");
		        });
	}
	void actor(Json &parent, const char *key, const std::vector<std::string> &slots)
	{
		ScopedId id_scope_22(key);
		heading(key);
		const auto value = parent.contains(key) ? parent.at(key) : Json::object();
		Json kind{{"Actor source", value.contains("slot") ? "slot" : value.contains("entity") ? "entity" : "handle"}};
		if (choice(kind, "Actor source", {"slot", "entity", "handle"}))
		{
			const auto selected = kind.at("Actor source").get<std::string>();
			parent[key] = selected == "entity" ? Json{{"entity", 1}} : Json{{selected, ""}};
		}
		if (parent.contains(key))
		{
			if (parent.at(key).contains("slot"))
				choice(parent[key], "slot", slots);
			else if (parent.at(key).contains("entity"))
				integer(parent[key], "entity", 1);
			else
				string(parent[key], "handle");
		}
		else if (button("Bind actor"))
			parent[key] = {{"slot", slots.empty() ? "" : slots.front()}};
	}
	void scenario(WorkshopDocument &document)
	{
		auto &map = document.map();
		optional_string(map, "briefing_key");
		if (map.contains("ship_briefings"))
			structure(map["ship_briefings"]);
		else if (button("Add ship briefings"))
			map["ship_briefings"] = Json::object();
		records(map, "factions", {{"id", "faction"}, {"label", "Faction"}},
		        [&](Json &f)
		        {
			        string(f, "id");
			        string(f, "label");
		        });
		const auto faction_ids = map.contains("factions") ? ids(map.at("factions")) : std::vector<std::string>{};
		records(map, "relationships", {{"a", ""}, {"b", ""}, {"relation", "neutral"}},
		        [&](Json &r)
		        {
			        choice(r, "a", faction_ids);
			        choice(r, "b", faction_ids);
			        choice(r, "relation", {"friendly", "neutral", "hostile"});
		        });
		records(map, "sensor_findings",
		        {{"id", "finding"},
		         {"text_key", "Sensor finding"},
		         {"target_entity", 1},
		         {"scan_ticks", 100},
		         {"requires", {"findings"}}},
		        [&](Json &f)
		        {
			        string(f, "id");
			        string(f, "text_key");
			        integer(f, "target_entity", 1);
			        integer(f, "scan_ticks", 100);
			        strings(f, "requires");
			        optional_string(f, "requires_event");
			        optional_string(f, "unless_event");
			        optional_string(f, "discovery_event");
			        optional_string(f, "tag");
			        optional_string(f, "destination_station");
		        });
		records(map, "starting_stocks", {{"ship_slot", ""}, {"definition", ""}, {"stores", Json::object()}},
		        [&](Json &setup)
		        {
			        string(setup, "ship_slot");
			        choice(setup, "definition", document.definition_choices(WorkshopDefinitionKind::Entity));
			        structured_field(setup, "stores");
		        });
		records(
		    map, "interdictions",
		    {{"id", "encounter"},
		     {"player_slot", ""},
		     {"target_entity", 1},
		     {"start_event", ""},
		     {"tree", ""},
		     {"dialogue", ""},
		     {"threat_radius_mm", 600000},
		     {"clearance_radius_mm", 750000},
		     {"engagement_radius_mm", 2500000},
		     {"formation_radius_mm", 800000},
		     {"takeover_ticks", 2400},
		     {"variants", Json::object()},
		     {"pleas", Json::array()},
		     {"occupation_teams", Json::array()},
		     {"takeover_effects", {{"repair", false}, {"original_crew", "detain"}, {"capture_visitors", true}}}},
		    [&](Json &m)
		    {
			    string(m, "id");
			    string(m, "player_slot");
			    integer(m, "target_entity", 1);
			    string(m, "start_event");
			    optional_string(m, "target_character");
			    choice(m, "tree", document.definition_choices(WorkshopDefinitionKind::Tree));
			    choice(m, "dialogue", document.definition_choices(WorkshopDefinitionKind::Tree));
			    for (auto key : {"threat_radius_mm", "clearance_radius_mm", "engagement_radius_mm",
			                     "formation_radius_mm", "takeover_ticks", "combat_standoff_mm", "cruise_speed_mm_s"})
				    integer(m, key, 1);
			    strings(m, "occupation_teams");
			    if (!m.contains("takeover_effects"))
				    m["takeover_effects"] = {{"repair", true}, {"original_crew", "kill"}, {"capture_visitors", true}};
			    auto &effects = m["takeover_effects"];
			    boolean(effects, "repair", true);
			    choice(effects, "original_crew", {"kill", "detain"});
			    boolean(effects, "capture_visitors", true);
			    structured_field(m, "variants");
			    structured_field(m, "pleas");
		    });
		records(map, "away_missions",
		        {{"id", "mission"},
		         {"title_key", "Away mission"},
		         {"ship_slot", ""},
		         {"target_entity", 1},
		         {"conversation", ""},
		         {"reveal_event", ""},
		         {"tree", ""},
		         {"slots", Json::array()}},
		        [&](Json &m)
		        {
			        string(m, "id");
			        string(m, "title_key");
			        string(m, "ship_slot");
			        integer(m, "target_entity", 1);
			        string(m, "conversation");
			        string(m, "reveal_event");
			        boolean(m, "retain_craft", true);
			        choice(m, "tree", document.definition_choices(WorkshopDefinitionKind::Tree));
			        boolean(m, "allow_ship_transport", false);
			        strings(m, "docking_ports");
			        optional_string(m, "ship_transport_reason");
			        integer(m, "deployment_range_mm", 20000);
			        integer(m, "deployment_ticks", 20);
			        records(m, "slots", {{"id", "repair"}, {"kind", "team"}, {"required", true}},
			                [&](Json &s)
			                {
				                string(s, "id");
				                choice(s, "kind", {"team", "officer"});
				                optional_string(s, "trait");
				                optional_string(s, "team_type");
				                optional_string(s, "capability");
				                boolean(s, "required", true);
				                boolean(s, "assign_ai", false);
			                });
		        });
		records(map, "conversations",
		        {{"id", "conversation"}, {"tree", ""}, {"bindings", Json::object()}, {"members", Json::array()}},
		        [&](Json &c)
		        {
			        string(c, "id");
			        choice(c, "tree", document.definition_choices(WorkshopDefinitionKind::Tree));
			        optional_string(c, "title_key");
			        optional_string(c, "start_event");
			        strings(c, "cancel_events");
			        strings(c, "members");
			        strings(c, "events");
			        structured_field(c, "bindings");
		        });
		records(map, "regions", {{"id", "region"}, {"shape", "circle"}, {"x_mm", 0}, {"y_mm", 0}, {"radius_mm", 10000}},
		        [&](Json &region)
		        {
			        string(region, "id");
			        choice(region, "shape", {"circle", "rectangle", "annulus"});
			        integer(region, "x_mm");
			        integer(region, "y_mm");
			        if (region.value("shape", std::string("circle")) != "rectangle")
				        integer(region, "radius_mm", 10000);
			        else
			        {
				        integer(region, "half_width_mm", 10000);
				        integer(region, "half_height_mm", 10000);
			        }
			        if (region.value("shape", std::string("circle")) == "annulus")
				        integer(region, "inner_radius_mm", 5000);
			        bool display = region.contains("display");
			        if (ImGui::Checkbox(label("Map marker").c_str(), &display))
			        {
				        if (display)
					        region["display"] = {{"name", region.value("id", "Region")},
					                             {"navigation", true},
					                             {"radar", true},
					                             {"colour", {.5, .7, .9}},
					                             {"tags", {"contact"}}};
				        else
					        region.erase("display");
			        }
			        if (display)
			        {
				        auto &d = region["display"];
				        string(d, "name");
				        strings(d, "tags");
				        for (const auto *key : {"navigation", "radar"})
				        {
					        bool visible = d.value(key, false);
					        if (ImGui::Checkbox(label(key).c_str(), &visible))
						        d[key] = visible;
				        }
				        auto c = d.value("colour", Json::array({.5, .7, .9}));
				        float rgb[]{c[0], c[1], c[2]};
				        if (ImGui::ColorEdit3(label("Map marker colour").c_str(), rgb))
					        d["colour"] = {rgb[0], rgb[1], rgb[2]};
			        }
			        bool fog = region.contains("nebula");
			        if (ImGui::Checkbox(label("Nebula").c_str(), &fog))
			        {
				        if (fog)
					        region["nebula"] = {{"density", .01},
					                            {"sensor_factor", 1.0},
					                            {"half_height_mm", 100000},
					                            {"colour", {.5, .3, .8}}};
				        else
					        region.erase("nebula");
			        }
			        if (fog)
			        {
				        auto &n = region["nebula"];
				        for (const char *key : {"density", "sensor_factor"})
				        {
					        float value = n.value(key, 1.f);
					        if (ImGui::InputFloat(label(key).c_str(), &value))
						        n[key] = value;
				        }
				        integer(n, "half_height_mm", 100000);
				        const auto c = n.value("colour", Json::array({.5, .3, .8}));
				        float colour[3] = {c[0], c[1], c[2]};
				        if (ImGui::ColorEdit3(label("Nebula colour").c_str(), colour))
					        n["colour"] = {colour[0], colour[1], colour[2]};
			        }
			        bool asteroids = region.contains("asteroids");
			        if (ImGui::Checkbox(label("Asteroid field").c_str(), &asteroids))
			        {
				        if (asteroids)
					        region["asteroids"] = {
					            {"seed", 1}, {"spacing_mm", 25000}, {"density", .0004}, {"types", Json::array()}};
				        else
					        region.erase("asteroids");
			        }
			        if (asteroids)
			        {
				        auto &a = region["asteroids"];
				        for (const auto &[key, fallback] :
				             std::vector<std::pair<const char *, std::int64_t>>{{"seed", 1},
				                                                                {"spacing_mm", 25000},
				                                                                {"weight", 1000},
				                                                                {"jitter_mm", 8000},
				                                                                {"clustering_permille", 500},
				                                                                {"noise_cells", 8},
				                                                                {"decorative_height_mm", 20000},
				                                                                {"decorative_fill_permille", 250},
				                                                                {"spin_min_mdeg_s", 1000},
				                                                                {"spin_max_mdeg_s", 10000},
				                                                                {"visual_distance_mm", 250000},
				                                                                {"unload_margin_mm", 50000}})
					        integer(a, key, fallback);
				        float density = a.value("density", .0004f);
				        if (ImGui::InputFloat(label("Density (rocks per square metre)").c_str(), &density, .0001f,
				                              .001f, "%.6f"))
					        a["density"] = density;
				        for (const char *key : {"types", "decorative_types"})
					        records(a, key, {{"definition", ""}, {"sprite_entry", ""}, {"weight", 1000}},
					                [&](Json &type)
					                {
						                choice(type, "definition",
						                       document.definition_choices(WorkshopDefinitionKind::Entity));
						                string(type, "sprite_entry");
						                integer(type, "weight", 1000);
					                });
			        }
		        });
		if (!map.contains("scenario"))
		{
			if (button("Create scenario"))
				map["scenario"] = Json::object();
			return;
		}
		auto &scenario = map["scenario"];
		choice(scenario, "tree", document.definition_choices(WorkshopDefinitionKind::Tree), true);
		scalar_map(scenario, "parameters");
		strings(scenario, "events");
		strings(scenario, "publications");
		const auto slots = map.contains("ship_slots") ? ids(map.at("ship_slots")) : std::vector<std::string>{};
		if (ScopedTree section(label("Main scenario selectors").c_str()); section)
		{
			if (scenario.contains("bindings"))
				scenario_selectors(scenario["bindings"], scenario, map, slots);
			else if (button("Configure main selectors"))
				scenario["bindings"] = Json::object();
		}
		records(scenario, "situations",
		        {{"id", "situation"},
		         {"tree", ""},
		         {"parameters", Json::object()},
		         {"bindings", Json::object()},
		         {"start", false}},
		        [&](Json &situation)
		        {
			        string(situation, "id");
			        choice(situation, "tree", document.definition_choices(WorkshopDefinitionKind::Tree));
			        scalar_map(situation, "parameters");
			        if (situation.contains("bindings"))
				        scenario_selectors(situation["bindings"], scenario, map, slots);
			        else if (button("Configure situation selectors"))
				        situation["bindings"] = Json::object();
			        boolean(situation, "start");
			        optional_string(situation, "instance_name");
		        });
		records(scenario, "spawn_points", {{"id", "point"}, {"x_mm", 0}, {"y_mm", 0}},
		        [&](Json &point)
		        {
			        string(point, "id");
			        integer(point, "x_mm");
			        integer(point, "y_mm");
		        });
		records(scenario, "spawn_groups", {{"id", "group"}, {"entities", Json::array()}},
		        [&](Json &group)
		        {
			        string(group, "id");
			        records(group, "entities", {{"definition", ""}, {"x_mm", 0}, {"y_mm", 0}, {"heading_mdeg", 0}},
			                [&](Json &entity)
			                {
				                choice(entity, "definition",
				                       document.definition_choices(WorkshopDefinitionKind::Entity));
				                integer(entity, "x_mm");
				                integer(entity, "y_mm");
				                integer(entity, "heading_mdeg");
			                });
		        });
		const auto regions = map.contains("regions") ? ids(map.at("regions")) : std::vector<std::string>{};
		records(
		    scenario, "objectives",
		    {{"id", "objective"}, {"type", "reach_region"}, {"region", ""}, {"title_key", ""}, {"description_key", ""}},
		    [&](Json &objective)
		    {
			    string(objective, "id");
			    choice(objective, "type", {"reach_region", "condition"});
			    boolean(objective, "optional");
			    if (objective.value("type", "") == "condition")
			    {
				    if (!objective.contains("complete_when"))
					    objective["complete_when"] = {{"fact", ""}, {"op", "eq"}, {"value", true}};
				    structure(objective["complete_when"]);
				    if (objective.contains("fail_when"))
					    structure(objective["fail_when"]);
				    else if (button("Add failure condition"))
					    objective["fail_when"] = {{"fact", ""}, {"op", "eq"}, {"value", true}};
			    }
			    else
				    choice(objective, "region", regions);
			    actor(objective, "actor", slots);
			    string(objective, "title_key");
			    string(objective, "description_key");
			    string(objective, "competition");
			    if (!objective.contains("recipients"))
			    {
				    if (button("Bind objective recipients"))
					    objective["recipients"] = {{"ship", {{"slot", slots.empty() ? "" : slots.front()}}},
					                               {"consoles", Json::array()}};
			    }
			    else
			    {
				    actor(objective["recipients"], "ship", slots);
				    strings(objective["recipients"], "consoles");
			    }
		    });
		records(scenario, "fact_bindings",
		        {{"id", "fact"}, {"kind", "entity"}, {"subject", {{"entity", 1}}}, {"field", "lifecycle"}},
		        [&](Json &f)
		        {
			        structure(f);
		        });
		records(scenario, "event_bindings", {{"kind", "entity.departed"}, {"event", "event"}},
		        [&](Json &e)
		        {
			        structure(e);
		        });
		records(scenario, "reports",
		        {{"id", "report"},
		         {"ship", {{"slot", slots.empty() ? "" : slots.front()}}},
		         {"title_key", "Mission report"},
		         {"objectives", Json::array()},
		         {"facts", Json::array()},
		         {"rules", Json::array()},
		         {"fallback",
		          {{"result", "unfinished"},
		           {"title_key", "Unfinished"},
		           {"explanation_key", "Work remains unfinished."}}}},
		        [&](Json &r)
		        {
			        structure(r);
		        });
		records(scenario, "objective_sequences", {{"id", "sequence"}, {"objectives", Json::array()}, {"repeat", false}},
		        [&](Json &sequence)
		        {
			        string(sequence, "id");
			        strings(sequence, "objectives");
			        boolean(sequence, "repeat");
		        });
		if (ScopedTree scope(label("Scenario limits").c_str()); scope)
		{
			if (!scenario.contains("limits"))
			{
				if (button("Override default limits"))
					scenario["limits"] = Json::object();
			}
			else
			{
				integer(scenario["limits"], "spawn_per_tick", 64);
				integer(scenario["limits"], "spawned_population", 1024);
				integer(scenario["limits"], "instances", 128);
				integer(scenario["limits"], "actions", 65536);
				integer(scenario["limits"], "objectives", 4096);
				integer(scenario["limits"], "events", 16384);
			}
		}
		text("Objective definitions become active only when a scenario action adds them. Reach-region completion uses "
		     "actual executed movement, including a crossing without stopping.");
	}
	void scenario_selectors(Json &bindings, const Json &scenario, const Json &map,
	                        const std::vector<std::string> &slots)
	{
		actor(bindings, "actor", slots);
		optional_string(bindings, "knowledge_console");
		string(bindings, "objective");
		choice(bindings, "region", map.contains("regions") ? ids(map.at("regions")) : std::vector<std::string>{}, true);
		std::vector<std::string> events;
		if (scenario.contains("events"))
			for (const auto &event : scenario.at("events"))
				events.push_back(event.get<std::string>());
		choice(bindings, "event", events, true);
	}
	void services(Json &parent)
	{
		heading("External service endpoints");
		static std::map<ImGuiID, std::string> names;
		const auto identity = ImGui::GetID("service-name");
		ImGui::InputText(label("Service name").c_str(), &names[identity]);
		if (button("Add endpoint") && !names[identity].empty())
		{
			if (!parent.contains("services"))
				parent["services"] = Json::object();
			if (!parent["services"].contains(names[identity]))
				parent["services"][names[identity]] = {{"kind", "power"}, {"direction", "output"}, {"target", ""}};
		}
		if (parent.contains("services"))
			object_rows(parent["services"],
			            [&](const std::string &name, Json &row)
			            {
				            ImGui::TextUnformatted(name.c_str());
				            choice(row, "kind", {"power", "coolant", "information"});
				            choice(row, "direction", {"input", "output"});
				            string(row, "target");
				            return button("Remove endpoint");
			            });
	}
	std::string equipment_kind(const WorkshopDocument &document, const Json &instance) const
	{
		if (instance.contains("kind"))
			return instance.at("kind").get<std::string>();
		const auto name = instance.value("template", instance.value("extends", std::string{}));
		return name.empty()
		           ? std::string{}
		           : document.preview_definition(WorkshopDefinitionKind::Equipment, name).value("kind", std::string{});
	}
	void stock_fields(Json &stock)
	{
		string(stock, "capacity_tag");
		integer(stock, "capacity", 100);
		boolean(stock, "single_resource");
		optional_string(stock, "selection");
		if (!stock.contains("filter"))
			stock["filter"] = Json::object();
		{
			ScopedId scope("stock-filter");
			strings(stock["filter"], "tags");
			strings(stock["filter"], "resources");
			boolean(stock["filter"], "require_all");
		}
		scalar_map(stock, "initial");
	}
	void processor_fields(Json &processor)
	{
		optional_string(processor, "recipe");
		strings(processor, "recipes");
		strings(processor, "recipe_tags");
		optional_string(processor, "initial_recipe");
		choice(processor, "mode", {"batch", "continuous"});
		integer(processor, "work_per_second", 20);
		integer(processor, "energy_per_second");
		scalar_map(processor, "inputs");
		scalar_map(processor, "outputs");
	}
	void rule_bindings_fields(Json &source, WorkshopDocument &document)
	{
		records(source, "rules", {{"id", "rule"}, {"rule", ""}, {"target", "subject"}},
		        [&](Json &binding)
		        {
			        string(binding, "id");
			        choice(binding, "rule", document.definition_choices(WorkshopDefinitionKind::Effect));
			        choice(binding, "target",
			               {"owner", "source", "instigator", "subject", "target", "attacker", "protected"});
			        optional_string(binding, "equipment");
			        optional_string(binding, "officer");
			        strings(binding, "tags");
		        });
	}
	void rule_expression_field(Json &source, const char *key, Json initial)
	{
		ScopedId id(key);
		ScopedTree panel(key);
		if (!panel)
			return;
		if (!source.contains(key))
		{
			if (button("Add expression"))
				source[key] = std::move(initial);
		}
		if (source.contains(key))
		{
			structure(source[key]);
			if (button("Remove expression"))
				source.erase(key);
		}
	}
	void effect_operations_fields(Json &group, WorkshopDocument &document)
	{
		optional_string(group, "payer");
		integer(group, "energy");
		records(group, "costs", {{"payer", "target"}, {"store", ""}, {"resource", ""}, {"amount", 0}},
		        [&](Json &cost)
		        {
			        choice(cost, "payer",
			               {"owner", "source", "instigator", "subject", "target", "attacker", "protected"});
			        optional_string(cost, "equipment");
			        string(cost, "store");
			        choice(cost, "resource", document.definition_choices(WorkshopDefinitionKind::Resource));
			        integer(cost, "amount");
		        });
		records(group, "operations", {{"kind", "fill"}, {"target", "protected"}, {"store", ""}, {"resource", ""}},
		        [&](Json &operation)
		        {
			        const auto old = operation.value("kind", std::string("fill"));
			        choice(operation, "kind",
			               {"damage", "apply", "remove", "add", "consume", "set", "fill", "transfer"});
			        const auto kind = operation.value("kind", std::string("fill"));
			        if (kind != old)
			        {
				        const auto target = operation.value("target", std::string("protected"));
				        operation = {{"kind", kind}, {"target", target}};
			        }
			        choice(operation, "target",
			               {"owner", "source", "instigator", "subject", "target", "attacker", "protected"});
			        if (kind == "damage")
				        integer(operation, "damage", 1);
			        else if (kind == "apply" || kind == "remove")
			        {
				        if (kind == "remove")
					        choice(operation, "scope", {"local", "owner"});
				        if (kind == "remove" && operation.value("scope", std::string("local")) == "owner")
				        {
					        operation["target"] = "owner";
					        for (const auto *field :
					             {"grant", "effect", "equipment", "officer", "all_sources", "role", "delta_permille"})
						        operation.erase(field);
					        return;
				        }
				        string(operation, "grant");
				        optional_string(operation, "equipment");
				        optional_string(operation, "officer");
				        if (kind == "apply")
				        {
					        choice(operation, "effect", document.definition_choices(WorkshopDefinitionKind::Effect));
					        optional_string(operation, "role");
					        ScopedId delta_id("delta_permille");
					        if (operation.contains("delta_permille"))
					        {
						        integer(operation, "delta_permille");
						        if (button("Use template deltas"))
							        operation.erase("delta_permille");
					        }
					        else if (button("Override modifier deltas"))
						        operation["delta_permille"] = 0;
				        }
			        }
			        else
			        {
				        string(operation, "store");
				        choice(operation, "resource", document.definition_choices(WorkshopDefinitionKind::Resource));
				        if (kind != "fill")
					        integer(operation, "amount", 1);
				        if (kind == "transfer")
					        string(operation, "from_store");
			        }
		        });
	}
	void templates(WorkshopDocument &document)
	{
		auto &defs = document.definitions();
		records(
		    defs, "resources",
		    {{"id", "resource"}, {"label", "Resource"}, {"tags", Json::array({"cargo"})}, {"sizes", {{"cargo", 1}}}},
		    [&](Json &resource)
		    {
			    string(resource, "id");
			    string(resource, "label");
			    choice(resource, "extends", document.definition_choices(WorkshopDefinitionKind::Resource), true);
			    strings(resource, "tags");
			    scalar_map(resource, "sizes");
			    names(resource, "effects", document.definition_choices(WorkshopDefinitionKind::Effect));
			    if (resource.contains("ammunition"))
			    {
				    ScopedId scope("resource-ammunition");
				    auto &ammo = resource["ammunition"];
				    integer(ammo, "fire_ticks");
				    integer(ammo, "load_ticks");
				    if (ammo.contains("projectile"))
					    projectile_fields(ammo["projectile"]);
				    else if (button("Add ammunition projectile"))
					    ammo["projectile"] = Json::object();
				    if (button("Remove ammunition profile"))
					    resource.erase("ammunition");
			    }
			    else if (button("Add ammunition profile"))
				    resource["ammunition"] = Json::object();
		    });
		records(defs, "recipes",
		        {{"id", "recipe"},
		         {"label", "Recipe"},
		         {"work", 20},
		         {"inputs", Json::object()},
		         {"outputs", Json::object()}},
		        [&](Json &recipe)
		        {
			        string(recipe, "id");
			        string(recipe, "label");
			        choice(recipe, "extends", document.definition_choices(WorkshopDefinitionKind::Recipe), true);
			        strings(recipe, "tags");
			        integer(recipe, "work", 20);
			        scalar_map(recipe, "inputs");
			        scalar_map(recipe, "outputs");
		        });
		if (auto *ship = select_entity(document))
			records(*ship, "processors", {{"id", "processor"}, {"equipment", ""}, {"mode", "batch"}},
			        [&](Json &processor)
			        {
				        string(processor, "id");
				        string(processor, "equipment");
				        processor_fields(processor);
			        });
		records(
		    defs, "effects", {{"id", "effect"}},
		    [&](Json &effect)
		    {
			    string(effect, "id");
			    choice(effect, "kind",
			           {"shield", "modifier", "operations", "directed_mode", "targeting_policy", "payload", "periodic",
			            "ability", "psychic", "ability_grant", "rule"},
			           true);
			    choice(effect, "extends", document.definition_choices(WorkshopDefinitionKind::Effect), true);
			    text("Changing template kind preserves authored fields. Verification reports fields incompatible with "
			         "the selected kind.");
			    auto kind = effect.value("kind", std::string{});
			    if (kind.empty() && effect.contains("extends"))
			    {
				    try
				    {
					    kind = document
					               .preview_definition(WorkshopDefinitionKind::Effect,
					                                   effect.at("extends").get<std::string>())
					               .value("kind", std::string{});
				    }
				    catch (const std::exception &)
				    {
				    }
			    }
			    if (kind == "shield" || kind == "modifier" || kind == "ability_grant")
				    rule_bindings_fields(effect, document);
			    if (kind == "rule")
			    {
				    choice(effect, "trigger",
				           {"activate", "deactivate", "tick", "enter", "exit", "occupancy", "maintain",
				            "pre_shield_damage", "post_shield_damage", "pre_compartment_damage",
				            "post_compartment_damage", "pre_system_damage", "post_system_damage", "post_entity_damage",
				            "destroy"});
				    choice(effect, "phase", {"instant", "periodic", "maintained"});
				    choice(effect, "effect", document.definition_choices(WorkshopDefinitionKind::Effect), true);
				    if (effect.value("phase", std::string("instant")) == "periodic")
					    integer(effect, "period_ticks", 1);
				    rule_expression_field(effect, "when", true);
				    rule_expression_field(effect, "damage_permille", {{"value", 1000}, {"unit", "permille"}});
				    rule_expression_field(effect, "energy", {{"value", 0}, {"unit", "energy"}});
				    rule_expression_field(effect, "values", Json::object());
				    rule_expression_field(effect, "costs", Json::array());
				    rule_expression_field(effect, "funding", {{"shortage", "pause"}, {"costs", Json::array()}});
				    return;
			    }
			    if (kind == "ability")
			    {
				    string(effect, "label");
				    boolean(effect, "sustained", false);
				    boolean(effect, "require_healthy", true);
				    choice(effect, "target", {"self", "contact", "ally"});
				    choice(effect, "recovery_on", {"activation", "stop"});
				    choice(effect, "interruption", {"cancel", "pause"});
				    choice(effect, "shortage", {"proportional", "pause", "stop"});
				    for (const auto *field :
				         {"preparation_ticks", "recovery_ticks", "energy_per_second", "preparation_energy_per_second",
				          "apparatus_recovery_ticks", "interruption_recovery_ticks"})
					    integer(effect, field);
				    optional_string(effect, "apparatus_recovery_key");
				    for (const auto *field : {"start_effect", "activation_effect", "psychic_effect"})
					    choice(effect, field, document.definition_choices(WorkshopDefinitionKind::Effect), true);
				    records(effect, "exclusions", {{"id", "exclusive"}, {"scope", "source"}},
				            [&](Json &exclusion)
				            {
					            string(exclusion, "id");
					            choice(exclusion, "scope", {"source", "apparatus"});
				            });
				    return;
			    }
			    if (kind == "psychic")
			    {
				    choice(effect, "psychic_kind", {"cloak", "scramble", "ward"});
				    optional_string(effect, "label");
				    integer(effect, "strength_permille", 700);
				    if (effect.value("psychic_kind", std::string{}) == "scramble")
				    {
					    optional_string(effect, "apparent_class");
					    for (const auto *field :
					         {"ghost_count", "offset_mm", "spacing_mm", "speed_mm_s", "acceleration_mm_s2",
					          "turn_mdeg_s", "radius_mm", "scan_ticks", "fade_ticks"})
						    integer(effect, field, effect.value(field, 1LL));
				    }
				    return;
			    }
			    if (kind == "ability_grant")
			    {
				    choice(effect, "ability", document.definition_choices(WorkshopDefinitionKind::Effect));
				    integer(effect, "duration_ticks", 100);
				    return;
			    }
			    if (kind == "targeting_policy")
			    {
				    string(effect, "label");
				    choice(effect, "policy", {"designated", "intercept", "off"});
				    return;
			    }
			    if (kind == "directed_mode")
			    {
				    string(effect, "label");
				    choice(effect, "operation", {"launch", "beam", "scan"});
				    if (effect.value("operation", std::string{}) != "scan" || effect.contains("firing_audio"))
					    audio_fields(effect, "firing_audio");
				    names(effect, "targeting", document.definition_choices(WorkshopDefinitionKind::Effect));
				    names(effect, "payloads", document.definition_choices(WorkshopDefinitionKind::Effect));
				    boolean(effect, "resource_payloads", true);
				    integer(effect, "range_mm", 1000000);
				    integer(effect, "ammo_use", 1);
				    integer(effect, "fire_ticks", 20);
				    integer(effect, "penetration", 1000);
				    integer(effect, "damage_per_m", 1000000);
				    for (const auto *field : {"energy_per_second", "discrete_energy", "auto_energy_per_second",
				                              "damage_per_second", "load_ticks", "shield_bypass_permille"})
					    integer(effect, field);
				    if (effect.value("operation", std::string{}) == "scan")
					    scalar_map(effect, "capabilities");
				    if (effect.contains("projectile"))
				    {
					    projectile_fields(effect["projectile"]);
					    if (button("Remove mode projectile"))
						    effect.erase("projectile");
				    }
				    else if (button("Add mode projectile"))
					    effect["projectile"] = Json::object();
				    return;
			    }
			    if (kind == "payload")
			    {
				    rule_bindings_fields(effect, document);
				    return;
			    }
			    if (kind == "periodic")
			    {
				    integer(effect, "duration_ticks", 100);
				    rule_bindings_fields(effect, document);
				    return;
			    }
			    if (kind == "operations")
			    {
				    effect.erase("contributions");
				    effect.erase("duration_ticks");
				    effect_operations_fields(effect, document);
				    return;
			    }
			    effect.erase("operations");
			    effect.erase("payer");
			    effect.erase("energy");
			    const bool modifier = kind == "modifier";
			    integer(effect, "duration_ticks");
			    if (button("Add contribution"))
			    {
				    auto &contributions = effect["contributions"];
				    if (!contributions.is_object())
					    contributions = Json::object();
				    std::string name = "arc";
				    unsigned index = 1;
				    while (contributions.contains(name))
					    name = "arc" + std::to_string(index++);
				    contributions[name] =
				        modifier ? Json{{"property", "recharge"}, {"delta_permille", 100}} : Json{{"capacity", 100}};
			    }
			    std::string remove;
			    if (effect.contains("contributions") && effect.at("contributions").is_object())
				    for (auto &[name, arc] : effect["contributions"].items())
				    {
					    ScopedId scope(name.c_str());
					    heading(name.c_str());
					    if (modifier)
					    {
						    std::vector<std::string> properties;
						    for (const auto &property : modifier_properties())
							    properties.push_back(property.id);
						    choice(arc, "property", properties);
						    optional_string(arc, "role");
						    integer(arc, "delta_permille");
						    if (button("Remove contribution"))
							    remove = name;
						    continue;
					    }
					    for (const char *key :
					         {"capacity", "initial", "bearing_mdeg", "arc_mdeg", "layer", "sustain_energy_per_second",
					          "energy_per_strength", "recharge_per_second", "off_recharge_per_second", "warmup_ticks",
					          "discharge_per_second", "discharge_efficiency_permille"})
						    integer(arc, key);
					    audio_fields(arc, "audio", true);
					    scalar_map(arc, "modifiers");
					    rule_bindings_fields(arc, document);
					    if (button("Remove contribution"))
						    remove = name;
				    }
			    if (!remove.empty())
				    effect["contributions"].erase(remove);
		    });
		records(defs, "equipment",
		        {{"id", "equipment"}, {"kind", "engine"}, {"max_speed_mm_s", 10000}, {"acceleration_mm_s2", 1000}},
		        [&](Json &equipment)
		        {
			        string(equipment, "id");
			        choice(equipment, "extends", document.definition_choices(WorkshopDefinitionKind::Equipment), true);
			        choice(equipment, "kind",
			               {"engine", "steering", "radar", "generator", "distribution", "coolant", "storage", "lateral",
			                "boost", "impulse", "shield", "magazine", "weapon", "turret", "team"});
			        const auto kind = equipment_kind(document, equipment);
			        system_details(equipment, kind);
			        rule_bindings_fields(equipment, document);
			        if (kind == "shield")
				        choice(equipment, "shield_effect", document.definition_choices(WorkshopDefinitionKind::Effect));
			        telemetry_records(equipment, document);
			        scalar_map(equipment, "parameters");
		        });
		records(defs, "assemblies",
		        {{"id", "assembly"},
		         {"systems", Json::object()},
		         {"resources", Json::object()},
		         {"services", Json::object()}},
		        [&](Json &assembly)
		        {
			        string(assembly, "id");
			        text("Assembly internal identities receive a placement prefix. Bind service names explicitly; "
			             "$service references resolve to those bindings.");
			        static std::string system_id, service_id;
			        ImGui::InputText(label("Internal System identity").c_str(), &system_id);
			        if (button("Add internal System") && !system_id.empty())
			        {
				        assembly["systems"][system_id] = {{"template", ""}};
				        system_id.clear();
			        }
			        if (assembly.contains("systems"))
				        for (auto it = assembly["systems"].begin(); it != assembly["systems"].end(); ++it)
				        {
					        ScopedId id_scope_23(it.key().c_str());
					        ImGui::TextUnformatted(it.key().c_str());
					        choice(it.value(), "template",
					               document.definition_choices(WorkshopDefinitionKind::Equipment), true);
					        choice(it.value(), "kind",
					               {"engine", "steering", "radar", "generator", "distribution", "coolant", "storage",
					                "lateral", "boost", "impulse"},
					               true);
					        system_details(it.value(), equipment_kind(document, it.value()));
					        text("Absent values inherit from the equipment template. Endpoint names and $service "
					             "bindings remain local to this assembly until placement.");
					        scalar_map(it.value(), "parameters");
					        telemetry_records(it.value(), document);
				        }
			        ImGui::InputText(label("Service name").c_str(), &service_id);
			        if (button("Add service") && !service_id.empty())
			        {
				        assembly["services"][service_id] = {{"kind", "power"}, {"direction", "input"}};
				        service_id.clear();
			        }
			        if (assembly.contains("services"))
				        object_rows(assembly["services"],
				                    [&](const std::string &name, Json &row)
				                    {
					                    ImGui::TextUnformatted(name.c_str());
					                    choice(row, "kind", {"power", "coolant", "information"});
					                    choice(row, "direction", {"input", "output"});
					                    string(row, "target");
					                    return button("Remove service");
				                    });
			        if (ScopedTree scope(label("Assembly resource network").c_str()); scope)
			        {
				        if (assembly.contains("resources"))
				        {
					        std::vector<std::string> external;
					        if (assembly.contains("services"))
						        for (const auto &service : ids(assembly.at("services")))
							        external.push_back("$" + service);
					        network_editor(assembly["resources"], external);
				        }
				        else if (button("Create assembly network"))
					        assembly["resources"] = Json::object();
			        }
		        });
	}
	void placements(WorkshopDocument &document)
	{
		if (auto *ship = select_entity(document))
		{
			names(*ship, "message_templates", document.definition_choices(WorkshopDefinitionKind::Message));
			structured_field(*ship, "parameters");
			services(*ship);
			records(
			    *ship, "assemblies",
			    {{"id", "placement"}, {"assembly", ""}, {"bindings", Json::object()}, {"overrides", Json::object()}},
			    [&](Json &placement)
			    {
				    string(placement, "id");
				    choice(placement, "assembly", document.definition_choices(WorkshopDefinitionKind::Assembly));
				    scalar_map(placement, "bindings");
				    structured_field(placement, "overrides");
			    });
		}
	}
	void control_layout(WorkshopDocument &document)
	{
		auto *ship = select_entity(document);
		if (!ship)
			return;
		const auto systems = fitted_ids(document, *ship);
		const auto &resolved = composition.empty() ? *ship : composition;
		std::vector<std::string> consoles;
		for (const auto &station : resolved.value("stations", Json::array()))
			for (const auto &console : station.at("consoles"))
				consoles.push_back(console.at("id").get<std::string>());
		if (consoles.empty() && resolved.contains("console"))
			consoles.push_back("main");
		if (!ship->contains("control_layout"))
		{
			if (resolved.contains("control_layout"))
			{
				text("Controller layout is inherited.");
				if (button("Override Controller layout"))
					(*ship)["control_layout"] = resolved.at("control_layout");
			}
			else if (button("Create Controller layout"))
				(*ship)["control_layout"] = {{"schema", 1},
				                             {"controllers", Json::array()},
				                             {"profiles", Json::array()},
				                             {"default_profile", ""}};
			if (!ship->contains("control_layout"))
				return;
		}
		auto &layout = (*ship)["control_layout"];
		text("Controllers own command sections. Profiles choose human or AI operation; absent humans use AI. Remove "
		     "legacy policy/preset bindings before verifying an explicit layout.");
		records(layout, "controllers",
		        {{"id", "controller"}, {"console", ""}, {"fallback", "hold"}, {"commands", Json::array()}},
		        [&](Json &controller)
		        {
			        string(controller, "id");
			        choice(controller, "console", consoles);
			        if (choice(controller, "policy", {"combat"}, true) &&
			            !controller.value("policy", std::string{}).empty())
			        {
				        controller.erase("tree");
				        controller["fallback"] = "hold";
			        }
			        if (choice(controller, "tree", document.definition_choices(WorkshopDefinitionKind::Tree), true))
			        {
				        if (controller.contains("tree") && !controller.at("tree").get<std::string>().empty())
					        controller.erase("fallback");
				        else
				        {
					        controller.erase("tree");
					        controller["fallback"] = "hold";
				        }
			        }
			        records(controller, "commands",
			                {{"id", "command"}, {"action", "speed_mm_s"}, {"initial", 0}, {"consumers", Json::array()}},
			                [&](Json &command)
			                {
				                string(command, "id");
				                std::vector<std::string> actions;
				                for (const auto &action : operating_actions)
					                actions.emplace_back(action.name);
				                choice(command, "action", actions);
				                integer(command, "initial");
				                if (const auto *contract = operating_action(command.value("action", std::string{})))
				                {
					                const auto arguments = operating_arguments(contract->action);
					                if (!arguments.empty())
					                {
						                if (!command.contains("arguments") ||
						                    command["arguments"].size() != arguments.size())
						                {
							                command["arguments"] = Json::array();
							                for (const auto &field : arguments)
								                command["arguments"].push_back(
								                    std::max<std::int64_t>(0, field.minimum));
						                }
						                for (std::size_t i = 0; i < arguments.size(); ++i)
						                {
							                Json edit{{"value", command["arguments"][i]}};
							                ScopedId scope(std::string(arguments[i].name).c_str());
							                text(std::string(arguments[i].name).c_str());
							                integer(edit, "value");
							                command["arguments"][i] = edit["value"];
						                }
					                }
					                else
						                command.erase("arguments");
				                }
				                for (const auto *key : {"minimum", "maximum"})
				                {
					                ScopedId scope(key);
					                if (command.contains(key))
					                {
						                integer(command, key);
						                if (button("Use fitted limit"))
							                command.erase(key);
					                }
					                else if (button(key))
						                command[key] = 0;
				                }
				                string(command, "label_key");
				                const auto action = command.value("action", std::string{});
				                if (action == "intent_integer" || action == "intent_boolean" || action == "waypoint")
				                {
					                if (!command.contains("choices"))
					                {
						                if (button("Add labelled choices"))
							                command["choices"] = Json::array();
					                }
					                else
					                {
						                records(command, "choices", {{"value", 0}, {"label_key", "Choice"}},
						                        [&](Json &choice)
						                        {
							                        integer(choice, "value");
							                        string(choice, "label_key");
						                        });
						                if (button("Use continuous values"))
							                command.erase("choices");
					                }
				                }
				                names(command, "consumers", systems);
			                });
		        });
		records(layout, "profiles", {{"id", "profile"}, {"consoles", Json::object()}},
		        [&](Json &profile)
		        {
			        string(profile, "id");
			        for (const auto &console : consoles)
			        {
				        ScopedId scope(console.c_str());
				        heading(console.c_str());
				        std::vector<std::string> controllers;
				        for (const auto &controller : layout.at("controllers"))
					        if (controller.value("console", std::string{}) == console)
						        controllers.push_back(controller.at("id").get<std::string>());
				        auto &allocation = profile["consoles"][console];
				        if (!allocation.is_object())
					        allocation = Json::object();
				        names(allocation, "human", controllers);
				        names(allocation, "ai", controllers);
			        }
		        });
		choice(layout, "default_profile", ids(layout.at("profiles")));
	}
	void controller_bindings(Json &tree, WorkshopDocument &document, const Json &resolved)
	{
		const auto editable = [&](const char *key)
		{
			ScopedId scope(key);
			if (!tree.contains(key) && resolved.contains(key))
			{
				heading(key);
				text("Inherited bindings. Override the complete group before editing it.");
				auto inherited = resolved.at(key);
				structure(inherited, false);
				if (button("Override inherited binding group"))
					tree[key] = std::move(inherited);
				else
					return false;
			}
			if (tree.contains(key) && button("Remove local binding group"))
			{
				tree.erase(key);
				return false;
			}
			return true;
		};

		if (editable("action_bindings"))
			records(tree, "action_bindings", {{"id", "action"}, {"tree", ""}},
			        [&](Json &binding)
			        {
				        string(binding, "id");
				        choice(binding, "tree", document.definition_choices(WorkshopDefinitionKind::Tree));
				        for (const auto *key : {"channels", "observations"})
				        {
					        if (!binding.contains(key))
						        binding[key] = Json::object();
					        heading(key);
					        structure(binding[key], true);
				        }
			        });
		optional_string(tree, "decision_context");
		if (editable("decision_candidates"))
			records(tree, "decision_candidates",
			        {{"id", "candidate"},
			         {"description", "Describe this officer decision"},
			         {"available", Json::array()},
			         {"tasks", Json::array()}},
			        [&](Json &candidate)
			        {
				        string(candidate, "id");
				        string(candidate, "description");
				        string(candidate, "deviation");
				        heading("Availability from permitted observations");
				        structure(candidate["available"], true);
				        heading("Prepared action and coordination tasks");
				        structure(candidate["tasks"], true);
			        });
		if (tree.contains("controller_action") || button("Declare reusable Controller action"))
		{
			auto &action = tree["controller_action"];
			if (action.is_null())
				action = {{"label", "Controller action"}};
			string(action, "label");
			string(action, "description");
			records(action, "inputs", {{"name", "target"}, {"type", "integer"}},
			        [&](Json &input)
			        {
				        string(input, "name");
				        string(input, "description");
				        string(input, "unit");
				        choice(input, "type", {"integer", "boolean", "text"});
			        });
			if (!action.contains("available"))
				action["available"] = Json::array();
			heading("Availability conditions");
			structure(action["available"], true);
		}
		if (editable("command_bindings"))
			records(tree, "command_bindings", {{"channel", "command"}, {"type", "integer"}, {"semantics", "setting"}},
			        [&](Json &binding)
			        {
				        string(binding, "channel");
				        choice(binding, "type", {"integer", "boolean"});
				        choice(binding, "semantics", {"setting", "request"});
				        choice(binding, "action", {"", "destination", "follow_contact", "ability_activate"});
				        if (binding.value("action", std::string{}).empty())
					        binding.erase("action");
			        });
		if (editable("ftl_system"))
			optional_string(tree, "ftl_system");
		if (editable("system_bindings"))
			records(tree, "system_bindings", {{"name", "observed"}, {"system", ""}, {"field", "enabled"}},
			        [&](Json &binding)
			        {
				        string(binding, "name");
				        string(binding, "system");
				        string(binding, "field");
			        });
		if (editable("command_reads"))
			records(tree, "command_reads",
			        {{"name", "accepted"}, {"controller", ""}, {"channel", ""}, {"type", "integer"}},
			        [&](Json &binding)
			        {
				        string(binding, "name");
				        string(binding, "controller");
				        string(binding, "channel");
				        choice(binding, "type", {"integer", "boolean"});
			        });
		if (editable("ability_bindings"))
			strings(tree, "ability_bindings");
		if (editable("contact_bindings"))
			records(tree, "contact_bindings", {{"name", "contact"}, {"controller", ""}, {"channel", ""}},
			        [&](Json &binding)
			        {
				        string(binding, "name");
				        string(binding, "controller");
				        string(binding, "channel");
			        });
		if (editable("publication_templates"))
			strings(tree, "publication_templates");
		if (editable("message_templates"))
			names(tree, "message_templates", document.definition_choices(WorkshopDefinitionKind::Message));
		const auto knowledge = document.preview_knowledge();
		if (editable("message_bindings"))
			records(tree, "message_bindings", {{"name", "payload"}, {"schema", ""}, {"field", ""}},
			        [&](Json &binding)
			        {
				        string(binding, "name");
				        choice(binding, "schema", ids(knowledge.at("schemas")));
				        std::vector<std::string> fields;
				        for (const auto &schema : knowledge.at("schemas"))
					        if (schema.at("id") == binding.value("schema", std::string{}))
						        fields = ids(schema.at("fields"));
				        choice(binding, "field", fields);
			        });
	}
	void controller_palette(const Json &tree, WorkshopDocument &document)
	{
		vocabulary["controller"] = {
		    {"actions", Json::array()}, {"observations", Json::array()}, {"intents", Json::array()}};
		try
		{
			const auto registry = document.preview_controller_registry(tree);
			const auto type_name = [](state_tree::Type type)
			{
				static const char *names[] = {"missing", "boolean", "integer",   "text",   "entity",
				                              "system",  "console", "objective", "region", "message"};
				return names[static_cast<unsigned>(type)];
			};
			for (const auto &field : registry.observations)
				vocabulary["controller"]["observations"].push_back(
				    {{"id", field.name}, {"type", type_name(field.type)}});
			for (const auto &field : registry.intents)
				vocabulary["controller"]["intents"].push_back({{"id", field.name}, {"type", type_name(field.type)}});
			for (const auto &action : registry.actions)
			{
				Json arguments = Json::array();
				for (std::size_t i = 0; i < action.arguments.size(); ++i)
					arguments.push_back({{"name", std::to_string(i + 1)}, {"type", type_name(action.arguments[i])}});
				vocabulary["controller"]["actions"].push_back(
				    {{"id", action.name}, {"name", action.name}, {"arguments", arguments}});
			}
		}
		catch (const std::exception &error)
		{
			ImGui::TextWrapped("%s", error.what());
		}
	}
	void messages(WorkshopDocument &document)
	{
		auto &defs = document.definitions();
		const auto known = document.preview_knowledge();
		const auto schemas = ids(known.at("schemas"));
		Json context{{"context", "controller"}};
		std::map<std::string, Json> contexts;
		std::vector<std::string> options;
		for (const auto &id : document.definition_choices(WorkshopDefinitionKind::Tree))
		{
			try
			{
				const auto resolved = document.preview_tree(id);
				if (resolved.value("context", std::string{}) == "controller")
				{
					options.push_back(id);
					contexts.emplace(id, resolved);
				}
			}
			catch (const std::exception &error)
			{
				ImGui::TextWrapped("%s", error.what());
			}
		}
		Json selection{{"Binding tree", message_tree}};
		if (choice(selection, "Binding tree", options, true))
			message_tree = selection.value("Binding tree", std::string{});
		if (contexts.contains(message_tree))
			context = contexts.at(message_tree);
		controller_palette(context, document);
		text("Binding tree supplies authoring choices only. Every actual sender must satisfy the template's bindings.");
		records(defs, "messages",
		        {{"id", "message"},
		         {"schema", ""},
		         {"title_key", ""},
		         {"body_key", ""},
		         {"urgency", 0},
		         {"destination", {{"kind", "ship"}, {"id", ""}}},
		         {"delay_ticks", 1},
		         {"expires_after_ticks", 0},
		         {"request_key", ""},
		         {"fields", Json::object()},
		         {"parameters", Json::object()}},
		        [&](Json &message)
		        {
			        string(message, "id");
			        choice(message, "schema", schemas);
			        string(message, "title_key");
			        string(message, "body_key");
			        integer(message, "urgency");
			        boolean(message, "automatic", true);
			        boolean(message, "human_seeking", false);
			        choice(message["destination"], "kind", {"controller", "console", "station", "ship"});
			        if (message["destination"].value("kind", std::string{}) == "controller")
				        string(message["destination"], "console");
			        string(message["destination"], "id");
			        integer(message, "delay_ticks", 1);
			        integer(message, "expires_after_ticks");
			        string(message, "request_key");
			        std::vector<std::string> fields;
			        if (known.contains("schemas"))
				        for (const auto &schema : known.at("schemas"))
					        if (schema.at("id") == message.value("schema", std::string{}))
						        fields = ids(schema.at("fields"));
			        binding_map(message, "fields", context, fields,
			                    {"integer", "boolean", "text", "argument", "parameter", "observation", "intent",
			                     "memory", "elapsed"});
			        binding_map(message, "parameters", context, {},
			                    {"integer", "boolean", "text", "argument", "parameter", "observation", "intent",
			                     "memory", "elapsed"});
		        });

		text("Payload fields are typed data. Display parameters format translated text; AI never parses that text. "
		     "Sending requires a declared action or reporting rule, and delivery does not grant control authority.");
	}
	void disposable_test(WorkshopDocument &document, ToolMessage &status)
	{
		heading("Disposable production test");
		text("Run an isolated simulation of the verified draft. Pause, step and restart apply to this test.");
		Json selection{{"Mission", test_map}, {"Ship", test_ship}, {"Slot", test_slot}};
		if (choice(selection, "Mission", document.definition_choices(WorkshopDefinitionKind::Map)))
			test_map = selection.at("Mission").get<std::string>();
		if (choice(selection, "Ship", document.definition_choices(WorkshopDefinitionKind::Entity)))
			test_ship = selection.at("Ship").get<std::string>();
		std::vector<std::string> slots;
		if (!test_map.empty())
		{
			try
			{
				const auto map = document.preview_definition(WorkshopDefinitionKind::Map, test_map);
				if (map.contains("ship_slots"))
					slots = ids(map.at("ship_slots"));
			}
			catch (const std::exception &error)
			{
				ImGui::TextWrapped("%s", error.what());
			}
		}
		if (choice(selection, "Slot", slots))
			test_slot = selection.at("Slot").get<std::string>();
		for (const auto &[title, action] :
		     std::vector<std::pair<const char *, GameplayTestAction>>{{"Start test", GameplayTestAction::Start},
		                                                              {"Pause test", GameplayTestAction::Pause},
		                                                              {"Step test", GameplayTestAction::Step},
		                                                              {"Restart test", GameplayTestAction::Restart},
		                                                              {"Stop test", GameplayTestAction::Stop}})
		{
			if (button(title))
			{
				if (action == GameplayTestAction::Start && (test_map.empty() || test_slot.empty() || test_ship.empty()))
					status = "Choose a mission, slot and ship before starting a test.";
				else
				{
					GameplayTestRequest request;
					request.action = action;
					request.mission = test_map;
					request.slot = test_slot;
					request.ship = test_ship;
					if (action == GameplayTestAction::Start)
					{
						request.pack = document.export_pack();
						request.dependencies = document.dependency_archives();
					}
					test_request = std::move(request);
					status = "Disposable test request queued.";
				}
			}
			ImGui::SameLine();
		}
		ImGui::NewLine();
		heading("Disposable runtime inspection");
		if (feedback.contains("controllers"))
		{
			Json selection{{"Inspected controller", inspected_controller}};
			if (choice(selection, "Inspected controller", ids(feedback.at("controllers")), true))
				inspected_controller = selection.value("Inspected controller", std::string{});
			for (auto &controller : feedback["controllers"])
				if (identity(controller) == inspected_controller)
					structure(controller, false);
		}
		if (feedback.empty())
			text("No disposable test feedback has been received.");
		else
			structure(feedback, false);
	}

	void console_properties(WorkshopDocument &document, Application &app, Json &console)
	{
		pseudo = app.pseudo_locale;
		boolean(console, "human_seeking", false);
		boolean(console, "command", false);
		boolean(console, "hide_without_ai", false);
		boolean(console, "viewscreen", false);
		optional_string(console, "default_stance");
		records(console, "stances", {{"id", "stance"}, {"label", "Stance"}},
		        [&](Json &stance)
		        {
			        string(stance, "id");
			        string(stance, "label");
			        optional_string(stance, "description");
		        });
		if (console.value("viewscreen", false))
		{
			if (!console.contains("compact"))
				console["compact"] = {{"widgets", console.value("widgets", Json::array())}};
			ScopedTree compact("Compact Viewscreen HUD");
			if (compact)
				structure(console["compact"], true);
		}
		gameplay_panel(console);
		const auto known = document.preview_knowledge();
		std::vector<std::string> roles;
		for (const auto &role : known.at("roles"))
			roles.push_back(role.get<std::string>());
		names(console, "knowledge_roles", roles);
		names(console, "knowledge_producers", roles);
		std::set<std::string> available;
		std::vector<Json> controller_choices;
		auto collect = [&](const Json &ship)
		{
			bool uses = ship.value("console", std::string{}) == console.at("id").get<std::string>();
			if (ship.contains("stations"))
				for (const auto &station : ship.at("stations"))
					for (const auto &seat : station.at("consoles"))
						uses = uses || seat.value("definition", std::string{}) == console.at("id").get<std::string>();
			if (uses && ship.contains("systems"))
				for (const auto &id : ids(ship.at("systems")))
					available.insert(id);
			if (uses && ship.contains("control_layout"))
				for (const auto &controller : ship.at("control_layout").value("controllers", Json::array()))
					controller_choices.push_back(controller);
		};
		try
		{
			for (const auto &[id, ship] : document.preview_equipment().entities)
				collect(ship);
		}
		catch (const std::exception &)
		{
			for (const auto &ship : document.definitions().at("entities"))
				collect(ship);
		}
		const std::vector<std::string> systems(available.begin(), available.end());
		if (console.contains("widgets"))
			for (auto &widget : console["widgets"])
			{
				ScopedId identity(widget.at("id").get<std::string>().c_str());
				ImGui::TextUnformatted(widget.at("id").get_ref<const std::string &>().c_str());
				if (widget.at("kind") == "radar")
					boolean(widget, "fire_arcs");
				if (widget.at("kind") == "throttle" || widget.at("kind") == "heading")
				{
					bool controlled = widget.contains("controller");
					if (ImGui::Checkbox(label("Use Controller command").c_str(), &controlled))
					{
						if (controlled)
						{
							widget.erase("system");
							widget["controller"] = "";
							widget["channel"] = "";
						}
						else
						{
							widget.erase("controller");
							widget.erase("channel");
							widget["binding"] =
							    widget.at("kind") == "throttle" ? "engine.desired_speed" : "steering.desired_heading";
						}
					}
				}
				if (widget.contains("controller"))
				{
					std::set<std::string> owners, channels;
					for (const auto &controller : controller_choices)
						for (const auto &command : controller.value("commands", Json::array()))
							if (command.value("action", "") ==
							    (widget.at("kind") == "throttle" ? "speed_mm_s" : "heading_mdeg"))
							{
								owners.insert(controller.at("id"));
								if (controller.at("id") == widget.at("controller"))
									channels.insert(command.at("id"));
							}
					choice(widget, "controller", {owners.begin(), owners.end()});
					choice(widget, "channel", {channels.begin(), channels.end()});
				}
				else
					choice(widget, "system", systems, true);
			}
	}

	void properties(WorkshopDocument &document, Application &app, ToolMessage &status, const std::string &selection,
	                const std::string &selected_entity)
	{
		pseudo = app.pseudo_locale;
		entity = selected_entity;
		try
		{
			if (selection == "Entity")
			{
				using Inspector = void (GameplayEditor::*)(WorkshopDocument &);
				for (const auto &[title, inspect] : std::vector<std::pair<const char *, Inspector>>{
				         {"Systems and ownership", &GameplayEditor::fitted},
				         {"Compartments and combat", &GameplayEditor::combat},
				         {"Power and cooling", &GameplayEditor::resources},
				         {"Fitting and assemblies", &GameplayEditor::placements},
				         {"Knowledge feeds and publications", &GameplayEditor::knowledge_bindings},
				         {"Console Controllers and complexity", &GameplayEditor::control_layout}})
				{
					if (controller_probe && std::string(title) == "Console Controllers and complexity")
						ImGui::SetNextItemOpen(true, ImGuiCond_Always);
					if (ImGui::CollapsingHeader(label(title).c_str()))
					{
						ScopedId scope(title);
						(this->*inspect)(document);
					}
				}
			}
			else if (selection == "Templates and assemblies")
				templates(document);
			else if (selection == "Knowledge")
				knowledge(document);
			else if (selection == "Coordination")
				messages(document);
			else if (selection == "State trees")
			{
				auto active = selection;
				trees(document, true, active);
			}
			else if (selection == "Scenario")
				scenario(document);
		}
		catch (const std::exception &error)
		{
			status = error.what();
			ImGui::TextWrapped("%s", status.c_str());
		}
	}
	void areas(WorkshopDocument &document, Application &app, ToolMessage &status, std::string &selection)
	{
		pseudo = app.pseudo_locale;
		if (tool_window(label("State trees").c_str()))
		{
			try
			{
				trees(document, false, selection);
			}
			catch (const std::exception &error)
			{
				status = error.what();
			}
		}
		ImGui::End();
		if (tool_window(label("Scenario").c_str()))
		{
			text("Select a map to inspect its regions, objectives, spawns and situations in Properties.");
			auto &maps = document.definitions()["maps"];
			const auto current = document.map().value("id", std::string{});
			for (std::size_t i = 0; i < maps.size(); ++i)
			{
				const auto id = maps[i].at("id").get<std::string>();
				if (ImGui::Selectable(id.c_str(), selection == "Scenario" && current == id))
				{
					document.select_map(i);
					selection = "Scenario";
				}
			}
			if (button("Inspect scenario"))
				selection = "Scenario";
			if (button("Inspect ship slots"))
				selection = "Ship slots";
		}
		ImGui::End();
		if (controller_probe)
			ImGui::SetNextWindowFocus();
		if (tool_window(label("Test").c_str()))
		{
			try
			{
				disposable_test(document, status);
			}
			catch (const std::exception &error)
			{
				status = error.what();
			}
		}
		ImGui::End();
	}
};
GameplayEditor gameplay_editor;
} // namespace
void draw_workshop_areas(WorkshopDocument &document, Application &app, ToolMessage &status, std::string &selection)
{
	if (gameplay_editor.vocabulary.empty())
		set_gameplay_authoring_vocabulary(gameplay_tree_vocabulary());
	gameplay_editor.areas(document, app, status, selection);
}
void draw_gameplay_properties(WorkshopDocument &document, Application &app, ToolMessage &status,
                              const std::string &selection, const std::string &entity)
{
	if (gameplay_editor.vocabulary.empty())
		set_gameplay_authoring_vocabulary(gameplay_tree_vocabulary());
	gameplay_editor.properties(document, app, status, selection, entity);
}
void draw_console_gameplay_properties(WorkshopDocument &document, Application &app, Json &console)
{
	gameplay_editor.console_properties(document, app, console);
}
void set_gameplay_authoring_vocabulary(const Json &vocabulary)
{
	if (vocabulary.contains("controller") || vocabulary.contains("system") || vocabulary.contains("scenario"))
	{
		gameplay_editor.vocabulary = vocabulary;
		return;
	}
	Json contexts = Json::object();
	for (const auto &[context, mask] :
	     std::vector<std::pair<std::string, unsigned>>{{"system", 1}, {"scenario", 2}, {"controller", 4}})
	{
		contexts[context] = {{"actions", Json::array()}, {"observations", Json::array()}, {"intents", Json::array()}};
		for (const char *kind : {"actions", "observations", "intents"})
			if (vocabulary.contains(kind))
				for (auto descriptor : vocabulary.at(kind))
					if ((descriptor.value("contexts", 3u) & mask) != 0)
					{
						descriptor["id"] = descriptor.at("name");
						contexts[context][kind].push_back(std::move(descriptor));
					}
	}
	const auto dialogue = conversation_registry();
	contexts["conversation"] = {
	    {"actions", Json::array()}, {"observations", Json::array()}, {"intents", Json::array()}};
	for (const auto &f : dialogue.observations)
		contexts["conversation"]["observations"].push_back(
		    {{"id", f.name},
		     {"name", f.name},
		     {"type", f.type == state_tree::Type::Text      ? "text"
		              : f.type == state_tree::Type::Boolean ? "boolean"
		                                                    : "integer"}});
	for (const auto &a : dialogue.actions)
		contexts["conversation"]["actions"].push_back(
		    {{"id", a.name},
		     {"name", a.name},
		     {"arguments", Json::array({{{"name", "definition"}, {"type", "text"}}})},
		     {"effect", true}});
	const auto teams = team_registry();
	contexts["team"] = {{"observations", Json::array()}, {"actions", Json::array()}};
	for (const auto &f : teams.observations)
		contexts["team"]["observations"].push_back(
		    {{"id", f.name}, {"type", f.type == state_tree::Type::Boolean ? "boolean" : "integer"}});
	for (const auto &a : teams.actions)
		contexts["team"]["actions"].push_back({{"id", a.name}, {"arguments", Json::array()}});
	const auto away = away_registry(Json{{"boarding", true}});
	contexts["away"] = {{"observations", Json::array()}, {"actions", Json::array()}};
	for (const auto &f : away.observations)
		contexts["away"]["observations"].push_back({{"id", f.name},
		                                            {"name", f.name},
		                                            {"type", f.type == state_tree::Type::Text      ? "text"
		                                                     : f.type == state_tree::Type::Boolean ? "boolean"
		                                                                                           : "integer"}});
	for (const auto &a : away.actions)
		contexts["away"]["actions"].push_back(
		    {{"id", a.name},
		     {"name", a.name},
		     {"arguments", Json::array({{{"name", "value"}, {"type", a.name == "work" ? "integer" : "text"}}})},
		     {"effect", true}});
	const auto encounter = interdiction_registry();
	contexts["interdiction"] = {{"observations", Json::array()}, {"actions", Json::array()}};
	for (const auto &f : encounter.observations)
		contexts["interdiction"]["observations"].push_back(
		    {{"id", f.name}, {"name", f.name}, {"type", f.type == state_tree::Type::Text ? "text" : "boolean"}});
	for (const auto &a : encounter.actions)
		contexts["interdiction"]["actions"].push_back(
		    {{"id", a.name},
		     {"name", a.name},
		     {"arguments", Json::array({{{"name", "value"}, {"type", "text"}}})},
		     {"effect", true}});
	gameplay_editor.vocabulary = std::move(contexts);
}
bool probe_gameplay_controller_test(WorkshopDocument &document, unsigned stage)
{
	auto &editor = gameplay_editor;
	if (stage == 0)
	{
		editor.controller_probe = true;
		editor.test_map = "test.map";
		editor.test_slot = "test.slot";
		editor.test_ship = "test.controllable";
		return document.definitions()
		           .at("entities")
		           .at(0)
		           .at("control_layout")
		           .at("controllers")
		           .at(0)
		           .at("commands")
		           .at(0)
		           .at("initial") == 3000;
	}
	if (!editor.probe_action.empty())
		return false;
	if (stage == 1)
	{
		editor.probe_action = "Start test";
		return true;
	}
	const auto &feedback = editor.feedback;
	if (!feedback.value("running", false) || feedback.value("tick", 0) < 1)
		return false;
	if (stage == 2)
	{
		bool command = false;
		for (const auto &owner : feedback.value("controllers", Json::array()))
			for (const auto &field : owner.value("commands", Json::array()))
				if (field.value("channel", "") == "speed" && field.value("value", 0) == 3000 &&
				    field.at("revision") == field.at("consumed"))
					command = true;
		if (!command)
			return false;
		editor.probe_action = "Pause test";
	}
	if (stage == 3)
	{
		if (!feedback.value("paused", false))
			return false;
		editor.probe_tick = feedback.at("tick").get<std::uint64_t>();
		editor.probe_action = "Step test";
	}
	if (stage == 4)
	{
		if (!feedback.value("paused", false) || feedback.at("tick") != editor.probe_tick + 1)
			return false;
		editor.probe_action = "Restart test";
	}
	return true;
}
void set_gameplay_test_feedback(const Json &feedback)
{
	gameplay_editor.feedback = feedback;
}
std::optional<GameplayTestRequest> take_gameplay_test_request()
{
	return std::exchange(gameplay_editor.test_request, std::nullopt);
}
} // namespace phoenix
