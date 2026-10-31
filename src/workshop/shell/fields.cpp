module;
#include <imgui.h>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <stdexcept>
#include <nlohmann/json.hpp>
#include <string>
#include <utility>

module phoenix.workshop.shell;
import phoenix.workshop.authoring_core;

namespace phoenix
{
bool edit_shared_field(const char *label, Json &object, const char *descriptor)
{
	return edit_shared_control(label, object, descriptor, Json::object());
}
bool edit_shared_control(const char *label, Json &object, const char *descriptor, const Json &context)
{
	const auto spec = workshop_control(descriptor, object, context);
	const auto type = spec.at("type").get<std::string>();
	const auto widget = spec.value("widget", std::string{});
	Json value = spec.at("value");
	if (spec.value("mismatch", false))
	{
		ImGui::TextUnformatted(tool_label("The value type does not match the selected property kind.").c_str());
		if (tool_button(tool_label("Reset to selected property kind").c_str()))
		{
			(void)workshop_set_control(object, descriptor, spec.at("default"), context);
			return true;
		}
		return false;
	}
	if (type == "string" && value.is_object())
	{
		ImGui::TextUnformatted(tool_label("Local tree: edit in State trees").c_str());
		return false;
	}
	bool changed = false;
	if (spec.contains("choices"))
	{
		const auto current = value.get<std::string>();
		if (ImGui::BeginCombo(label, current.c_str()))
		{
			if (spec.value("optional", false) && ImGui::Selectable(tool_label("None").c_str(), current.empty()))
			{
				value = "";
				changed = true;
			}
			for (const auto &option : spec.at("choices"))
				if (ImGui::Selectable(option.get_ref<const std::string &>().c_str(), option == current))
				{
					value = option;
					changed = true;
				}
			ImGui::EndCombo();
		}
	}
	else if (type == "array")
	{
		const auto length = spec.at("length").get<int>();
		if (length < 1 || length > 4)
			throw std::runtime_error("Unsupported control vector width");
		if (spec.value("items", std::string{}) == "integer")
		{
			std::int64_t values[4]{};
			for (int i = 0; i < length; ++i)
				values[i] = value.at(i);
			const auto low = spec.value("minimum", std::int64_t{0}), high = spec.value("maximum", std::int64_t{0});
			if (widget == "drag-vector")
				changed =
				    ImGui::DragScalarN(label, ImGuiDataType_S64, values, length, spec.value("step", 1.f), &low, &high);
			else
				changed = ImGui::InputScalarN(label, ImGuiDataType_S64, values, length);
			if (changed)
			{
				value = Json::array();
				for (int i = 0; i < length; ++i)
					value.push_back(values[i]);
			}
		}
		else
		{
			float values[4]{};
			for (int i = 0; i < length; ++i)
				values[i] = value.at(i);
			const float low = spec.value("minimum", 0.f), high = spec.value("maximum", 0.f);
			if (widget == "colour")
				changed = length == 4 ? ImGui::ColorEdit4(label, values) : ImGui::ColorEdit3(label, values);
			else if (widget == "slider-vector")
				changed = ImGui::SliderScalarN(label, ImGuiDataType_Float, values, length, &low, &high);
			else if (widget == "drag-vector")
				changed = ImGui::DragScalarN(label, ImGuiDataType_Float, values, length, spec.value("step", .01f), &low,
				                             &high);
			else
				changed = ImGui::InputScalarN(label, ImGuiDataType_Float, values, length);
			if (changed)
			{
				value = Json::array();
				for (int i = 0; i < length; ++i)
					value.push_back(values[i]);
			}
		}
	}
	else if (type == "string")
	{
		auto text = value.get<std::string>();
		if (widget == "theme-token" && !text.empty() && text.front() == '#')
		{
			const auto channels = workshop_colour_channels(text);
			if (!channels.is_null())
			{
				float rgba[4];
				for (int i = 0; i < 4; ++i)
					rgba[i] = channels.at(i);
				if (ImGui::ColorEdit4((std::string(label) + " colour").c_str(), rgba))
				{
					text = workshop_colour_text(Json::array({rgba[0], rgba[1], rgba[2], rgba[3]}));
					changed = true;
				}
			}
		}
		changed = edit_value(label, text) || changed;
		value = text;
	}
	else if (type == "boolean")
	{
		auto v = value.get<bool>();
		changed = edit_value(label, v);
		value = v;
	}
	else if (type == "integer")
	{
		auto v = value.get<std::int64_t>();
		if (widget == "slider")
		{
			const auto low = spec.at("minimum").get<std::int64_t>(), high = spec.at("maximum").get<std::int64_t>();
			changed = ImGui::SliderScalar(label, ImGuiDataType_S64, &v, &low, &high);
		}
		else
			changed = edit_value(label, v);
		value = v;
	}
	else if (type == "number")
	{
		auto v = value.get<double>();
		if (widget == "slider" || widget == "drag")
		{
			const auto low = spec.at("minimum").get<double>(), high = spec.at("maximum").get<double>();
			changed = widget == "slider"
			              ? ImGui::SliderScalar(label, ImGuiDataType_Double, &v, &low, &high)
			              : ImGui::DragScalar(label, ImGuiDataType_Double, &v, spec.value("step", .01f), &low, &high);
		}
		else if (spec.contains("step"))
		{
			const auto step = spec.at("step").get<double>(), fast = spec.value("fast_step", step * 10);
			const auto format = spec.value("format", std::string("%.3f"));
			changed = ImGui::InputDouble(label, &v, step, fast, format.c_str());
		}
		else
			changed = edit_value(label, v);
		value = v;
	}
	if (changed)
		(void)workshop_set_control(object, descriptor, value, context);
	return changed;
}
bool edit_shared_choice(const char *label, Json &owner, const char *key, const std::vector<std::string> &choices,
                        bool optional)
{
	if (owner.contains(key) && owner.at(key).is_object())
	{
		ImGui::TextUnformatted(tool_label("Local tree: edit in State trees").c_str());
		return false;
	}
	const auto current = owner.value(key, std::string{});
	bool changed = false;
	if (ImGui::BeginCombo(label, current.empty() ? tool_label("None").c_str() : current.c_str()))
	{
		if (optional && ImGui::Selectable(tool_label("None").c_str(), current.empty()))
		{
			(void)workshop_custom_action(owner, "field.choice",
			                             {{"field", key}, {"value", ""}, {"choices", choices}, {"optional", optional}});
			changed = true;
		}
		for (const auto &choice : choices)
			if (ImGui::Selectable(choice.c_str(), choice == current))
			{
				(void)workshop_custom_action(
				    owner, "field.choice",
				    {{"field", key}, {"value", choice}, {"choices", choices}, {"optional", optional}});
				changed = true;
			}
		ImGui::EndCombo();
	}
	return changed;
}
bool edit_shared_member(const char *label, Json &owner, const char *field, const Json &item)
{
	bool selected = owner.contains(field) &&
	                std::find(owner.at(field).begin(), owner.at(field).end(), item) != owner.at(field).end();
	if (!ImGui::Checkbox(label, &selected))
		return false;
	(void)workshop_custom_action(owner, "list.member", {{"field", field}, {"value", item}, {"enabled", selected}});
	return true;
}
void edit_shared_group(Json &object, const char *group)
{
	edit_shared_group(object, group, Json::object());
}
void edit_shared_group(Json &object, const char *group, const Json &context)
{
	for (const auto &[id, field] : workshop_fields().items())
		if (field.value("group", std::string{}) == group)
			edit_shared_control(tool_label(field.at("label").get<std::string>()).c_str(), object, id.c_str(), context);
}
bool edit_field(const char *label, Json &object, const char *key, std::string fallback)
{
	auto value = object.value(key, fallback);
	if (!edit_value(label, value))
		return false;
	object[key] = value;
	return true;
}

bool edit_field(const char *label, Json &object, const char *key, std::int64_t fallback)
{
	auto value = object.value(key, fallback);
	if (!edit_value(label, value))
		return false;
	object[key] = value;
	return true;
}

bool edit_field(const char *label, Json &object, const char *key, bool fallback)
{
	auto value = object.value(key, fallback);
	if (!edit_value(label, value))
		return false;
	object[key] = value;
	return true;
}

bool edit_field(const char *label, Json &object, const char *key, double fallback)
{
	auto value = object.value(key, fallback);
	if (!edit_value(label, value))
		return false;
	object[key] = value;
	return true;
}

void array_rows(Json &rows, const std::function<bool(Json &, std::size_t)> &inspect)
{
	int remove = -1;
	for (std::size_t i = 0; i < rows.size(); ++i)
	{
		ScopedId identity(static_cast<int>(i));
		if (inspect(rows[i], i))
			remove = static_cast<int>(i);
	}
	if (remove >= 0)
		rows.erase(rows.begin() + remove);
}

void object_rows(Json &rows, const std::function<bool(const std::string &, Json &)> &inspect)
{
	std::string remove;
	for (auto it = rows.begin(); it != rows.end(); ++it)
	{
		ScopedId identity(it.key().c_str());
		if (inspect(it.key(), it.value()))
			remove = it.key();
	}
	if (!remove.empty())
		rows.erase(remove);
}

void append_row(Json &parent, const char *key, Json value)
{
	if (!parent.contains(key))
		parent[key] = Json::array();
	parent[key].push_back(std::move(value));
}
} // namespace phoenix
