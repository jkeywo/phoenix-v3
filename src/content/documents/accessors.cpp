module;
#include <cstddef>
#include <map>
#include <memory>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

module phoenix.content.documents;

namespace phoenix
{
Json &WorkshopDocument::definitions()
{
	return definitions_;
}
const Json &WorkshopDocument::definitions() const
{
	return definitions_;
}
Json &WorkshopDocument::map()
{
	return definitions_["maps"][map_index_];
}
const Json &WorkshopDocument::map() const
{
	return definitions_["maps"][map_index_];
}
Json &WorkshopDocument::manifest()
{
	return pack_.manifest;
}
const PackDraft &WorkshopDocument::pack() const
{
	return pack_;
}
const Catalog &WorkshopDocument::libraries() const
{
	return library_catalog_;
}
bool WorkshopHistory::can_undo() const
{
	return past_.size() > 1;
}
bool WorkshopHistory::can_redo() const
{
	return !future_.empty();
}
} // namespace phoenix
