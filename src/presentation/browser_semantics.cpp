#include <phoenix/semantics.hpp>
#include <emscripten.h>
#include <deque>
#include <chrono>
namespace
{
std::deque<std::string> actions;
}
extern "C" EMSCRIPTEN_KEEPALIVE void phoenix_semantic_action(const char *id)
{
	if (actions.size() < 64)
		actions.emplace_back(id);
}
// clang-format off: embedded JavaScript must retain its operators.
EM_JS(void, begin_semantics, (), {
    const root = document.getElementById('semantics');
    for (const child of root.children) if (child.dataset.action) child.dataset.stale = 'true';
});
EM_JS(void, append_semantic_button, (const char* id, const char* label), {
    const action = UTF8ToString(id);
    const root = document.getElementById('semantics');
    let button = Array.from(root.children).find(child => child.dataset.action === action);
    if (!button) {
        button = document.createElement('button'); button.dataset.action = action;
        button.addEventListener('click', () => Module.ccall('phoenix_semantic_action', null, ['string'], [action]));
        root.appendChild(button);
    }
    button.textContent = UTF8ToString(label); delete button.dataset.stale;
});
EM_JS(void, end_semantics, (), {
    const root = document.getElementById('semantics');
    for (const child of Array.from(root.children)) if (child.dataset.stale) child.remove();
});
EM_JS(void, update_semantic_status, (const char* text), {
    const status = document.getElementById('player-status');
    if (status && status.textContent !== UTF8ToString(text)) status.textContent = UTF8ToString(text);
});
// clang-format on
namespace phoenix
{
void publish_semantics(const Application &app)
{
	static auto next_status = std::chrono::steady_clock::time_point{};
	const auto now = std::chrono::steady_clock::now();
	if (now >= next_status)
	{
		update_semantic_status(app.accessible_status.c_str());
		next_status = now + std::chrono::seconds(1);
	}
	static std::vector<SemanticControl> previous;
	auto controls = navigation_controls(app);
	bool changed = previous.size() != controls.size();
	if (!changed)
		for (std::size_t i = 0; i < controls.size(); ++i)
			if (previous[i].id != controls[i].id || previous[i].label != controls[i].label)
				changed = true;
	if (!changed)
		return;
	begin_semantics();
	for (const auto &control : controls)
		append_semantic_button(control.id.c_str(), control.label.c_str());
	end_semantics();
	previous = std::move(controls);
}
void drain_semantic_actions(Application &app)
{
	while (!actions.empty())
	{
		invoke_navigation(app, actions.front());
		actions.pop_front();
	}
}
void install_native_accessibility(void *)
{
}
void remove_native_accessibility(void *)
{
}
} // namespace phoenix
