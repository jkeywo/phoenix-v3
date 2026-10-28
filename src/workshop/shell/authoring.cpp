module;
#include <imgui.h>
#include <misc/cpp/imgui_stdlib.h>
#include <algorithm>
#include <cstdint>
#include <functional>
#include <map>
#include <nlohmann/json.hpp>
#include <set>
#include <string>
#include <utility>
#include <vector>

module phoenix.workshop.authoring;

namespace phoenix
{
std::string AuthoringControls::label(const char *text) const
{
	return translate(text, pseudo) + "###" + text;
}

void AuthoringControls::text(const char *message) const
{
	ImGui::TextWrapped("%s", translate(message, pseudo).c_str());
}

bool AuthoringControls::button(const char *message) const
{
	const bool pressed = tool_button(label(message).c_str());
	if (controller_probe && probe_action == message)
	{
		probe_action.clear();
		return true;
	}
	return pressed;
}

void AuthoringControls::heading(const char *message) const
{
	ImGui::SeparatorText(translate(message, pseudo).c_str());
}

bool AuthoringControls::string(Json &value, const char *key, const char *title)
{
	return edit_field(label(title ? title : key).c_str(), value, key, std::string{});
}

void AuthoringControls::audio_fields(Json &owner, const char *key, bool shield)
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

void AuthoringControls::optional_string(Json &value, const char *key)
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

bool AuthoringControls::integer(Json &value, const char *key, std::int64_t fallback)
{
	return edit_field(label(key).c_str(), value, key, fallback);
}

bool AuthoringControls::boolean(Json &value, const char *key, bool fallback)
{
	return edit_field(label(key).c_str(), value, key, fallback);
}

bool AuthoringControls::decimal(Json &value, const char *key, double fallback)
{
	return edit_field(label(key).c_str(), value, key, fallback);
}

void AuthoringControls::gameplay_panel(Json &owner)
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

bool AuthoringControls::choice(Json &value, const char *key, const std::vector<std::string> &options, bool optional)
{
	const auto current = value.value(key, std::string{});
	bool changed = false;
	if (ScopedCombo scope(label(key).c_str(), current.empty() ? translate("None", pseudo).c_str() : current.c_str());
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

std::string AuthoringControls::identity(const Json &row) const
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

std::vector<std::string> AuthoringControls::ids(const Json &array) const
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

std::string AuthoringControls::fresh(const Json &array, const std::string &prefix) const
{
	const auto names = ids(array);
	for (unsigned i = 1;; ++i)
	{
		auto name = prefix + std::to_string(i);
		if (std::find(names.begin(), names.end(), name) == names.end())
			return name;
	}
}

void AuthoringControls::names(Json &value, const char *key, const std::vector<std::string> &options)
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

void AuthoringControls::strings(Json &value, const char *key)
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

void AuthoringControls::numbers(Json &value, const char *key)
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

void AuthoringControls::records(Json &parent, const char *key, Json prototype,
                                const std::function<void(Json &)> &inspect)
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
			           const auto title = row.is_object() ? row.value("id", row.value("kind", std::to_string(i + 1)))
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

void AuthoringControls::scalar_map(Json &parent, const char *key)
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

void AuthoringControls::structure(Json &value, bool writable, unsigned depth)
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

void AuthoringControls::structured_field(Json &parent, const char *key)
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
} // namespace phoenix
