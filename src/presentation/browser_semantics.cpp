#include <phoenix/semantics.hpp>
#include <emscripten.h>
#include <deque>
namespace {
std::deque<std::string> actions;
}
extern "C" EMSCRIPTEN_KEEPALIVE void phoenix_semantic_action(const char* id) {
    if (actions.size() < 64) actions.emplace_back(id);
}
EM_JS(void, begin_semantics, (), {
    const root = document.getElementById('semantics');
    root.replaceChildren();
});
EM_JS(void, append_semantic_button, (const char* id, const char* label), {
    const button = document.createElement('button');
    button.textContent = UTF8ToString(label);
    const action = UTF8ToString(id);
    button.addEventListener('click', () => Module.ccall('phoenix_semantic_action', null, ['string'], [action]));
    document.getElementById('semantics').appendChild(button);
});
namespace phoenix {
void publish_semantics(const Application& app) {
    static std::vector<SemanticControl> previous;
    auto controls = navigation_controls(app);
    bool changed = previous.size() != controls.size();
    if (!changed) for (std::size_t i = 0; i < controls.size(); ++i) if (previous[i].id != controls[i].id || previous[i].label != controls[i].label) changed = true;
    if (!changed) return;
    begin_semantics();
    for (const auto& control : controls) append_semantic_button(control.id.c_str(), control.label.c_str());
    previous = std::move(controls);
}
void drain_semantic_actions(Application& app) {
    while (!actions.empty()) { invoke_navigation(app, actions.front()); actions.pop_front(); }
}
void install_native_accessibility(void*) {}
void remove_native_accessibility(void*) {}
}
