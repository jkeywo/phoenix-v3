module;
#include <imgui.h>
#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <nlohmann/json.hpp>
#include <string>
#include <utility>

module phoenix.workshop.shell;

namespace phoenix
{
void tool_real_field(const char *label, Json &object, const char *key, float fallback, float minimum, float maximum)
{
	float n = object.value(key, fallback);
	if (ImGui::DragFloat(tool_label(label).c_str(), &n, .01f, minimum, maximum))
		object[key] = std::clamp(n, minimum, maximum);
}
void tool_colour_field(const char *label, Json &object, const char *key, const Json &fallback, bool alpha)
{
	auto value = object.value(key, fallback);
	float colour[4] = {1, 1, 1, 1};
	for (std::size_t i = 0; i < value.size(); ++i)
		colour[i] = value[i];
	const bool changed = alpha ? ImGui::ColorEdit4(tool_label(label).c_str(), colour)
	                           : ImGui::ColorEdit3(tool_label(label).c_str(), colour);
	if (changed)
	{
		object[key] = Json::array();
		for (int i = 0; i < (alpha ? 4 : 3); ++i)
			object[key].push_back(colour[i]);
	}
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
