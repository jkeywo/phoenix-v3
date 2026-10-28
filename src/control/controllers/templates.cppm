module;
#include <nlohmann/json.hpp>
#include <vector>

export module phoenix.control.templates;
export import phoenix.control.controllers;
export import phoenix.content.catalog;

export namespace phoenix
{
// Compile immutable entity templates. Live generations and tree instances are
// copied into each entity by World assembly, never shared between operators.
std::vector<ConsoleControllerRuntime> compile_controller_templates(const Catalog &, const ControllableLayout &);
} // namespace phoenix
