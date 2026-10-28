module;
#include <algorithm>
#include <array>
#include <cstdint>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>
#include <nlohmann/json.hpp>

export module phoenix.native.workshop_bridge;
export import phoenix.application;
export namespace phoenix
{
void pump_native_workshop_test(Application &, double elapsed_seconds);
}
