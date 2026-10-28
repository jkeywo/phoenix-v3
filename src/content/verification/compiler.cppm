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

export module phoenix.content.verification;
export import phoenix.data;
export import phoenix.content.catalog;
export import phoenix.content.effects.types;
export import phoenix.content.packs;

export namespace phoenix
{
Catalog resolve_packs(const std::vector<Pack> &ordered);
// Authoring preview only: the final draft is deliberately absent from archive identities.
Catalog resolve_draft(const std::vector<Pack> &libraries, const PackDraft &draft);
Catalog prepared_catalog(const Catalog &catalog, SharedPropertyCatalog = {});
void verify_catalog(const Catalog &catalog);
} // namespace phoenix
