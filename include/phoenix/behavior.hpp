#pragma once
#include <phoenix/content.hpp>
namespace phoenix {
struct TreeState { std::string state; std::uint64_t elapsed = 0; bool entered = false; };
// Each state emits bounded operating requests on entry, waits a declared tick
// duration and follows a named transition. Requests still require admission.
void verify_tree(const Json& definition, const std::string& source);
Json step_tree(const Json& definition, TreeState& state);
Json save_tree(const TreeState& state);
TreeState load_tree(const Json& definition, const Json& saved);
}
