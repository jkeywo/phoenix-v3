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

export module phoenix.workshop.browser_test_client;
export import phoenix.application;
export namespace phoenix
{
void pump_browser_workshop_test(Application &);
}
