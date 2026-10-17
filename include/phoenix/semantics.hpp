#pragma once
#include <phoenix/application.hpp>
#include <string>
#include <vector>
namespace phoenix {
struct SemanticControl { std::string id; std::string label; };
std::string translate(const std::string& key, bool pseudo);
std::vector<SemanticControl> navigation_controls(const Application& app);
bool invoke_navigation(Application& app, const std::string& action);
void publish_semantics(const Application& app);
void drain_semantic_actions(Application& app);
void install_native_accessibility(void* window);
void remove_native_accessibility(void* window);
bool probe_native_accessibility(Application& app);
}
