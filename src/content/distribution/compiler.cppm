module;
#include <set>
#include <cstdint>
#include <array>
#include <map>
#include <memory>
#include <optional>
#include <nlohmann/json.hpp>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <variant>

export module phoenix.content.distribution;
export import phoenix.data;
export import phoenix.content.catalog;
export import phoenix.content.packs;

export namespace phoenix
{
Pack console_assets(const Catalog &catalog, const std::string &ship, const std::string &console = {},
                    const std::string &background = {});
Pack console_assets(const Catalog &catalog, const std::string &ship, const std::vector<std::string> &consoles,
                    const std::string &background = {});
// Public setup hints for the HTML launcher. Admission/setup still verify in C++.
Json launcher_catalog(const Catalog &catalog);
std::string mission_briefing(const Json &map, const std::string &ship);
// Display-only metadata and optional public ship packages, pinned to exact source archives.
Json pack_menu(const std::vector<Pack> &ordered);
Catalog menu_catalog(const std::vector<Json> &menus);
void validate_ship_client(const Pack &pack, const Json &descriptor);
// The catalog must be the verified resolution of this pack.
void validate_ship_client(const Pack &pack, const Catalog &catalog, const Json &descriptor);
Pack ship_client_assets(const Catalog &catalog, const std::string &ship, const std::string &background = {});
std::map<std::string, Bytes> pack_companions(const std::vector<Pack> &ordered, const std::string &filename);
Json ship_client_descriptor(const Catalog &catalog, const std::string &ship, const std::string &hash,
                            const std::map<std::string, std::string> &urls);
} // namespace phoenix
