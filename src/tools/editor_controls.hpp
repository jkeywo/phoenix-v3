#pragma once
struct ScopedId
{
	inline explicit ScopedId(const char *value)
	{
		ImGui::PushID(value);
	}
	inline explicit ScopedId(int value)
	{
		ImGui::PushID(value);
	}
	inline ~ScopedId()
	{
		ImGui::PopID();
	}
	ScopedId(const ScopedId &) = delete;
};

inline bool edit_value(const char *label, std::string &value)
{
	return ImGui::InputText(label, &value);
}
inline bool edit_value(const char *label, std::int64_t &value)
{
	return ImGui::InputScalar(label, ImGuiDataType_S64, &value);
}
inline bool edit_value(const char *label, bool &value)
{
	return ImGui::Checkbox(label, &value);
}
inline bool edit_value(const char *label, double &value)
{
	return ImGui::InputDouble(label, &value, 0.01, 0.1, "%.3f");
}
template <class Value> bool edit_field(const char *label, Json &object, const char *key, Value fallback)
{
	auto value = object.value(key, fallback);
	if (!edit_value(label, value))
		return false;
	object[key] = value;
	return true;
}
template <class Inspect> void array_rows(Json &rows, Inspect inspect)
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
template <class Inspect> void object_rows(Json &rows, Inspect inspect)
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
inline void append_row(Json &parent, const char *key, Json value)
{
	if (!parent.contains(key))
		parent[key] = Json::array();
	parent[key].push_back(std::move(value));
}
