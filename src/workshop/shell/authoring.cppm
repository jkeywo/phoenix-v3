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

export module phoenix.workshop.authoring;
export import phoenix.workshop.shell;

export namespace phoenix
{
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

// Shared typed field controls; document and editor selection remain with callers.
struct AuthoringControls
{
	bool controller_probe = false;
	mutable std::string probe_action;
	bool pseudo = false;
	std::string label(const char *text) const;
	void text(const char *message) const;
	bool button(const char *message) const;
	void heading(const char *message) const;
	bool string(Json &value, const char *key, const char *title = nullptr);
	void audio_fields(Json &owner, const char *key, bool shield = false);
	void optional_string(Json &value, const char *key);
	bool integer(Json &value, const char *key, std::int64_t fallback = 0);
	bool boolean(Json &value, const char *key, bool fallback = false);
	bool decimal(Json &value, const char *key, double fallback);
	void gameplay_panel(Json &owner);
	bool choice(Json &value, const char *key, const std::vector<std::string> &options, bool optional = false);
	std::string identity(const Json &row) const;
	std::vector<std::string> ids(const Json &array) const;
	std::string fresh(const Json &array, const std::string &prefix) const;
	void names(Json &value, const char *key, const std::vector<std::string> &options);
	void strings(Json &value, const char *key);
	void numbers(Json &value, const char *key);
	void records(Json &parent, const char *key, Json prototype, const std::function<void(Json &)> &inspect);
	void scalar_map(Json &parent, const char *key);
	void structure(Json &value, bool writable = true, unsigned depth = 0);
	void structured_field(Json &parent, const char *key);
};
} // namespace phoenix
