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
void WorkshopDocument::refresh_revision() const
{
	if (revision_source_ == definitions_ && revision_manifest_ == pack_.manifest &&
	    observed_resources_ == resource_revision_)
		return;
	revision_source_ = definitions_;
	revision_manifest_ = pack_.manifest;
	observed_resources_ = resource_revision_;
	++revision_;
}
std::uint64_t WorkshopDocument::revision() const
{
	if (!in_frame_)
		refresh_revision();
	return revision_;
}
void WorkshopDocument::begin_frame()
{
	if (!in_frame_)
		refresh_revision();
	in_frame_ = true;
}
void WorkshopDocument::end_frame(bool may_have_edits)
{
	// Keep managed mode across frames. Browsing and wheel input do not mutate content.
	if (may_have_edits || observed_resources_ != resource_revision_)
		refresh_revision();
}
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
	return definitions_.at("maps").at(map_index_);
}
const Json &WorkshopDocument::map() const
{
	return definitions_.at("maps").at(map_index_);
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
