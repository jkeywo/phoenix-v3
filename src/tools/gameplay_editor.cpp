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

	Json feedback = Json::object();
	std::optional<GameplayTestRequest> test_request;
	std::string entity, tree, test_ship, test_slot, test_map;
	std::string inspected_controller, selected_system, system_entity;
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
		return ImGui::Button(label(message).c_str());
	}
	void heading(const char *message) const
	{
		ImGui::SeparatorText(translate(message, pseudo).c_str());
	}
	bool string(Json &value, const char *key, const char *title = nullptr)
	{
		return edit_field(label(title ? title : key).c_str(), value, key, std::string{});
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
	std::vector<std::string> definitions(const WorkshopDocument &document, const char *kind) const
	{
		std::vector<std::string> result;
		if (document.definitions().contains(kind))
			result = ids(document.definitions().at(kind));
		const auto &library = document.libraries();
		const auto *values = &library.entities;
		if (std::string(kind) == "trees")
			values = &library.trees;
		if (std::string(kind) == "consoles")
			values = &library.consoles;
		if (std::string(kind) == "equipment")
			values = &library.equipment;
		if (std::string(kind) == "assemblies")
			values = &library.assemblies;
		if (std::string(kind) == "messages")
			values = &library.messages;
		if (std::string(kind) == "maps")
			values = &library.maps;
		for (const auto &[key, value] : *values)
			if (std::find(result.begin(), result.end(), key) == result.end())
				result.push_back(key);
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
	void system_details(Json &spec, const std::string &fallback_kind = "engine")
	{
		for (const char *key : {"power_store", "generator", "power_group", "conduit", "cooling_link", "coolant_source"})
			optional_string(spec, key);
		integer(spec, "energy_per_second");
		integer(spec, "discrete_energy");
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
		const auto kind = spec.value("kind", fallback_kind);
		if (kind == "engine" || kind == "boost" || kind == "impulse")
		{
			integer(spec, "max_speed_mm_s");
			integer(spec, "acceleration_mm_s2");
		}
		if (kind == "engine" || kind == "steering")
			integer(spec, "turn_mdeg_s");
		if (kind == "radar")
			integer(spec, "range_mm");
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
			}
		};
		if (kind == "boost" || kind == "impulse")
			mode(spec, kind == "impulse");
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
	void fitted(WorkshopDocument &document)
	{
		auto *selected = select_entity(document);
		if (!selected)
			return;
		auto &ship = *selected;
		choice(ship, "extends", definitions(document, "entities"), true);
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
				choice(spec, "template", definitions(document, "equipment"), true);
				choice(spec, "kind",
				       {"engine", "steering", "radar", "generator", "distribution", "coolant", "storage", "lateral",
				        "boost", "impulse"},
				       true);
				auto kind = equipment_kind(document, spec);
				if (kind.empty())
					kind = selected_system;
				system_details(spec, kind);
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
		if (!ship.contains("control_layout") && !composition.contains("control_layout"))
		{
			heading("Resolved controller bindings");
			for (const auto &system : systems)
			{
				ScopedId identity(system.c_str());
				ImGui::TextUnformatted(system.c_str());
				if (ship.contains("controllers"))
					choice(ship["controllers"], system.c_str(), definitions(document, "trees"), true);
				else if (button("Create controller bindings"))
					ship["controllers"] = Json::object();
			}
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
				            choice(console, "definition", definitions(document, "consoles"));
				            names(console, "systems", systems);
				            names(console, "feeds", systems);
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
	Json knowledge_view(const WorkshopDocument &document) const
	{
		Json result = document.libraries().knowledge;
		if (!result.is_object())
			result = Json::object();
		if (!result.contains("roles"))
			result["roles"] = Json::array();
		if (!result.contains("schemas"))
			result["schemas"] = Json::array();
		if (document.definitions().contains("knowledge"))
		{
			const auto &local = document.definitions().at("knowledge");
			for (const auto &role : local.value("roles", Json::array()))
				if (std::find(result["roles"].begin(), result["roles"].end(), role) == result["roles"].end())
					result["roles"].push_back(role);
			for (const auto &schema : local.value("schemas", Json::array()))
			{
				auto existing = std::find_if(result["schemas"].begin(), result["schemas"].end(),
				                             [&](const Json &candidate)
				                             {
					                             return candidate.at("id") == schema.at("id");
				                             });
				if (existing == result["schemas"].end())
					result["schemas"].push_back(schema);
				else
					*existing = schema;
			}
		}
		return result;
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
		const auto visible_contract = knowledge_view(document);
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
		const auto visible_contract = knowledge_view(document);
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
		const auto knowledge = knowledge_view(document);
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
				options = vocabulary_ids(definition.value("context", std::string("system")), "observations");
			if (mode == "observation" && definition.contains("record_bindings"))
				for (const auto &field : definition.at("record_bindings"))
					options.push_back(field.at("name").get<std::string>());
			if (mode == "observation" && definition.contains("summary_bindings"))
				for (const auto &field : definition.at("summary_bindings"))
					options.push_back(field.at("name").get<std::string>());
			if (mode == "argument")
				options = {"amount", "flag", "detail"};
			if (mode == "intent")
				options = vocabulary_ids(definition.value("context", std::string("system")), "intents");
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
					const auto actions = vocabulary_ids(definition.value("context", std::string("system")), "actions");
					if (!actions.empty())
						choice(task, "action", actions);
					else
					{
						string(task, "action");
						text("Action palette awaits the production context vocabulary. Verification rejects "
						     "unsupported names.");
					}
					boolean(task, "once", true);
					const auto signature = action_signature(definition.value("context", std::string("system")),
					                                        task.at("action").get<std::string>());
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
					choice(task, "call", definitions(document, "trees"));
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
			if (choice(derivation, "Base tree", definitions(document, "trees")))
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
				if (!resolved.contains("context"))
				{
					text("Legacy timed controller. State identities are object keys and remain stable.");
					choice(definition, "initial", ids(resolved.at("states")));
					if (definition.contains("states"))
						for (auto it = definition["states"].begin(); it != definition["states"].end(); ++it)
						{
							ScopedId identity(it.key().c_str());
							ImGui::TextUnformatted(it.key().c_str());
							integer(it.value(), "ticks", 1);
							choice(it.value(), "next", ids(resolved.at("states")));
							for (auto field = it.value().begin(); field != it.value().end(); ++field)
								if (field.key() != "ticks")
								{
									if (field.value().is_number_integer())
										integer(it.value(), field.key().c_str());
									else if (field.value().is_boolean())
										boolean(it.value(), field.key().c_str());
								}
						}
					return;
				}
				choice(definition, "extends", definitions(document, "trees"), true);
				if (definition.contains("extends"))
				{
					text("Inherited source");
					ImGui::TextUnformatted(definition.at("extends").get<std::string>().c_str());
				}
				Json context{{"context", resolved.at("context")}};
				if (choice(context, "context", {"controller", "system", "scenario"}))
					definition["context"] = context.at("context");
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
				if (resolved.value("context", std::string("system")) == "system")
					summary_bindings(definition);
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
	void summary_bindings(Json &tree)
	{
		records(tree, "summary_bindings", {{"name", "intent.power"}, {"channel", "power"}},
		        [&](Json &binding)
		        {
			        string(binding, "name");
			        string(binding, "channel");
			        text("Integer observation from the named delegated control channel.");
		        });
	}
	void record_bindings(Json &tree, WorkshopDocument &document, const Json &resolved)
	{
		const auto knowledge = knowledge_view(document);
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
		records(map, "regions", {{"id", "region"}, {"shape", "circle"}, {"x_mm", 0}, {"y_mm", 0}, {"radius_mm", 10000}},
		        [&](Json &region)
		        {
			        string(region, "id");
			        choice(region, "shape", {"circle", "rectangle"});
			        integer(region, "x_mm");
			        integer(region, "y_mm");
			        if (region.value("shape", std::string("circle")) == "circle")
				        integer(region, "radius_mm", 10000);
			        else
			        {
				        integer(region, "half_width_mm", 10000);
				        integer(region, "half_height_mm", 10000);
			        }
		        });
		if (!map.contains("scenario"))
		{
			if (button("Create scenario"))
				map["scenario"] = Json::object();
			return;
		}
		auto &scenario = map["scenario"];
		choice(scenario, "tree", definitions(document, "trees"), true);
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
			        choice(situation, "tree", definitions(document, "trees"));
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
				                choice(entity, "definition", definitions(document, "entities"));
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
			    choice(objective, "type", {"reach_region"});
			    choice(objective, "region", regions);
			    actor(objective, "actor", slots);
			    string(objective, "title_key");
			    string(objective, "description_key");
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
		auto name = instance.value("template", instance.value("extends", std::string{}));
		std::set<std::string> seen;
		while (!name.empty() && seen.size() < 64 && seen.insert(name).second)
		{
			const Json *source = nullptr;
			if (document.definitions().contains("equipment"))
				for (const auto &candidate : document.definitions().at("equipment"))
					if (candidate.at("id") == name)
						source = &candidate;
			if (!source && document.libraries().equipment.contains(name))
				source = &document.libraries().equipment.at(name);
			if (!source)
				break;
			if (source->contains("kind"))
				return source->at("kind").get<std::string>();
			name = source->value("extends", std::string{});
		}
		return {};
	}
	void templates(WorkshopDocument &document)
	{
		auto &defs = document.definitions();
		records(defs, "equipment",
		        {{"id", "equipment"}, {"kind", "engine"}, {"max_speed_mm_s", 10000}, {"acceleration_mm_s2", 1000}},
		        [&](Json &equipment)
		        {
			        string(equipment, "id");
			        choice(equipment, "extends", definitions(document, "equipment"), true);
			        choice(equipment, "kind",
			               {"engine", "steering", "radar", "generator", "distribution", "coolant", "storage", "lateral",
			                "boost", "impulse"});
			        const auto kind = equipment_kind(document, equipment);
			        system_details(equipment, kind);
			        choice(equipment, "controller", definitions(document, "trees"), true);
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
					        choice(it.value(), "template", definitions(document, "equipment"), true);
					        choice(it.value(), "kind",
					               {"engine", "steering", "radar", "generator", "distribution", "coolant", "storage",
					                "lateral", "boost", "impulse"},
					               true);
					        system_details(it.value(), equipment_kind(document, it.value()));
					        choice(it.value(), "controller", definitions(document, "trees"), true);
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
			names(*ship, "message_templates", definitions(document, "messages"));
			structured_field(*ship, "parameters");
			services(*ship);
			records(
			    *ship, "assemblies",
			    {{"id", "placement"}, {"assembly", ""}, {"bindings", Json::object()}, {"overrides", Json::object()}},
			    [&](Json &placement)
			    {
				    string(placement, "id");
				    choice(placement, "assembly", definitions(document, "assemblies"));
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
			        if (choice(controller, "tree", definitions(document, "trees"), true))
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

		if (editable("command_bindings"))
			records(tree, "command_bindings", {{"channel", "command"}, {"type", "integer"}, {"semantics", "setting"}},
			        [&](Json &binding)
			        {
				        string(binding, "channel");
				        choice(binding, "type", {"integer", "boolean"});
				        choice(binding, "semantics", {"setting", "request"});
				        choice(binding, "action", {"", "destination", "follow_contact"});
				        if (binding.value("action", std::string{}).empty())
					        binding.erase("action");
			        });
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
		if (editable("contact_bindings"))
			records(tree, "contact_bindings", {{"name", "contact"}, {"controller", ""}, {"channel", ""}},
			        [&](Json &binding)
			        {
				        string(binding, "name");
				        string(binding, "controller");
				        string(binding, "channel");
			        });
		if (editable("message_templates"))
			names(tree, "message_templates", definitions(document, "messages"));
		const auto knowledge = knowledge_view(document);
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
			auto catalog = document.libraries();
			for (const auto &source : document.definitions().value("trees", Json::array()))
				catalog.trees[source.at("id").get<std::string>()] = source;
			for (const auto &source : document.definitions().value("messages", Json::array()))
				catalog.messages[source.at("id").get<std::string>()] = source;
			catalog.knowledge_definition = compile_knowledge(knowledge_view(document));
			const auto registry = gameplay_registry(tree, catalog, state_tree::Context::Controller);
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
	void presets(WorkshopDocument &document)
	{
		auto *ship = select_entity(document);
		if (!ship)
			return;
		(void)fitted_ids(document, *ship);
		if (ship->contains("control_layout") || composition.contains("control_layout"))
		{
			text("This entity uses Console Controller complexity profiles.");
			return;
		}
		if (!ship->contains("presets") && composition.contains("presets"))
		{
			text("Station presets are inherited.");
			if (button("Override inherited presets"))
				(*ship)["presets"] = composition.at("presets");
		}
		std::map<std::string, std::vector<std::string>> console_systems;
		const auto &station_source = composition.empty() ? *ship : composition;
		if (station_source.contains("stations"))
			for (const auto &station : station_source.at("stations"))
				for (const auto &console : station.at("consoles"))
				{
					auto &owned = console_systems[console.at("id").get<std::string>()];
					if (console.contains("systems"))
						for (const auto &system : console.at("systems"))
							owned.push_back(system.get<std::string>());
				}
		else
			console_systems["main"] =
			    station_source.contains("systems") ? ids(station_source.at("systems")) : std::vector<std::string>{};
		choice(*ship, "default_preset",
		       ship->contains("presets") ? ids(ship->at("presets")) : std::vector<std::string>{}, true);
		records(*ship, "presets", {{"id", "preset"}, {"name", ""}, {"description", ""}, {"consoles", Json::object()}},
		        [&](Json &preset)
		        {
			        string(preset, "id");
			        string(preset, "name");
			        string(preset, "description");
			        for (const auto &[console, systems] : console_systems)
			        {
				        ScopedId identity(console.c_str());
				        if (ScopedTree inspector(console.c_str()); inspector)
				        {
					        if (!preset.contains("consoles") || !preset.at("consoles").contains(console))
					        {
						        if (button("Configure Console preset"))
							        preset["consoles"][console] = {{"direct", systems},
							                                       {"delegated", Json::array()},
							                                       {"summary_intents", Json::array()}};
					        }
					        else
					        {
						        auto &binding = preset["consoles"][console];
						        names(binding, "direct", systems);
						        names(binding, "delegated", systems);
						        std::vector<std::string> delegated;
						        if (binding.contains("delegated"))
							        for (const auto &system : binding.at("delegated"))
								        delegated.push_back(system.get<std::string>());
						        records(
						            binding, "summary_intents",
						            {{"system", ""},
						             {"channel", ""},
						             {"levels", Json::array({0})},
						             {"default", 0},
						             {"level_labels", Json::array({"No"})}},
						            [&](Json &summary)
						            {
							            choice(summary, "system", delegated);
							            std::vector<std::string> channels;
							            if (composition_catalog.entities.contains(entity) &&
							                composition.contains("systems") &&
							                composition.at("systems").contains(summary.value("system", std::string{})))
								            channels = gameplay_summary_channels(
								                composition_catalog, entity, summary.at("system").get<std::string>());
							            choice(summary, "channel", channels);
							            if (summary.contains("channel") &&
							                std::find(channels.begin(), channels.end(),
							                          summary.at("channel").get<std::string>()) == channels.end())
								            text("This channel is not available from the fitted capability or its "
								                 "controller. Choose a declared channel before verification.");
							            numbers(summary, "levels");
							            integer(summary, "default");
							            strings(summary, "level_labels");
						            });
						        text("Direct and delegated lists cover each owned System once. Summary levels are "
						             "physical channel values and require matching translated level labels.");
					        }
				        }
			        }
		        });
	}
	void messages(WorkshopDocument &document)
	{
		auto &defs = document.definitions();
		const auto known = knowledge_view(document);
		const auto schemas = ids(known.at("schemas"));
		const Json context{{"context", "system"}};
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
			        choice(message["destination"], "kind", {"controller", "console", "station", "ship", "system"});
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
	void disposable_test(WorkshopDocument &document, std::string &status)
	{
		heading("Disposable production test");
		text("Run an isolated simulation of the verified draft. Pause, step and restart apply to this test.");
		Json selection{{"Mission", test_map}, {"Ship", test_ship}, {"Slot", test_slot}};
		if (choice(selection, "Mission", definitions(document, "maps")))
			test_map = selection.at("Mission").get<std::string>();
		if (choice(selection, "Ship", definitions(document, "entities")))
			test_ship = selection.at("Ship").get<std::string>();
		std::vector<std::string> slots;
		if (document.libraries().maps.contains(test_map) &&
		    document.libraries().maps.at(test_map).contains("ship_slots"))
			slots = ids(document.libraries().maps.at(test_map).at("ship_slots"));
		if (document.definitions().contains("maps"))
			for (const auto &map : document.definitions().at("maps"))
				if (map.at("id") == test_map && map.contains("ship_slots"))
					slots = ids(map.at("ship_slots"));
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
					status = translate("Choose a mission, slot and ship before starting a test.", pseudo);
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
					status = translate("Disposable test request queued.", pseudo);
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
		gameplay_panel(console);
		const auto known = knowledge_view(document);
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

	void properties(WorkshopDocument &document, Application &app, std::string &status, const std::string &selection,
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
				         {"Power and cooling", &GameplayEditor::resources},
				         {"Fitting and assemblies", &GameplayEditor::placements},
				         {"Knowledge feeds and publications", &GameplayEditor::knowledge_bindings},
				         {"Console Controllers and complexity", &GameplayEditor::control_layout},
				         {"Station presets", &GameplayEditor::presets}})
					if (ImGui::CollapsingHeader(label(title).c_str()))
					{
						ScopedId scope(title);
						(this->*inspect)(document);
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
	void areas(WorkshopDocument &document, Application &app, std::string &status, std::string &selection)
	{
		pseudo = app.pseudo_locale;
		if (ImGui::Begin(label("State trees").c_str()))
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
		if (ImGui::Begin(label("Scenario").c_str()))
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
		if (ImGui::Begin(label("Test").c_str()))
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
void draw_workshop_areas(WorkshopDocument &document, Application &app, std::string &status, std::string &selection)
{
	if (gameplay_editor.vocabulary.empty())
		set_gameplay_authoring_vocabulary(gameplay_tree_vocabulary());
	gameplay_editor.areas(document, app, status, selection);
}
void draw_gameplay_properties(WorkshopDocument &document, Application &app, std::string &status,
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
	if (vocabulary.contains("system") || vocabulary.contains("scenario"))
	{
		gameplay_editor.vocabulary = vocabulary;
		return;
	}
	Json contexts = Json::object();
	for (const auto &[context, mask] : std::vector<std::pair<std::string, unsigned>>{{"system", 1}, {"scenario", 2}})
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
	gameplay_editor.vocabulary = std::move(contexts);
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
